#pragma once

enum event_type {
	other,
	create_process,
	close_process
};

struct event_info {
	event_type type;
	unsigned long processId;
};

#define PROCESS_Y L"C:\\Windows\\System32\\paint.exe"