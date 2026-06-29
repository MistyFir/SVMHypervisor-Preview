# SVMHypervisor

基于 AMD SVM (Secure Virtual Machine) 的 NPT Hook 框架，运行于 Windows 内核模式。

## 概述

SVMHypervisor 利用 AMD-V (SVM) 的嵌套页表 (NPT) 机制，通过为 Guest 创建影子页面 (Shadow Page) 实现无痕 Hook。被 Hook 的函数在原始内存页上不可见，仅当 Guest 执行到目标地址时，NPT 将其重定向到包含 Hook 代码的影子页面，从而实现隐藏式拦截。

核心原理：
- **Shadow Page 0**：原始页面（Guest 正常访问时看到的内容）
- **Shadow Page 1**：Hook 页面（Guest 执行时 NPT 重定向到此页，包含跳转到 Hook 函数的代码）
- **蹦床 (Trampoline)**：用于保存原始指令并正确回调原函数

## 导出 API

### 全局变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `g_VmStart` | `BOOLEAN` | 置 `TRUE` 启动 SVM 虚拟化 |
| `g_Unload` | `BOOLEAN` | 置 `TRUE` 允许驱动卸载 |
| `g_bDebug` | `BOOLEAN` | 置 `TRUE` 开启调试模式 |
| `g_Test` | `BOOLEAN` | 测试变量，置`TRUE` `test`函数返回`STATUS_SUCCESS`，置`FALSE`返回`STATUS_ACCESS_DENIED` |
| `g_Test1` | `BOOLEAN` | 测试变量，用于测试Hypervisor只读内存是否生效 |
| `g_CpuContexts` | `PCPU_CONTEXT` | 各 CPU 上下文数组，Guest状态下不可用 |
| `CpuCount` | `ULONG` | 活跃 CPU 核心数 |

### Hook 管理 API

#### SvmAddHookInfo
```c
PHOOK_INFO SvmAddHookInfo(PCSTR TagStr, UINT64 VirtualAddress, SIZE_T MapSize);
```
创建一个 Hook 信息节点，管理目标虚拟地址范围的影子页面。
- `TagStr`：标签字符串，用于查找（建议为 `NULL`）
- `VirtualAddress`：要 Hook 的目标虚拟地址
- `MapSize`：覆盖的内存大小
- **返回**：`PHOOK_INFO` 指针，后续所有操作都依赖此句柄

#### SvmFindHookInfoTag
```c
PHOOK_INFO SvmFindHookInfoTag(PCSTR TagStr);
```
通过标签字符串查找已注册的 Hook 信息。

#### SvmFindHookInfoGuid
```c
PHOOK_INFO SvmFindHookInfoGuid(PGUID HookId);
```
通过 GUID 查找已注册的 Hook 信息。

#### SvmCreateShadowPage
```c
BOOLEAN SvmCreateShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
```
为指定 Hook 信息创建影子页面。影子页面初始内容为原始页面的副本。
- `ShadowPageIndex`：影子页面索引（`0` ~ `MAX_SHADOW_PAGE-1`），每个索引对应一组独立的影子页面

#### SvmSetGuestShadowPage
```c
BOOLEAN SvmSetGuestShadowPage(PCPU_CONTEXT CpuContext, PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, BOOLEAN NoExecute, BOOLEAN Write);
```
将指定影子页面设置到 Guest 的嵌套页表中，使其对 Guest 可见。
- `ShadowPageIndex`：影子页面索引，传入 `NO_SHADOW_PAGE` 则恢复为原始页面
- `NoExecute`：是否标记为不可执行
- `Write`：是否允许写入

#### SvmShadowCopyMemory
```c
BOOLEAN SvmShadowCopyMemory(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex, UINT64 VirtualAddress, PVOID Data, SIZE_T DataSize);
```
向指定影子页面中写入数据。数据会被写入影子页面对应虚拟地址的位置。
- `VirtualAddress`：目标虚拟地址（用于计算偏移）
- `Data`：要写入的数据缓冲区
- `DataSize`：数据大小

