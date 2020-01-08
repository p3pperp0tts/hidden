#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "HiddenLib.h"
#include "..\\Hidden\DeviceAPI.h"

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS     ((NTSTATUS)0x00000000L)

typedef struct _HidContextInternal {
	HANDLE hdevice;
} HidContextInternal, *PHidContextInternal;

typedef struct _LSA_UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

typedef struct _RTL_RELATIVE_NAME {
	UNICODE_STRING RelativeName;
	HANDLE         ContainingDirectory;
	void*          CurDirRef;
} RTL_RELATIVE_NAME, *PRTL_RELATIVE_NAME;

typedef BOOLEAN(NTAPI*RtlDosPathNameToRelativeNtPathName_U_Prototype)(
	_In_       PCWSTR DosFileName,
	_Out_      PUNICODE_STRING NtFileName,
	_Out_opt_  PWSTR* FilePath,
	_Out_opt_  PRTL_RELATIVE_NAME RelativeName
);

typedef NTSTATUS(WINAPI*RtlFormatCurrentUserKeyPath_Prototype)(
	PUNICODE_STRING CurrentUserKeyPath
);

typedef VOID(WINAPI*RtlFreeUnicodeString_Prototype)(
	PUNICODE_STRING UnicodeString
);

static RtlDosPathNameToRelativeNtPathName_U_Prototype RtlDosPathNameToRelativeNtPathName_U = nullptr;
static RtlFormatCurrentUserKeyPath_Prototype RtlFormatCurrentUserKeyPath = nullptr;
static RtlFreeUnicodeString_Prototype RtlFreeUnicodeString = nullptr;

