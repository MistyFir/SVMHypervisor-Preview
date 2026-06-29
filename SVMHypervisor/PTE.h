#pragma once
#include <ntifs.h>
#include "X64Diasm.h"
#define PA_TO_VA(pa) MmGetVirtualForPhysical((PHYSICAL_ADDRESS){.QuadPart = (pa)})
#define GET_PAGE_OFFSET(VirtualAddress) ((DWORD_PTR)(VirtualAddress) & 0x0FFF)
#define GET_PAGE_INDEX(VirtualAddress) ((DWORD_PTR)(VirtualAddress) >> 12)
#define GET_4KB_PAGE_BASE(VirtualAddress) (((((UINT64)VirtualAddress) >> 12) << 12))
#define GET_2MB_PAGE_BASE(VirtualAddress) (((((UINT64)(VirtualAddress)) >> 21) << 21))
#define GET_PAGE_ALIGN_LENGTH(MapSize) (((MapSize)+0xFFFULL) &~0xFFFULL)
#define GET_PML4(PageIndex) (((PageIndex) >> 27) & 0x1FF)
#define GET_PDPT(PageIndex) (((PageIndex) >> 18) & 0x1FF)
#define GET_PD(PageIndex) (((PageIndex) >> 9) & 0x1FF)
#define GET_PT(PageIndex) ((PageIndex) & 0x1FF)
#define MAX_POOL_PAGES 8192
#define INVALID_PA ((UINT64)-1)
#define STACK_SIZE 0x20000
#pragma warning(disable:4201)
typedef union _VIRTUAL_ADDRESS_X64
{
    ULONG_PTR AsUInt64;
    struct
    {
        ULONG_PTR PageOffset : 12;
        ULONG_PTR PtIndex : 9;
        ULONG_PTR PdIndex : 9;
        ULONG_PTR PdptIndex : 9;
        ULONG_PTR Pml4Index : 9; 
        ULONG_PTR Reserved : 16;
    } Bits;
} VIRTUAL_ADDRESS_X64, * PVIRTUAL_ADDRESS_X64;
typedef struct _PT_ENTRY_4KB
{
    union
    {
        UINT64 AsUInt64;
        struct
        {
            UINT64 Valid : 1;               // [0]
            UINT64 Write : 1;               // [1]
            UINT64 User : 1;                // [2]
            UINT64 WriteThrough : 1;        // [3]
            UINT64 CacheDisable : 1;        // [4]
            UINT64 Accessed : 1;            // [5]
            UINT64 Dirty : 1;               // [6]
            UINT64 Pat : 1;                 // [7]
            UINT64 Global : 1;              // [8]
            UINT64 Avl : 3;                 // [9:11]
            UINT64 PageFrameNumber : 40;    // [12:51]
            UINT64 Reserved1 : 11;          // [52:62]
            UINT64 NoExecute : 1;           // [63]
        } Fields;
    };
} PT_ENTRY_4KB, * PPT_ENTRY_4KB;
typedef struct _PML4_ENTRY_2MB
{
    union
    {
        UINT64 AsUInt64;
        struct
        {
            UINT64 Valid : 1;               // [0]
            UINT64 Write : 1;               // [1]
            UINT64 User : 1;                // [2]
            UINT64 WriteThrough : 1;        // [3]
            UINT64 CacheDisable : 1;        // [4]
            UINT64 Accessed : 1;            // [5]
            UINT64 Reserved1 : 3;           // [6:8]
            UINT64 Avl : 3;                 // [9:11]
            UINT64 PageFrameNumber : 40;    // [12:51]
            UINT64 Reserved2 : 11;          // [52:62]
            UINT64 NoExecute : 1;           // [63]
        } Bits;
    };
} PML4_ENTRY_2MB, * PPML4_ENTRY_2MB,
PDP_ENTRY_2MB, * PPDP_ENTRY_2MB;
typedef struct _PD_ENTRY_2MB
{
    union
    {
        UINT64 AsUInt64;
        struct
        {
            UINT64 Valid : 1;               // [0]
            UINT64 Write : 1;               // [1]
            UINT64 User : 1;                // [2]
            UINT64 WriteThrough : 1;        // [3]
            UINT64 CacheDisable : 1;        // [4]
            UINT64 Accessed : 1;            // [5]
            UINT64 Dirty : 1;               // [6]
            UINT64 LargePage : 1;           // [7]
            UINT64 Global : 1;              // [8]
            UINT64 Avl : 3;                 // [9:11]
            UINT64 Pat : 1;                 // [12]
            UINT64 Reserved1 : 8;           // [13:20]
            UINT64 PageFrameNumber : 31;    // [21:51]
            UINT64 Reserved2 : 11;          // [52:62]
            UINT64 NoExecute : 1;           // [63]
        } Bits;
    };
} PD_ENTRY_2MB, * PPD_ENTRY_2MB;
typedef struct _PML4_ENTRY_4KB
{
    union
    {
        UINT64 AsUInt64;
        struct
        {
            UINT64 Valid : 1;               // [0]
            UINT64 Write : 1;               // [1]
            UINT64 User : 1;                // [2]
            UINT64 WriteThrough : 1;        // [3]
            UINT64 CacheDisable : 1;        // [4]
            UINT64 Accessed : 1;            // [5]
            UINT64 Reserved1 : 3;           // [6:8]
            UINT64 Avl : 3;                 // [9:11]
            UINT64 PageFrameNumber : 40;    // [12:51]
            UINT64 Reserved2 : 11;          // [52:62]
            UINT64 NoExecute : 1;           // [63]
        } Bits;
    };
} PML4_ENTRY_4KB, * PPML4_ENTRY_4KB,
PDP_ENTRY_4KB, * PPDP_ENTRY_4KB,
PD_ENTRY_4KB, * PPD_ENTRY_4KB;
#pragma pack(push,1)
typedef union _PAGE_FAULT_EXIT_INFO
{
    UINT64 ErrorCode;
    struct
    {
        UINT64 Present : 1;   // Bit 0
        UINT64 Write : 1;   // Bit 1
        UINT64 User : 1;   // Bit 2
        UINT64 ReservedBits : 1;   // Bit 3
        UINT64 Id : 1;   // Bit 4
        UINT64 Reserved1 : 1;   // Bit 5
        UINT64 ShadowStackAccess : 1; // Bit 6
        UINT64 Reserved2 : 25;  // Bits 7-31
        UINT64 GuestPhysicalAddressFault : 1;  // Bit 32
        UINT64 GuestPageTablesFault : 1;       // Bit 33
        UINT64 Reserved3 : 3;   // Bits 34-36
        UINT64 ShadowStackCheck : 1; // Bit 37
        UINT64 Reserved4 : 26;  // Bits 38-63
    } Fields;
} PAGE_FAULT_EXIT_INFO, * PPAGE_FAULT_EXIT_INFO;
typedef union _PDP_ENTRY_1GB
{
    UINT64 AsUInt64;
    struct
    {
        UINT64 Present : 1;     //Bit 0
        UINT64 Write : 1;   // Bit 1
        UINT64 User : 1;   // Bit 2
        UINT64 Writethrough : 1;   // Bit 3
        UINT64 CacheDisable : 1;   // Bit 4
        UINT64 Accessed : 1;    // Bit 5
        UINT64 Dirty : 1;   // Bit 6
        UINT64 PageSize : 1;    // Bit 7
        UINT64 GlobalPage : 1;  // Bit 8
        UINT64 Avl : 3;    // Bit 9
        UINT64 Pat : 1;   // Bit 12
        UINT64 Reserved1 : 17;    // Bit 13
        UINT64 PageFrameNumber : 22;    // Bit 30
        UINT64 Available : 7;    //  Bit 52
        UINT64 UsageDependsPke : 4;    // Bit 59
        UINT64 NoExecute : 1;   // Bit 63
    }Fields;
}PDP_ENTRY_1GB,*PPDP_ENTRY_1GB;
#pragma pack(pop)
__forceinline DWORD64 GetPML4Index(DWORD_PTR VirtualAddress)
{
	return GET_PML4(GET_PAGE_INDEX(VirtualAddress));
}
__forceinline DWORD64 GetPDPTIndex(DWORD_PTR VirtualAddress)
{
	return GET_PDPT(GET_PAGE_INDEX(VirtualAddress));
}
__forceinline DWORD64 GetPDIndex(DWORD_PTR VirtualAddress)
{
	return GET_PD(GET_PAGE_INDEX(VirtualAddress));
}
__forceinline DWORD64 GetPTIndex(DWORD_PTR VirtualAddress)
{
	return GET_PT(GET_PAGE_INDEX(VirtualAddress));
}
typedef struct _MEMORY_INFO
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    SIZE_T Size;
}MEMORY_INFO,*PMEMORY_INFO;
typedef struct _PTE_INFO
{
    MEMORY_INFO PML4;
    MEMORY_INFO PDPT;
    MEMORY_INFO PD[512];
    MEMORY_INFO PT;
}PTE_INFO,*PPTE_INFO;
typedef struct _CPU_CONTEXT
{
    MEMORY_INFO GuestVmcb;
    MEMORY_INFO HostVmcb;
    MEMORY_INFO Hsave;
    MEMORY_INFO Msrpm;
    MEMORY_INFO Iopm;
    MEMORY_INFO GuestStack;
    MEMORY_INFO HostStack;
    UINT64 Rsp;
    CONTEXT ThreadContext;
    UINT32 CpuIndex;
	PTE_INFO PageTableInfo;
    KSPIN_LOCK VmExitLock;
	KIRQL OldIrql;
    struct
    {
        UINT64 VmmcallCookie;
        UINT64 Tsc;
        BOOLEAN Valid;
    }Token;
    BOOLEAN Running;
}CPU_CONTEXT, * PCPU_CONTEXT;
typedef struct _PDP_LARGE_TABLE_INFO
{
    struct
    {
        PDP_ENTRY_1GB PdpArray[512];
    }PdpPage[512];
}PDP_LARGE_TABLE_INFO,*PPDP_LARGE_TABLE_INFO;
typedef struct _PAGE_INFO
{
    MEMORY_INFO MemoryList[MAX_POOL_PAGES];
    UINT32 AccessedList[MAX_POOL_PAGES];
}PAGE_INFO,*PPAGE_INFO;
#define MAKE_MEMORY_INFO(ValueName,VirtualAddress,Size)(\
MEMORY_INFO ValueName = { 0 };\
ValueName.VirtualAddress = VirtualAddress;\
ValueName.Size = Size;\
ValueName.PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);\)
__forceinline void BuildMemoryInfo(PMEMORY_INFO MemInfo,UINT64 PhysicalAddress,PVOID VirtualAddress,SIZE_T Size)
{
    MemInfo->PhysicalAddress.QuadPart = PhysicalAddress;
    MemInfo->VirtualAddress = VirtualAddress;
    MemInfo->Size = Size;
}
extern PAGE_INFO PageList;
EXTERN_C _declspec(dllexport) PCPU_CONTEXT g_CpuContexts;
EXTERN_C MEMORY_INFO g_ZeroPage;
BOOLEAN CreateSvmPageTable(PCPU_CONTEXT CpuContext);
BOOLEAN AllocateNptPageTable(PMEMORY_INFO MemoryInfo, SIZE_T size);
void FreeNptPageTable(PMEMORY_INFO MemoryInfo);
void FreeSvmPageTable(PCPU_CONTEXT CpuContext);
BOOLEAN InitPageList();
void FreeAllPageList();
BOOLEAN UpdateNpt(PCPU_CONTEXT CpuContext,UINT64 faultGPA, PPAGE_FAULT_EXIT_INFO PageFaultInfo);
BOOLEAN FixedNptTable(PCPU_CONTEXT CpuContext, UINT64 faultGPA, PPAGE_FAULT_EXIT_INFO PageFaultInfo);
BOOLEAN SplitLargePage(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddr);
PMEMORY_INFO PopFromPageList(PUINT32 OutIndex);
BOOLEAN PushFromPageList(PMEMORY_INFO Memory, UINT32 Index);
BOOLEAN Is2MBytePageTable(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddr);
BOOLEAN BuildNestedPageTables1GByte(PCPU_CONTEXT CpuContext);
UINT64 GpaToHpa(PCPU_CONTEXT CpuContext, UINT64 GuestPhysicalAddress);
BOOLEAN SetNestedPageProtection(PCPU_CONTEXT Context, UINT64 VirtualAddress, SIZE_T MapSize, BOOLEAN Hide, BOOLEAN NoExecute, BOOLEAN Write);