#### SvmAddHookFuncInfo
```c
PHOOK_FUNC_INFO SvmAddHookFuncInfo(PHOOK_INFO hookInfo, UINT64 OriginalFuncAddress, UINT64 HookFuncAddress, SIZE_T FuncLength);
```
向 Hook 信息中添加一个函数 Hook 记录。
- `OriginalFuncAddress`：被 Hook 的原函数地址
- `HookFuncAddress`：Hook 处理函数地址
- `FuncLength`：跳转代码长度（通过 `SvmGetJmpCodeBufferLength()` 获取）

#### SvmRemoveHookFuncInfo
```c
void SvmRemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo, BOOLEAN Lock);
```
移除一个函数 Hook 记录并释放蹦床内存。

#### SvmFindHookFuncInfo
```c
PHOOK_FUNC_INFO SvmFindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress, BOOLEAN Lock);
```
根据 RIP 地址查找对应的函数 Hook 记录。

### 蹦床 (Trampoline) API

#### SvmAllocateJmpTrampoline
```c
PVOID SvmAllocateJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo, SIZE_T Length);
```
为函数 Hook 分配 4KB 大小对齐的蹦床内存（可执行内存），大小通过 `SvmGetJmpCodeFuncBufferLength()` 获取。

#### SvmFreeJmpTrampoline
```c
void SvmFreeJmpTrampoline(PHOOK_FUNC_INFO HookFuncInfo);
```
释放蹦床内存。

#### SvmGetJmpCodeFuncBuffer / SvmGetJmpCodeFuncBufferLength
```c
void SvmGetJmpCodeFuncBuffer(PVOID Buffer, size_t Length);
UINT64 SvmGetJmpCodeFuncBufferLength();
```
获取蹦床功能代码模板及其长度。蹦床代码负责：保存原始指令 -> 调用 Hook 函数 -> 回调原函数剩余部分 -> 返回。

### 引用计数 API

#### SvmHookReference / SvmHookDereference
```c
void SvmHookReference(PHOOK_INFO HookInfo);
void SvmHookDereference(PHOOK_INFO HookInfo);
```
增减 Hook 信息的引用计数。

#### SvmIsHookRefCountZero
```c
BOOLEAN SvmIsHookRefCountZero(PHOOK_INFO HookInfo);
```
检查引用计数是否为零。

### 其他 API

#### SvmProtectDriverSection
```c
BOOLEAN SvmProtectDriverSection(UINT64 VirtualAddress, SIZE_T Size, BOOLEAN NoExecute, PBOOLEAN CoreStatus, UINT32 CoreCount);
```
该例程使用 Hypervisor 管理的 SLAT 表（二级地址转换）保护驱动程序部分的物理内存。将指定内存区域设为`READ`或`READ-EXECUTE`，最终内存大小为 4KB 的整数倍。Guest 无法修改受 SLAT 保护的内存，且在受保护期间，无法删除此保护。
- `NoExecute`：是否禁止执行
- `CoreStatus`：各核心设置结果（可为 `NULL`）
- `CoreCount`：最大核心数量（0 表示不记录状态）

#### GetInstructionLength
```c
UINT8 GetInstructionLength(PVOID CodeAddr, UINT8 MaxLength);
```
计算指定地址处的 x64 指令长度，用于确定需要复制的原函数序言字节数。
- `CodeAddr`：指令起始地址。
- `MaxLengh`：获取长度的最大限制数。

#### SvmEnumNextHookInfo
```c
PHOOK_INFO SvmEnumNextHookInfo(PHOOK_INFO CurrentHookInfo);
```
枚举下一个 Hook 信息节点，传入 `NULL` 获取第一个。

#### SvmFreeGuestShadowPage
```c
void SvmFreeGuestShadowPage(PHOOK_INFO HookInfo, UINT32 ShadowPageIndex);
```
释放指定索引的影子页面。

## HOOK 示例

以下示例展示如何使用导出 API Hook 一个内核函数。以 Hook `ZwClose` 为例：