HidStatus _API Hid_InitializeWithNoConnection()
{
	if (!RtlDosPathNameToRelativeNtPathName_U)
	{
		*(FARPROC*)&RtlDosPathNameToRelativeNtPathName_U = GetProcAddress(
			GetModuleHandleW(L"ntdll.dll"),
			"RtlDosPathNameToRelativeNtPathName_U"
			);
		if (!RtlDosPathNameToRelativeNtPathName_U)
			return HID_SET_STATUS(FALSE, GetLastError());
	}

	if (!RtlFormatCurrentUserKeyPath)
	{
		*(FARPROC*)&RtlFormatCurrentUserKeyPath = GetProcAddress(
			GetModuleHandleW(L"ntdll.dll"),
			"RtlFormatCurrentUserKeyPath"
			);
		if (!RtlFormatCurrentUserKeyPath)
			return HID_SET_STATUS(FALSE, GetLastError());
	}

	if (!RtlFreeUnicodeString)
	{
		*(FARPROC*)&RtlFreeUnicodeString = GetProcAddress(
			GetModuleHandleW(L"ntdll.dll"),
			"RtlFreeUnicodeString"
			);
		if (!RtlFreeUnicodeString)
			return HID_SET_STATUS(FALSE, GetLastError());
	}

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus _API Hid_Initialize(PHidContext pcontext, const wchar_t* deviceName)
{
	HANDLE hdevice = INVALID_HANDLE_VALUE;
	PHidContextInternal context;
	HidStatus status;

	status = Hid_InitializeWithNoConnection();
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	if (!deviceName)
		deviceName = DEVICE_WIN32_NAME;

	hdevice = CreateFileW(
				deviceName,
				GENERIC_READ | GENERIC_WRITE,
				0,
				NULL, 
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				NULL
			);
	if (hdevice == INVALID_HANDLE_VALUE)
		return HID_SET_STATUS(FALSE, GetLastError());

	context = (PHidContextInternal)malloc(sizeof(HidContextInternal));
	if (context == nullptr)
	{
		CloseHandle(hdevice);
		return HID_SET_STATUS(FALSE, ERROR_NOT_ENOUGH_MEMORY);
	}

	context->hdevice = hdevice;
	*pcontext = (HidContext)context;

	return HID_SET_STATUS(TRUE, 0);
}

void _API Hid_Destroy(HidContext context)
{
	PHidContextInternal cntx = (PHidContextInternal)context;
	CloseHandle(cntx->hdevice);
	free(cntx);
}

bool ConvertToNtPath(const wchar_t* path, wchar_t* normalized, size_t normalizedLen)
{
	UNICODE_STRING ntPath;
	DWORD size;
	bool result = false;

	size = GetFullPathNameW(path, (DWORD)normalizedLen, normalized, NULL);
	if (size == 0)
		return false;

	memset(&ntPath, 0, sizeof(ntPath));

	if (RtlDosPathNameToRelativeNtPathName_U(normalized, &ntPath, NULL, NULL) == FALSE)
		return false;

	if (normalizedLen * sizeof(wchar_t) > ntPath.Length)
	{
		memcpy(normalized, ntPath.Buffer, ntPath.Length);
		normalized[ntPath.Length / sizeof(wchar_t)] = L'\0';
		result = true;
	}

	HeapFree(GetProcessHeap(), 0, ntPath.Buffer);

	return result;
}

bool NormalizeRegistryPath(HidRegRootTypes root, const wchar_t* key, wchar_t* normalized, size_t normalizedLen)
{
	static const wchar_t* hklm = L"\\Registry\\Machine\\";
	static const wchar_t* hku = L"\\Registry\\User\\";
	size_t keyLen, rootLen;

	keyLen = wcslen(key);

	if (root == RegHKCU)
	{
		UNICODE_STRING currUser;

		RtlFormatCurrentUserKeyPath(&currUser);

		rootLen = currUser.Length / sizeof(wchar_t);

		if (normalizedLen < rootLen + keyLen + 2)
		{
			RtlFreeUnicodeString(&currUser);
			return false;
		}

		memcpy(normalized, currUser.Buffer, rootLen * sizeof(wchar_t));
		normalized[rootLen] = L'\\';
		memcpy(normalized + rootLen + 1, key, keyLen * sizeof(wchar_t));
		normalized[rootLen + keyLen + 1] = L'\0';

		RtlFreeUnicodeString(&currUser);
	}
	else if (root == RegHKLM)
	{
		rootLen = wcslen(hklm);

		if (normalizedLen < rootLen + keyLen + 1)
			return false;

		memcpy(normalized, hklm, rootLen * sizeof(wchar_t));
		memcpy(normalized + rootLen, key, keyLen * sizeof(wchar_t));
		normalized[rootLen + keyLen] = L'\0';
	}
	else if (root == RegHKU)
	{
		rootLen = wcslen(hku);

		if (normalizedLen < rootLen + keyLen + 1)
			return false;

		memcpy(normalized, hku, rootLen * sizeof(wchar_t));
		memcpy(normalized + rootLen, key, keyLen * sizeof(wchar_t));
		normalized[rootLen + keyLen] = L'\0';
	}
	else
	{
		return false;
	}

	return true;
}

HidStatus AllocNormalizedPath(const wchar_t* path, wchar_t** normalized)
{
	enum { NORMALIZATION_OVERHEAD = 32 };
	wchar_t* buf;
	size_t len;

	len = wcslen(path) + NORMALIZATION_OVERHEAD;

	buf = (wchar_t*)malloc(len * sizeof(wchar_t));
	if (!buf)
		return HID_SET_STATUS(FALSE, ERROR_NOT_ENOUGH_MEMORY);

	if (!ConvertToNtPath(path, buf, len))
	{
		free(buf);
		return HID_SET_STATUS(FALSE, ERROR_INVALID_DATA);
	}

	*normalized = buf;
	return HID_SET_STATUS(TRUE, 0);
}

HidStatus AllocNormalizedRegistryPath(HidRegRootTypes root, const wchar_t* key, wchar_t** normalized)
{
	enum { NORMALIZATION_OVERHEAD = 96 };
	wchar_t* buf;
	size_t len;

	len = wcslen(key) + NORMALIZATION_OVERHEAD;

	buf = (wchar_t*)malloc(len * sizeof(wchar_t));
	if (!buf)
		return HID_SET_STATUS(FALSE, ERROR_NOT_ENOUGH_MEMORY);

	if (!NormalizeRegistryPath(root, key, buf, len))
	{
		free(buf);
		return HID_SET_STATUS(FALSE, ERROR_INVALID_DATA);
	}

	*normalized = buf;
	return HID_SET_STATUS(TRUE, 0);
}

void FreeNormalizedPath(wchar_t* normalized)
{
	free(normalized);
}

HidStatus SendIoctl_QueryDriverStatusPacket(PHidContextInternal context, HidActiveState* state)
{
	Hid_DriverStatus packet = { 0 };
	Hid_StatusPacket result;
	DWORD returned;

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_GET_DRIVER_STATE, &packet, sizeof(packet), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	*state = (result.info.state ? HidActiveState::StateEnabled : HidActiveState::StateDisabled);
	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_SetDriverStatusPacket(PHidContextInternal context, HidActiveState state)
{
	Hid_DriverStatus packet;
	Hid_StatusPacket result;
	DWORD returned;

	packet.state = (state == HidActiveState::StateEnabled ? 1 : 0);
	packet.reserved = 0;

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_SET_DRIVER_STATE, &packet, sizeof(packet), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_HideObjectPacket(PHidContextInternal context, const wchar_t* path, unsigned short type, HidObjId* objId)
{
	PHid_HideObjectPacket hide;
	Hid_StatusPacket result;
	size_t size, len, total;
	DWORD returned;

	len = wcslen(path);
	if (len == 0 || len > 1024)
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	// Pack data to packet

	total = (len + 1) * sizeof(wchar_t);
	size = sizeof(Hid_HideObjectPacket) + total;
	hide = (PHid_HideObjectPacket)_alloca(size);
	hide->dataSize = (unsigned short)total;
	hide->objType = type;

	memcpy((char*)hide + sizeof(Hid_HideObjectPacket), path, total);

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_ADD_HIDDEN_OBJECT, hide, (DWORD)size, &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	if (objId)
		*objId = result.info.id;

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_UnhideObjectPacket(PHidContextInternal context, unsigned short type, HidObjId objId)
{
	Hid_UnhideObjectPacket unhide;
	Hid_StatusPacket result;
	DWORD returned;

	unhide.objType = type;
	unhide.id = objId;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_REMOVE_HIDDEN_OBJECT, &unhide, sizeof(unhide), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_UnhideAllObjectsPacket(PHidContextInternal context, unsigned short type)
{
	Hid_UnhideAllObjectsPacket unhide;
	Hid_StatusPacket result;
	DWORD returned;

	unhide.objType = type;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_REMOVE_ALL_HIDDEN_OBJECTS, &unhide, sizeof(unhide), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_AddPsObjectPacket(PHidContextInternal context, const wchar_t* path, unsigned short type, HidPsInheritTypes inheritType, bool applyForProcess, HidObjId* objId)
{
	PHid_AddPsObjectPacket hide;
	Hid_StatusPacket result;
	size_t size, len, total;
	DWORD returned;

	len = wcslen(path);
	if (len == 0 || len > 1024)
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	// Pack data to packet

	total = (len + 1) * sizeof(wchar_t);
	size = sizeof(Hid_AddPsObjectPacket) + total;
	hide = (PHid_AddPsObjectPacket)_alloca(size);
	hide->dataSize = (unsigned short)total;
	hide->objType = type;
	hide->inheritType = inheritType;
	hide->applyForProcesses = applyForProcess;

	memcpy((char*)hide + sizeof(Hid_AddPsObjectPacket), path, total);

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_ADD_OBJECT, hide, (DWORD)size, &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	if (objId)
		*objId = result.info.id;

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_RemovePsObjectPacket(PHidContextInternal context, unsigned short type, HidObjId objId)
{
	Hid_RemovePsObjectPacket remove;
	Hid_StatusPacket result;
	DWORD returned;

	remove.objType = type;
	remove.id = objId;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_REMOVE_OBJECT, &remove, sizeof(remove), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_RemoveAllPsObjectsPacket(PHidContextInternal context, unsigned short type)
{
	Hid_UnhideAllObjectsPacket remove;
	Hid_StatusPacket result;
	DWORD returned;

	remove.objType = type;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_REMOVE_ALL_OBJECTS, &remove, sizeof(remove), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_GetPsStatePacket(PHidContextInternal context, HidProcId procId, unsigned short type, HidActiveState* state, HidPsInheritTypes* inheritType)
{
	char buffer[sizeof(Hid_StatusPacket) + sizeof(Hid_GetPsObjectInfoPacket)];
	PHid_GetPsObjectInfoPacket info;
	PHid_StatusPacket result;
	DWORD returned;

	memset(buffer, 0, sizeof(buffer));

	info = (PHid_GetPsObjectInfoPacket)buffer;
	info->objType = type;
	info->procId = procId;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_GET_OBJECT_STATE, info, sizeof(Hid_GetPsObjectInfoPacket), &buffer, sizeof(buffer), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned < sizeof(Hid_StatusPacket))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_BLOCK_LENGTH);

	result = (PHid_StatusPacket)buffer;
	info = (PHid_GetPsObjectInfoPacket)(buffer + sizeof(Hid_StatusPacket));

	if (!NT_SUCCESS(result->status))
		return HID_SET_STATUS(FALSE, result->status);

	if (returned != sizeof(Hid_StatusPacket) + sizeof(Hid_GetPsObjectInfoPacket))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_BLOCK_LENGTH);

	*state = (info->enable ? HidActiveState::StateEnabled : HidActiveState::StateDisabled);
	*inheritType = (HidPsInheritTypes)info->inheritType;

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_SetPsStatePacket(PHidContextInternal context, HidProcId procId, unsigned short type, HidActiveState state, HidPsInheritTypes inheritType)
{
	Hid_SetPsObjectInfoPacket info;
	Hid_StatusPacket result;
	DWORD returned;

	info.objType = type;
	info.procId = procId;
	info.enable = (state == HidActiveState::StateEnabled);
	info.inheritType = inheritType;

	// Send IOCTL to device

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_SET_OBJECT_STATE, &info, sizeof(info), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	// Check result

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus SendIoctl_SetHangProcessesExit(PHidContextInternal context, BOOLEAN state)
{
	Hid_BooleanOption packet;
	Hid_StatusPacket result;
	DWORD returned;

	packet.value = state;

	if (!DeviceIoControl(context->hdevice, HID_IOCTL_SET_HANG_PROCESSES_EXIT, &packet, sizeof(packet), &result, sizeof(result), &returned, NULL))
		return HID_SET_STATUS(FALSE, GetLastError());

	if (returned != sizeof(result))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	if (!NT_SUCCESS(result.status))
		return HID_SET_STATUS(FALSE, result.status);

	return HID_SET_STATUS(TRUE, 0);
}

// Control interface

HidStatus _API Hid_SetState(HidContext context, HidActiveState state)
{
	return SendIoctl_SetDriverStatusPacket((PHidContextInternal)context, state);
}

HidStatus _API Hid_GetState(HidContext context, HidActiveState* pstate)
{
	return SendIoctl_QueryDriverStatusPacket((PHidContextInternal)context, pstate);
}

// Registry hiding interface

HidStatus _API Hid_AddHiddenRegKey(HidContext context, HidRegRootTypes root, const wchar_t* regKey, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedRegistryPath(root, regKey, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_HideObjectPacket((PHidContextInternal)context, normalized, RegKeyObject, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveHiddenRegKey(HidContext context, HidObjId objId)
{
	return SendIoctl_UnhideObjectPacket((PHidContextInternal)context, RegKeyObject, objId);
}

HidStatus _API Hid_RemoveAllHiddenRegKeys(HidContext context)
{
	return SendIoctl_UnhideAllObjectsPacket((PHidContextInternal)context, RegKeyObject);
}

HidStatus _API Hid_AddHiddenRegValue(HidContext context, HidRegRootTypes root, const wchar_t* regValue, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedRegistryPath(root, regValue, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_HideObjectPacket((PHidContextInternal)context, normalized, RegValueObject, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveHiddenRegValue(HidContext context, HidObjId objId)
{
	return SendIoctl_UnhideObjectPacket((PHidContextInternal)context, RegValueObject, objId);
}

HidStatus _API Hid_RemoveAllHiddenRegValues(HidContext context)
{
	return SendIoctl_UnhideAllObjectsPacket((PHidContextInternal)context, RegValueObject);
}

// File system hiding interface

HidStatus _API Hid_AddHiddenFile(HidContext context, const wchar_t* filePath, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedPath(filePath, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_HideObjectPacket((PHidContextInternal)context, normalized, FsFileObject, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveHiddenFile(HidContext context, HidObjId objId)
{
	return SendIoctl_UnhideObjectPacket((PHidContextInternal)context, FsFileObject, objId);
}

HidStatus _API Hid_RemoveAllHiddenFiles(HidContext context)
{
	return SendIoctl_UnhideAllObjectsPacket((PHidContextInternal)context, FsFileObject);
}

HidStatus _API Hid_AddHiddenDir(HidContext context, const wchar_t* dirPath, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedPath(dirPath, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_HideObjectPacket((PHidContextInternal)context, normalized, FsDirObject, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveHiddenDir(HidContext context, HidObjId objId)
{
	return SendIoctl_UnhideObjectPacket((PHidContextInternal)context, FsDirObject, objId);
}

HidStatus _API Hid_RemoveAllHiddenDirs(HidContext context)
{
	return SendIoctl_UnhideAllObjectsPacket((PHidContextInternal)context, FsDirObject);
}

// Process exclude interface

HidStatus _API Hid_AddExcludedImage(HidContext context, const wchar_t* imagePath, HidPsInheritTypes inheritType, bool applyForProcess, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedPath(imagePath, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_AddPsObjectPacket((PHidContextInternal)context, normalized, PsExcludedObject, inheritType, applyForProcess, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveExcludedImage(HidContext context, HidObjId objId)
{
	return SendIoctl_RemovePsObjectPacket((PHidContextInternal)context, PsExcludedObject, objId);
}

HidStatus _API Hid_RemoveAllExcludedImages(HidContext context)
{
	return SendIoctl_RemoveAllPsObjectsPacket((PHidContextInternal)context, PsExcludedObject);
}

HidStatus _API Hid_GetExcludedState(HidContext context, HidProcId procId, HidActiveState* state, HidPsInheritTypes* inheritType)
{
	return SendIoctl_GetPsStatePacket((PHidContextInternal)context, procId, PsExcludedObject, state, inheritType);
}

HidStatus _API Hid_AttachExcludedState(HidContext context, HidProcId procId, HidPsInheritTypes inheritType)
{
	return SendIoctl_SetPsStatePacket((PHidContextInternal)context, procId, PsExcludedObject, HidActiveState::StateEnabled, inheritType);
}

HidStatus _API Hid_RemoveExcludedState(HidContext context, HidProcId procId)
{
	return SendIoctl_SetPsStatePacket((PHidContextInternal)context, procId, PsExcludedObject, HidActiveState::StateDisabled, HidPsInheritTypes::WithoutInherit);
}

// Process protect interface

HidStatus _API Hid_AddProtectedImage(HidContext context, const wchar_t* imagePath, HidPsInheritTypes inheritType, bool applyForProcess, HidObjId* objId)
{
	HidStatus status;
	wchar_t* normalized;

	status = AllocNormalizedPath(imagePath, &normalized);
	if (!HID_STATUS_SUCCESSFUL(status))
		return status;

	status = SendIoctl_AddPsObjectPacket((PHidContextInternal)context, normalized, PsProtectedObject, inheritType, applyForProcess, objId);
	FreeNormalizedPath(normalized);

	return status;
}

HidStatus _API Hid_RemoveProtectedImage(HidContext context, HidObjId objId)
{
	return SendIoctl_RemovePsObjectPacket((PHidContextInternal)context, PsProtectedObject, objId);
}

HidStatus _API Hid_RemoveAllProtectedImages(HidContext context)
{
	return SendIoctl_RemoveAllPsObjectsPacket((PHidContextInternal)context, PsProtectedObject);
}

HidStatus _API Hid_GetProtectedState(HidContext context, HidProcId procId, HidActiveState* state, HidPsInheritTypes* inheritType)
{
	return SendIoctl_GetPsStatePacket((PHidContextInternal)context, procId, PsProtectedObject, state, inheritType);
}

HidStatus _API Hid_AttachProtectedState(HidContext context, HidProcId procId, HidPsInheritTypes inheritType)
{
	return SendIoctl_SetPsStatePacket((PHidContextInternal)context, procId, PsProtectedObject, HidActiveState::StateEnabled, inheritType);
}

HidStatus _API Hid_RemoveProtectedState(HidContext context, HidProcId procId)
{
	return SendIoctl_SetPsStatePacket((PHidContextInternal)context, procId, PsProtectedObject, HidActiveState::StateDisabled, HidPsInheritTypes::WithoutInherit);
}

// Process other interface

HidStatus _API Hid_SetHangProcessesExit(HidContext context, BOOLEAN state)
{
	return SendIoctl_SetHangProcessesExit((PHidContextInternal)context, state);
}

HidStatus _API Hid_NormalizeFilePath(const wchar_t* filePath, wchar_t* normalized, size_t normalizedLen)
{
	if (!ConvertToNtPath(filePath, normalized, normalizedLen))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	return HID_SET_STATUS(TRUE, 0);
}

HidStatus _API Hid_NormalizeRegistryPath(HidRegRootTypes root, const wchar_t* regPath, wchar_t* normalized, size_t normalizedLen)
{
	if (!NormalizeRegistryPath(root, regPath, normalized, normalizedLen))
		return HID_SET_STATUS(FALSE, ERROR_INVALID_PARAMETER);

	return HID_SET_STATUS(TRUE, 0);
}
