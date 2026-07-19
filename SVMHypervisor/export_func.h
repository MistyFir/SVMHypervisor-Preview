#pragma once
#include <ntifs.h>
#include "Hook.h"
#include "VMCB.h"
#include <intrin.h>
#include "asmfunc.h"
#if DBG
#define BP_DEBUG(x)		\
    do {                             \
        if (!(x)) {                  \
            __debugbreak();  \
        }                            \
    } while (0)

#else

#define BP_DEBUG(x)                  \
    do {                             \
        (void)(x);                   \
    } while (0)
#endif
#define HV_VMMCALL_READMEMORY 0x40000000
#define MAX_CALLBACK_COUNT 64
#define CPUID_CALLBACK 1
#define VMM_CALLBACK 2
#define BP_CALLBACK 3
typedef VOID(__stdcall* VMEXIT_CALLBACK)(PCPU_CONTEXT Context, PGUEST_REGS Regs);
typedef VOID(__stdcall* SVM_UNLOAD_CALLBACK)(PDRIVER_OBJECT DriverObject);
EXTERN_C _declspec(dllexport) SVM_UNLOAD_CALLBACK g_MdlUnloadCallback;
EXTERN_C LIST_ENTRY HookListHead;
EXTERN_C KSPIN_LOCK HookListLock;
EXTERN_C _declspec(dllexport) BOOLEAN g_Test;
EXTERN_C _declspec(dllexport) BOOLEAN g_Test1;
EXTERN_C _declspec(dllexport) BOOLEAN g_bDebug;
EXTERN_C _declspec(dllexport) BOOLEAN g_Unload;
EXTERN_C _declspec(dllexport) DWORD64 g_Pid;
EXTERN_C KSPIN_LOCK g_CpuidCallbackListLock;
EXTERN_C KSPIN_LOCK g_BpCallbackListLock;
EXTERN_C KSPIN_LOCK g_HyperCallbackListLock;
EXTERN_C VMEXIT_CALLBACK g_CpuidCallbackList[MAX_CALLBACK_COUNT];
EXTERN_C VMEXIT_CALLBACK g_BpCallbackList[MAX_CALLBACK_COUNT];
EXTERN_C VMEXIT_CALLBACK g_HyperCallbackList[MAX_CALLBACK_COUNT];
EXTERN_C _declspec(dllexport) PCPU_CONTEXT SvmGetCpuContextIndex(ULONG_PTR Index);
EXTERN_C _declspec(dllexport) void SvmGetGuestVmcb(PCPU_CONTEXT CpuContext, PMEMORY_INFO GuestVmcb);
EXTERN_C _declspec(dllexport) BOOLEAN SvmAddVmexitCallback(VMEXIT_CALLBACK Callback, UINT32 Flag, PUINT32 Index);
EXTERN_C _declspec(dllexport) BOOLEAN SvmRemoveVmexitCallback(UINT32 Flag, UINT32 Index);
EXTERN_C _declspec(dllexport) void SvmGetJmpCodeBuffer(PVOID Buffer,size_t Length);
EXTERN_C _declspec(dllexport) UINT64 SvmGetJmpCodeBufferLength();
EXTERN_C _declspec(dllexport) void SvmGetJmpCodeFuncBuffer(PVOID Buffer,size_t Length);
EXTERN_C _declspec(dllexport) UINT64 SvmGetJmpCodeFuncBufferLength();
EXTERN_C _declspec(dllexport) PVOID SvmAllocateJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo, SIZE_T Length);
EXTERN_C _declspec(dllexport) void SvmFreeJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo);
EXTERN_C _declspec(dllexport) PHOOK_INFO SvmAddHookInfo(PCSTR TagStr, UINT64 VirtualAddress, SIZE_T MapSize);
EXTERN_C _declspec(dllexport) PHOOK_INFO SvmFindHookInfoTag(PCSTR TagStr);
EXTERN_C _declspec(dllexport) PHOOK_INFO SvmFindHookInfoGuid(PGUID HookId);
EXTERN_C _declspec(dllexport) BOOLEAN SvmCreateShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
EXTERN_C _declspec(dllexport) BOOLEAN SvmSetGuestShadowPage(PCPU_CONTEXT CpuContext, PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, BOOLEAN NoExecute, BOOLEAN Write);
EXTERN_C _declspec(dllexport) BOOLEAN SvmShadowCopyMemory(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, UINT64 VirtualAddress, PVOID Data, SIZE_T DataSize);
EXTERN_C _declspec(dllexport) PHOOK_FUNC_INFO SvmAddHookFuncInfo(PHOOK_INFO hookInfo, UINT64 OriginalFuncAddress, UINT64 HookFuncAddress, SIZE_T FuncLength);
EXTERN_C _declspec(dllexport) PHOOK_FUNC_INFO SvmFindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress, BOOLEAN Lock);
EXTERN_C _declspec(dllexport) void SvmFreeGuestShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
EXTERN_C _declspec(dllexport) void SvmHookReference(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllexport) void SvmHookDereference(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllexport) PHOOK_INFO SvmFindHookInfoPageBase(PLIST_ENTRY ListHead, UINT64 VirtualAddress);
EXTERN_C _declspec(dllexport) PHOOK_FUNC_INFO SvmFindHookFuncInfoByJmpTrampoline(PHOOK_INFO hookInfo, UINT64 JmpTrampolineRipAddress, BOOLEAN Lock);
EXTERN_C _declspec(dllexport) BOOLEAN SvmIsHookRefCountZero(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllexport) PHOOK_INFO SvmEnumNextHookInfo(PHOOK_INFO CurrentHookInfo);
EXTERN_C _declspec(dllexport) void SvmRemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo, BOOLEAN Lock);
EXTERN_C _declspec(dllexport) BOOLEAN SvmProtectDriverSection(UINT64 VirtualAddress, SIZE_T Size, BOOLEAN NoExecute, PBOOLEAN CoreStatus, UINT32 CoreCount);
EXTERN_C _declspec(dllexport) UINT64 SvmGetReturnOffset();
EXTERN_C _declspec(dllexport) NTSTATUS PsTerminateThreadByPointer(PETHREAD pThread, NTSTATUS exitCode, BOOLEAN bDirectTerminate);
EXTERN_C _declspec(dllexport) NTSTATUS PsTerminateProcessByPid(DWORD64 pid);
EXTERN_C _declspec(dllexport) NTSTATUS __stdcall test();
EXTERN_C _declspec(dllexport) NTSTATUS __stdcall test2();
__forceinline UINT64 HvReadMemory(UINT32 Index)
{
	UINT32 cpuIndex = KeGetCurrentProcessorNumber();
	GROUP_AFFINITY affinity = { 0 }, oldAffinity = { 0 };
	PROCESSOR_NUMBER processorNumber = { 0 };
	KeGetProcessorNumberFromIndex(cpuIndex, &processorNumber);
	affinity.Group = processorNumber.Group;
	affinity.Mask = 1ULL << processorNumber.Number;
	KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);
	_disable();
	UINT64 cookie;
	__writerax(HV_VMMCALL_READMEMORY);
	__debugbreak();
	__readrax(&cookie);
	UINT64 value = __svm_vmmcall(HV_VMMCALL_READMEMORY, cookie, Index);
	_enable();
	KeRevertToUserGroupAffinityThread(&oldAffinity);
	return value;
}