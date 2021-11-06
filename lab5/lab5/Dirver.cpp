#include <ntddk.h>
#include <wdf.h>
#include "Driver.h"

#define DRIVER_TAG 'gTag'

void OnDriverUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS OnMJCloseCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS OnMJRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
void OnProcessCreated(PEPROCESS Process, HANDLE ProcessId,
    PS_CREATE_NOTIFY_INFO* CreateInfo);
NTSTATUS complete_irp_request(PIRP irp, NTSTATUS status, ULONG_PTR info);


FAST_MUTEX mutex;
LIST_ENTRY launched_processes_list_head;
LIST_ENTRY notify_user_list_head;

extern "C" NTSTATUS
    DriverEntry(
        _In_ PDRIVER_OBJECT     DriverObject,
        _In_ PUNICODE_STRING    RegistryPath
    )
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING device_name = RTL_CONSTANT_STRING(L"\\Device\\NotepadCalcLauncher");
    UNICODE_STRING symbolic_link = RTL_CONSTANT_STRING(L"\\??\\NotepadCalcLauncher");
    PDEVICE_OBJECT device;
    NTSTATUS status = STATUS_SUCCESS;

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

    status = PsSetCreateProcessNotifyRoutineEx(OnProcessCreated, FALSE);
    if (!NT_SUCCESS(status))
    {
        IoDeleteSymbolicLink(&symbolic_link);
        IoDeleteDevice(device);
        return status;
    }


    InitializeListHead(&launched_processes_list_head);
    InitializeListHead(&notify_user_list_head);
    ExInitializeFastMutex(&mutex);

    DriverObject->DriverUnload = OnDriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = OnMJCloseCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = OnMJCloseCreate;
    DriverObject->MajorFunction[IRP_MJ_READ] = OnMJRead;
    

    return status;
}


NTSTATUS complete_irp_request(PIRP irp, NTSTATUS status, ULONG_PTR info) {
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}


void OnProcessCreated(PEPROCESS Process, HANDLE ProcessId,
    PS_CREATE_NOTIFY_INFO* CreateInfo) {

    UNREFERENCED_PARAMETER(Process);

    if (CreateInfo != nullptr) {
        UNICODE_STRING processX = RTL_CONSTANT_STRING(PROCESS_X);
        if ((CreateInfo->ImageFileName == nullptr)
            || RtlCompareMemory(&processX, CreateInfo->ImageFileName, TRUE) != 0)
        {
            return;
        }
        
        event* event_item = (event*)ExAllocatePoolWithTag(PagedPool,
            sizeof(event), DRIVER_TAG);
        if (event_item == nullptr)
        {
            return;
        }

        event_item->info.ev_type = create_process;
        event_item->info.processId = HandleToULong(ProcessId);
        event_item->links_number = 2;

        ExAcquireFastMutex(&mutex);
        InsertTailList(&launched_processes_list_head, &event_item->launched_processes_list_addr);
        InsertTailList(&notify_user_list_head, &event_item->notify_user_list_addr);
        ExReleaseFastMutex(&mutex);
    }
    else {
        ULONG ulongProcessId = HandleToULong(ProcessId);
        event* killedProcess = nullptr;
        bool to_release = FALSE;
        ExAcquireFastMutex(&mutex);
        PLIST_ENTRY temp = launched_processes_list_head.Flink;
        while (temp != &launched_processes_list_head)
        {
            event* current = CONTAINING_RECORD(temp, event, launched_processes_list_addr);
            if (current->info.processId == ulongProcessId)
            {
                killedProcess = current;
                RemoveEntryList(&killedProcess->launched_processes_list_addr);
                killedProcess->links_number--;
                to_release = killedProcess->links_number == 0;
                break;
            }

            temp = temp->Flink;
        }

        ExReleaseFastMutex(&mutex);

        if (killedProcess == nullptr)
        {
            return;
        }

        if (to_release)
        {
            ExFreePool(killedProcess);
        }

        event* event_item = (event*)ExAllocatePoolWithTag(PagedPool,
            sizeof(event), DRIVER_TAG);
        if (event_item == NULL)
        {
            return;
        }

        event_item->info.ev_type = close_process;
        event_item->info.processId = HandleToULong(ProcessId);
        event_item->links_number = 2;

        ExAcquireFastMutex(&mutex);

        InsertTailList(&notify_user_list_head, &event_item->notify_user_list_addr);

        ExReleaseFastMutex(&mutex);
    }
}


NTSTATUS OnMJCloseCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return complete_irp_request(Irp, STATUS_SUCCESS, 0);
}


NTSTATUS OnMJRead(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack_location = IoGetCurrentIrpStackLocation(Irp);
    ULONG stack_length = stack_location->Parameters.Read.Length;
    if (stack_length < sizeof(event_info))
    {
        return complete_irp_request(Irp, STATUS_BUFFER_TOO_SMALL, 0);
    }

    event_info* buffer = (event_info*)Irp->AssociatedIrp.SystemBuffer;
    event* item = nullptr;
    bool to_release = FALSE;
    ExAcquireFastMutex(&mutex);
    if (notify_user_list_head.Flink != &notify_user_list_head)
    {
        item = CONTAINING_RECORD(notify_user_list_head.Flink, event, notify_user_list_addr);
        RemoveEntryList(&item->notify_user_list_addr);
        item->links_number--;
        to_release = item->links_number == 0;
    }

    ExReleaseFastMutex(&mutex);

    if (item != nullptr)
    {
        buffer->processId = item->info.processId;
        buffer->ev_type = item->info.ev_type;
        if (to_release)
        {
            ExFreePool(item);
        }

        return complete_irp_request(Irp, STATUS_SUCCESS, sizeof(event_info));
    }

    return complete_irp_request(Irp, STATUS_SUCCESS, 0);
}


void OnDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symbolic_link = RTL_CONSTANT_STRING(L"\\??\\NotepadCalcLauncher");
    PsSetCreateProcessNotifyRoutineEx(OnProcessCreated, TRUE);
    IoDeleteSymbolicLink(&symbolic_link);
    IoDeleteDevice(DriverObject->DeviceObject);
}