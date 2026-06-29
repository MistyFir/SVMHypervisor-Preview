#include "Hook.h"
#include <ntstrsafe.h>
#include <guiddef.h>
#include "asmfunc.h"
#include "VMCB.h"
#define HOOK_POOL 'HKIO'
#pragma code_seg(".entry$002")
PHOOK_PAGE_INFO AllocateHookPageInfo(SIZE_T PageCount)
{
	SIZE_T allocSize = GET_PAGE_ALIGN_LENGTH(sizeof(HOOK_PAGE_INFO) * PageCount);
	PHOOK_PAGE_INFO HookPageInfo = (PHOOK_PAGE_INFO)ExAllocatePool2(POOL_FLAG_NON_PAGED, allocSize, HOOK_POOL);
	if (HookPageInfo)
	{
		memset(HookPageInfo, 0, allocSize);
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)HookPageInfo, allocSize, TRUE, TRUE, FALSE);
		}
		return HookPageInfo;
	}
	else return NULL;
}
VOID FreeHookPageInfo(PHOOK_PAGE_INFO HookPageInfo,SIZE_T PageCount)
{
	if (HookPageInfo)
	{
		SIZE_T allocSize = GET_PAGE_ALIGN_LENGTH(sizeof(HOOK_PAGE_INFO) * PageCount);
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)HookPageInfo, allocSize, FALSE, TRUE, FALSE);
		}
		ExFreePoolWithTag(HookPageInfo, HOOK_POOL);
	}
}
PHOOK_INFO AddHookInfo(PLIST_ENTRY ListHead, PCSTR TagStr, UINT64 VirtualAddress, SIZE_T MapSize, PKSPIN_LOCK HookListLock)
{
	if (!ListHead || !HookListLock) return NULL;
	size_t hookInfoSize = GET_PAGE_ALIGN_LENGTH(sizeof(HOOK_INFO));
	PHOOK_INFO hookInfo = ExAllocatePool2(POOL_FLAG_NON_PAGED, hookInfoSize, HOOK_POOL);
	if (!hookInfo) return NULL;
	memset(hookInfo, 0, sizeof(HOOK_INFO));
	hookInfo->CbSize = sizeof(HOOK_INFO);
	if (TagStr)
	{
		RtlStringCbCopyA(hookInfo->TagStr, sizeof(hookInfo->TagStr), TagStr);
	}
	ExUuidCreate(&(hookInfo->HookId));
	InitializeListHead(&(hookInfo->HookFuncList.HookFuncListHead));
	KeInitializeSpinLock(&(hookInfo->HookFuncList.HookFuncListLock));
	hookInfo->PageBaseCount = GET_PAGE_ALIGN_LENGTH(MapSize+GET_PAGE_OFFSET(VirtualAddress)) / PAGE_SIZE;
	if (hookInfo->PageBaseCount == 0)
	{
		ExFreePoolWithTag(hookInfo, HOOK_POOL);
		return NULL;
	}
	hookInfo->HookPageInfo = AllocateHookPageInfo(hookInfo->PageBaseCount);
	if (!hookInfo->HookPageInfo)
	{
		ExFreePoolWithTag(hookInfo, HOOK_POOL);
		return NULL;
	}
	PVOID VirtualBaseAddress = (PVOID)GET_4KB_PAGE_BASE(VirtualAddress);
	if (!VirtualBaseAddress) {
		ExFreePoolWithTag(hookInfo, HOOK_POOL);
		return NULL;
	}
	for (SIZE_T i = 0; i < hookInfo->PageBaseCount; i++)
	{
		PVOID tmpVirtualAddr = PTR_ADD(PVOID, VirtualBaseAddress, PAGE_SIZE * i);
		UINT64 tmpPhysicalAddr = MmGetPhysicalAddress(tmpVirtualAddr).QuadPart;
		for (UINT32 j = 0; j < CpuCount; j++)
		{
			if (Is2MBytePageTable(&(g_CpuContexts[j]), tmpPhysicalAddr))
			{
				if (!SplitLargePage(&(g_CpuContexts[j]), tmpPhysicalAddr))
				{
#ifdef DBG
					DbgPrintEx(77, 0, "[-]Core %d Failed to split large page for physical address: 0x%llX\n", j,tmpPhysicalAddr);
#endif // DBG
					ExFreePoolWithTag(hookInfo, HOOK_POOL);
					return NULL;
				}
			}
		}
		BuildMemoryInfo(&(hookInfo->HookPageInfo[i].PageBaseInfo), tmpPhysicalAddr, tmpVirtualAddr, PAGE_SIZE);
	}
	BOOLEAN result = FALSE;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		result = SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)hookInfo, hookInfoSize, TRUE, TRUE, FALSE);
		if (!result) break;
	}
	if (!result)
	{
		for (UINT32 i = 0; i < CpuCount; i++)
		{
			SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)hookInfo, hookInfoSize, TRUE, FALSE, TRUE);
		}
		ExFreePoolWithTag(hookInfo, HOOK_POOL);
		return NULL;
	}
	ExInterlockedInsertHeadList(ListHead, &hookInfo->HookList, HookListLock);
	return hookInfo;
}
PHOOK_INFO FindHookInfoTag(PLIST_ENTRY ListHead, PCSTR TagStr, PKSPIN_LOCK HookListLock)
{
	if (!ListHead || !TagStr) return NULL;
	if (IsListEmpty(ListHead)) return NULL;
	PHOOK_INFO HookInfo = NULL;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	for (PLIST_ENTRY entry = ListHead->Flink; entry != ListHead; entry=entry->Flink)
	{
		PHOOK_INFO tmpInfo = CONTAINING_RECORD(entry, HOOK_INFO, HookList);
		if (!strncmp(TagStr, (PCSTR) & (tmpInfo->TagStr), sizeof(tmpInfo->TagStr)))
		{
			HookInfo = tmpInfo;
			break;
		}
	}
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
	return HookInfo;
}
PHOOK_INFO FindHookInfoGuid(PLIST_ENTRY ListHead, PGUID HookId, PKSPIN_LOCK HookListLock)
{
	if (!ListHead || !HookId) return NULL;
	if (IsListEmpty(ListHead)) return NULL;
	PHOOK_INFO HookInfo = NULL;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	for (PLIST_ENTRY entry = ListHead->Flink; entry != ListHead; entry = entry->Flink)
	{
		PHOOK_INFO tmpInfo = CONTAINING_RECORD(entry, HOOK_INFO, HookList);
		if (IsEqualGUID(&(tmpInfo->HookId),HookId))
		{
			HookInfo = tmpInfo;
			break;
		}
	}
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
	return HookInfo;
}
PHOOK_INFO FindHookInfoPageBase(PLIST_ENTRY ListHead, UINT64 VirtualAddress, PKSPIN_LOCK HookListLock)
{
	if (!ListHead) return NULL;
	if (IsListEmpty(ListHead)) return NULL;
	PHOOK_INFO hookInfo = NULL;
	GUID emptyGuid = { 0 };
	UINT64 PhysicalAddress = MmGetPhysicalAddress((PVOID)VirtualAddress).QuadPart;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	for (PLIST_ENTRY entry = ListHead->Flink; entry != ListHead; entry = entry->Flink)
	{
		PHOOK_INFO tmpInfo = CONTAINING_RECORD(entry, HOOK_INFO, HookList);
		if (!tmpInfo->HookPageInfo) continue;
		if (IsEqualGUID(&tmpInfo->HookId, &emptyGuid) || !tmpInfo->HookPageInfo[0].PageBaseInfo.VirtualAddress || !tmpInfo->HookPageInfo[0].PageBaseInfo.PhysicalAddress.QuadPart)
		{
			continue;
		}
		if (((UINT64)tmpInfo->HookPageInfo[tmpInfo->PageBaseCount - 1].PageBaseInfo.PhysicalAddress.QuadPart + PAGE_SIZE) > PhysicalAddress && ((UINT64)tmpInfo->HookPageInfo[0].PageBaseInfo.PhysicalAddress.QuadPart) <= PhysicalAddress)
		{
			hookInfo = tmpInfo;
			break;
		}
	}
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
	return hookInfo;
}
BOOLEAN CreateShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex)
{
	GUID emptyGuid = { 0 };
	if (!HookInfo || IsEqualGUID(&(HookInfo->HookId), &emptyGuid))
	{
		return FALSE;
	}
	if (!HookInfo->HookPageInfo) return FALSE;
	if (!HookInfo->HookPageInfo[0].PageBaseInfo.VirtualAddress || !HookInfo->PageBaseCount)
	{
		return FALSE;
	}
	if (ShadowPageIndex >= MAX_SHADOW_PAGE)
	{
		return FALSE;
	}
	BOOLEAN result = TRUE;
	for (SIZE_T i = 0; i < HookInfo->PageBaseCount; i++)
	{
		if (!AllocateNptPageTable(&(HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex]), PAGE_SIZE))
		{
			result = FALSE;
			break;
		}
		memcpy(HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex].VirtualAddress, HookInfo->HookPageInfo[i].PageBaseInfo.VirtualAddress, PAGE_SIZE);
		for (UINT32 j = 0; j < CpuCount; j++)
		{
			SetNestedPageProtection(&(g_CpuContexts[j]), (UINT64)(HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex].VirtualAddress), PAGE_SIZE, TRUE, TRUE, FALSE);
		}
	}
	if (!result)
	{
		for (SIZE_T i = 0; i < HookInfo->PageBaseCount; i++)
		{
			FreeNptPageTable(&(HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex]));
		}
	}
	return result;
}
BOOLEAN SetGuestShadowPage(PCPU_CONTEXT CpuContext,PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, BOOLEAN NoExecute, BOOLEAN Write, PUINT32 TlbControl)
{
	if (!HookInfo) return FALSE;
	if (ShadowPageIndex >=MAX_SHADOW_PAGE && ShadowPageIndex != NO_SHADOW_PAGE)
	{
		return FALSE;
	}
	if (!HookInfo->HookPageInfo) return FALSE;
	for (SIZE_T i = 0; i < HookInfo->PageBaseCount; i++)
	{
		PMEMORY_INFO MemInfo = NULL;
		if (ShadowPageIndex == NO_SHADOW_PAGE)
		{
			MemInfo = (PMEMORY_INFO) & (HookInfo->HookPageInfo[i].PageBaseInfo);
		}
		else
		{
			MemInfo = (PMEMORY_INFO) & (HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex]);
		}
		if (MemInfo->VirtualAddress == NULL || MemInfo->Size == 0)
			return FALSE;
		PPML4_ENTRY_4KB pml4Entry = (PPML4_ENTRY_4KB)CpuContext->PageTableInfo.PML4.VirtualAddress;
		UINT64 pml4Index = GetPML4Index(HookInfo->HookPageInfo[i].PageBaseInfo.PhysicalAddress.QuadPart);
		if (!pml4Entry[pml4Index].Bits.Valid)
		{
			return FALSE;
		}
		PPDP_ENTRY_4KB pdpEntry = (PPDP_ENTRY_4KB)PA_TO_VA(pml4Entry[pml4Index].Bits.PageFrameNumber << 12);
		if (!pdpEntry)
		{
			return FALSE;
		}
		UINT64 pdpIndex = GetPDPTIndex(HookInfo->HookPageInfo[i].PageBaseInfo.PhysicalAddress.QuadPart);
		if (!pdpEntry[pdpIndex].Bits.Valid)
		{
			return FALSE;
		}
		PPD_ENTRY_4KB pdEntry = (PPD_ENTRY_4KB)PA_TO_VA(pdpEntry[pdpIndex].Bits.PageFrameNumber << 12);
		if (!pdEntry)
		{
			return FALSE;
		}
		UINT64 pdIndex = GetPDIndex(HookInfo->HookPageInfo[i].PageBaseInfo.PhysicalAddress.QuadPart);
		PD_ENTRY_2MB tmpPd = { 0 };
		tmpPd.AsUInt64 = pdEntry[pdIndex].AsUInt64;
		if (tmpPd.Bits.LargePage)
		{
			if (!SplitLargePage(CpuContext, HookInfo->HookPageInfo[i].PageBaseInfo.PhysicalAddress.QuadPart)) return FALSE;
			tmpPd.AsUInt64 = pdEntry[pdIndex].AsUInt64;
			if (!tmpPd.Bits.Valid) return FALSE;
		}
		PPT_ENTRY_4KB ptEntry = (PPT_ENTRY_4KB)PA_TO_VA(pdEntry[pdIndex].Bits.PageFrameNumber << 12);
		if (!ptEntry)
		{
			return FALSE;
		}
		UINT64 ptIndex = GetPTIndex(HookInfo->HookPageInfo[i].PageBaseInfo.PhysicalAddress.QuadPart);
		if (!ptEntry[ptIndex].Fields.Valid)
		{
			return FALSE;
		}
		PT_ENTRY_4KB tmpPt = { 0 };
		tmpPt.AsUInt64 = ptEntry[ptIndex].AsUInt64;
		tmpPt.Fields.NoExecute = NoExecute;
		tmpPt.Fields.Write = Write;
		tmpPt.Fields.Accessed = 1;
		tmpPt.Fields.Dirty = 1;
		tmpPt.Fields.PageFrameNumber = MemInfo->PhysicalAddress.QuadPart >> 12;
		_InterlockedExchange64((LONG64*)&(ptEntry[ptIndex].AsUInt64), tmpPt.AsUInt64);
	}
	if(TlbControl) *TlbControl = 0x01;
	return TRUE;
}
BOOLEAN ShadowCopyMemory(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, UINT64 VirtualAddress, PVOID Data, SIZE_T DataSize)
{
	if (!HookInfo || HookInfo->PageBaseCount == 0 || ShadowPageIndex >= MAX_SHADOW_PAGE)
		return FALSE;
	if (HookInfo->HookPageInfo == NULL) return FALSE;
	if (!Data || DataSize == 0)
		return FALSE;
	for (SIZE_T i = 0; i < HookInfo->PageBaseCount; i++)
	{
		if (!(HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex].VirtualAddress)) return FALSE;
	}
	UINT64 virtualBaseAddr = GET_4KB_PAGE_BASE(VirtualAddress);
	UINT64 offset = VirtualAddress - virtualBaseAddr;
	UINT64 index = (UINT64)-1;
	for (UINT64 i = 0; i < HookInfo->PageBaseCount; i++)
	{
		if ((UINT64)(HookInfo->HookPageInfo[i].PageBaseInfo.VirtualAddress) == virtualBaseAddr)
		{
			index = i;
			break;
		}
	}
	if (index == (UINT64)-1)
	{
		return FALSE;
	}
	UINT64 dataOffset = 0;
	for (UINT64 i = index; i < HookInfo->PageBaseCount; i++)
	{
		if (dataOffset >= DataSize)
		{
			break;
		}
		UINT64 tmpOffset = 0;
		if (i == index) tmpOffset = offset;
		UINT64 startAddress = PTR_ADD(UINT64, HookInfo->HookPageInfo[i].PageBaseInfo.VirtualAddress, tmpOffset);
		UINT64 endAddress = min(
			PTR_ADD(UINT64, HookInfo->HookPageInfo[i].PageBaseInfo.VirtualAddress, PAGE_SIZE),
			PTR_ADD(UINT64, HookInfo->HookPageInfo[index].PageBaseInfo.VirtualAddress, offset + DataSize)
		);
		size_t tmpSize = endAddress - startAddress;
		PUCHAR startShadowAddr = PTR_ADD(PUCHAR, HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex].VirtualAddress, tmpOffset);
		for (UINT64 j = 0; j < tmpSize; j++)
		{
			if (dataOffset >= DataSize)
			{
				break;
			}
			startShadowAddr[j] = ((PUCHAR)Data)[dataOffset++];
		}
	}
	return TRUE;
}
void FreeGuestShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex)
{
	if (!HookInfo) return;
	if (!HookInfo->HookPageInfo) return;
	for (SIZE_T i = 0; i < HookInfo->PageBaseCount; i++)
	{
		PMEMORY_INFO MemInfo = (PMEMORY_INFO) & (HookInfo->HookPageInfo[i].ShadowPageBaseInfo[ShadowPageIndex]);
		FreeNptPageTable(MemInfo);
	}
}
void RemoveHookInfo(PLIST_ENTRY ListHead, PHOOK_INFO HookInfo, PKSPIN_LOCK HookListLock)
{
	if (!ListHead || !HookInfo) return;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	PHOOK_INFO tmpInfo = FindHookInfoGuid(ListHead, &HookInfo->HookId, NULL);
	if (!tmpInfo)
	{
		if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
		return;
	}
	while (tmpInfo->RefCount > 0)
	{
		if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
		LARGE_INTEGER interval = { 0 };
		interval.QuadPart = -10000 * 10;
		KeDelayExecutionThread(KernelMode, FALSE, &interval);
		if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	}
	RemoveEntryList(&tmpInfo->HookList);
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
	if (!tmpInfo->DataPage)
	{
		RemoveAllHookFuncInfo(tmpInfo,TRUE);
	}
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetGuestShadowPage(&(g_CpuContexts[i]), tmpInfo, NO_SHADOW_PAGE, FALSE, TRUE, NULL);
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)tmpInfo, GET_PAGE_ALIGN_LENGTH(tmpInfo->CbSize), FALSE, FALSE, TRUE);
	}
	for (int j = 0; j < MAX_SHADOW_PAGE; j++) {
		FreeGuestShadowPage(tmpInfo, j);
	}
	if (tmpInfo->HookPageInfo) FreeHookPageInfo(tmpInfo->HookPageInfo,tmpInfo->PageBaseCount);
	ExFreePoolWithTag(tmpInfo, HOOK_POOL);
}
PHOOK_FUNC_INFO AddHookFuncInfo(PHOOK_INFO hookInfo, UINT64 OriginalFuncAddress, UINT64 HookFuncAddress, SIZE_T FuncLength)
{
	if (!hookInfo || !OriginalFuncAddress || !HookFuncAddress || FuncLength == 0) return NULL;
	if (hookInfo->DataPage) return NULL;
	UINT64 funcInfoPageSize = GET_PAGE_ALIGN_LENGTH(sizeof(HOOK_FUNC_INFO));
	PHOOK_FUNC_INFO funcInfo = ExAllocatePool2(POOL_FLAG_NON_PAGED, funcInfoPageSize, HOOK_POOL);
	if (!funcInfo) return NULL;
	memset(funcInfo, 0, funcInfoPageSize);
	funcInfo->OriginalFuncAddress = (PVOID)OriginalFuncAddress;
	funcInfo->HookFuncAddress = (PVOID)HookFuncAddress;
	funcInfo->FuncLength = FuncLength;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)funcInfo, funcInfoPageSize, TRUE, TRUE, FALSE);
	}
	ExInterlockedInsertHeadList(&hookInfo->HookFuncList.HookFuncListHead, &funcInfo->HookFuncList, &hookInfo->HookFuncList.HookFuncListLock);
	return funcInfo;
}
PHOOK_FUNC_INFO FindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress,BOOLEAN Lock)
{
	if (!RipAddress || !hookInfo) return NULL;
	if (hookInfo->DataPage) return NULL;
	if (IsListEmpty(&hookInfo->HookFuncList.HookFuncListHead)) return NULL;
	PHOOK_FUNC_INFO funcInfo = NULL;
	KIRQL irql = KeGetCurrentIrql();
	if (Lock)
	{
		KeAcquireSpinLock(&hookInfo->HookFuncList.HookFuncListLock, &irql);
	}
	for (PLIST_ENTRY entry = hookInfo->HookFuncList.HookFuncListHead.Flink; entry != &hookInfo->HookFuncList.HookFuncListHead; entry = entry->Flink)
	{
		PHOOK_FUNC_INFO tmpInfo = CONTAINING_RECORD(entry, HOOK_FUNC_INFO, HookFuncList);
		if (RipAddress >= (UINT64)tmpInfo->OriginalFuncAddress && RipAddress < (UINT64)tmpInfo->OriginalFuncAddress + tmpInfo->FuncLength)
		{
			funcInfo = tmpInfo;
			break;
		}
	}
	if (Lock)
	{
		KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
	}
	return funcInfo;
}
PHOOK_FUNC_INFO FindHookFuncInfoByJmpTrampoline(PHOOK_INFO hookInfo, UINT64 JmpTrampolineRipAddress, BOOLEAN Lock)
{
	if (!JmpTrampolineRipAddress || !hookInfo) return NULL;
	if (hookInfo->DataPage) return NULL;
	if (IsListEmpty(&hookInfo->HookFuncList.HookFuncListHead)) return NULL;
	PHOOK_FUNC_INFO funcInfo = NULL;
	KIRQL irql = KeGetCurrentIrql();
	if (Lock)
	{
		KeAcquireSpinLock(&hookInfo->HookFuncList.HookFuncListLock, &irql);
	}
	for (PLIST_ENTRY entry = hookInfo->HookFuncList.HookFuncListHead.Flink; entry != &hookInfo->HookFuncList.HookFuncListHead; entry = entry->Flink)
	{
		PHOOK_FUNC_INFO tmpInfo = CONTAINING_RECORD(entry, HOOK_FUNC_INFO, HookFuncList);
		if ((UINT64)tmpInfo->JumpTrampolineAddress <= JmpTrampolineRipAddress && ((UINT64)tmpInfo->JumpTrampolineAddress + tmpInfo->JumpTrampolineSize) > JmpTrampolineRipAddress)
		{
			funcInfo = tmpInfo;
			break;
		}
	}
	if (Lock)
	{
		KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
	}
	return funcInfo;
}
void RemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo,BOOLEAN Lock)
{
	if (!hookInfo || !funcInfo) return;
	KIRQL irql = KeGetCurrentIrql();
	if (Lock)
	{
		KeAcquireSpinLock(&hookInfo->HookFuncList.HookFuncListLock, &irql);
	}
	PHOOK_FUNC_INFO tmpInfo = FindHookFuncInfo(hookInfo, (UINT64)funcInfo->OriginalFuncAddress, FALSE);
	if (!tmpInfo)
	{
		if (Lock)
		{
			KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
		}
		return;
	}
	RemoveEntryList(&tmpInfo->HookFuncList);
	if (Lock)
	{
		KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
	}
	FreeJmpTrampoline(tmpInfo);
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)tmpInfo, GET_PAGE_ALIGN_LENGTH(sizeof(HOOK_FUNC_INFO)), FALSE, FALSE, TRUE);
	}
	ExFreePoolWithTag(tmpInfo, HOOK_POOL);
}
void RemoveAllHookFuncInfo(PHOOK_INFO hookInfo, BOOLEAN Lock)
{
	if (!hookInfo) return;
	KIRQL irql = KeGetCurrentIrql();
	if (Lock)
	{
		KeAcquireSpinLock(&hookInfo->HookFuncList.HookFuncListLock, &irql);
	}
	while (!IsListEmpty(&hookInfo->HookFuncList.HookFuncListHead))
	{
		PLIST_ENTRY entry = hookInfo->HookFuncList.HookFuncListHead.Flink;
		PHOOK_FUNC_INFO funcInfo = CONTAINING_RECORD(entry, HOOK_FUNC_INFO, HookFuncList);
		RemoveEntryList(&funcInfo->HookFuncList);
		if(Lock) KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
		FreeJmpTrampoline(funcInfo);
		ExFreePoolWithTag(funcInfo, HOOK_POOL);
		if(Lock) KeAcquireSpinLock(&hookInfo->HookFuncList.HookFuncListLock, &irql);
	}
	if (Lock)
	{
		KeReleaseSpinLock(&hookInfo->HookFuncList.HookFuncListLock, irql);
	}
}
void RemoveAllHookInfo(PLIST_ENTRY ListHead, PKSPIN_LOCK HookListLock)
{
	if (!ListHead) return;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	PLIST_ENTRY entry = ListHead->Flink;
	while (entry != ListHead)
	{
		PLIST_ENTRY nextEntry = entry->Flink;
		PHOOK_INFO hookInfo = CONTAINING_RECORD(entry, HOOK_INFO, HookList);
		RemoveEntryList(&hookInfo->HookList);
		if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
		if (!hookInfo->DataPage) RemoveAllHookFuncInfo(hookInfo, TRUE);
		for (int j = 0; j < MAX_SHADOW_PAGE; j++) {
			FreeGuestShadowPage(hookInfo, j);
		}
		if (hookInfo->HookPageInfo) FreeHookPageInfo(hookInfo->HookPageInfo,hookInfo->PageBaseCount);
		ExFreePoolWithTag(hookInfo, HOOK_POOL);
		if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
		entry = nextEntry;
	}
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
}
void HookReference(PHOOK_INFO HookInfo)
{
	if (!HookInfo) return;
	_InterlockedIncrement64((LONG64*)&(HookInfo->RefCount));
}
void HookDereference(PHOOK_INFO HookInfo)
{
	if (!HookInfo) return;
	if (HookInfo->RefCount > 0)
	{
		_InterlockedDecrement64((LONG64*)&(HookInfo->RefCount));
	}
}
BOOLEAN IsHookRefCountZero(PHOOK_INFO HookInfo)
{
	if (HookInfo->DataPage) return FALSE;
	if (HookInfo->RefCount > 0) return FALSE;
	return TRUE;
}
PHOOK_INFO EnumNextHookInfo(PLIST_ENTRY ListHead, PHOOK_INFO CurrentHookInfo, PKSPIN_LOCK HookListLock)
{
	if (!ListHead) return NULL;
	if (IsListEmpty(ListHead)) return NULL;
	PHOOK_INFO hookInfo = NULL;
	KIRQL irql = KeGetCurrentIrql();
	if (HookListLock) KeAcquireSpinLock(HookListLock, &irql);
	PLIST_ENTRY entry = NULL;
	if (!CurrentHookInfo)
	{
		entry = ListHead->Flink;
	}
	else
	{
		entry = CurrentHookInfo->HookList.Flink;
	}
	if (entry != ListHead)
	{
		hookInfo = CONTAINING_RECORD(entry, HOOK_INFO, HookList);
	}
	if (HookListLock) KeReleaseSpinLock(HookListLock, irql);
	return hookInfo;
}
PVOID AllocateJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo,SIZE_T Length)
{
	if (!HookFuncInfo || !Length || HookFuncInfo->JumpTrampolineAddress) return NULL;
	SIZE_T alignLength = GET_PAGE_ALIGN_LENGTH(Length);
	PVOID pJmpTrampoline = ExAllocatePool2(POOL_FLAG_NON_PAGED_EXECUTE, alignLength, JMP_POOL);
	if (!pJmpTrampoline) return NULL;
	memset(pJmpTrampoline, 0x90, alignLength);
	HookFuncInfo->JumpTrampolineAddress = pJmpTrampoline;
	HookFuncInfo->JumpTrampolineSize = alignLength;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)pJmpTrampoline, alignLength, FALSE, FALSE, FALSE);
	}
	return pJmpTrampoline;
}
void FreeJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo)
{
	if (!HookFuncInfo || !HookFuncInfo->JumpTrampolineAddress) return;
	PVOID pJmpTrampoline = HookFuncInfo->JumpTrampolineAddress;
	SIZE_T length = HookFuncInfo->JumpTrampolineSize;
	HookFuncInfo->JumpTrampolineAddress = NULL;
	HookFuncInfo->JumpTrampolineSize = 0;
	for (UINT32 i = 0; i < CpuCount; i++)
	{
		SetNestedPageProtection(&(g_CpuContexts[i]), (UINT64)pJmpTrampoline, length, FALSE, FALSE, TRUE);
	}
	ExFreePoolWithTag(pJmpTrampoline, JMP_POOL);
}
#pragma code_seg()