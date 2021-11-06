#include <Windows.h>

#include <iostream>
#include <map>

#include "../lab5/Driver.h"

int main(void)
{
	std::map<long, HANDLE> notepad_calc_pairs;
	HANDLE file_handle = CreateFile(L"\\\\.\\NotepadCalcLauncher", GENERIC_READ,
		0, nullptr,
		OPEN_EXISTING, 0, nullptr);

	if (file_handle == INVALID_HANDLE_VALUE) {
		std::cout << "Can't launch. Driver not launched." << std::endl;
		std::cin.get();
		return 0;
	}

	while (true)
	{
		event_info ev_info = { };
		DWORD bytes;
		if (!ReadFile(file_handle, &ev_info, 
			sizeof(event_info), &bytes, nullptr))
		{
			std::cout << "Invalid io" << std::endl;
			break;
		}

		if (bytes == 0)
		{
			Sleep(300);
			continue;
		}

		if (bytes != sizeof(event_info))
		{
			std::cout << "Invalid io" << std::endl;
			break;
		}

		if (ev_info.ev_type == create_process)
		{
			std::cout << "Create process -  " << ev_info.processId << std::endl;
			STARTUPINFO startup_info = { sizeof(STARTUPINFO), 0 };
			startup_info.cb = sizeof(startup_info);
			PROCESS_INFORMATION process_info = { 0 };
			CreateProcessW(PROCESS_Y, nullptr,
				0, 0, 0,
				CREATE_DEFAULT_ERROR_MODE, 0,
				0, &startup_info, &process_info);
			CloseHandle(process_info.hThread);
			notepad_calc_pairs[ev_info.processId] = process_info.hProcess;
			continue;
		}

		if (ev_info.ev_type == close_process)
		{
			std::cout << "Close process - " << ev_info.processId << std::endl;
			TerminateProcess(notepad_calc_pairs[ev_info.processId], 1);
			CloseHandle(notepad_calc_pairs[ev_info.processId]);
			continue;
		}

		break;
	}

	CloseHandle(file_handle);
	std::cout << "Error working with driver." << std::endl;
	std::cin.get();
	return 0;
}