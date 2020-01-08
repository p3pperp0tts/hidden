#pragma once

#include "Commands.h"

class CommandPsOption : public ICommand
{
	const wchar_t* m_command = nullptr;

	enum EOptionType {
		HangProcessesExitOn,
		HangProcessesExitOff,
	};

	EOptionType m_option;

public:

	CommandPsOption();
	virtual ~CommandPsOption();

	virtual bool CompareCommand(std::wstring& command);
	virtual void LoadArgs(Arguments& args, CommandModeType mode);
	virtual void PerformCommand(Connection& connection);

	virtual CommandPtr CreateInstance();
};
