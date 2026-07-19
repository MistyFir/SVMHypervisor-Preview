#pragma once
#include <ntifs.h>
#include<guiddef.h>
#include <ntimage.h>
#define MAX_SHADOW_PAGE 128
#define NO_SHADOW_PAGE (UINT32)-1
#define JMP_POOL 'JpOl'
#define INT_3 0xCC
#define PA_TO_VA(pa) MmGetVirtualForPhysical((PHYSICAL_ADDRESS){.QuadPart = (pa)})
#define DEF_PTR(type,baseAddress,offset) (*(type*)(((UCHAR*)(baseAddress))+((__int64)(offset))))
#define PTR_ADD(type,baseAddress,offset) ((type)(((UCHAR*)(baseAddress))+((__int64)(offset))))
#define GET_4KB_PAGE_BASE(VirtualAddress) (((((UINT64)VirtualAddress) >> 12) << 12))
#define GET_PAGE_ALIGN_LENGTH(MapSize) (((MapSize)+0xFFFULL) &~0xFFFULL)
#define GET_PAGE_OFFSET(VirtualAddress) ((DWORD_PTR)(VirtualAddress) & 0x0FFF)
#define GET_2MB_PAGE_BASE(VirtualAddress) (((((UINT64)(VirtualAddress)) >> 21) << 21))
#define MAX_CALLBACK_COUNT 64
#define CPUID_CALLBACK 1
#define VMM_CALLBACK 2
#define BP_CALLBACK 3
typedef struct _MEMORY_INFO
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T Size;
}MEMORY_INFO, * PMEMORY_INFO;
typedef struct _CPU_CONTEXT
CPU_CONTEXT, * PCPU_CONTEXT;
typedef struct _HOOK_FUNC_INFO
{
    LIST_ENTRY HookFuncList;
    PVOID OriginalFuncAddress;
    PVOID HookFuncAddress;
    PVOID JumpTrampolineAddress;
    SIZE_T JumpTrampolineOffset;
    SIZE_T JumpTrampolineSize;
    SIZE_T FuncLength;
    UINT64 HookCount;
}HOOK_FUNC_INFO, * PHOOK_FUNC_INFO;
typedef struct _HOOK_PAGE_INFO
{
    MEMORY_INFO PageBaseInfo;
    MEMORY_INFO ShadowPageBaseInfo[MAX_SHADOW_PAGE];
}HOOK_PAGE_INFO, * PHOOK_PAGE_INFO;
typedef struct _HOOK_INFO
{
    LIST_ENTRY HookList;
    SIZE_T CbSize;
    CHAR TagStr[64];
    GUID HookId;
    SIZE_T PageBaseCount;
    BOOLEAN DataPage;
    PHOOK_PAGE_INFO HookPageInfo;
    struct
    {
        LIST_ENTRY HookFuncListHead;
        KSPIN_LOCK HookFuncListLock;
    }HookFuncList;
    volatile UINT64 RefCount;
}HOOK_INFO, * PHOOK_INFO;
#pragma pack(push,1)
typedef struct _JMP_CODE_INFO
{
    struct
    {
        UINT8 Reserved[6];
    }Execute;
    struct
    {
        UINT8 JmpAddress[8];
    }Data;
}JMP_CODE_INFO, * PJMP_CODE_INFO;
typedef struct _JMP_FUNC_TRAMPOLINE
{
    struct
    {
        UINT8 HookFuncAddress[8];
        UINT8 CallbackAddress[8];
        UINT8 ReturnAddress[8];
    }Data;
    struct
    {
        UINT8 Reserved1[0x109];
        UINT8 OriginalCode[90];
        UINT8 Reserved2[0x21];
    }Execute;
} JMP_FUNC_TRAMPOLINE, * PJMP_FUNC_TRAMPOLINE;
#pragma pack(pop)
typedef struct _HOOK_REGS
{
    UINT64 Rcx;
    UINT64 Rdx;
    UINT64 R8;
    UINT64 R9;
    UINT64 Param5;
    UINT64 Param6;
    UINT64 Param7;
}HOOK_REGS, * PHOOK_REGS;
#pragma pack(push,1)
typedef struct vmcb_section
{
    UINT16 Selector;
    UINT16 Attrib;
    UINT32 limit;
    UINT64 base;
}VMCB_SECTION, * PVMCB_SECTION;
#pragma pack(pop)
typedef union _EVENTINJ
{
    UINT64 Uo64;
    struct
    {
        UINT64 Vector : 8;
        UINT64 Type : 3;
        UINT64 ErrorCodeValid : 1;
        UINT64 Reserved1 : 19;
        UINT64 Valid : 1;
        UINT64 ErrorCode : 32;
    }Bits;
}EVENTINJ, * PEVENTINJ;
#pragma pack(push,1)
typedef struct _VMCB_CONTROL_AREA
{
    UINT16 InterceptCrRead;
    UINT16 InterceptCrWrite;
    UINT16 InterceptDrRead;
    UINT16 InterceptDrWrite;
    UINT32 InterceptException;
    UINT32 InterceptMisc1;
    UINT32 InterceptMisc2;
    UINT8 Reserved1[0x03c - 0x014];
    UINT16 PauseFilterThreshold;
    UINT16 PauseFilterCount;
    UINT64 IopmBasePa;
    UINT64 MsrpmBasePa;
    UINT64 TscOffset;
    UINT32 GuestAsid;
    UINT32 TlbControl;
    UINT64 VIntr;
    UINT64 InterruptShadow;
    UINT64 ExitCode;
    UINT64 ExitInfo1;
    UINT64 ExitInfo2;
    UINT64 ExitIntInfo;
    UINT64 NpEnable;
    UINT64 AvicApicBar;
    UINT64 GuestPaOfGhcb;
    EVENTINJ EventInj;
    UINT64 NCr3;
    UINT64 LbrVirtualizationEnable;
    UINT64 VmcbClean;
    UINT64 NRip;
    UINT8 NumOfBytesFetched;
    UINT8 GuestInstructionBytes[15];
    UINT64 AvicApicBackingPagePointer;
    UINT64 Reserved2;
    UINT64 AvicLogicalTablePointer;
    UINT64 AvicPhysicalTablePointer;
    UINT64 Reserved3;
    UINT64 VmcbSaveStatePointer;
    UINT8 Reserved4[0x400 - 0x110];
}VMCB_CONTROL_AREA, * PVMCB_CONTROL_AREA;
typedef struct _VMCB_STATE_SAVE_AREA
{
    VMCB_SECTION es;
    VMCB_SECTION cs;
    VMCB_SECTION ss;
    VMCB_SECTION ds;
    VMCB_SECTION fs;
    VMCB_SECTION gs;
    VMCB_SECTION gdtr;
    VMCB_SECTION ldtr;
    VMCB_SECTION idtr;
    VMCB_SECTION tr;
    UINT8 Reserved1[0x0cb - 0x0a0];
    UINT8 Cpl;
    UINT32 Reserved2;
    UINT64 Efer;
    UINT8 Reserved3[0x148 - 0x0d8];
    UINT64 Cr4;
    UINT64 Cr3;
    UINT64 Cr0;
    UINT64 Dr7;
    UINT64 Dr6;
    UINT64 Rflags;
    UINT64 Rip;
    UINT8 Reserved4[0x1d8 - 0x180];
    UINT64 Rsp;
    UINT8 Reserved5[0x1f8 - 0x1e0];
    UINT64 Rax;
    UINT64 Star;
    UINT64 LStar;
    UINT64 CStar;
    UINT64 SfMask;
    UINT64 KernelGsBase;
    UINT64 SysenterCs;
    UINT64 SysenterEsp;
    UINT64 SysenterEip;
    UINT64 Cr2;
    UINT8 Reserved6[0x268 - 0x248];
    UINT64 GPat;
    UINT64 DbgCtl;
    UINT64 BrFrom;
    UINT64 BrTo;
    UINT64 LastExcepFrom;
    UINT64 LastExcepTo;
} VMCB_STATE_SAVE_AREA, * PVMCB_STATE_SAVE_AREA;
#pragma pack(pop)
typedef struct _VMCB
{
    VMCB_CONTROL_AREA ControlArea;
    VMCB_STATE_SAVE_AREA StateSaveArea;
    UINT8 Reserved1[0x1000 - sizeof(VMCB_CONTROL_AREA) - sizeof(VMCB_STATE_SAVE_AREA)];
} VMCB, * PVMCB;
typedef struct _GUEST_REGS {
    unsigned __int64 rax;
    unsigned __int64 rcx;
    unsigned __int64 rdx;
    unsigned __int64 rbx;
    unsigned __int64 rsp;
    unsigned __int64 rbp;
    unsigned __int64 rsi;
    unsigned __int64 rdi;
    unsigned __int64 r8;
    unsigned __int64 r9;
    unsigned __int64 r10;
    unsigned __int64 r11;
    unsigned __int64 r12;
    unsigned __int64 r13;
    unsigned __int64 r14;
    unsigned __int64 r15;
} GUEST_REGS, * PGUEST_REGS;
typedef VOID(__stdcall* VMEXIT_CALLBACK)(PCPU_CONTEXT Context, PGUEST_REGS Regs);
typedef VOID(__stdcall* SVM_UNLOAD_CALLBACK)(PDRIVER_OBJECT DriverObject);
EXTERN_C _declspec(dllimport) SVM_UNLOAD_CALLBACK g_MdlUnloadCallback;
EXTERN_C _declspec(dllimport) PCPU_CONTEXT g_CpuContexts;
EXTERN_C _declspec(dllimport) BOOLEAN g_VmStart;
EXTERN_C _declspec(dllimport) BOOLEAN g_Test;
EXTERN_C _declspec(dllimport) BOOLEAN g_Test1;
EXTERN_C _declspec(dllimport) BOOLEAN g_Unload;
EXTERN_C _declspec(dllimport) BOOLEAN g_bDebug;
EXTERN_C _declspec(dllimport) DWORD64 g_Pid;
EXTERN_C _declspec(dllimport) volatile ULONG CpuCount;
EXTERN_C __declspec(dllimport) BOOLEAN CheckSvmSupport();
EXTERN_C _declspec(dllimport) PCPU_CONTEXT SvmGetCpuContextIndex(ULONG_PTR Index);
EXTERN_C _declspec(dllimport) void SvmGetGuestVmcb(PCPU_CONTEXT CpuContext, PMEMORY_INFO GuestVmcb);
EXTERN_C _declspec(dllimport) BOOLEAN SvmAddVmexitCallback(VMEXIT_CALLBACK Callback, UINT32 Flag, PUINT32 Index);
EXTERN_C _declspec(dllimport) BOOLEAN SvmRemoveVmexitCallback(UINT32 Flag, UINT32 Index);
EXTERN_C _declspec(dllimport) UINT8 GetInstructionLength(PVOID CodeAddr, UINT8 MaxLength);
EXTERN_C _declspec(dllimport) void SvmGetJmpCodeBuffer(PVOID Buffer, size_t Length);
EXTERN_C _declspec(dllimport) UINT64 SvmGetJmpCodeBufferLength();
EXTERN_C _declspec(dllimport) void SvmGetJmpCodeFuncBuffer(PVOID Buffer, size_t Length);
EXTERN_C _declspec(dllimport) UINT64 SvmGetJmpCodeFuncBufferLength();
EXTERN_C _declspec(dllimport) PVOID SvmAllocateJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo, SIZE_T Length);
EXTERN_C _declspec(dllimport) void SvmFreeJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo);
EXTERN_C _declspec(dllimport) PHOOK_INFO SvmAddHookInfo(PCSTR TagStr, UINT64 VirtualAddress, SIZE_T MapSize);
EXTERN_C _declspec(dllimport) PHOOK_INFO SvmFindHookInfoTag(PCSTR TagStr);
EXTERN_C _declspec(dllimport) PHOOK_INFO SvmFindHookInfoGuid(PGUID HookId);
EXTERN_C _declspec(dllimport) BOOLEAN SvmCreateShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
EXTERN_C _declspec(dllimport) BOOLEAN SvmSetGuestShadowPage(PCPU_CONTEXT CpuContext, PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, BOOLEAN NoExecute, BOOLEAN Write);
EXTERN_C _declspec(dllimport) BOOLEAN SvmShadowCopyMemory(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, UINT64 VirtualAddress, PVOID Data, SIZE_T DataSize);
EXTERN_C _declspec(dllimport) PHOOK_FUNC_INFO SvmAddHookFuncInfo(PHOOK_INFO hookInfo, UINT64 OriginalFuncAddress, UINT64 HookFuncAddress, SIZE_T FuncLength);
EXTERN_C _declspec(dllimport) PHOOK_FUNC_INFO SvmFindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress, BOOLEAN Lock);
EXTERN_C _declspec(dllimport) void SvmFreeGuestShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
EXTERN_C _declspec(dllimport) void SvmHookReference(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllimport) void SvmHookDereference(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllimport) PHOOK_INFO SvmFindHookInfoPageBase(PLIST_ENTRY ListHead, UINT64 VirtualAddress);
EXTERN_C _declspec(dllimport) PHOOK_FUNC_INFO SvmFindHookFuncInfoByJmpTrampoline(PHOOK_INFO hookInfo, UINT64 JmpTrampolineRipAddress, BOOLEAN Lock);
EXTERN_C _declspec(dllimport) BOOLEAN SvmIsHookRefCountZero(PHOOK_INFO HookInfo);
EXTERN_C _declspec(dllimport) PHOOK_INFO SvmEnumNextHookInfo(PHOOK_INFO CurrentHookInfo);
EXTERN_C _declspec(dllimport) void SvmRemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo, BOOLEAN Lock);
EXTERN_C _declspec(dllimport) BOOLEAN SvmProtectDriverSection(UINT64 VirtualAddress, SIZE_T Size, BOOLEAN NoExecute, PBOOLEAN CoreStatus, UINT32 CoreCount);
EXTERN_C _declspec(dllimport) UINT64 SvmGetReturnOffset();
EXTERN_C _declspec(dllimport) NTSTATUS PsTerminateThreadByPointer(PETHREAD pThread, NTSTATUS exitCode, BOOLEAN bDirectTerminate);
EXTERN_C _declspec(dllimport) NTSTATUS PsTerminateProcessByPid(DWORD64 pid);
EXTERN_C _declspec(dllimport) NTSTATUS __stdcall test();
EXTERN_C _declspec(dllimport) NTSTATUS __stdcall test2();