#pragma once


enum event_type {
	other,
	create_process,
	close_process
};

struct event_info {
	event_type ev_type;
	ULONG processId;
};

struct event {
	event_info info;
	LIST_ENTRY notify_user_list_addr;
	LIST_ENTRY launched_processes_list_addr;
	int links_number;
};

#define PROCESS_X L"\\??\\c:\\windows\\System32\\notepad.exe"
#define PROCESS_Y L"C:\\Windows\\System32\\mspaint.exe"