```c
#include <ntifs.h>
#include "test.h"          // SVMHypervisor 导出头文件
#include "amd_defs.h"
#include <intrin.h>

#pragma section(".rhook", read, execute)
#pragma comment(linker, "/SECTION:.rhook,ER,ALIGN=4096")

typedef NTSTATUS(__stdcall* _NtClose)(HANDLE Handle);

// Hook 处理函数必须放在可执行段中
#pragma code_seg(".rhook$001")
NTSTATUS __stdcall Hook_NtClose(PHOOK_REGS Regs)
{
    HANDLE Handle = (HANDLE)Regs->Rcx;
    DbgPrintEx(77, 0, "[SVMHook] NtClose called! Handle: 0x%p\n", Handle);

    // 返回 STATUS_SUCCESS 继续执行原函数
    // 返回 STATUS_ACCESS_DENIED 拒绝调用
    return STATUS_SUCCESS;
}
#pragma code_seg()

static PHOOK_INFO      g_NtCloseHookInfo   = NULL;
static PHOOK_FUNC_INFO g_NtCloseFuncInfo    = NULL;
static PVOID           g_NtCloseOriginal    = NULL;

NTSTATUS InstallNtCloseHook()
{
    // 1. 获取原函数地址
    UNICODE_STRING funcName = RTL_CONSTANT_STRING(L"ZwClose");
    g_NtCloseOriginal = MmGetSystemRoutineAddress(&funcName);
    if (!g_NtCloseOriginal)
    {
        DbgPrintEx(77, 0, "[-] Failed to resolve NtClose.\n");
        return STATUS_NOT_FOUND;
    }

    // 2. 创建 Hook 信息节点
    g_NtCloseHookInfo = SvmAddHookInfo("NtClose", (UINT64)g_NtCloseOriginal, PAGE_SIZE * 2);
    if (!g_NtCloseHookInfo)
    {
        DbgPrintEx(77, 0, "[-] SvmAddHookInfo failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 3. 创建 Shadow Page 0（原始页面副本）
    if (!SvmCreateShadowPage(g_NtCloseHookInfo, 0))
    {
        DbgPrintEx(77, 0, "[-] SvmCreateShadowPage(0) failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 4. 创建 Shadow Page 1（Hook 页面副本）
    if (!SvmCreateShadowPage(g_NtCloseHookInfo, 1))
    {
        DbgPrintEx(77, 0, "[-] SvmCreateShadowPage(1) failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 5. 添加函数 Hook 记录
    g_NtCloseFuncInfo = SvmAddHookFuncInfo(
        g_NtCloseHookInfo,
        (UINT64)g_NtCloseOriginal,
        (UINT64)Hook_NtClose,
        (SIZE_T)SvmGetJmpCodeBufferLength()
    );
    if (!g_NtCloseFuncInfo)
    {
        DbgPrintEx(77, 0, "[-] SvmAddHookFuncInfo failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 6. 分配蹦床内存
    PJMP_FUNC_TRAMPOLINE trampoline = (PJMP_FUNC_TRAMPOLINE)SvmAllocateJmpTrampoline(
        g_NtCloseFuncInfo,
        (SIZE_T)SvmGetJmpCodeFuncBufferLength()
    );
    if (!trampoline)
    {
        DbgPrintEx(77, 0, "[-] SvmAllocateJmpTrampoline failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 7. 填充蹦床结构体
    //    复制蹦床代码模板
    SvmGetJmpCodeFuncBuffer(trampoline,(SIZE_T)SvmGetJmpCodeFuncBufferLength());

    //    计算原函数序言长度
    UINT8 originalCodeLen = GetInstructionLength(g_NtCloseOriginal, 20);
    DbgPrintEx(77, 0, "[*] NtClose prologue length: %d bytes.\n", originalCodeLen);

    //    设置蹦床偏移
    g_NtCloseFuncInfo->JumpTrampolineOffset = sizeof(trampoline->Data);

    //    设置三个关键地址
    DEF_PTR(UINT64, trampoline->Data.HookFuncAddress, 0) = (UINT64)Hook_NtClose;
    DEF_PTR(UINT64, trampoline->Data.CallbackAddress, 0) = (UINT64)g_NtCloseOriginal + originalCodeLen;
    DEF_PTR(UINT64, trampoline->Data.ReturnAddress, 0) = (UINT64)trampoline + SvmGetReturnOffset();

    //    保存原函数序言到蹦床
    memcpy(trampoline->Execute.OriginalCode, g_NtCloseOriginal, originalCodeLen);

    // 8. 在 Shadow Page 1 中用 INT3 替换原函数序言
    UINT8 jmpCode[256] = { 0 };
    memset(jmpCode, 0x90, sizeof(jmpCode));
    jmpCode[0] = INT_3;  // INT3 断点指令
    SvmShadowCopyMemory(g_NtCloseHookInfo, 1, (UINT64)g_NtCloseOriginal, jmpCode, originalCodeLen);

    // 9. 对所有核心设置 Shadow Page 0 为执行页面
    for (UINT32 i = 0; i < CpuCount; i++)
    {
        SvmSetGuestShadowPage(&g_CpuContexts[i], g_NtCloseHookInfo, 0, TRUE, FALSE);
    }

    DbgPrintEx(77, 0, "[+] NtClose hook installed successfully.\n");
    return STATUS_SUCCESS;
}
```

