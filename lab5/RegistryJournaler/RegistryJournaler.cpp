#include <iostream>
#include <Windows.h>
#include <sstream>

#include "../driver2/Driver.h"

int main(int argc, char* argv[])
{
	
    HANDLE journal_handle = CreateFile(L"\\\\.\\RegistryJournal", FILE_SHARE_WRITE, 0,
        nullptr, OPEN_EXISTING, 0, nullptr);

	if (journal_handle == INVALID_HANDLE_VALUE) {
		std::cout << "Can't launch. Driver not launched." << std::endl;
		std::cin.get();
		return 0;
	}

	if (argc != 2)
	{
		std::cout << "Invalid number of arguments. Expected 1(PID)." << std::endl;
	}

	std::istringstream s(argv[1]);
	unsigned long pid;
	if (s >> pid)
	{
		DWORD returned;
		std::cout << pid << std::endl;
		
		if (DeviceIoControl(journal_handle, IOCTL_SET_CURRENT_PID, 
			&pid, sizeof(pid), nullptr, 0,
			&returned, nullptr)) {
			return 0;
		}
		else {
			std::cout << "Error set value" << std::endl;
			return 1;
		}
	}
	else
	{
		std::cout << "Invalid PID argument value." << std::endl;
	}

	return 0;
}