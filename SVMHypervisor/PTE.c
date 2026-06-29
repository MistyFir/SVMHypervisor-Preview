#include "PTE.h"
#pragma data_seg("shadow$002")
KSPIN_LOCK PageListLock = { 0 };
#pragma data_seg()
#pragma code_seg(".entry$002")
BOOLEAN InitPageList()
{
	KeInitializeSpinLock(&PageListLock);
	BOOLEAN result = TRUE;
	for (UINT32 i = 0; i < MAX_POOL_PAGES; i++)
	{
		if (!AllocateNptPageTable(&(PageList.MemoryList[i]), PAGE_SIZE))
		{
			_InterlockedExchange((LONG*)&(PageList.AccessedList[i]), 1);
			result = FALSE;
		}
		else
		{
			InterlockedCompareExchange((LONG*) & (PageList.AccessedList[i]), 0, PageList.AccessedList[i]);
		}
	}
	return result;
}
void FreeAllPageList()
{
	for (UINT32 i = 0; i < MAX_POOL_PAGES; i++)
	{
		InterlockedCompareExchange((LONG*) & (PageList.AccessedList[i]), 2, PageList.AccessedList[i]);
		FreeNptPageTable(&(PageList.MemoryList[i]));
	}
}
PMEMORY_INFO PopFromPageList(PUINT32 OutIndex)
{
	KIRQL irql = KeGetCurrentIrql();
	KeAcquireSpinLock(&PageListLock, &irql);
	for (UINT32 i = 0; i < MAX_POOL_PAGES; i++)
	{
		if (PageList.AccessedList[i] == 0)
		{
			PageList.AccessedList[i] = 1;
			KeReleaseSpinLock(&PageListLock, irql);
			if (OutIndex) *OutIndex = i;
			return &(PageList.MemoryList[i]);
		}
		/*if (InterlockedCompareExchange((volatile LONG*)&(PageList.AccessedList[i]), 1, 0) == 0)
		{
			return &(PageList.MemoryList[i]);
		}*/
	}
	KeReleaseSpinLock(&PageListLock, irql);
	return NULL;
}
BOOLEAN PushFromPageList(PMEMORY_INFO Memory, UINT32 Index)
{
	if (Index < MAX_POOL_PAGES)
	{
		if (((DWORD64) & (PageList.MemoryList[Index])) == (DWORD64)Memory)
		{
			KIRQL irql = KeGetCurrentIrql();
			KeAcquireSpinLock(&PageListLock, &irql);
			memset(Memory->VirtualAddress, 0, PAGE_SIZE);
			if (PageList.AccessedList[Index])
			{
				PageList.AccessedList[Index] = 0;
			}
			KeReleaseSpinLock(&PageListLock, irql);
			//InterlockedExchange((volatile LONG*)&(PageList.AccessedList[i]), 0);
			return TRUE;
		}
	}
	else
	{
		for (UINT32 i = 0; i < MAX_POOL_PAGES; i++)
		{
			if (((DWORD64) & (PageList.MemoryList[i])) == (DWORD64)Memory)
			{
				KIRQL irql = KeGetCurrentIrql();
				KeAcquireSpinLock(&PageListLock, &irql);
				memset(Memory->VirtualAddress, 0, PAGE_SIZE);
				if (PageList.AccessedList[i])
				{
					PageList.AccessedList[i] = 0;
				}
				KeReleaseSpinLock(&PageListLock, irql);
				//InterlockedExchange((volatile LONG*)&(PageList.AccessedList[i]), 0);
				return TRUE;
			}
		}
	}
	return FALSE;
}
BOOLEAN AllocateNptPageTable(PMEMORY_INFO MemoryInfo, SIZE_T size)
{
	if (!MemoryInfo)
	{
		return FALSE;
	}
	memset(MemoryInfo, 0, sizeof(MEMORY_INFO));
	PHYSICAL_ADDRESS phyAddr = { 0 };
	phyAddr.QuadPart = -1;
	MemoryInfo->VirtualAddress = MmAllocateContiguousMemory(size, phyAddr);
	if (!MemoryInfo->VirtualAddress)
	{
		DbgPrintEx(77, 0, "[-]failed to MmAllocateContiguousMemory.\n");
		return FALSE;
	}
	memset(MemoryInfo->VirtualAddress, 0, size);
	MemoryInfo->PhysicalAddress = MmGetPhysicalAddress(MemoryInfo->VirtualAddress);
	MemoryInfo->Size = size;
	//DbgPrintEx(77,0,"[+]Allocated at VA: %p, PA: %llX\n",MemoryInfo->VirtualAddress,MemoryInfo->PhysicalAddress.QuadPart);
	return TRUE;
}
void FreeNptPageTable(PMEMORY_INFO MemoryInfo) {
	if (MemoryInfo->VirtualAddress != NULL) {
		MmFreeContiguousMemory(MemoryInfo->VirtualAddress);
		MemoryInfo->VirtualAddress = NULL;
		MemoryInfo->PhysicalAddress.QuadPart = 0;
		MemoryInfo->Size = 0;
	}
}
BOOLEAN CreateSvmPageTable(PCPU_CONTEXT CpuContext)
{
	BOOLEAN result = FALSE;
	do
	{
		if (!AllocateNptPageTable(&(CpuContext->PageTableInfo.PML4), PAGE_SIZE))
		{
			DbgPrintEx(77, 0, "[-]failed to Allocate PML4\n");
			break;
		}
		if (!AllocateNptPageTable(&(CpuContext->PageTableInfo.PDPT), PAGE_SIZE))
		{
			DbgPrintEx(77, 0, "[-]failed to Allocate PDPT\n");
			break;
		}
		BOOLEAN pdResult = TRUE;
		for (size_t i = 0; i < 512; i++)
		{
			if (!AllocateNptPageTable(&(CpuContext->PageTableInfo.PD[i]), PAGE_SIZE))
			{
				DbgPrintEx(77, 0, "[-]failed to Allocate PD\n");
				pdResult = FALSE;
				break;
			}
		}
		if (!pdResult) break;
		result = TRUE;
	} while (FALSE);
	if (!result)
	{
		FreeNptPageTable(&CpuContext->PageTableInfo.PML4);
		FreeNptPageTable(&CpuContext->PageTableInfo.PDPT);
		for (size_t i = 0; i < 512; i++)
		{
			FreeNptPageTable(&CpuContext->PageTableInfo.PD[i]);
		}
		return result;
	}
	PPML4_ENTRY_4KB pml4 = (PPML4_ENTRY_4KB)CpuContext->PageTableInfo.PML4.VirtualAddress;
	PPDP_ENTRY_4KB pdpt = (PPDP_ENTRY_4KB)CpuContext->PageTableInfo.PDPT.VirtualAddress;
	pml4[0].AsUInt64 = 0;
	pml4[0].Bits.Valid = 1;
	pml4[0].Bits.Accessed = 1;
	pml4[0].Bits.Write = 1;
	pml4[0].Bits.User = 1;
	pml4[0].Bits.PageFrameNumber = CpuContext->PageTableInfo.PDPT.PhysicalAddress.QuadPart >> 12;
	for (int i = 0; i < 512; i++)
	{
		pdpt[i].AsUInt64 = 0;
		pdpt[i].Bits.Accessed = 1;
		pdpt[i].Bits.Valid = 1;
		pdpt[i].Bits.Write = 1;
		pdpt[i].Bits.User = 1;
		pdpt[i].Bits.PageFrameNumber = CpuContext->PageTableInfo.PD[i].PhysicalAddress.QuadPart >> 12;
	}
	for (size_t i = 0; i < 512; i++)
	{
		PPD_ENTRY_2MB pd_base = (PPD_ENTRY_2MB)CpuContext->PageTableInfo.PD[i].VirtualAddress;
		for (size_t j = 0; j < 512; j++)
		{
			pd_base[j].AsUInt64 = 0;
			pd_base[j].Bits.Accessed = 1;
			pd_base[j].Bits.Valid = 1;
			pd_base[j].Bits.Write = 1;
			pd_base[j].Bits.User = 1;
			pd_base[j].Bits.Dirty = 1;
			pd_base[j].Bits.PageFrameNumber = i * 512 + j;
			pd_base[j].Bits.LargePage = 1;
		}
	}
	//DbgPrintEx(77, 0, "[+]InitSvmPageTable success.\n");
	return TRUE;
}
BOOLEAN UpdateNpt(PCPU_CONTEXT CpuContext, UINT64 faultGPA, PPAGE_FAULT_EXIT_INFO PageFaultInfo)
{
	(PageFaultInfo);
	PPML4_ENTRY_4KB pml4 = CpuContext->PageTableInfo.PML4.VirtualAddress;
	UINT64 pml4Index = GetPML4Index(faultGPA);
	if (!pml4[pml4Index].Bits.Valid)
	{
		UINT32 tmpIndex = 0;
		PMEMORY_INFO memory = PopFromPageList(&tmpIndex);
		if (!memory) return FALSE;
		UINT64 newEntry = (memory->PhysicalAddress.QuadPart >> 12) << 12 | 0x7;
		if (InterlockedCompareExchange64((__int64*)&(pml4[pml4Index].AsUInt64), newEntry, 0) != 0)
		{
			PushFromPageList(memory,tmpIndex);
			return FALSE;
		}
	}
	PPDP_ENTRY_4KB pdpt = (PPDP_ENTRY_4KB)PA_TO_VA(pml4[pml4Index].Bits.PageFrameNumber << 12);
	if (!pdpt)
	{
		return FALSE;
	}
	UINT64 pdptIndex = GetPDPTIndex(faultGPA);
	if (!pdpt[pdptIndex].Bits.Valid)
	{
		UINT32 tmpIndex = 0;
		PMEMORY_INFO memory = PopFromPageList(&tmpIndex);
		if (!memory) return FALSE;
		PDP_ENTRY_4KB newEntry = { 0 };
		newEntry.AsUInt64 = (memory->PhysicalAddress.QuadPart >> 12) << 12 | 0x7;
		if (InterlockedCompareExchange64((__int64*)&(pdpt[pdptIndex].AsUInt64), newEntry.AsUInt64, 0) != 0)
		{
			PushFromPageList(memory,tmpIndex);
			return FALSE;
		}
	}
	PPD_ENTRY_4KB pd = (PPD_ENTRY_4KB)PA_TO_VA(pdpt[pdptIndex].Bits.PageFrameNumber << 12);
	if (!pd)
	{
		return FALSE;
	}
	UINT64 pdIndex = GetPDIndex(faultGPA);
	PD_ENTRY_2MB tmpPde = { 0 };
	tmpPde.AsUInt64 = pd[pdIndex].AsUInt64;
	if (tmpPde.Bits.LargePage)
	{
		SplitLargePage(CpuContext, faultGPA);
	}
	if (!pd[pdIndex].Bits.Valid)
	{
		UINT32 tmpIndex = 0;
		PMEMORY_INFO memory = PopFromPageList(&tmpIndex);
		if (!memory) return FALSE;
		PD_ENTRY_4KB newEntry = { 0 };
		newEntry.AsUInt64 = (memory->PhysicalAddress.QuadPart >> 12) << 12 | 0x7;
		if (InterlockedCompareExchange64((__int64*)&(pd[pdIndex].AsUInt64), newEntry.AsUInt64, 0) != 0)
		{
			PushFromPageList(memory,tmpIndex);
			return FALSE;
		}
	}
	PPT_ENTRY_4KB pt = (PPT_ENTRY_4KB)PA_TO_VA(pd[pdIndex].Bits.PageFrameNumber << 12);
	UINT64 ptIndex = GetPTIndex(faultGPA);
	PT_ENTRY_4KB finalEntry = { 0 };
	finalEntry.Fields.PageFrameNumber = faultGPA>>12;
	finalEntry.Fields.Valid = 1;
	finalEntry.Fields.Accessed = 1;
	finalEntry.Fields.Dirty = 1;
	finalEntry.Fields.Write = 1;
	finalEntry.Fields.User = 1;
	finalEntry.Fields.Accessed = 1;
	InterlockedExchange64((__int64*)&pt[ptIndex].AsUInt64, finalEntry.AsUInt64);
	return TRUE;
}
BOOLEAN FixedNptTable(PCPU_CONTEXT CpuContext, UINT64 faultGPA, PPAGE_FAULT_EXIT_INFO PageFaultInfo)
{
	(PageFaultInfo);
	BOOLEAN result = FALSE;
	PPML4_ENTRY_4KB pml4 = CpuContext->PageTableInfo.PML4.VirtualAddress;
	UINT64 pml4Idx = GetPML4Index(faultGPA);
	if (pml4[pml4Idx].Bits.Valid)
	{
		PPDP_ENTRY_4KB pdpt = (PPDP_ENTRY_4KB)PA_TO_VA(pml4[pml4Idx].Bits.PageFrameNumber << 12);
		if (pdpt)
		{
			UINT64 pdptIdx = GetPDPTIndex(faultGPA);
			if (pdpt[pdptIdx].Bits.Valid)
			{
				PPD_ENTRY_4KB pd = (PPD_ENTRY_4KB)PA_TO_VA(pdpt[pdptIdx].Bits.PageFrameNumber << 12);
				if (pd)
				{
					UINT64 pdIdx = GetPDIndex(faultGPA);
					PD_ENTRY_2MB tmpPde = { 0 };
					tmpPde.AsUInt64 = pd[pdIdx].AsUInt64;
					if (tmpPde.Bits.LargePage)
					{
						SplitLargePage(CpuContext, faultGPA);
					}
					if (pd[pdIdx].Bits.Valid)
					{
						PPT_ENTRY_4KB pt = (PPT_ENTRY_4KB)PA_TO_VA(pd[pdIdx].Bits.PageFrameNumber << 12);
						if (pt)
						{
							UINT64 ptIdx = GetPTIndex(faultGPA);
							pt[ptIdx].Fields.User = 1;
							pt[ptIdx].Fields.Write = 1;
							pt[ptIdx].Fields.Dirty = 1;
							pt[ptIdx].Fields.Accessed = 1;
							if (PageFaultInfo->Fields.Id)
								pt[ptIdx].Fields.NoExecute = 0;
							result = TRUE;
						}
					}
				}
			}
		}
	}
	return result;
}
void FreeSvmPageTable(PCPU_CONTEXT CpuContext)
{
	FreeNptPageTable(&CpuContext->PageTableInfo.PML4);
	FreeNptPageTable(&CpuContext->PageTableInfo.PDPT);
	for (size_t i = 0; i < 512; i++)
	{
		FreeNptPageTable(&(CpuContext->PageTableInfo.PD[i]));
	}
	FreeNptPageTable(&CpuContext->PageTableInfo.PT);
}
BOOLEAN BuildNestedPageTables1GByte(PCPU_CONTEXT CpuContext)
{
	if (!AllocateNptPageTable(&CpuContext->PageTableInfo.PDPT,sizeof(PDP_LARGE_TABLE_INFO)))
	{
		return FALSE;
	}
	PPML4_ENTRY_4KB pml4Entry = (PPML4_ENTRY_4KB)CpuContext->PageTableInfo.PML4.VirtualAddress;
	PPDP_LARGE_TABLE_INFO pdpEntry = (PPDP_LARGE_TABLE_INFO)CpuContext->PageTableInfo.PDPT.VirtualAddress;
	for (int pml4Index = 0; pml4Index < 512; pml4Index++)
	{
		pml4Entry[pml4Index].Bits.Valid = 1;
		pml4Entry[pml4Index].Bits.Write = 1;
		pml4Entry[pml4Index].Bits.User = 1;
		pml4Entry[pml4Index].Bits.PageFrameNumber = MmGetPhysicalAddress(&(pdpEntry->PdpPage[pml4Index])).QuadPart >> 12;
		for (int pdpIndex = 0; pdpIndex < 512; pdpIndex++)
		{
			pdpEntry->PdpPage[pml4Index].PdpArray[pdpIndex].Fields.Present = 1;
			pdpEntry->PdpPage[pml4Index].PdpArray[pdpIndex].Fields.Write = 1;
			pdpEntry->PdpPage[pml4Index].PdpArray[pdpIndex].Fields.User = 1;
			pdpEntry->PdpPage[pml4Index].PdpArray[pdpIndex].Fields.PageSize = 1;
			pdpEntry->PdpPage[pml4Index].PdpArray[pdpIndex].Fields.PageFrameNumber = (pml4Index * 512ULL + pdpIndex);
		}
	}
	return TRUE;
}
BOOLEAN Is2MBytePageTable(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddr)
{
	PPML4_ENTRY_4KB pml4Entry = CpuContext->PageTableInfo.PML4.VirtualAddress;
	UINT64 pml4Index = GetPML4Index(GuestPhysicalAddr);
	if (!pml4Entry[pml4Index].Bits.Valid)
	{
		return FALSE;
	}
	PPDP_ENTRY_4KB pdpEntry = (PPDP_ENTRY_4KB)PA_TO_VA(pml4Entry[pml4Index].Bits.PageFrameNumber << 12);
	UINT64 pdpIndex = GetPDPTIndex(GuestPhysicalAddr);
	if (!pdpEntry[pdpIndex].Bits.Valid)
	{
		return FALSE;
	}
	PPD_ENTRY_2MB pdEntry = (PPD_ENTRY_2MB)PA_TO_VA(pdpEntry[pdpIndex].Bits.PageFrameNumber << 12);
	UINT64 pdIndex = GetPDIndex(GuestPhysicalAddr);
	if (pdEntry[pdIndex].Bits.Valid && pdEntry[pdIndex].Bits.LargePage)
		return TRUE;
	else
		return FALSE;
}
BOOLEAN SplitLargePage(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddr)
{
	PPML4_ENTRY_4KB pml4Entry = CpuContext->PageTableInfo.PML4.VirtualAddress;
	if (!pml4Entry) return FALSE;
	UINT64 pml4Index = GetPML4Index(GuestPhysicalAddr);
	if (!pml4Entry[pml4Index].Bits.Valid)
	{
		return FALSE;
	}
	PPDP_ENTRY_4KB pdpEntry = (PPDP_ENTRY_4KB)PA_TO_VA(pml4Entry[pml4Index].Bits.PageFrameNumber<<12);
	if (!pdpEntry) return FALSE;
	UINT64 pdpIndex = GetPDPTIndex(GuestPhysicalAddr);
	if (!pdpEntry[pdpIndex].Bits.Valid)
	{
		return FALSE;
	}
	PPD_ENTRY_2MB pdEntry = (PPD_ENTRY_2MB)PA_TO_VA(pdpEntry[pdpIndex].Bits.PageFrameNumber << 12);
	if (!pdEntry) return FALSE;
	UINT64 pdIndex = GetPDIndex(GuestPhysicalAddr);
	if ((!pdEntry[pdIndex].Bits.Valid) || (!pdEntry[pdIndex].Bits.LargePage))
	{
		return FALSE;
	}
	UINT64 oldPdePhyscialAddr = pdEntry[pdIndex].Bits.PageFrameNumber << 21;
	PD_ENTRY_2MB oldPde = { .AsUInt64 = pdEntry[pdIndex].AsUInt64 };
	UINT32 tmpIndex = 0;
	PMEMORY_INFO ptInfo = PopFromPageList(&tmpIndex);
	if ((!ptInfo) || (!ptInfo->PhysicalAddress.QuadPart) || (!ptInfo->VirtualAddress)) return FALSE;
	PD_ENTRY_4KB uValueA = { 0 };
	uValueA.AsUInt64 = 0;
	uValueA.Bits.User = oldPde.Bits.User;
	uValueA.Bits.Write = oldPde.Bits.Write;
	uValueA.Bits.Avl = oldPde.Bits.Avl;
	uValueA.Bits.WriteThrough = oldPde.Bits.WriteThrough;
	uValueA.Bits.Valid = 1;
	uValueA.Bits.Accessed = 1;
	uValueA.Bits.NoExecute =  oldPde.Bits.NoExecute;
	uValueA.Bits.CacheDisable = oldPde.Bits.CacheDisable;
	uValueA.Bits.PageFrameNumber = ptInfo->PhysicalAddress.QuadPart >> 12;
	if (_InterlockedCompareExchange64(
		(LONG64*)&pdEntry[pdIndex].AsUInt64,
		uValueA.AsUInt64,
		(LONG64)oldPde.AsUInt64) != (LONG64)oldPde.AsUInt64)
	{
		PushFromPageList(ptInfo,tmpIndex); 
		return FALSE;
	}
	PPT_ENTRY_4KB ptEntry = (PPT_ENTRY_4KB)(ptInfo->VirtualAddress);
	for (int ptIndex = 0; ptIndex < 512; ptIndex++)
	{
		PT_ENTRY_4KB uValueB = { 0 };
		uValueB.Fields.User = oldPde.Bits.User;
		uValueB.Fields.Write = oldPde.Bits.Write;
		uValueB.Fields.Valid = 1;
		uValueB.Fields.Dirty = 1;
		uValueB.Fields.Accessed = 1;
		uValueB.Fields.WriteThrough = oldPde.Bits.WriteThrough;
		uValueB.Fields.NoExecute = oldPde.Bits.NoExecute;
		uValueB.Fields.CacheDisable = oldPde.Bits.CacheDisable;
		uValueB.Fields.PageFrameNumber = (oldPdePhyscialAddr >> 12) + ptIndex;
		_InterlockedExchange64((LONG64*)&(ptEntry[ptIndex].AsUInt64), uValueB.AsUInt64);
	}
	return TRUE;
}
UINT64 GpaToHpa(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddress)
{
	PPML4_ENTRY_4KB pml4Entry = (PPML4_ENTRY_4KB)CpuContext->PageTableInfo.PML4.VirtualAddress;
	if (!pml4Entry) return INVALID_PA;
	UINT64 pml4Index = GetPML4Index(GuestPhysicalAddress);
	if (!pml4Entry[pml4Index].Bits.Valid) return INVALID_PA;
	PPDP_ENTRY_4KB pdpEntry = (PPDP_ENTRY_4KB)PA_TO_VA(pml4Entry[pml4Index].Bits.PageFrameNumber << 12);
	if(!pdpEntry) return INVALID_PA;
	UINT64 pdpIndex = GetPDPTIndex(GuestPhysicalAddress);
	if (!pdpEntry[pdpIndex].Bits.Valid) return INVALID_PA;
	PPD_ENTRY_4KB pdEntry = (PPD_ENTRY_4KB)PA_TO_VA(pdpEntry[pdpIndex].Bits.PageFrameNumber << 12);
	if (!pdEntry) return INVALID_PA;
	UINT64 pdIndex = GetPDIndex(GuestPhysicalAddress);
	if (!pdEntry[pdIndex].Bits.Valid) return INVALID_PA;
	PPD_ENTRY_2MB pd2MByteEntry = (PPD_ENTRY_2MB)pdEntry;
	if (pd2MByteEntry[pdIndex].Bits.LargePage)
	{
		return (pd2MByteEntry[pdIndex].Bits.PageFrameNumber << 21) | (GuestPhysicalAddress - GET_2MB_PAGE_BASE(GuestPhysicalAddress));
	}
	else
	{
		PPT_ENTRY_4KB ptEntry = (PPT_ENTRY_4KB)PA_TO_VA(pdEntry[pdIndex].Bits.PageFrameNumber << 12);
		if (!ptEntry) return INVALID_PA;
		UINT64 ptIndex = GetPTIndex(GuestPhysicalAddress);
		if (!ptEntry[ptIndex].Fields.Valid) return INVALID_PA;
		return (ptEntry[ptIndex].Fields.PageFrameNumber << 12) | (GuestPhysicalAddress - GET_4KB_PAGE_BASE(GuestPhysicalAddress));
	}
}
BOOLEAN SetNestedPageProtection(PCPU_CONTEXT Context, UINT64 VirtualAddress, SIZE_T MapSize, BOOLEAN Hide, BOOLEAN NoExecute, BOOLEAN Write)
{
	UINT64 mapPageSize = GET_PAGE_ALIGN_LENGTH(MapSize);
	UINT64 pageCount = mapPageSize / PAGE_SIZE;
	for (UINT64 i = 0; i < pageCount; i++)
	{
		UINT64 physicalAddr = MmGetPhysicalAddress((PVOID)(((VirtualAddress >> 12) + i) << 12)).QuadPart;
		PPML4_ENTRY_4KB pml4Entry = Context->PageTableInfo.PML4.VirtualAddress;
		if (!pml4Entry) return FALSE;
		UINT64 pml4Index = GetPML4Index(physicalAddr);
		if (!pml4Entry[pml4Index].Bits.Valid) return FALSE;
		PPDP_ENTRY_4KB pdpEntry = (PPDP_ENTRY_4KB)PA_TO_VA(pml4Entry[pml4Index].Bits.PageFrameNumber << 12);
		if (!pdpEntry) return FALSE;
		UINT64 pdpIndex = GetPDPTIndex(physicalAddr);
		if (!pdpEntry[pdpIndex].Bits.Valid) return FALSE;
		PPD_ENTRY_4KB pdEntry = (PPD_ENTRY_4KB)PA_TO_VA(pdpEntry[pdpIndex].Bits.PageFrameNumber << 12);
		if (!pdEntry) return FALSE;
		UINT64 pdIndex = GetPDIndex(physicalAddr);
		if (((PPD_ENTRY_2MB) & (pdEntry[pdIndex]))->Bits.LargePage)
		{
			if (!SplitLargePage(Context, physicalAddr)) return FALSE;
		}
		PPT_ENTRY_4KB ptEntry = (PPT_ENTRY_4KB)PA_TO_VA(pdEntry[pdIndex].Bits.PageFrameNumber << 12);
		if (!ptEntry) return FALSE;
		UINT64 ptIndex = GetPTIndex(physicalAddr);
		PT_ENTRY_4KB tmpPt = { 0 };
		tmpPt.AsUInt64 = ptEntry[ptIndex].AsUInt64;
		tmpPt.Fields.Write = Write;
		tmpPt.Fields.NoExecute = NoExecute;
		if (Hide)
		{
			tmpPt.Fields.PageFrameNumber = g_ZeroPage.PhysicalAddress.QuadPart >> 12;
			tmpPt.Fields.Write = FALSE;
			tmpPt.Fields.NoExecute = TRUE;
		}
		else tmpPt.Fields.PageFrameNumber = physicalAddr >> 12;
		_InterlockedExchange64((PLONG64) & (ptEntry[ptIndex].AsUInt64), tmpPt.AsUInt64);
	}
	return TRUE;
}
#pragma code_seg()