## HOOK 流程详解

### 安装 Hook 的完整步骤

```
1. SvmAddHookInfo()           -> 创建 Hook 信息节点，管理目标地址的页面
2. SvmCreateShadowPage(0)     -> 创建 Shadow Page 0（原始页面副本）
3. SvmCreateShadowPage(1)     -> 创建 Shadow Page 1（Hook 页面副本）
4. SvmAddHookFuncInfo()       -> 添加函数 Hook 记录
5. SvmAllocateJmpTrampoline() -> 分配蹦床可执行内存
6. 填充蹦床结构体：
   ├── 复制蹦床代码模板 (AsmGetJmpCodeFuncBase)
   ├── 设置 HookFuncAddress  -> Hook 处理函数地址
   ├── 设置 CallbackAddress  -> 原函数 + 序言长度（跳过被覆盖的指令）
   ├── 设置 ReturnAddress    -> 蹦床返回地址
   └── 复制原函数序言到 OriginalCode
7. SvmShadowCopyMemory(1)     -> 在 Shadow Page 1 中写入 INT3 (0xCC) 替换原函数序言
8. SvmSetGuestShadowPage(0)   -> 对所有核心设置 Shadow Page 0 为执行页面
```

### 执行流程

```
Guest 执行原函数地址
    |
    v  NPT 将执行重定向到 Shadow Page 1
    |
    v  执行到 INT3 (0xCC)
    |
    v  触发 #BP -> VM-Exit
    |
    v  VMM 查找 HookFuncInfo，跳转到蹦床
    |
    v  蹦床执行：
   ├── 执行原函数序言（OriginalCode）
   ├── 跳转到 HookFuncAddress（Hook 处理函数）
   │       └── Hook 函数可通过返回值控制行为
   ├── 跳转到 CallbackAddress（原函数序言之后的代码）
   └── 跳转到 ReturnAddress 返回
```

### Shadow Page 机制

| Shadow Page | 用途 | Guest 视角 |
|-------------|------|-----------|
| Page 0 | 原始页面 | Guest 读取/执行时看到原始内容，NPT 设为 NoExecute |
| Page 1 | Hook 页面 | Guest 执行时 NPT 重定向到此页，包含 INT3 断点 |
| NO_SHADOW_PAGE | 恢复原始 | 卸载 Hook 时恢复 NPT 指向原始物理页 |

### 蹦床结构 (JMP_FUNC_TRAMPOLINE)

