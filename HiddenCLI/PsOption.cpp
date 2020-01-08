#include "PsOption.h"
#include <iostream>

using namespace std;

CommandPsOption::CommandPsOption() : m_command(L"/psoption")
{
}

CommandPsOption::~CommandPsOption()
{
}

bool CommandPsOption::CompareCommand(std::wstring& command)
{
	return (command == m_command);
}

void CommandPsOption::LoadArgs(Arguments& args, CommandModeType mode)
{
	wstring option, enable;

	if (!args.GetNext(option))
		throw WException(ERROR_INVALID_PARAMETER, L"Error, mismatched argument #1 for command 'psoption'");

	if (option == L"HangProcessesExit")
	{
		if (!args.GetNext(enable))
			throw WException(ERROR_INVALID_PARAMETER, L"Error, mismatched argument #2 for command 'psoption'");

		if (enable == L"on")
			m_option = EOptionType::HangProcessesExitOn;
		else if (enable == L"off")
			m_option = EOptionType::HangProcessesExitOff;
		else
			throw WException(ERROR_INVALID_PARAMETER, L"Error, mismatched argument #2 for command 'psoption'");
	}
	else
		throw WException(ERROR_INVALID_PARAMETER, L"Error, mismatched argument #1 for command 'psoption'");
}

void CommandPsOption::PerformCommand(Connection& connection)
{
	HidStatus status;

	switch (m_option)
	{
		case EOptionType::HangProcessesExitOn:
			status = Hid_SetHangProcessesExit(connection.GetContext(), TRUE);
			break;
		case EOptionType::HangProcessesExitOff:
			status = Hid_SetHangProcessesExit(connection.GetContext(), FALSE);
			break;
		default:
			throw WException(ERROR_UNKNOWN_COMPONENT, L"Internal error, invalid type for command 'psoption'");
	}
	
	if (!HID_STATUS_SUCCESSFUL(status))
		throw WException(HID_STATUS_CODE(status), L"Error, command 'psoption' rejected");

	g_stderr << L"Command 'psoption' successful" << endl;
}

CommandPtr CommandPsOption::CreateInstance()
{
	return CommandPtr(new CommandPsOption());
}
