#pragma once
#include "PTE.h"
#include <ntifs.h>
EXTERN_C void __sgdt(void* Destination);
EXTERN_C UINT64 asm_lar(UINT64 selector);
EXTERN_C UINT64 asm_add(UINT64, UINT64);
EXTERN_C UINT16 AsmGetCs();
EXTERN_C UINT16 AsmGetDs();
EXTERN_C UINT16 AsmGetEs();
EXTERN_C UINT16 AsmGetSs();
EXTERN_C UINT16 AsmGetFs();
EXTERN_C UINT16 AsmGetGs();
EXTERN_C UINT16 AsmGetTr();
EXTERN_C VOID __jump(void*);
#define __readcs AsmGetCs
#define __readds AsmGetDs
#define __reades AsmGetEs
#define __readss AsmGetSs
#define __readfs AsmGetFs
#define __readgs AsmGetGs
#define __readtr AsmGetTr
#define __lar asm_lar
EXTERN_C PUCHAR AsmGetJmpCodeBase();
EXTERN_C UINT64 AsmGetJmpCodeLength();
EXTERN_C PUCHAR AsmGetJmpCodeFuncBase();
EXTERN_C UINT64 AsmGetJmpCodeFuncLength();
EXTERN_C void AsmLaunchVm(PCPU_CONTEXT context);
EXTERN_C UINT64 __readdr6();
EXTERN_C UINT64 __readdr7();
EXTERN_C UINT64 __readrsp();
EXTERN_C UINT64 __svm_vmmcall(UINT64,UINT64,UINT64);
EXTERN_C void AsmResetShadow();
EXTERN_C UINT64 AsmGetReturnOffset();
EXTERN_C UINT64 AsmGetVmAsmStartAddress();
EXTERN_C UINT64 AsmGetVmAsmEndAddress();
EXTERN_C void __writerax(UINT64);
EXTERN_C void __readrax(PUINT64);
