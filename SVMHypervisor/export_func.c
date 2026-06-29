#include "export_func.h"
#include "VMCB.h"
#pragma code_seg(".entry$002")
void SvmGetGuestVmcb(PCPU_CONTEXT CpuContext, PMEMORY_INFO GuestVmcb)
{
	if (CpuContext && GuestVmcb)
	{
		memcpy(GuestVmcb, &(CpuContext->GuestVmcb), sizeof(MEMORY_INFO));
	}
}
BOOLEAN SvmAddVmexitCallback(VMEXIT_CALLBACK Callback, UINT32 Flag, PUINT32 Index)
{
	if (!Callback || !Flag) return FALSE;
	BOOLEAN result = FALSE;
	VMEXIT_CALLBACK* CallbackList = NULL;
	PKSPIN_LOCK Lock = NULL;
	if (Flag == VMM_CALLBACK)
	{
		CallbackList = g_HyperCallbackList;
		Lock = &g_HyperCallbackListLock;
	}
	else if (Flag == CPUID_CALLBACK)
	{
		CallbackList = g_CpuidCallbackList;
		Lock = &g_CpuidCallbackListLock;
	}
	else if (Flag == BP_CALLBACK)
	{
		CallbackList = g_BpCallbackList;
		Lock = &g_BpCallbackListLock;
	}
	else return result;
	KIRQL oldIrql = 0;
	KeAcquireSpinLock(Lock, &oldIrql);
	for (UINT32 i = 0; i < MAX_CALLBACK_COUNT; i++)
	{
		if (!CallbackList[i])
		{
			CallbackList[i] = Callback;
			if (Index) *Index = i;
			result = TRUE;
			break;
		}
	}
	KeReleaseSpinLock(Lock, oldIrql);
	return result;
}
BOOLEAN SvmRemoveVmexitCallback(UINT32 Flag, UINT32 Index)
{
	if (!Flag || Index >= MAX_CALLBACK_COUNT) return FALSE;
	VMEXIT_CALLBACK* CallbackList = NULL;
	PKSPIN_LOCK Lock = NULL;
	if (Flag == VMM_CALLBACK)
	{
		CallbackList = g_HyperCallbackList;
		Lock = &g_HyperCallbackListLock;
	}
	else if (Flag == CPUID_CALLBACK)
	{
		CallbackList = g_CpuidCallbackList;
		Lock = &g_CpuidCallbackListLock;
	}
	else if (Flag == BP_CALLBACK)
	{
		CallbackList = g_BpCallbackList;
		Lock = &g_BpCallbackListLock;
	}
	else return FALSE;
	KIRQL oldIrql = 0;
	KeAcquireSpinLock(Lock, &oldIrql);
	if (CallbackList[Index]) CallbackList[Index] = NULL;
	KeReleaseSpinLock(Lock, oldIrql);
	return TRUE;
}
PHOOK_INFO SvmAddHookInfo(PCSTR TagStr, UINT64 VirtualAddress, SIZE_T MapSize)
{
	return AddHookInfo(&HookListHead, TagStr, VirtualAddress, MapSize, &HookListLock);
}
PHOOK_INFO SvmFindHookInfoTag(PCSTR TagStr)
{
	return FindHookInfoTag(&HookListHead, TagStr, &HookListLock);
}
PHOOK_INFO SvmFindHookInfoGuid(PGUID HookId)
{
	return FindHookInfoGuid(&HookListHead, HookId, &HookListLock);
}
BOOLEAN SvmCreateShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex)
{
	return CreateShadowPage(HookInfo, ShadowPageIndex);
}
BOOLEAN SvmSetGuestShadowPage(PCPU_CONTEXT CpuContext, PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, BOOLEAN NoExecute, BOOLEAN Write)
{
	return SetGuestShadowPage(CpuContext, HookInfo, ShadowPageIndex, NoExecute, Write, NULL);
}
BOOLEAN SvmShadowCopyMemory(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, UINT64 VirtualAddress, PVOID Data, SIZE_T DataSize)
{
	return ShadowCopyMemory(HookInfo, ShadowPageIndex, VirtualAddress, Data, DataSize);
}
void SvmRemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo, BOOLEAN Lock)
{
	RemoveHookFuncInfo(hookInfo, funcInfo, Lock);
}
PHOOK_FUNC_INFO SvmAddHookFuncInfo(PHOOK_INFO hookInfo, UINT64 OriginalFuncAddress, UINT64 HookFuncAddress, SIZE_T FuncLength)
{
	return AddHookFuncInfo(hookInfo, OriginalFuncAddress, HookFuncAddress, FuncLength);
}
BOOLEAN SvmIsHookRefCountZero(PHOOK_INFO HookInfo)
{
	return IsHookRefCountZero(HookInfo);
}
PHOOK_INFO SvmEnumNextHookInfo(PHOOK_INFO CurrentHookInfo)
{
	return EnumNextHookInfo(&HookListHead, CurrentHookInfo, &HookListLock);
}
void SvmHookReference(PHOOK_INFO HookInfo)
{
	HookReference(HookInfo);
}
void SvmHookDereference(PHOOK_INFO HookInfo)
{
	HookDereference(HookInfo);
}
PHOOK_FUNC_INFO SvmFindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress, BOOLEAN Lock)
{
	return FindHookFuncInfo(hookInfo, RipAddress, Lock);
}
PHOOK_INFO SvmFindHookInfoPageBase(PLIST_ENTRY ListHead, UINT64 VirtualAddress)
{
	return FindHookInfoPageBase(ListHead, VirtualAddress, &HookListLock);
}
PHOOK_FUNC_INFO SvmFindHookFuncInfoByJmpTrampoline(PHOOK_INFO hookInfo, UINT64 JmpTrampolineRipAddress, BOOLEAN Lock)
{
	return FindHookFuncInfoByJmpTrampoline(hookInfo, JmpTrampolineRipAddress, Lock);
}
void SvmFreeGuestShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex)
{
	FreeGuestShadowPage(HookInfo, ShadowPageIndex);
}
BOOLEAN SvmProtectDriverSection(UINT64 VirtualAddress,SIZE_T Size, BOOLEAN NoExecute, PBOOLEAN CoreStatus, UINT32 CoreCount)
{
	PHOOK_INFO hookInfo = NULL;
	hookInfo = FindHookInfoPageBase(&HookListHead, VirtualAddress, &HookListLock);
	if (hookInfo) return FALSE;
	hookInfo = AddHookInfo(&HookListHead, NULL, VirtualAddress, Size, &HookListLock);
	if (!hookInfo) return FALSE;
	hookInfo->DataPage = TRUE;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		BOOLEAN result = SetGuestShadowPage(&(g_CpuContexts[i]), hookInfo, NO_SHADOW_PAGE, NoExecute, FALSE, NULL);
		if (CoreCount && i < CoreCount && CoreStatus)
		{
			CoreStatus[i] = result;
		}
	}
	return TRUE;
}
void SvmGetJmpCodeBuffer(PVOID Buffer,size_t Length)
{
	memcpy(Buffer, AsmGetJmpCodeBase(), min(Length, AsmGetJmpCodeLength()));
}
UINT64 SvmGetJmpCodeBufferLength()
{
	return AsmGetJmpCodeLength();
}
void SvmGetJmpCodeFuncBuffer(PVOID Buffer, size_t Length)
{
	memcpy(Buffer, AsmGetJmpCodeFuncBase(), min(Length, AsmGetJmpCodeFuncLength()));
}
UINT64 SvmGetJmpCodeFuncBufferLength()
{
	return AsmGetJmpCodeFuncLength();
}
PVOID SvmAllocateJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo, SIZE_T Length)
{
	return AllocateJmpTrampoline(HookFuncInfo, Length);
}
void SvmFreeJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo)
{
	FreeJmpTrampoline(HookFuncInfo);
}
UINT64 SvmGetReturnOffset()
{
	return AsmGetReturnOffset();
}
#pragma code_seg()