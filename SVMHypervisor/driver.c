#include <ntifs.h>
#include "CPUID.h"
#include "PTE.h"
#include "asmfunc.h"
#include "VMCB.h"
#include "amd_defs.h"
#include "export_func.h"
#include "Hook.h"
#include <ntstrsafe.h>
#include <intrin.h>
VOID DriverUnload(PDRIVER_OBJECT DriverObject);
#define MAX_READ 256
#define PROCESS_TERMINATE 0x0001
#define ORIGINAL_CODE_SIZE 15
#define VMASM_LENGTH (AsmGetVmAsmEndAddress() - AsmGetVmAsmStartAddress())
#pragma warning(disable:4311)
#pragma warning(disable:4702)
#pragma warning(disable:6387)
typedef NTSTATUS(__stdcall* _ZwOpenProcess)(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PCLIENT_ID ClientId);
typedef NTSTATUS(__stdcall* _PsTerminateThreadByPointer)(PETHREAD pThread, NTSTATUS exitCode, BOOLEAN bDirectTerminate);
void EndVmExitHandlerAddress();
static void EntryStartAddr();
static void EndEntryAddr();
#define ENTRY_SECTION_SIZE ((ULONG_PTR)EndEntryAddr - (ULONG_PTR)EntryStartAddr)
BOOLEAN PrepareAllCpus();
#define TEST_PAGE_SIZE (PAGE_SIZE * 2 - 3)
#pragma section(".entry",read,execute)
#pragma section("shadow",read,write)
#pragma section("test", read, write)
#pragma data_seg("test")
UINT8 ReservedTest[PAGE_SIZE - 0x20] = { 0 };
UINT8 TestBytes[PAGE_SIZE + 0x20] = { 0 };
#pragma data_seg()
#pragma comment(linker, "/SECTION:test,RW,ALIGN=4096")
#pragma section(".vmexit", read, execute)
#pragma section(".HOOK", read, execute)
#pragma section("roc", read, write)
#pragma data_seg("shadow$001")
UINT64 ShadowStart = 0;
#pragma data_seg()
#pragma data_seg("shadow$002")
LIST_ENTRY HookListHead = { 0 };
KSPIN_LOCK HookListLock = { 0 };
PCALLBACK_OBJECT g_PowerCallbackObj = NULL;
PETHREAD g_ResetShadowPageThreadObject = NULL;
PVOID g_PowerCallbackHandle = NULL;
PCPU_CONTEXT g_CpuContexts = NULL;
PAGE_INFO PageList = { 0 };
PVOID g_ReadMemoryTable[MAX_READ] = { 0 };
KSPIN_LOCK g_CpuidCallbackListLock = { 0 };
KSPIN_LOCK g_BpCallbackListLock = { 0 };
KSPIN_LOCK g_HyperCallbackListLock = { 0 };
VMEXIT_CALLBACK g_CpuidCallbackList[MAX_CALLBACK_COUNT] = { 0 };
VMEXIT_CALLBACK g_BpCallbackList[MAX_CALLBACK_COUNT] = { 0 };
VMEXIT_CALLBACK g_HyperCallbackList[MAX_CALLBACK_COUNT] = { 0 };
#pragma data_seg()
#pragma data_seg("shadow$003")
UINT64 ShadowEnd = 0;
#pragma data_seg()
#define SHADOW_SIZE ((((UINT64)&ShadowEnd)+sizeof(UINT64)) - (UINT64)&ShadowStart)
#pragma comment(linker, "/SECTION:shadow,RW,ALIGN=4096")
#pragma data_seg("roc")
BOOLEAN g_Test1 = FALSE;
BOOLEAN g_VmStart = FALSE;
volatile BOOLEAN g_bSvmRunning = 0;
volatile ULONG CpuCount = 0;
volatile BOOLEAN g_SuspendGuest = FALSE;
PMDL g_Mdl = NULL;
BOOLEAN g_Unload = FALSE;
BOOLEAN g_bDebug = FALSE;
PVOID g_OriginalFunc = NULL;
MEMORY_INFO g_ZeroPage = { 0 };
#pragma code_seg()
#pragma comment(linker, "/SECTION:roc,RW,ALIGN=4096")
typedef NTSTATUS(__stdcall* SVM_WORKER)(ULONG_PTR context);
#pragma section("nroc", read, write)
#pragma data_seg("nroc")
PUINT8 g_Address = NULL;
BOOLEAN g_Test = FALSE;
DWORD64 g_Pid = 0;
#pragma code_seg()
#pragma comment(linker, "/SECTION:nroc,RW,ALIGN=4096")
#pragma code_seg(".entry$001")
void EntryStartAddr() { return; }
PVOID SearchPattern(PVOID baseAddr, SIZE_T size, UCHAR* pattern, const char* mask)
{
	PVOID result = NULL;
	UINT64 patternLength = strlen(mask);
	BOOLEAN found = FALSE;
	for (UINT64 i = 0; i <= size - patternLength; i++) {
		found = TRUE;
		for (UINT64 j = 0; j < patternLength; j++) {
			if (mask[j] != '?' && *(UCHAR*)((DWORD_PTR)baseAddr + i + j) != pattern[j]) {
				found = FALSE;
				break;
			}
		}
		if (found) {
			result = (PVOID)((DWORD_PTR)baseAddr + i);
			break;
		}
	}
	return result;
}
PVOID FindPspTerminateThreadByPointer()
{
	PVOID PspTerminateThreadByPointerAddr = NULL;
	UNICODE_STRING funcName = RTL_CONSTANT_STRING(L"PsTerminateSystemThread");
	PVOID pPsTerminateSystemThread = MmGetSystemRoutineAddress(&funcName);
	if (!pPsTerminateSystemThread)
	{
		return NULL;
	}
	UCHAR pattern[] = { 0xe8,0x00,0x00,0x00,0x00 };
	char mask[] = "x????";
	DWORD_PTR uValueA = 0;
	INT32 iValueA = 0;
	uValueA = (DWORD_PTR)SearchPattern(pPsTerminateSystemThread, 0x2F, pattern, mask);
	if (!uValueA)
	{
		return NULL;
	}
	iValueA = *(INT32*)(uValueA + 1);
	uValueA += 5;
	PspTerminateThreadByPointerAddr = (PVOID)(uValueA + iValueA);
	return PspTerminateThreadByPointerAddr;
}
NTSTATUS PsTerminateThreadByPointer(PETHREAD pThread, NTSTATUS exitCode, BOOLEAN bDirectTerminate)
{
	_PsTerminateThreadByPointer pPsTerminateThreadByPointer = (_PsTerminateThreadByPointer)g_OriginalFunc;
	if (!pPsTerminateThreadByPointer)
	{
		return STATUS_NO_MEMORY;
	}
	return pPsTerminateThreadByPointer(pThread, exitCode, bDirectTerminate);
}
NTSTATUS PsTerminateProcessByPid(DWORD64 pid)
{
	NTSTATUS funcStatus = STATUS_SUCCESS;
	PETHREAD ethrd = NULL;
	PEPROCESS pProcess = NULL;
	for (DWORD64 tid = 4; tid < 262144; tid += 4)
	{
		NTSTATUS status = PsLookupThreadByThreadId((HANDLE)tid, &ethrd);
		if (!NT_SUCCESS(status))
		{
			continue;
		}
		pProcess = IoThreadToProcess(ethrd);
		if (!pProcess)
		{
			ObDereferenceObject(ethrd);
			ethrd = NULL;
			continue;
		}
		DWORD64 processId = (DWORD64)PsGetProcessId(pProcess);
		if (processId == pid)
		{
			status = PsTerminateThreadByPointer(ethrd, STATUS_SUCCESS, 0);
			if (status != STATUS_SUCCESS)
			{
				funcStatus = status;
			}
		}
		ObDereferenceObject(ethrd);
		ethrd = NULL;
		pProcess = NULL;
	}
	return funcStatus;
}
#pragma code_seg()
#pragma section(".tebb", read, execute)
#pragma code_seg(".tebb$001")
NTSTATUS __stdcall test()
{
	DbgPrintEx(77, 0, "[*]test called at core %d\n", KeGetCurrentProcessorNumber());
	return STATUS_SUCCESS;
}
#pragma code_seg()
#pragma code_seg(".tebb$002")
NTSTATUS __stdcall test2()
{
	DbgPrintEx(77, 0, "[*]test2 called at core %d\n", KeGetCurrentProcessorNumber());
	return STATUS_SUCCESS;
}
#pragma code_seg()
#pragma code_seg(".HOOK$001")
NTSTATUS __stdcall hook_test(PHOOK_REGS Regs)
{
	UNREFERENCED_PARAMETER(Regs);
	if (!g_Test) return STATUS_ACCESS_DENIED;
	return STATUS_SUCCESS;
}
void HookEnd_test() { return; }
#pragma code_seg()
#pragma code_seg(".HOOK$002")
NTSTATUS __stdcall Hook_Func(PHOOK_REGS Regs)
{
	// Protect the target process and hook PspTerminateThreadByPointer
	PETHREAD Thread = (PETHREAD)Regs->Rcx;
	PETHREAD ResetShadowPageThreadObject = (PETHREAD)HvReadMemory(0);
	if ((UINT64)PsGetThreadId(Thread) == (UINT64)PsGetThreadId(ResetShadowPageThreadObject))
	{
		return STATUS_ACCESS_DENIED;
	}
#ifdef DBG
	if (PsGetThreadProcessId(Thread) == (HANDLE)g_Pid)
	{
		if ((HANDLE)g_Pid!=PsGetCurrentProcessId())
		{
			if (g_bDebug) DbgPrintEx(77, 0, "[*]EPT HOOK PspTerminateThreadByPointer Success! Target PID: %llu Current PID: %llu Current TID: %llu\n", (DWORD64)g_Pid,(DWORD64)PsGetCurrentProcessId(),(DWORD64)PsGetCurrentThreadId());
			return STATUS_ACCESS_DENIED;
		}
	}
#endif
	return STATUS_SUCCESS;
}
#pragma code_seg()
#pragma code_seg(".HOOK$003")
void HookEnd() { return; }
#pragma code_seg()
#pragma comment(linker, "/SECTION:.HOOK,ER,ALIGN=4096")
#pragma comment(linker, "/SECTION:.tebb,ER,ALIGN=4096")
#pragma code_seg(".entry$002")
NTSTATUS SvmWorkerCallback(PCPU_CONTEXT context)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RtlCaptureContext(&(context->ThreadContext));
	if (!CheckIsVirtualCpu(context->CpuIndex))
	{
		if (!InitVMCB(context))
		{
			return status;
		}
		StartSVM(context);
		KeBugCheck(0x3B);
	}
	status = STATUS_SUCCESS;
	return status;
}
VOID ResetShadowPageCallback(PVOID context)
{
	UNREFERENCED_PARAMETER(context);
	KeSetPriorityThread((PKTHREAD)KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = -10000 * 100;
	while (!g_bSvmRunning)
	{
		KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	}
	while (TRUE)
	{
		KeSetPriorityThread((PKTHREAD)KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
		timeout.QuadPart = -10000 * 5;
		if (!g_VmStart) return;
		for (UINT32 i = 0; i < min(CpuCount, MAX_SVM_THREADS); i++)
		{
			PROCESSOR_NUMBER processorNumber = { 0 };
			GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
			KeGetProcessorNumberFromIndex(i, &processorNumber);
			affinity.Group = processorNumber.Group;
			affinity.Mask = 1ULL << processorNumber.Number;
			KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
			if (g_bSvmRunning && !g_SuspendGuest)
			{
				AsmResetShadow();
			}
			KeRevertToUserGroupAffinityThread(&oldAffinity);
		}
		if (!g_bSvmRunning) break;
		KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	}
}
NTSTATUS CreateResetShadowPageCallback()
{
	NTSTATUS status = STATUS_SUCCESS;
	HANDLE threadHandle = NULL;
	status = PsCreateSystemThread(
		&threadHandle,
		0,
		NULL,
		NULL,
		NULL,
		(PKSTART_ROUTINE)ResetShadowPageCallback,
		NULL
	);
	ObReferenceObjectByHandle(threadHandle, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID*)&g_ResetShadowPageThreadObject, NULL);
	ZwClose(threadHandle);
#if DBG
	if (g_bDebug) DbgPrintEx(77, 0, "ResetShadowPageThreadObject Address: %llX\n",(UINT64)g_ResetShadowPageThreadObject);
#endif
	return status;
}
NTSTATUS CreateAllSvmWorkCallback()
{
	NTSTATUS status = STATUS_SUCCESS;
	for (ULONG i = 0; i < min(CpuCount,MAX_SVM_THREADS); i++)
	{
		PROCESSOR_NUMBER processorNumber = { 0 };
		GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
		KeGetProcessorNumberFromIndex(i, &processorNumber);
		affinity.Group = processorNumber.Group;
		affinity.Mask = 1ULL << processorNumber.Number;
		KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
		PCPU_CONTEXT cpu_context = &(g_CpuContexts[i]);
		cpu_context->CpuIndex = i;
		SvmWorkerCallback(cpu_context);
		KeRevertToUserGroupAffinityThread(&oldAffinity);
	}
	return status;
}
BOOLEAN PrepareAllCpus()
{
	if (!AllocateNptPageTable(&g_ZeroPage, PAGE_SIZE)) return FALSE;
	g_CpuContexts = ExAllocatePool2(POOL_FLAG_NON_PAGED, GET_PAGE_ALIGN_LENGTH(CpuCount * sizeof(CPU_CONTEXT)), 'SVM');
	if (!g_CpuContexts)
	{
		DbgPrintEx(77, 0, "[-]Failed to allocate g_CpuContexts.\n");
		return FALSE;
	}
	memset(g_CpuContexts, 0, GET_PAGE_ALIGN_LENGTH(CpuCount * sizeof(CPU_CONTEXT)));
	BOOLEAN result = FALSE;
	for (unsigned long i = 0; i < CpuCount; i++)
	{
		KeInitializeSpinLock(&(g_CpuContexts[i].VmExitLock));
		g_CpuContexts[i].OldIrql = KeGetCurrentIrql();
		result = AllocVMCB(&(g_CpuContexts[i]));
		if (!result)
		{
			DbgPrintEx(77, 0, "[-]Failed to allocate VMCB memory.\n");
			break;
		}
		result = CreateSvmPageTable(&(g_CpuContexts[i]));
		if (!result)
		{
			DbgPrintEx(77, 0, "[-]Failed to create SVM page table.\n");
			break;
		}
	}
	if (!result)
	{
		for (unsigned long i = 0; i < CpuCount; i++)
		{
			FreeVMCB(&(g_CpuContexts[i]));
		}
	}
	return result;
}
BOOLEAN CreatePowerCallback(PVOID PowerCallback)
{
	OBJECT_ATTRIBUTES objAttr = { 0 };
	UNICODE_STRING callbackName = RTL_CONSTANT_STRING(L"\\Callback\\PowerState");
	InitializeObjectAttributes(&objAttr, &callbackName, OBJ_CASE_INSENSITIVE, NULL, NULL);

	NTSTATUS status = ExCreateCallback(&g_PowerCallbackObj, &objAttr, FALSE, TRUE);
	if (NT_SUCCESS(status))
	{
		g_PowerCallbackHandle = ExRegisterCallback(
			g_PowerCallbackObj,
			(PCALLBACK_FUNCTION)PowerCallback,
			NULL
		);
		return TRUE;
	}
	else return FALSE;
}
VOID PowerNotifyCallback(PVOID Context, PVOID Argument1, PVOID Argument2)
{
	UNREFERENCED_PARAMETER(Context);
	if ((UINT64)Argument1 == PO_CB_SYSTEM_STATE_LOCK)
	{
		if ((UINT64)Argument2 == FALSE)
		{
			SuspendAllGuest();
		}
		else if ((UINT64)Argument2 == TRUE)
		{
			ResumeAllGuest();
		}
	}
}
NTSTATUS VmStartWorker(PVOID context)
{
	PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)context;
	g_OriginalFunc = FindPspTerminateThreadByPointer();
	PVOID OriginalFunc = g_OriginalFunc;
	CpuCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	if (!CheckSvmSupport())
	{
		return STATUS_UNSUCCESSFUL;
	}
	InitPageList();
	if (!PrepareAllCpus())
	{
		KeBugCheckEx(0x3B, 0, 0, 0, 0);
	}
	InitializeListHead(&HookListHead);
	KeInitializeSpinLock(&HookListLock);
	KeInitializeSpinLock(&g_HyperCallbackListLock);
	KeInitializeSpinLock(&g_CpuidCallbackListLock);
	KeInitializeSpinLock(&g_BpCallbackListLock);
	for (size_t i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts, GET_PAGE_ALIGN_LENGTH(CpuCount * sizeof(CPU_CONTEXT)), TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)&ShadowStart, GET_PAGE_ALIGN_LENGTH(SHADOW_SIZE), TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].GuestVmcb.VirtualAddress, g_CpuContexts[i].GuestVmcb.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostVmcb.VirtualAddress, g_CpuContexts[i].HostVmcb.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostStack.VirtualAddress, g_CpuContexts[i].HostStack.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Hsave.VirtualAddress, g_CpuContexts[i].Hsave.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Iopm.VirtualAddress, g_CpuContexts[i].Iopm.Size, FALSE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Msrpm.VirtualAddress, g_CpuContexts[i].Msrpm.Size, FALSE, TRUE, FALSE);
	}
	g_ReadMemoryTable[0] = &g_ResetShadowPageThreadObject;
	PHOOK_INFO protectEntry = AddHookInfo(&HookListHead, NULL, (UINT64)EntryStartAddr, ENTRY_SECTION_SIZE, &HookListLock);
	if (protectEntry)
	{
		protectEntry->DataPage = TRUE;
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			if (SetGuestShadowPage(&(g_CpuContexts[i]), protectEntry, NO_SHADOW_PAGE, FALSE, FALSE, NULL))
			{
				DbgPrintEx(77, 0, "[+]successed to set entry shadow page for CPU %d.\n", i);
			}
		}
	}
	PHOOK_INFO protectVmasm = AddHookInfo(&HookListHead, NULL, AsmGetVmAsmStartAddress(), VMASM_LENGTH, &HookListLock);
	if (protectVmasm)
	{
		protectVmasm->DataPage = TRUE;
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			if (SetGuestShadowPage(&(g_CpuContexts[i]), protectVmasm, NO_SHADOW_PAGE, FALSE, FALSE, NULL))
			{
				DbgPrintEx(77, 0, "[+]successed to set vmasm shadow page for CPU %d.\n", i);
			}
		}
	}
	UINT64 hookLength = (UINT64)HookEnd - (UINT64)hook_test;
	PHOOK_INFO HookFuncProtection = AddHookInfo(&HookListHead, NULL, (UINT64)hook_test, hookLength, &HookListLock);
	if (HookFuncProtection)
	{
		HookFuncProtection->DataPage = TRUE;
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			if (SetGuestShadowPage(&(g_CpuContexts[i]), HookFuncProtection, NO_SHADOW_PAGE, FALSE, FALSE, NULL))
			{
				DbgPrintEx(77, 0, "[+]successed to set hook protection shadow page for CPU %d.\n", i);
			}
		}
	}
	PHOOK_INFO hookInfo = AddHookInfo(&HookListHead, NULL, (UINT64)&g_Test1, PAGE_SIZE, &HookListLock);
	if (hookInfo)
	{
		hookInfo->DataPage = TRUE;
		for (size_t i = 0; i < CpuCount; i++)
		{
			if (SetGuestShadowPage(&(g_CpuContexts[i]), hookInfo, NO_SHADOW_PAGE, TRUE, FALSE, NULL))
			{
				DbgPrintEx(77, 0, "[+]successed to set shadow page for CPU %llu.\n", i);
			}
		}
	}
	memset(TestBytes, 0xAA, sizeof(TestBytes));
	PHOOK_INFO hookInfo3 = AddHookInfo(&HookListHead, NULL, (UINT64)ReservedTest, PAGE_SIZE * 2, &HookListLock);
	if (!hookInfo3) KeBugCheckEx(0x3B, (ULONG_PTR)AddHookInfo, 0, 0, 0);
	hookInfo3->DataPage = TRUE;
	if (!CreateShadowPage(hookInfo3, 0)) KeBugCheckEx(0x3B, (ULONG_PTR)CreateShadowPage, 0, 0, 0);
	UINT8 tmpData[0x40] = { 0 };
	memset(tmpData, 0x41, sizeof(tmpData));
	ShadowCopyMemory(hookInfo3, 0, (UINT64)TestBytes, tmpData, 0x40);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetGuestShadowPage(&(g_CpuContexts[i]), hookInfo3, 0, TRUE, FALSE, NULL);
	}
	PMDL mdl = IoAllocateMdl((PVOID)OriginalFunc, PAGE_SIZE * 3, FALSE, FALSE, NULL);
	if (!mdl) KeBugCheckEx(0x3B, (ULONG_PTR)IoAllocateMdl, 0, 0, 0);
	MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
	g_Mdl = mdl;
	PHOOK_INFO testHookInfo = AddHookInfo(&HookListHead, NULL, (UINT64)test, PAGE_SIZE, &HookListLock);
	if (!testHookInfo) KeBugCheck(0x3B);
	if (!CreateShadowPage(testHookInfo, 0)) KeBugCheckEx(0x3B, (ULONG_PTR)CreateShadowPage, 0, 0, 0);
	if (!CreateShadowPage(testHookInfo, 1)) KeBugCheckEx(0x3B, (ULONG_PTR)CreateShadowPage, 0, 0, 0);
	PHOOK_FUNC_INFO testHookFuncInfo = AddHookFuncInfo(testHookInfo, (UINT64)test, (UINT64)hook_test, AsmGetJmpCodeLength());
	if(!testHookFuncInfo)  KeBugCheckEx(0x3B, (ULONG_PTR)AddHookFuncInfo, 0, 0, 0);
	PJMP_FUNC_TRAMPOLINE testFuncJmp = (PJMP_FUNC_TRAMPOLINE)AllocateJmpTrampoline(testHookFuncInfo, AsmGetJmpCodeFuncLength());
	if(!testFuncJmp) KeBugCheckEx(0x3B, (ULONG_PTR)AllocateJmpTrampoline, 0, 0, 0);
	memcpy(testFuncJmp, AsmGetJmpCodeFuncBase(), AsmGetJmpCodeFuncLength());
	testHookFuncInfo->JumpTrampolineOffset = sizeof(testFuncJmp->Data);
	UINT8 testCodeLen = GetInstructionLength((PVOID)test, 20);
	DEF_PTR(UINT64, testFuncJmp->Data.HookFuncAddress, 0) = (UINT64)hook_test;
	DEF_PTR(UINT64, testFuncJmp->Data.CallbackAddress, 0) = (UINT64)test + testCodeLen;
	DEF_PTR(UINT64, testFuncJmp->Data.ReturnAddress, 0) = (UINT64)testFuncJmp + AsmGetReturnOffset();
	memcpy(testFuncJmp->Execute.OriginalCode, (PVOID)test, testCodeLen);
	if(g_bDebug) DbgPrintEx(77, 0, "[*]test function prologue length: %d bytes.\n", testCodeLen);
	UINT8 testJmpCode[256] = { 0 };
	memset(testJmpCode, 0x90, 256);
	testJmpCode[0] = 0xCC;
	ShadowCopyMemory(testHookInfo, 1, (UINT64)test, testJmpCode, testCodeLen);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetGuestShadowPage(&(g_CpuContexts[i]), testHookInfo, 0, TRUE, FALSE, NULL);
	}
	PHOOK_INFO hookInfo4 = AddHookInfo(&HookListHead, NULL, (UINT64)OriginalFunc, PAGE_SIZE * 2, &HookListLock);
	if(!hookInfo4) KeBugCheckEx(0x3B, (ULONG_PTR)AddHookInfo, 0, 0, 0);
	if (!CreateShadowPage(hookInfo4, 0)) KeBugCheckEx(0x3B, (ULONG_PTR)CreateShadowPage, 0, 0, 0);
	if (!CreateShadowPage(hookInfo4, 1)) KeBugCheckEx(0x3B, (ULONG_PTR)CreateShadowPage, 0, 0, 0);
	PHOOK_FUNC_INFO hookFuncInfo = AddHookFuncInfo(hookInfo4, (UINT64)OriginalFunc, (UINT64)Hook_Func, AsmGetJmpCodeLength());
	if (!hookFuncInfo) KeBugCheckEx(0x3B, (ULONG_PTR)AddHookFuncInfo, 0, 0, 0);
	PJMP_FUNC_TRAMPOLINE FuncJmp = (PJMP_FUNC_TRAMPOLINE)AllocateJmpTrampoline(hookFuncInfo, AsmGetJmpCodeFuncLength());
	if (!FuncJmp) KeBugCheckEx(0x3B, (ULONG_PTR)AllocateJmpTrampoline, 0, 0, 0);
	UINT8 FuncOriginalCodeLen = GetInstructionLength(OriginalFunc, 20);
	if (g_bDebug) DbgPrintEx(77, 0, "[*]Original function prologue length: %d bytes.\n", FuncOriginalCodeLen);
	memcpy(FuncJmp, AsmGetJmpCodeFuncBase(), AsmGetJmpCodeFuncLength());
	DEF_PTR(UINT64, FuncJmp->Data.ReturnAddress, 0) = (UINT64)FuncJmp + AsmGetReturnOffset();
	DEF_PTR(UINT64, FuncJmp->Data.HookFuncAddress, 0) = (UINT64)Hook_Func;
	DEF_PTR(UINT64, FuncJmp->Data.CallbackAddress, 0) = (UINT64)OriginalFunc + FuncOriginalCodeLen;
	memcpy(FuncJmp->Execute.OriginalCode, (PVOID)OriginalFunc, FuncOriginalCodeLen);
	hookFuncInfo->JumpTrampolineOffset = sizeof(FuncJmp->Data);
	UCHAR JmpCode[256] = { 0 };
	memset(JmpCode, 0x90, sizeof(JmpCode));
	JmpCode[0] = 0xCC;
	ShadowCopyMemory(hookInfo4, 1, (UINT64)OriginalFunc, JmpCode, FuncOriginalCodeLen);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetGuestShadowPage(&(g_CpuContexts[i]), hookInfo4, 0, TRUE, FALSE, NULL);
	}
	DbgPrintEx(77, 0, "[*]Waiting to start SVM......\n");
	while (g_VmStart == FALSE)
	{
		LARGE_INTEGER timeout = { 0 };
		timeout.QuadPart = -10000 * 10;
		KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	}
	if (!CreatePowerCallback((PVOID)PowerNotifyCallback))
	{
		KeBugCheckEx(0x3B, 0, 0, 0, 0);
	}
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		g_CpuContexts[i].Running = TRUE;
	}
	CreateResetShadowPageCallback();
	g_SuspendGuest = FALSE;
	g_bSvmRunning = TRUE;
	if (g_Unload)
	{
		DriverObject->DriverUnload = DriverUnload;
	}
	CreateAllSvmWorkCallback();
	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = -10000 * 200;
	KeDelayExecutionThread(KernelMode, FALSE, &timeout);
	if (g_bDebug)
	{
#ifdef DBG
		DbgPrintEx(77, 0, "[*]ResetShadowThreadObject address: %llX\n", HvReadMemory(0));
#endif
	}
	return STATUS_SUCCESS;
}
VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	if (!g_Unload) KeBugCheck(SYSTEM_SERVICE_EXCEPTION);
	g_VmStart = FALSE;
	g_bSvmRunning = FALSE;
	KeWaitForSingleObject(g_ResetShadowPageThreadObject, Executive, KernelMode, FALSE, NULL);
	ObDereferenceObject(g_ResetShadowPageThreadObject);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
		PROCESSOR_NUMBER processorNumber = { 0 };
		KeGetProcessorNumberFromIndex(i, &processorNumber);
		affinity.Group = processorNumber.Group;
		affinity.Mask = 1ULL << processorNumber.Number;
		KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
		g_CpuContexts[i].Running = FALSE;
		__svm_vmmcall(VMMCALL_SUSPEND,0,0);
		_disable();
		__svm_stgi();
		__writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
		_enable();
		KeRevertToUserGroupAffinityThread(&oldAffinity);
	}
	MmUnlockPages(g_Mdl);
	IoFreeMdl(g_Mdl);
	RemoveAllHookInfo(&HookListHead, &HookListLock);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		FreeVMCB(&(g_CpuContexts[i]));
		FreeSvmPageTable(&(g_CpuContexts[i]));
	}
	FreeAllPageList();
	ExUnregisterCallback(g_PowerCallbackHandle);
	ObDereferenceObject(g_PowerCallbackObj);
	ExFreePoolWithTag(g_CpuContexts, 'SVM');
}
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	HANDLE threadHandle = NULL;
	status = PsCreateSystemThread(
		&threadHandle,
		0,
		NULL,
		NULL,
		NULL,
		(PKSTART_ROUTINE)VmStartWorker,
		(PVOID)DriverObject
	);
	if (NT_SUCCESS(status))
	{
		ZwClose(threadHandle);
	}
	return status;
}
#pragma code_seg()
#pragma code_seg(".entry$003")
void EndEntryAddr() { return; }
#pragma code_seg()
#pragma comment(linker, "/SECTION:.entry,ER,ALIGN=4096")
#pragma code_seg(".entry$002")
void VmExitHandler(PCPU_CONTEXT context, PGUEST_REGS Regs)
{
	UNREFERENCED_PARAMETER(Regs);
	PVMCB GuestVmcbVa = (PVMCB)context->GuestVmcb.VirtualAddress;
	UINT64 exitCode = GuestVmcbVa->ControlArea.ExitCode;
	if(KeGetCurrentIrql() < DISPATCH_LEVEL)
	{
		KeAcquireSpinLock(&(context->VmExitLock), &(context->OldIrql));
	}
	__svm_vmload(context->HostVmcb.PhysicalAddress.QuadPart);
	switch (exitCode)
	{
	case VMEXIT_MSR:
	{
		UINT32 msr = (Regs->rcx & 0xFFFFFFFF);
		BOOLEAN writeAccess = (GuestVmcbVa->ControlArea.ExitInfo1 != 0);
		ULARGE_INTEGER value = { 0 };
		value.LowPart = (GuestVmcbVa->StateSaveArea.Rax & 0xFFFFFFFF);
		value.HighPart = (Regs->rdx & 0xFFFFFFFF);
		if (writeAccess != FALSE)
		{
			if (msr == MSR_EFER)
			{
				GuestVmcbVa->StateSaveArea.Efer = (value.QuadPart | EFER_SVME);
				goto Msr_Exit;
			}
			__writemsr(msr, value.QuadPart);
			goto Msr_Exit;
		}
		else
		{
			value.QuadPart = __readmsr(msr);
			GuestVmcbVa->StateSaveArea.Rax = value.LowPart;
			Regs->rdx = value.HighPart;
		}
	Msr_Exit:
		GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
		break;
	}
	case VMEXIT_VMMCALL:
	{
		UINT8 guestCpl = GuestVmcbVa->StateSaveArea.Cpl;
		if (guestCpl != 0) {
			GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
			GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 13;
			GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
			GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
			break;
		}
		if (!(GuestVmcbVa->ControlArea.NRip > GuestVmcbVa->StateSaveArea.Rip && GuestVmcbVa->ControlArea.NRip <= (GuestVmcbVa->StateSaveArea.Rip + 0x20)))
		{
			KeBugCheckEx(0x3B, GuestVmcbVa->StateSaveArea.Rip, GuestVmcbVa->ControlArea.NRip, 0, 0);
		}
		if (GuestVmcbVa->StateSaveArea.Rax == 0x5E3D)
		{
			if (GuestVmcbVa->StateSaveArea.Rip <= AsmGetVmAsmStartAddress() || GuestVmcbVa->StateSaveArea.Rip > AsmGetVmAsmEndAddress())
			{
				GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
				break;
			}
			PHOOK_INFO hookInfo = NULL;
			hookInfo = EnumNextHookInfo(&HookListHead, hookInfo, NULL);
			while (hookInfo)
			{
				if(!hookInfo->DataPage) SetGuestShadowPage(context, hookInfo, 0, TRUE, FALSE, &GuestVmcbVa->ControlArea.TlbControl);
				hookInfo = EnumNextHookInfo(&HookListHead, hookInfo, NULL);
			}
			GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
			break;
		}
		if (GuestVmcbVa->StateSaveArea.Rax == 0x5E3B)
		{
			PHOOK_INFO funcHookInfo = NULL;
			funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
			while (funcHookInfo)
			{
				PHOOK_FUNC_INFO funcHook = FindHookFuncInfoByJmpTrampoline(funcHookInfo, GuestVmcbVa->StateSaveArea.Rip, FALSE);
				if (funcHook)
				{
					HookDereference(funcHookInfo);
					break;
				}
				funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
			}
			GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
			break;
		}
		if (GuestVmcbVa->StateSaveArea.Rax == HV_VMMCALL_READMEMORY)
		{
			if (Regs->rdx == context->Token.VmmcallCookie && context->Token.Valid)
			{
				context->Token.Valid = FALSE;
				context->Token.VmmcallCookie = 0;
				context->Token.Tsc = 0;
				GuestVmcbVa->StateSaveArea.Rax = 0;
				if (Regs->r8 < MAX_READ)
				{
					GuestVmcbVa->StateSaveArea.Rax = DEF_PTR(UINT64, g_ReadMemoryTable[Regs->r8], 0);
				}
			}
			else GuestVmcbVa->StateSaveArea.Rax = 0;
			GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
			break;
		}
		for (UINT32 i = 0; i < MAX_CALLBACK_COUNT; i++)
		{
			if (g_HyperCallbackList[i])
			{
				g_HyperCallbackList[i](context, Regs);
			}
		}
		GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
		break;
	}
	case VMEXIT_INVALID:
	{
		KeBugCheck(0x3B);
		break;
	}
	case VMEXIT_SHUTDOWN:
	{
		KeBugCheckEx(0x3B, VMEXIT_SHUTDOWN, GuestVmcbVa->StateSaveArea.Rip, GuestVmcbVa->ControlArea.ExitInfo1, GuestVmcbVa->ControlArea.ExitInfo2);
	}
	case VMEXIT_EXCP_BP:
	{
		if (((GuestVmcbVa->StateSaveArea.Rip > (UINT64)hook_test && GuestVmcbVa->StateSaveArea.Rip <= (UINT64)HookEnd) || (GuestVmcbVa->StateSaveArea.Rip>(UINT64)EntryStartAddr && GuestVmcbVa->StateSaveArea.Rip <=(UINT64)EndEntryAddr)) && GuestVmcbVa->StateSaveArea.Rax == HV_VMMCALL_READMEMORY)
		{
			_rdrand64_step((PUINT64) & (context->Token.VmmcallCookie));
			context->Token.Valid = TRUE;
			context->Token.Tsc = __rdtsc();
			GuestVmcbVa->StateSaveArea.Rax = context->Token.VmmcallCookie;
			GuestVmcbVa->StateSaveArea.Rip += 1;
			break;
		}
		PHOOK_INFO hookInfo = NULL;
		hookInfo = EnumNextHookInfo(&HookListHead, hookInfo, NULL);
		BOOLEAN handled = FALSE;
		while (hookInfo)
		{
			PHOOK_FUNC_INFO funcHook = FindHookFuncInfo(hookInfo, GuestVmcbVa->StateSaveArea.Rip, FALSE);
			if (funcHook && GuestVmcbVa->StateSaveArea.Rip==(UINT64)funcHook->OriginalFuncAddress)
			{
				GuestVmcbVa->StateSaveArea.Rip = (UINT64)funcHook->JumpTrampolineAddress + funcHook->JumpTrampolineOffset;
				handled = TRUE;
				break;
			}
			hookInfo = EnumNextHookInfo(&HookListHead, hookInfo, NULL);
		}
		if (handled) break;
		if (g_bDebug)
		{
			GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
			GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 3;
			GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
			GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 0;
		}
		else GuestVmcbVa->StateSaveArea.Rip += 1;
		for (UINT32 i = 0; i < MAX_CALLBACK_COUNT; i++)
		{
			if (g_BpCallbackList[i])
			{
				g_BpCallbackList[i](context, Regs);
			}
		}
		break;
	}
#define VMEXIT_NPF 0x400
	case VMEXIT_NPF:
	{
		PAGE_FAULT_EXIT_INFO exitInfo1 = { 0 };
		exitInfo1.ErrorCode = GuestVmcbVa->ControlArea.ExitInfo1;
		if (!exitInfo1.Fields.Present)
		{
			UINT64 errcode = GuestVmcbVa->ControlArea.ExitInfo1;
			GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
			GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
			GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 14;
			GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
			GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCode = (errcode & 0x1F);
		}
		else
		{
			if (exitInfo1.Fields.Id > 0)
			{
				PHOOK_INFO hookInfo = FindHookInfoPageBase(&HookListHead, GuestVmcbVa->StateSaveArea.Rip, NULL);
				if (hookInfo && !hookInfo->DataPage)
				{
					PHOOK_FUNC_INFO funcInfo = FindHookFuncInfo(hookInfo, GuestVmcbVa->StateSaveArea.Rip, FALSE);
					if (funcInfo && GuestVmcbVa->StateSaveArea.Rip==(UINT64)funcInfo->OriginalFuncAddress)
					{
						HookReference(hookInfo);
					}
					SetGuestShadowPage(context, hookInfo, 1, FALSE, FALSE, &GuestVmcbVa->ControlArea.TlbControl);
				}
				else
				{
					GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
					GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
					GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 13;
					GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCode = 0;
					GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
				}
				break;
			}
			if (exitInfo1.Fields.Write)
			{
				if (((GpaToHpa(context, GuestVmcbVa->ControlArea.ExitInfo2) == (UINT64)(MmGetPhysicalAddress((PVOID)&g_VmStart).QuadPart)))
					&& (GuestVmcbVa->StateSaveArea.Rip > (UINT64)EntryStartAddr && GuestVmcbVa->StateSaveArea.Rip < (UINT64)EndEntryAddr))
				{
					if (g_VmStart==TRUE)
					{
						g_VmStart = FALSE;
						for (UINT32 i = 0; i < CpuCount; i++)
						{
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts, GET_PAGE_ALIGN_LENGTH(CpuCount * sizeof(CPU_CONTEXT)), FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)&ShadowStart, GET_PAGE_ALIGN_LENGTH(SHADOW_SIZE), FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].GuestVmcb.VirtualAddress, g_CpuContexts[i].GuestVmcb.Size, FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostVmcb.VirtualAddress, g_CpuContexts[i].HostVmcb.Size, FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostStack.VirtualAddress, g_CpuContexts[i].HostStack.Size, FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Hsave.VirtualAddress, g_CpuContexts[i].Hsave.Size, FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Iopm.VirtualAddress, g_CpuContexts[i].Iopm.Size, FALSE, FALSE, TRUE);
							SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Msrpm.VirtualAddress, g_CpuContexts[i].Msrpm.Size, FALSE, FALSE, TRUE);
						}
						memset(IsVirtualCpu, 0, MAX_SVM_THREADS);
						PHOOK_INFO hookInfo = NULL;
						hookInfo = FindHookInfoPageBase(&HookListHead, (UINT64)&g_SuspendGuest, NULL);
						if (hookInfo)
						{
							for (size_t i = 0; i < CpuCount; i++)
							{
								SetGuestShadowPage(&(g_CpuContexts[i]), hookInfo, NO_SHADOW_PAGE, TRUE, TRUE, &GuestVmcbVa->ControlArea.TlbControl);
							}
						}
						PHOOK_INFO funcHookInfo = NULL;
						funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
						while (funcHookInfo)
						{
							for (UINT32 i = 0; i < CpuCount; i++)
							{
								if (!funcHookInfo->DataPage)
								{
									SetGuestShadowPage(&(g_CpuContexts[i]), funcHookInfo, NO_SHADOW_PAGE, FALSE, FALSE, &GuestVmcbVa->ControlArea.TlbControl);
								}
							}
							funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
						}
					}
					else
					{
						GuestVmcbVa->StateSaveArea.Cr2 = GuestVmcbVa->StateSaveArea.Rip;
						GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
						GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
						GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 14;
						GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCode = (exitInfo1.ErrorCode & 0x1F);
						GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
					}
					break;
				}
				GuestVmcbVa->StateSaveArea.Cr2 = GuestVmcbVa->StateSaveArea.Rip;
				GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
				GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
				GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 14;
				GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCode = (exitInfo1.ErrorCode & 0x1F);
				GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
			}
		}
		break;
	}
	case VMEXIT_IOIO:
	{
		GuestVmcbVa->StateSaveArea.Rip = GuestVmcbVa->ControlArea.NRip;
		break;
	}
	default:
	{
		if(g_bDebug) __debugbreak();
		GuestVmcbVa->ControlArea.EventInj.Bits.Valid = 1;
		GuestVmcbVa->ControlArea.EventInj.Bits.Type = 3;
		GuestVmcbVa->ControlArea.EventInj.Bits.Vector = 13;
		GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCode = 0;
		GuestVmcbVa->ControlArea.EventInj.Bits.ErrorCodeValid = 1;
		break;
	}
	}
	GuestVmcbVa->ControlArea.VmcbClean = 0;
	if (KeGetCurrentIrql() > context->OldIrql)
	{
		KeReleaseSpinLock(&(context->VmExitLock), context->OldIrql);
	}
}
#pragma code_seg()
#pragma comment(linker, "/SECTION:.vmexit,ER,ALIGN=4096")
__forceinline void SuspendAllGuest()
{

	g_VmStart = FALSE;
	g_SuspendGuest = TRUE;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
		PROCESSOR_NUMBER processorNumber = { 0 };
		KeGetProcessorNumberFromIndex(i, &processorNumber);
		affinity.Group = processorNumber.Group;
		affinity.Mask = 1ULL << processorNumber.Number;
		KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
		g_CpuContexts[i].Running = FALSE;
		__svm_vmmcall(VMMCALL_SUSPEND,0,0);
		_disable();
		PVMCB vmcb = (PVMCB)g_CpuContexts[i].GuestVmcb.VirtualAddress;
		vmcb->ControlArea.VmcbClean = 0;
		__svm_stgi();
		__writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
		_enable();
		KeRevertToUserGroupAffinityThread(&oldAffinity);
	}
}
__forceinline void ResumeAllGuest()
{
	PCPU_CONTEXT contexts = g_CpuContexts;
	PHOOK_INFO funcHookInfo = NULL;
	g_VmStart = TRUE;
	g_SuspendGuest = FALSE;
	g_bSvmRunning = TRUE;
	for (size_t i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts, GET_PAGE_ALIGN_LENGTH(CpuCount * sizeof(CPU_CONTEXT)), TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)&ShadowStart, GET_PAGE_ALIGN_LENGTH(SHADOW_SIZE), TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].GuestVmcb.VirtualAddress, g_CpuContexts[i].GuestVmcb.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostVmcb.VirtualAddress, g_CpuContexts[i].HostVmcb.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].HostStack.VirtualAddress, g_CpuContexts[i].HostStack.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Hsave.VirtualAddress, g_CpuContexts[i].Hsave.Size, TRUE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Iopm.VirtualAddress, g_CpuContexts[i].Iopm.Size, FALSE, TRUE, FALSE);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)g_CpuContexts[i].Msrpm.VirtualAddress, g_CpuContexts[i].Msrpm.Size, FALSE, TRUE, FALSE);
	}
	funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
	while (funcHookInfo)
	{
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			if (!funcHookInfo->DataPage)
			{
				SetGuestShadowPage(&(contexts[i]), funcHookInfo, 0, TRUE, FALSE, NULL);
			}
		}
		funcHookInfo = EnumNextHookInfo(&HookListHead, funcHookInfo, NULL);
	}
	PHOOK_INFO hookInfo = NULL;
	hookInfo = FindHookInfoPageBase(&HookListHead, (UINT64)&g_SuspendGuest, &HookListLock);
	if (hookInfo)
	{
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			SetGuestShadowPage(&(contexts[i]), hookInfo, NO_SHADOW_PAGE, TRUE, FALSE, NULL);
		}
	}
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		PCPU_CONTEXT cpuContext = &(contexts[i]);
		GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
		PROCESSOR_NUMBER processorNumber = { 0 };
		KeGetProcessorNumberFromIndex(i, &processorNumber);
		affinity.Group = processorNumber.Group;
		affinity.Mask = 1ULL << processorNumber.Number;
		KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
		cpuContext->Running = TRUE;
		UINT32 index = cpuContext->CpuIndex;
		PCONTEXT threadctx = (PCONTEXT) & (cpuContext->ThreadContext);
		memset(threadctx, 0, sizeof(CONTEXT));
		RtlCaptureContext(threadctx);
		if (!CheckIsVirtualCpu(index))
		{
			StartSVM(cpuContext);
		}
		KeRevertToUserGroupAffinityThread(&oldAffinity);
	}
}