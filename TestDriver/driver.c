#include "include/test.h"
#include "include/amd_defs.h"
#include <intrin.h>
#pragma section(".rtest", read, write)
#pragma comment(linker, "/SECTION:rtest,RW,ALIGN=4096")
#pragma data_seg("rtest")
BOOLEAN g_driverTest = FALSE;
#pragma data_seg()
/*#define IOCTL_VM_START CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
NTSTATUS IoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_VM_START:
	{
		if (!g_VmStart) g_VmStart = TRUE;
		break;
	}
	default:
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}*/
VOID MyCallback(PVOID context)
{
	UNREFERENCED_PARAMETER(context);
	for (size_t i = 0; i < 10; i++)
	{
		if (test2() != STATUS_SUCCESS)
		{
			DbgPrintEx(77, 0, "[-]核心%d调用test2失败！\n", KeGetCurrentProcessorNumber());
		}
		NTSTATUS status = test();
		if (status == STATUS_ACCESS_DENIED)
		{
			DbgPrintEx(77, 0, "[-]核心%d拒绝访问！\n", KeGetCurrentProcessorNumber());
		}
		else if (status == STATUS_SUCCESS)
		{
			DbgPrintEx(77, 0, "[+]核心%d访问成功！\n", KeGetCurrentProcessorNumber());
		}
	}
	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = -10000 * 1000 * 30;
	//KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	for (size_t i = 0; i < 500; i++)
	{
		if (test2() != STATUS_SUCCESS)
		{
			DbgPrintEx(77, 0, "[-]核心%d调用test2失败！\n", KeGetCurrentProcessorNumber());
		}
		NTSTATUS status = test();
		if (status == STATUS_ACCESS_DENIED)
		{
			DbgPrintEx(77, 0, "[-]核心%d拒绝访问！\n", KeGetCurrentProcessorNumber());
		}
		else if (status == STATUS_SUCCESS)
		{
			DbgPrintEx(77, 0, "[+]核心%d访问成功！\n", KeGetCurrentProcessorNumber());
		}
	}
}
NTSTATUS UnloadDriver(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	return STATUS_SUCCESS;
}
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status = STATUS_SUCCESS;
	//SvmProtectDriverSection((UINT64)&g_driverTest, PAGE_SIZE, TRUE, NULL, 0);
	g_Unload = TRUE;
	g_bDebug = TRUE;
	if (!g_VmStart) g_VmStart = TRUE;
	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = -10000 * 1000;
	KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	PMDL mdl = IoAllocateMdl(&g_Test1, PAGE_SIZE, FALSE, FALSE, NULL);
	MmBuildMdlForNonPagedPool(mdl);
	PBOOLEAN mapTest1 = (PBOOLEAN)MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
	__writemsr(MSR_EFER, __readmsr(MSR_EFER)&~EFER_SVME);
	WRITE_PORT_UCHAR((PUCHAR)0xB2, 2);
	DbgPrintEx(77, 0, "[*]g_Test1 map address: 0x%llX.\n", (UINT64)mapTest1);
	DbgPrintEx(77, 0, "[*]driver_test address: 0x%llX.\n", (UINT64) & g_driverTest);
	PsTerminateProcessByPid(g_Pid);
#if DBG
	HANDLE hThread = NULL;
	for (int i = 0; i < 10; i++)
	{
		OBJECT_ATTRIBUTES objAttr = { 0 };
		InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
		PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &objAttr, NULL, NULL, (PKSTART_ROUTINE)MyCallback, NULL);
		if (hThread)
		{
			ZwClose(hThread);
			hThread = NULL;
		}
	}
#endif

	DriverObject->DriverUnload = UnloadDriver;
	return status;
}