```c
typedef struct _JMP_FUNC_TRAMPOLINE {
    struct {
        UINT8 HookFuncAddress[8];   // Hook 处理函数地址
        UINT8 CallbackAddress[8];   // 原函数跳过序言后的地址
        UINT8 ReturnAddress[8];     // 蹦床返回地址
    } Data;
    struct {
        UINT8 Reserved1[0x109];     // 跳转逻辑代码
        UINT8 OriginalCode[90];     // 原函数序言（被覆盖的指令）
        UINT8 Reserved2[0x21];      // 返回逻辑代码
    } Execute;
} JMP_FUNC_TRAMPOLINE;
```

- **Data 段**：存储三个关键地址，供 Execute 段的汇编代码使用
- **Execute 段**：实际执行的代码，从 `AsmGetJmpCodeFuncBase()` 获取模板
- **JumpTrampolineOffset**：设为 `sizeof(Data)`，标记 Data 段大小

### Hook 处理函数 (HOOK_REGS)

```c
typedef struct _HOOK_REGS {
    UINT64 Rcx;    // 第1个参数 / 返回值
    UINT64 Rdx;    // 第2个参数
    UINT64 R8;     // 第3个参数
    UINT64 R9;     // 第4个参数
    UINT64 Param5; // 第5个参数（栈）
    UINT64 Param6; // 第6个参数（栈）
    UINT64 Param7; // 第7个参数（栈）
} HOOK_REGS;
```

Hook 处理函数签名：
```c
NTSTATUS __stdcall YourHookFunc(PHOOK_REGS Regs);
```
- 返回 `STATUS_SUCCESS`：继续执行原函数
- 返回 `STATUS_ACCESS_DENIED`：拒绝调用，直接返回

## 数据页面保护

使用 `DataPage = TRUE` 的 Hook 信息可保护数据段不被 Guest 修改：

```c
// 方式一：使用便捷 API
SvmProtectDriverSection((UINT64)&g_MyVariable, PAGE_SIZE, TRUE, NULL, 0);

// 方式二：手动创建 Hook 信息并设置 DataPage
PHOOK_INFO dataProtect = SvmAddHookInfo("MyData", (UINT64)&g_MyVariable, PAGE_SIZE);
dataProtect->DataPage = TRUE;  // 标记为数据页面保护

for (UINT32 i = 0; i < CpuCount; i++)
{
    SvmSetGuestShadowPage(&g_CpuContexts[i], dataProtect, NO_SHADOW_PAGE, TRUE, FALSE);
}
```

## 驱动加载顺序

1. 加载 SVMHypervisor.sys（主驱动，初始化 SVM 虚拟化）
2. 等待 SVM 初始化完成（`g_VmStart == TRUE`）
3. 加载使用导出 API 的第三方驱动（如 TestDriver.sys）

第三方驱动示例入口：

```c
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    g_Unload = TRUE;
    g_bDebug = TRUE;
    if (!g_VmStart) g_VmStart = TRUE;

    // 等待 SVM 虚拟化启动完成
    LARGE_INTEGER timeout = { 0 };
    timeout.QuadPart = -10000 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &timeout);

    // 安装 Hook
    InstallNtCloseHook();

    DriverObject->DriverUnload = UnloadDriver;
    return STATUS_SUCCESS;
}
```

## 编译要求

- Visual Studio 2026 + WDK 28000.1761
- 目标平台：x64 Only
- 处理器要求：AMD CPU with SVM support
- 将SVMHypervisor项目属性中的导入库设置为自己的目录
- 链接 SVMHypervisor.lib 并包含 test.h / amd_defs.h

## 注意事项

- 该项目仅用于安全研究和学习目的
- Hook 处理函数必须放在可执行段中（如 `.rhook` section），且需要保护该段不被 Guest 修改
- 蹦床内存自动设置 NPT 保护（可执行但不可从 Guest 写入）
- 在 Guest 状态下无法卸载 Hook 
- `GetInstructionLength` 用于确定原函数序言长度，确保复制完整的指令边界
- 所有 `SvmSetGuestShadowPage` 调用需对每个 CPU 核心执行
