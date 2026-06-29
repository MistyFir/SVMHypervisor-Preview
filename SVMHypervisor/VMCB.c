#include "VMCB.h"
#include "asmfunc.h"
#include "PTE.h"
#include <intrin.h>
#include "amd_defs.h"
#include "Hook.h"
#pragma warning(disable:4244)
#pragma warning(disable:4242)
#pragma warning(disable:4189)
#pragma data_seg(".nonp")
#pragma comment(linker, "/SECTION:.nonp,RW,ALIGN:4096")
BOOLEAN IsVirtualCpu[MAX_SVM_THREADS] = { 0 };
#pragma data_seg()
#pragma code_seg(".entry$002")
BOOLEAN CheckIsVirtualCpu(UINT32 Index)
{
	if (!IsVirtualCpu[Index])
	{
		IsVirtualCpu[Index] = TRUE;
		return FALSE;
	}
	else return TRUE;
}
UINT64 GetSegmentBase(UINT64 gdt_base, UINT16 selector) {
	if ((selector & 0xFFF8) == 0) return 0;

	PSEGMENT_DESCRIPTOR descriptor = (PSEGMENT_DESCRIPTOR)(gdt_base + (selector & 0xFFF8));

	UINT64 base = (UINT64)descriptor->Fields.BaseLow |
		((UINT64)descriptor->Fields.BaseMiddle << 16) |
		((UINT64)descriptor->Fields.BaseHigh << 24);
	return base;
}
void FillSegment(UINT16 selector, PVMCB_SECTION section)
{
	DESCRIPTOR_TABLE_REGISTER gdt = { 0 };
	__sgdt(&gdt);
	if ((selector & ~3) == 0)
	{
		memset(section, 0, sizeof(VMCB_SECTION));
		return;
	}
	section->base = GetSegmentBase(gdt.Base, selector);
	PSEGMENT_DESCRIPTOR originalAttrib = (PSEGMENT_DESCRIPTOR)(gdt.Base + (selector & ~3));
	section->limit = __segmentlimit(selector);
	section->Selector = selector;
	SEGMENT_ATTRIBUTES attrib = { 0 };
	attrib.Fields.Type = originalAttrib->Fields.Type;
	attrib.Fields.S = originalAttrib->Fields.System;
	attrib.Fields.P = originalAttrib->Fields.Present;
	attrib.Fields.L = originalAttrib->Fields.LongMode;
	attrib.Fields.G = originalAttrib->Fields.Granularity;
	attrib.Fields.Dpl = originalAttrib->Fields.Dpl;
	attrib.Fields.Db = originalAttrib->Fields.DefaultBit;
	attrib.Fields.Avl = originalAttrib->Fields.Avl;
	attrib.Fields.Reserved = 0;
	section->Attrib = attrib.Uo16;
}
BOOLEAN AllocVMCB(PCPU_CONTEXT context)
{
	if (!context) return FALSE;
	BOOLEAN result = FALSE;
	do
	{
		result = (AllocateNptPageTable(&(context->GuestVmcb), PAGE_SIZE) 
			   && AllocateNptPageTable(&(context->Hsave), PAGE_SIZE) 
			   && AllocateNptPageTable(&(context->HostVmcb), PAGE_SIZE) 
			   && AllocateNptPageTable(&(context->Iopm), PAGE_SIZE * 3) 
			   && AllocateNptPageTable(&(context->Msrpm), PAGE_SIZE * 2));
	} while (FALSE);
	if (!result)
	{
		FreeVMCB(context);
		return result;
	}
	PVOID hostStack = NULL;
	hostStack = ExAllocatePool2(POOL_FLAG_NON_PAGED, STACK_SIZE, 'SVM');
	if (!hostStack)
	{
		FreeVMCB(context);
		return FALSE;
	}
	memset(hostStack, 0, STACK_SIZE);
	BuildMemoryInfo(&(context->HostStack), (UINT64)MmGetPhysicalAddress(hostStack).QuadPart, hostStack, STACK_SIZE);
	return result;
}
BOOLEAN InitVMCB(PCPU_CONTEXT context)
{
	if (!context) return FALSE;
	BOOLEAN result = FALSE;
	do
	{
		PVMCB vmcb = (PVMCB)context->GuestVmcb.VirtualAddress;
		if (!vmcb)
		{
			//DbgPrintEx(77, 0, "[-]Failed to Allocate vmcb.\n");
			break;
		}
		if (!context->Hsave.VirtualAddress || !context->Hsave.PhysicalAddress.QuadPart)
		{
			//DbgPrintEx(77, 0, "[-]Failed to Allocate hsave.\n");
			break;
		}
		if (!context->HostVmcb.PhysicalAddress.QuadPart || !context->HostVmcb.VirtualAddress)
		{
			//DbgPrintEx(77, 0, "[-]Failed to Allocate host vmcb.\n");
			break;
		}
		PVMCB hostVmcb = (PVMCB)context->HostVmcb.VirtualAddress;
		DESCRIPTOR_TABLE_REGISTER gdt = { 0 };
		__sgdt(&gdt);
		DESCRIPTOR_TABLE_REGISTER idt= { 0 };
		__sidt(&idt);
		vmcb->ControlArea.NpEnable = 1;
		vmcb->ControlArea.NCr3 = context->PageTableInfo.PML4.PhysicalAddress.QuadPart;
		vmcb->ControlArea.GuestAsid = context->CpuIndex + 1;
		vmcb->ControlArea.InterceptException |= INTERCEPT_EXCP_BP;
		vmcb->ControlArea.InterceptMisc1 |= INTERCEPT_MISC1_SHUTDOWN | INTERCEPT_MISC1_IOIO | INTERCEPT_MISC1_MSR;
		vmcb->ControlArea.InterceptMisc2 |= INTERCEPT_MISC2_VMMCALL | INTERCEPT_MISC2_VMRUN;
		vmcb->StateSaveArea.Cr0 = __readcr0();
		vmcb->StateSaveArea.Cr2 = __readcr2();
		vmcb->StateSaveArea.Cr3 = __readcr3();
		vmcb->StateSaveArea.Cr4 = __readcr4();
		vmcb->StateSaveArea.Efer = __readmsr(MSR_EFER) | EFER_SVME;
		vmcb->StateSaveArea.Rflags = context->ThreadContext.EFlags | 0x200;
		FillSegment(context->ThreadContext.SegEs, &vmcb->StateSaveArea.es);
		FillSegment(context->ThreadContext.SegCs, &vmcb->StateSaveArea.cs);
		FillSegment(context->ThreadContext.SegSs, &vmcb->StateSaveArea.ss);
		FillSegment(context->ThreadContext.SegDs, &vmcb->StateSaveArea.ds);
		FillSegment(context->ThreadContext.SegFs, &vmcb->StateSaveArea.fs);
		FillSegment(context->ThreadContext.SegGs, &vmcb->StateSaveArea.gs);
		vmcb->StateSaveArea.fs.base = __readmsr(MSR_FSBASE);
		vmcb->StateSaveArea.gs.base = __readmsr(MSR_GSBASE);
		FillSegment(AsmGetTr(), &vmcb->StateSaveArea.tr);
		vmcb->StateSaveArea.gdtr.base = gdt.Base;
		vmcb->StateSaveArea.gdtr.limit = gdt.Limit;
		vmcb->StateSaveArea.idtr.base = idt.Base;
		vmcb->StateSaveArea.idtr.limit = idt.Limit;
		vmcb->StateSaveArea.KernelGsBase = __readmsr(MSR_KERNEL_GSBASE);
		vmcb->StateSaveArea.Rax = context->GuestVmcb.PhysicalAddress.QuadPart;
		vmcb->StateSaveArea.Rsp = context->ThreadContext.Rsp;
		vmcb->StateSaveArea.Rip = context->ThreadContext.Rip;
		vmcb->ControlArea.IopmBasePa = context->Iopm.PhysicalAddress.QuadPart;
		vmcb->ControlArea.MsrpmBasePa = context->Msrpm.PhysicalAddress.QuadPart;
		IOPM_ENABLE_PORT(((PUCHAR)context->Iopm.VirtualAddress), 0xB2);
		IOPM_ENABLE_PORT(((PUCHAR)context->Iopm.VirtualAddress), 0xB0);
		((PUINT8)context->Msrpm.VirtualAddress)[0x820] |= 0x03;
		vmcb->StateSaveArea.GPat = __readmsr(MSR_PAT);
		hostVmcb->StateSaveArea.Rsp = PTR_ADD(UINT64, context->HostStack.VirtualAddress, STACK_SIZE - 8);
		*(UINT64*)hostVmcb->StateSaveArea.Rsp = (UINT64)context;
		result = TRUE;
	} while (FALSE);
	if (!result)
	{
		return FALSE;
	}
	return result;
}
BOOLEAN StartSVM(PCPU_CONTEXT context)
{
	if (!context) return FALSE;
	PVMCB guestVmcb = (PVMCB)context->GuestVmcb.VirtualAddress;
	PVMCB hostVmcb = (PVMCB)context->HostVmcb.VirtualAddress;
	guestVmcb->StateSaveArea.Rip = context->ThreadContext.Rip;
	guestVmcb->StateSaveArea.Rsp = context->ThreadContext.Rsp;
	guestVmcb->StateSaveArea.Efer = (__readmsr(MSR_EFER) | EFER_SVME);
	hostVmcb->StateSaveArea.Rsp = PTR_ADD(UINT64, context->HostStack.VirtualAddress, STACK_SIZE - 8);
	__writemsr(MSR_EFER, __readmsr(MSR_EFER) | EFER_SVME);
	__svm_vmsave(context->GuestVmcb.PhysicalAddress.QuadPart);
	__writemsr(MSR_VM_HSAVE_PA, context->Hsave.PhysicalAddress.QuadPart);
	__svm_vmsave(context->HostVmcb.PhysicalAddress.QuadPart);
	if(!context->GuestVmcb.PhysicalAddress.QuadPart||!context->GuestVmcb.VirtualAddress||!context->HostVmcb.PhysicalAddress.QuadPart)
	{
		//DbgPrintEx(77, 0, "[-]Failed to start SVM.\n");
		return FALSE;
	}
	AsmLaunchVm(context);
	//DbgPrintEx(77, 0, "[+]Start SVM Success.\n");
	return TRUE;
}
void FreeVMCB(PCPU_CONTEXT context)
{
	if (!context) return;
	FreeNptPageTable(&(context->GuestVmcb));
	FreeNptPageTable(&(context->Hsave));
	FreeNptPageTable(&(context->HostVmcb));
	FreeNptPageTable(&(context->Iopm));
	FreeNptPageTable(&(context->Msrpm));
	if (context->HostStack.VirtualAddress)
	{
		ExFreePoolWithTag(context->HostStack.VirtualAddress, 'SVM');
		memset(&(context->HostStack), 0, sizeof(MEMORY_INFO));
	}
}
#pragma code_seg()