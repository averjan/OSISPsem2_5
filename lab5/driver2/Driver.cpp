#include "Driver.h"
#include <ntddk.h>
#include <wdf.h>
#include <stdio.h>
#include <stdlib.h>

#define DRIVER_TAG 'ggaT'

void OnDriverUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS OnMJCloseCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnMJDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2);
NTSTATUS complete_irp_request(PIRP irp, NTSTATUS status, ULONG_PTR info);

ULONG current_pid;
LARGE_INTEGER cookie;

extern "C" NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT     DriverObject,
    _In_ PUNICODE_STRING    RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\RegistryJournal");
    UNICODE_STRING symbolic_link = RTL_CONSTANT_STRING(L"\\??\\RegistryJournal");
    PDEVICE_OBJECT device;
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"2121.21212121212121");

    status = IoCreateDevice(DriverObject, 0, &device_name,
        FILE_DEVICE_UNKNOWN, 0, FALSE, &device);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    device->Flags |= DO_BUFFERED_IO;

    status = IoCreateSymbolicLink(&symbolic_link, &device_name);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(device);
        return status;
    }

    status = CmRegisterCallbackEx(OnRegistryNotify, &altitude,
        DriverObject, nullptr, &cookie, nullptr);

    if (!NT_SUCCESS(status))
    {
        IoDeleteSymbolicLink(&symbolic_link);
        IoDeleteDevice(device);
        return status;
    }

    DriverObject->DriverUnload = OnDriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = OnMJCloseCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = OnMJCloseCreate;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = OnMJDeviceControl;


    return status;
}


NTSTATUS complete_irp_request(PIRP irp, NTSTATUS status, ULONG_PTR info) {
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}


NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(arg1);
    if ((REG_NOTIFY_CLASS)(ULONG_PTR)arg1 != RegNtPostSetValueKey
        && (REG_NOTIFY_CLASS)(ULONG_PTR)arg1 != RegNtPostCreateKey
        && (REG_NOTIFY_CLASS)(ULONG_PTR)arg1 != RegNtPostDeleteKey)
    {
        return STATUS_SUCCESS;
    }

    ULONG pid = HandleToULong(PsGetCurrentProcessId());
    if (pid != current_pid)
    {
        return STATUS_SUCCESS;
    }

    REG_POST_OPERATION_INFORMATION* info = (REG_POST_OPERATION_INFORMATION*)arg2;
    if (!NT_SUCCESS(info->Status))
    {
        return STATUS_SUCCESS;
    }

    UNICODE_STRING file_name = RTL_CONSTANT_STRING(L"\\SystemRoot\\RegistryJournal.txt");
    OBJECT_ATTRIBUTES attributes;
    
    InitializeObjectAttributes(&attributes, &file_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE file_handle;
    IO_STATUS_BLOCK io_status_block;

    NTSTATUS status = ZwCreateFile(
        &file_handle,
        FILE_APPEND_DATA,
        &attributes,
        &io_status_block,
        0,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN_IF,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
        nullptr,
        0
    );

    if (!NT_SUCCESS(status))
    {
        return STATUS_SUCCESS;
    }
    
    REG_SET_VALUE_KEY_INFORMATION* value_key_information
        = (REG_SET_VALUE_KEY_INFORMATION*)info->PreInformation;
    if (value_key_information == nullptr)
    {
        return STATUS_SUCCESS;
    }

    CHAR buffer[512];
    LARGE_INTEGER sysTime;
	KeQuerySystemTime(&sysTime);
    PCUNICODE_STRING name;
    CmCallbackGetKeyObjectIDEx(&cookie, info->Object, nullptr, &name, 0);
    int pos = sprintf(buffer, "Process ID: %u\nTime: %u\nValue:%d\n", pid, (ULONG)sysTime.QuadPart, (int)value_key_information->DataSize);

    CmCallbackReleaseKeyObjectIDEx(name);


    int buf_left = sizeof(buffer) - pos - (int)strlen("\n");
    int data_size = (int)value_key_information->DataSize > buf_left ? buf_left : (int)value_key_information->DataSize;
    char reg_data_buffer[512];
    wcstombs(reg_data_buffer, (WCHAR*)value_key_information->Data, data_size);
    
    for (int i = 0; i < data_size; i++)
    {
        sprintf(buffer + pos + i, "%c", reg_data_buffer[i]);
    }
    
    sprintf(buffer + pos + data_size, "\n");
    
    LARGE_INTEGER eof;
    eof.HighPart = 0xffffffff;
    eof.LowPart = FILE_WRITE_TO_END_OF_FILE;

    
    status = ZwWriteFile(
        file_handle,
        NULL,
        NULL,
        NULL,
        &io_status_block,
        (PVOID)buffer,
        (ULONG)strlen(buffer),
        &eof,
        NULL
    );
    
    ZwClose(file_handle);
    if (!NT_SUCCESS(status))
    {
        return STATUS_SUCCESS;
    }

    
    return STATUS_SUCCESS;
}


NTSTATUS OnMJCloseCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return complete_irp_request(Irp, STATUS_SUCCESS, 0);
}


NTSTATUS OnMJDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack_location = IoGetCurrentIrpStackLocation(Irp);
    if (stack_location->Parameters.DeviceIoControl.IoControlCode == IOCTL_SET_CURRENT_PID)
    {
        if (stack_location->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            return complete_irp_request(Irp, status, 0);
        }
        
        current_pid = *(ULONG*)stack_location->Parameters.DeviceIoControl.Type3InputBuffer;
    }
    else
    {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    return complete_irp_request(Irp, status, 0);
}


void OnDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symbolic_link = RTL_CONSTANT_STRING(L"\\??\\RegistryJournal");
    IoDeleteSymbolicLink(&symbolic_link);
    IoDeleteDevice(DriverObject->DeviceObject);
    CmUnRegisterCallback(cookie);
}
