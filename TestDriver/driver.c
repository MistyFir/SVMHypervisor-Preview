#include "include/test.h"
#include "include/amd_defs.h"
#include <intrin.h>
#define IOCTL_VM_START CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SVM_CONTROL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ_MSR CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WRITE_MSR CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_TEST_THREAD CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_PROTECTED_PROCESS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FORCE_TERMINATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
VOID MyCallback(PVOID context);
typedef struct _SVM_RUN_CONTROL
{
	BOOLEAN Debug;
	BOOLEAN Unload;
}SVM_RUN_CONTROL, * PSVM_RUN_CONTROL;
typedef struct _READ_MEMORY_INFO
{
	PVOID Address;
	UINT64 OutputBufferSize;
	PVOID OutputBuffer;
}READ_MEMORY_INFO,*PREAD_MEMORY_INFO;
typedef struct _WRITE_MEMORY_INFO
{
	PVOID Address;
	UINT64 InputBufferSize;
	PVOID InputBuffer;
}WRITE_MEMORY_INFO, * PWRITE_MEMORY_INFO;
typedef struct _READ_MSR_INFO
{
	UINT32 Msr;
	UINT64 OutputValue;
}READ_MSR_INFO, * PREAD_MSR_INFO;
typedef struct _WRITE_MSR_INFO
{
	UINT32 Msr;
	UINT64 InputValue;
}WRITE_MSR_INFO, * PWRITE_MSR_INFO;
UINT64 TestPid = 0;
#pragma section(".rtest", read, write)
#pragma comment(linker, "/SECTION:rtest,RW,ALIGN=4096")
#pragma data_seg("rtest")
BOOLEAN g_driverTest = FALSE;
PDEVICE_OBJECT g_DeviceObject = NULL;
PMDL g_NtOpenProcessMdl = NULL;
PMDL g_Test1Mdl = NULL;
PVOID g_Test1Map = NULL;
#pragma data_seg()
#pragma section(".hook", read, execute)
#pragma code_seg(".hook")
#define PROCESS_TERMINATE 0x0001
NTSTATUS HookNtOpenProcess(PHOOK_REGS Regs)
{
	PCLIENT_ID ClientId = (PCLIENT_ID)Regs->R9;
	if (TestPid != 0 && ClientId->UniqueProcess == (HANDLE)TestPid && PsGetCurrentProcessId() != (HANDLE)TestPid)
	{
#if DBG
		ACCESS_MASK DesiredAccess = (ACCESS_MASK)Regs->Rdx;
		if (g_bDebug && (DesiredAccess & PROCESS_TERMINATE))
		{
			DbgPrintEx(77, 0, "[*]NPT HOOK NtOpenProcess Success! Target PID: %llu Current PID: %llu Current TID: %llu\n", (DWORD64)TestPid, (DWORD64)PsGetCurrentProcessId(), (DWORD64)PsGetCurrentThreadId());
		}
#endif
		return STATUS_ACCESS_DENIED;
	}
	return STATUS_SUCCESS;
}
NTSTATUS HookTestFunc(PHOOK_REGS Regs)
{
	DbgPrintEx(77, 0, "[*]HookTestFunc Param5 Value: %llu\n", Regs->Param5);
	DbgPrintEx(77, 0, "[*]HookTestFunc Param6 Value: %llu\n", Regs->Param6);
	DbgPrintEx(77, 0, "[*]HookTestFunc Param7 Value: %llu\n", Regs->Param7);
	Regs->Param5 -= 1;
	Regs->Param6 -= 1;
	return STATUS_SUCCESS;
}
#pragma code_seg()
#pragma comment(linker, "/SECTION:.hook,RE,ALIGN=4096")
#pragma section(".test", read, execute)
#pragma code_seg(".test")
NTSTATUS TestFunc(UINT64 Arg1, UINT64 Arg2, UINT64 Arg3, UINT64 Arg4, UINT64 Arg5, UINT64 Arg6, UINT64 Arg7)
{
	UNREFERENCED_PARAMETER(Arg1);
	UNREFERENCED_PARAMETER(Arg2);
	UNREFERENCED_PARAMETER(Arg3);
	UNREFERENCED_PARAMETER(Arg4);
	DbgPrintEx(77, 0, "[*]TestFunc Param5 Value: %llu\n", Arg5);
	DbgPrintEx(77, 0, "[*]TestFunc Param6 Value: %llu\n", Arg6);
	DbgPrintEx(77, 0, "[*]TestFunc Param7 Value: %llu\n", Arg7);
	return STATUS_SUCCESS;
}
#pragma code_seg()
#pragma comment(linker, "/SECTION:.test,RE,ALIGN=4096")
NTSTATUS CreateClose(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
NTSTATUS IoControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS status = STATUS_SUCCESS;
	UINT64 inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
	Irp->IoStatus.Information = 0;
	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_VM_START:
	{
		if (!g_VmStart) g_VmStart = TRUE;
		break;
	}
	case IOCTL_SVM_CONTROL:
	{
		if (inputLength >= sizeof(SVM_RUN_CONTROL))
		{
			PSVM_RUN_CONTROL buffer = (PSVM_RUN_CONTROL)Irp->AssociatedIrp.SystemBuffer;
			if (g_VmStart == FALSE)
			{
				g_bDebug = buffer->Debug;
				g_Unload = buffer->Unload;
			}
			else status = STATUS_ACCESS_DENIED;
			Irp->IoStatus.Information = sizeof(SVM_RUN_CONTROL);
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_READ_MEMORY:
	{
		if (inputLength >= sizeof(READ_MEMORY_INFO))
		{
			PREAD_MEMORY_INFO info = (PREAD_MEMORY_INFO)Irp->AssociatedIrp.SystemBuffer;
			memcpy(info->OutputBuffer, info->Address, info->OutputBufferSize);
			Irp->IoStatus.Information = sizeof(READ_MEMORY_INFO);
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_WRITE_MEMORY:
	{
		if (inputLength >= sizeof(WRITE_MEMORY_INFO))
		{
			PWRITE_MEMORY_INFO info = (PWRITE_MEMORY_INFO)Irp->AssociatedIrp.SystemBuffer;
			__try
			{
				memcpy(info->Address, info->InputBuffer, info->InputBufferSize);
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				status = GetExceptionCode();
			}
			Irp->IoStatus.Information = 0;
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_READ_MSR:
	{
		if (inputLength >= sizeof(READ_MSR_INFO))
		{
			PREAD_MSR_INFO info = (PREAD_MSR_INFO)Irp->AssociatedIrp.SystemBuffer;
			info->OutputValue = __readmsr(info->Msr);
			Irp->IoStatus.Information = sizeof(READ_MSR_INFO);
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_WRITE_MSR:
	{
		if (inputLength >= sizeof(WRITE_MSR_INFO))
		{
			PWRITE_MSR_INFO info = (PWRITE_MSR_INFO)Irp->AssociatedIrp.SystemBuffer;
			__writemsr(info->Msr, info->InputValue);
			Irp->IoStatus.Information = 0;
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_TEST_THREAD:
	{
		HANDLE hThread = NULL;
		for (int i = 0; i < 8; i++)
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
		break;
	}
	case IOCTL_SET_PROTECTED_PROCESS:
	{
		if (inputLength >= sizeof(UINT64))
		{
			UINT64* pid = (UINT64*)Irp->AssociatedIrp.SystemBuffer;
			TestPid = *pid;
			g_Pid = TestPid;
			Irp->IoStatus.Information = 0;
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	case IOCTL_FORCE_TERMINATE:
	{
		if (inputLength >= sizeof(UINT64))
		{
			UINT64 pid = *(PUINT64)Irp->AssociatedIrp.SystemBuffer;
			PsTerminateProcessByPid(pid);
		}
		else status = STATUS_BUFFER_TOO_SMALL;
		break;
	}
	default:
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}
VOID TestInstallHook(PVOID funcAddress,PVOID HookAddress , UINT64 MapSize)
{
	PHOOK_INFO hookInfo = SvmAddHookInfo(NULL, (UINT64)funcAddress, MapSize);
	if (!hookInfo) KeBugCheck(0x3B);
	if (!SvmCreateShadowPage(hookInfo, 0)) KeBugCheck(0x3B);
	if (!SvmCreateShadowPage(hookInfo, 1)) KeBugCheck(0x3B);
	UINT8 codeLen = GetInstructionLength(funcAddress, 20);
	UINT8 jmpCode[20] = { 0 };
	memset(jmpCode, 0x90, sizeof(jmpCode));
	jmpCode[0] = INT_3;
	SvmShadowCopyMemory(hookInfo, 1, (UINT64)funcAddress, jmpCode, codeLen);
	PHOOK_FUNC_INFO hookFuncInfo = SvmAddHookFuncInfo(hookInfo, (UINT64)funcAddress, (UINT64)HookAddress, 20);
	if (!hookFuncInfo) KeBugCheck(0x3B);
	PJMP_FUNC_TRAMPOLINE jmpTrampoline = (PJMP_FUNC_TRAMPOLINE)SvmAllocateJmpTrampoline(hookFuncInfo, SvmGetJmpCodeFuncBufferLength());
	if (!jmpTrampoline) KeBugCheck(0x3B);
	SvmGetJmpCodeFuncBuffer(jmpTrampoline, SvmGetJmpCodeFuncBufferLength());
	hookFuncInfo->JumpTrampolineOffset = sizeof(jmpTrampoline->Data);
	DEF_PTR(UINT64, jmpTrampoline->Data.ReturnAddress, 0) = (UINT64)jmpTrampoline + SvmGetReturnOffset();
	DEF_PTR(UINT64, jmpTrampoline->Data.HookFuncAddress, 0) = (UINT64)HookAddress;
	DEF_PTR(UINT64, jmpTrampoline->Data.CallbackAddress, 0) = (UINT64)funcAddress + codeLen;
	memcpy(jmpTrampoline->Execute.OriginalCode, funcAddress, codeLen);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SvmSetGuestShadowPage(SvmGetCpuContextIndex(i), hookInfo, 0, TRUE, FALSE);
	}
}
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
VOID UnloadSvmMdl(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	if (g_NtOpenProcessMdl)
	{
		MmUnlockPages(g_NtOpenProcessMdl);
		IoFreeMdl(g_NtOpenProcessMdl);
		g_NtOpenProcessMdl = NULL;
	}
}
VOID UnloadDriver(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNICODE_STRING symbolicLink =
		RTL_CONSTANT_STRING(L"\\DosDevices\\SVMTest");
	if (g_Test1Map && g_Test1Mdl)
	{
		MmUnmapLockedPages(g_Test1Map, g_Test1Mdl);
		g_Test1Map = NULL;
	}
	if (g_Test1Mdl)
	{
		IoFreeMdl(g_Test1Mdl);
		g_Test1Mdl = NULL;
	}
	IoDeleteSymbolicLink(&symbolicLink);
	IoDeleteDevice(g_DeviceObject);
}
NTSTATUS TestStartThread(PVOID context)
{
	PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)context;
	g_MdlUnloadCallback = UnloadSvmMdl;
	SvmProtectDriverSection((UINT64)HookNtOpenProcess, PAGE_SIZE, FALSE, NULL, 0);
	UNICODE_STRING funcName = RTL_CONSTANT_STRING(L"NtOpenProcess");
	PVOID funcAddress = MmGetSystemRoutineAddress(&funcName);
	PMDL mdl = IoAllocateMdl(funcAddress, PAGE_SIZE * 3, FALSE, FALSE, NULL);
	if (!mdl) KeBugCheckEx(0x3B, (ULONG_PTR)IoAllocateMdl, 0, 0, 0);
	MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
	g_NtOpenProcessMdl = mdl;
	TestInstallHook(funcAddress, (PVOID)HookNtOpenProcess, PAGE_SIZE * 2);
	TestInstallHook((PVOID)TestFunc, (PVOID)HookTestFunc, PAGE_SIZE);
	while (g_VmStart == FALSE)
	{
		LARGE_INTEGER time = { 0 };
		time.QuadPart = -10000 * 1000;
		KeDelayExecutionThread(KernelMode, FALSE, &time);
	}
	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = -10000 * 1000;
	KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	mdl = IoAllocateMdl(&g_Test1, PAGE_SIZE, FALSE, FALSE, NULL);
	if (!mdl) KeBugCheckEx(0x3B, (ULONG_PTR)IoAllocateMdl, 0, 0, 0);
	MmBuildMdlForNonPagedPool(mdl);
	g_Test1Mdl = mdl;
	PBOOLEAN mapTest1 = (PBOOLEAN)MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
	g_Test1Map = mapTest1;
	if ((__readmsr(MSR_EFER) & EFER_SVME) == 0)
	{
		DbgPrintEx(77, 0, "[-]SVME is not enabled! \n");
	}
	__writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
	__writemsr(MSR_VM_HSAVE_PA, 0);
	UINT64 hsavePa = __readmsr(MSR_VM_HSAVE_PA);
	DbgPrintEx(77, 0, "[*]Hsave Physical Address: 0x%llX\n", hsavePa);
	WRITE_PORT_UCHAR((PUCHAR)0xB2, 2);
	DbgPrintEx(77, 0, "[*]g_Test1 map address: 0x%llX.\n", (UINT64)mapTest1);
	DbgPrintEx(77, 0, "[*]driver_test address: 0x%llX.\n", (UINT64)&g_driverTest);
	if (g_Unload) DriverObject->DriverUnload = UnloadDriver;
	TestFunc(0, 0, 0, 0, 5, 6, 7);
	__writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
	__writemsr(MSR_VM_HSAVE_PA, 3);
	hsavePa = __readmsr(MSR_VM_HSAVE_PA);
	DbgPrintEx(77, 0, "[*]Hsave Physical Address: 0x%llX\n", hsavePa);
	return STATUS_SUCCESS;
}
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject = NULL;

	UNICODE_STRING deviceName =
		RTL_CONSTANT_STRING(L"\\Device\\SVMTest");

	UNICODE_STRING symbolicLink =
		RTL_CONSTANT_STRING(L"\\DosDevices\\SVMTest");

	DriverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoControl;
	status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
	if (!NT_SUCCESS(status))
	{
		return status;
	}
	status = IoCreateSymbolicLink(
		&symbolicLink,
		&deviceName
	);
	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(deviceObject);
		return status;
	}
	deviceObject->Flags |= DO_BUFFERED_IO;
	deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	g_DeviceObject = deviceObject;
	HANDLE hThread = NULL;
	PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, (PKSTART_ROUTINE)TestStartThread, DriverObject);
	if(hThread)
	{
		ZwClose(hThread);
		hThread = NULL;
	}
	return status;
}