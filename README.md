# ⚡ SVMHypervisor

**基于 AMD SVM / NPT 的 Windows 内核级 Hook 研究框架**

[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D4?style=flat-square&logo=windows)](https://learn.microsoft.com/windows-hardware/drivers/)
[![Architecture](https://img.shields.io/badge/CPU-AMD%20SVM-ED1C24?style=flat-square)](https://www.amd.com/en/developer/sev.html)
[![Language](https://img.shields.io/badge/language-C%20%7C%20x64%20ASM-00599C?style=flat-square&logo=c)](https://github.com/topics/kernel-driver)
[![License](https://img.shields.io/badge/license-Research%20Only-orange?style=flat-square)](#安全说明)

SVMHypervisor 是一个运行在 Windows 内核模式下、基于 AMD SVM 与嵌套页表（NPT）的研究型 Hypervisor。项目通过按访问类型切换 NPT 映射实现函数 Hook 和内存写保护。

> [!CAUTION]
> 本项目包含可导致系统崩溃、任意内核内存访问和 MSR 修改的测试代码，仅用于隔离的研究与调试环境。

## 📚 内容导航

- [项目组成](#-项目组成)
- [工作原理](#-工作原理)
- [导出 API](#-导出-api)
- [Hook 示例](#-hook-示例)
- [Hook 流程详解](#-hook-流程详解)
- [数据页面保护](#-数据页面保护)
- [构建要求](#-构建要求)
- [启动顺序](#-启动顺序)
- [生命周期与清理](#-生命周期与清理)
- [注意事项与限制](#-注意事项与限制)
- [安全说明](#-安全说明)

## 🧩 项目组成

| 项目 | 类型 | 用途 |
|------|------|------|
| `SVMHypervisor` | x64 内核驱动 | 初始化 SVM、维护 VMCB/NPT、处理 VM-Exit，并导出 Hook API |
| `TestDriver` | x64 内核驱动 | 演示函数 Hook、页面保护、VM 启停和测试 IOCTL |
| `Ring3_Test` | x64 用户程序 | 与 `TestDriver` 的 `\\.\SVMTest` 设备通信 |

主要源码：

- `SVMHypervisor/driver.c`：驱动入口、SVM 生命周期和 VM-Exit 分派。
- `SVMHypervisor/VMCB.c`：VMCB 与虚拟 CPU 状态管理。
- `SVMHypervisor/PTE.c`：NPT 构建、拆分页和权限设置。
- `SVMHypervisor/Hook.c`：Hook、影子页和蹦床对象管理。
- `SVMHypervisor/export_func.c`：供第三方内核驱动使用的导出接口。
- `TestDriver/driver.c`：当前实现对应的完整使用示例。

## ⚙️ 工作原理

函数 Hook 使用同一 Guest 物理页对应的两份影子页：

- **Shadow Page 0**：原始内容副本，初始映射为只读且不可执行。
- **Shadow Page 1**：包含 `INT3` 的 Hook 副本，执行 NPF 后映射为只读且可执行。
- **Trampoline**：保存完整的原函数序言、Hook 地址、回调地址和返回地址。

执行流程：

1. Guest 从 Shadow Page 0 执行目标地址，因 NX 触发 NPF VM-Exit。
2. Hypervisor 验证目标 Hook，并将当前核心的 NPT 映射切换到 Shadow Page 1。
3. Guest 在原函数入口执行 `INT3`，触发 `#BP` VM-Exit。
4. Hypervisor 将 Guest RIP 改为 `JumpTrampolineAddress + JumpTrampolineOffset`。
5. 蹦床调用 Hook 处理函数并执行原序言，返回成功时继续执行原函数剩余部分。

映射切换按 CPU 核心独立生效，修改 NPT 后必须使对应核心的 TLB 失效。

## 🔌 导出 API

### 全局变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `g_VmStart` | `BOOLEAN` | 置 `TRUE` 启动 SVM 虚拟化 |
| `g_Unload` | `BOOLEAN` | 置 `TRUE` 允许驱动卸载 |
| `g_bDebug` | `BOOLEAN` | 置 `TRUE` 开启调试模式 |
| `g_Test` | `BOOLEAN` | 测试变量，置`TRUE` `test`函数返回`STATUS_SUCCESS`，置`FALSE`返回`STATUS_ACCESS_DENIED` |
| `g_Test1` | `BOOLEAN` | 测试 Hypervisor 只读内存保护 |
| `g_Pid` | `DWORD64` | TestDriver 使用的目标进程 ID |

`g_CpuContexts` 和 `CpuCount` 属于 Hypervisor 内部状态。第三方驱动获取 CPU 上下文时应调用 `SvmGetCpuContextIndex()`，不要直接解引用内部数组。

### CPU 与 VM-Exit API

```c
PCPU_CONTEXT SvmGetCpuContextIndex(ULONG_PTR Index);
void SvmGetGuestVmcb(PCPU_CONTEXT CpuContext, PMEMORY_INFO GuestVmcb);
BOOLEAN SvmAddVmexitCallback(VMEXIT_CALLBACK Callback, UINT32 Flag, PUINT32 Index);
BOOLEAN SvmRemoveVmexitCallback(UINT32 Flag, UINT32 Index);
```

回调类型包括 `CPUID_CALLBACK`、`VMM_CALLBACK` 和 `BP_CALLBACK`，每类最多注册 `MAX_CALLBACK_COUNT` 个回调。回调运行在 VM-Exit 上下文中，不得执行可能阻塞或不适用于当前 IRQL 的操作。

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
- `FuncLength`：用于匹配该函数入口地址范围的长度；当前 TestDriver 示例传入 `20`。它不是蹦床模板长度。

#### SvmRemoveHookFuncInfo
```c
void SvmRemoveHookFuncInfo(PHOOK_INFO hookInfo, PHOOK_FUNC_INFO funcInfo, BOOLEAN Lock);
```
移除一个函数 Hook 记录并释放蹦床内存。Hypervisor 一旦进入 Guest 状态后，该 API 不能再使用。

#### SvmFindHookFuncInfo
```c
PHOOK_FUNC_INFO SvmFindHookFuncInfo(PHOOK_INFO hookInfo, UINT64 RipAddress, BOOLEAN Lock);
```
根据 RIP 地址查找对应的函数 Hook 记录。

#### 其他查找接口

```c
PHOOK_INFO SvmFindHookInfoPageBase(PLIST_ENTRY ListHead, UINT64 VirtualAddress);
PHOOK_FUNC_INFO SvmFindHookFuncInfoByJmpTrampoline(PHOOK_INFO HookInfo, UINT64 RipAddress, BOOLEAN Lock);
```

这两个接口暴露了内部列表相关能力，调用方必须保证传入对象和锁参数的生命周期正确。

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
- `MaxLength`：获取长度的最大限制数。

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

## 🪝 Hook 示例

以下示例展示核心安装步骤。完整实现以 `TestDriver/driver.c` 中的 `TestInstallHook` 为准。示例 Hook `NtOpenProcess`：

```c
#include <ntifs.h>
#include "test.h"          // SVMHypervisor 导出头文件
#include "amd_defs.h"
#include <intrin.h>

#pragma section(".rhook", read, execute)
#pragma comment(linker, "/SECTION:.rhook,ER,ALIGN=4096")

#define HOOK_MATCH_LENGTH 20

// Hook 处理函数必须放在可执行段中
#pragma code_seg(".rhook$001")
NTSTATUS __stdcall Hook_NtOpenProcess(PHOOK_REGS Regs)
{
    ACCESS_MASK desiredAccess = (ACCESS_MASK)Regs->Rdx;

    if (desiredAccess & PROCESS_TERMINATE)
    {
        return STATUS_ACCESS_DENIED;
    }

    return STATUS_SUCCESS;
}
#pragma code_seg()

static PHOOK_INFO g_NtOpenProcessHookInfo = NULL;
static PHOOK_FUNC_INFO g_NtOpenProcessFuncInfo = NULL;
static PVOID g_NtOpenProcessOriginal = NULL;

NTSTATUS InstallNtOpenProcessHook()
{
    UNICODE_STRING funcName = RTL_CONSTANT_STRING(L"NtOpenProcess");
    g_NtOpenProcessOriginal = MmGetSystemRoutineAddress(&funcName);
    if (!g_NtOpenProcessOriginal)
    {
        DbgPrintEx(77, 0, "[-] Failed to resolve NtOpenProcess.\n");
        return STATUS_NOT_FOUND;
    }

    // 1. 创建 Hook 信息节点
    g_NtOpenProcessHookInfo = SvmAddHookInfo(NULL, (UINT64)g_NtOpenProcessOriginal, PAGE_SIZE * 2);
    if (!g_NtOpenProcessHookInfo)
    {
        DbgPrintEx(77, 0, "[-] SvmAddHookInfo failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 2. 创建 Shadow Page 0（原始页面副本）
    if (!SvmCreateShadowPage(g_NtOpenProcessHookInfo, 0))
    {
        DbgPrintEx(77, 0, "[-] SvmCreateShadowPage(0) failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 3. 创建 Shadow Page 1（Hook 页面副本）
    if (!SvmCreateShadowPage(g_NtOpenProcessHookInfo, 1))
    {
        DbgPrintEx(77, 0, "[-] SvmCreateShadowPage(1) failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 4. 添加函数 Hook 记录
    g_NtOpenProcessFuncInfo = SvmAddHookFuncInfo(
        g_NtOpenProcessHookInfo,
        (UINT64)g_NtOpenProcessOriginal,
        (UINT64)Hook_NtOpenProcess,
        HOOK_MATCH_LENGTH
    );
    if (!g_NtOpenProcessFuncInfo)
    {
        DbgPrintEx(77, 0, "[-] SvmAddHookFuncInfo failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 5. 分配蹦床内存
    PJMP_FUNC_TRAMPOLINE trampoline = (PJMP_FUNC_TRAMPOLINE)SvmAllocateJmpTrampoline(
        g_NtOpenProcessFuncInfo,
        (SIZE_T)SvmGetJmpCodeFuncBufferLength()
    );
    if (!trampoline)
    {
        DbgPrintEx(77, 0, "[-] SvmAllocateJmpTrampoline failed.\n");
        return STATUS_UNSUCCESSFUL;
    }

    // 6. 填充蹦床结构体
    //    复制蹦床代码模板
    SvmGetJmpCodeFuncBuffer(trampoline,(SIZE_T)SvmGetJmpCodeFuncBufferLength());

    //    计算原函数序言长度
    UINT8 originalCodeLen = GetInstructionLength(g_NtOpenProcessOriginal, HOOK_MATCH_LENGTH);
    if (!originalCodeLen || originalCodeLen > sizeof(trampoline->Execute.OriginalCode))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    //    设置蹦床偏移
    g_NtOpenProcessFuncInfo->JumpTrampolineOffset = sizeof(trampoline->Data);

    //    设置三个关键地址
    DEF_PTR(UINT64, trampoline->Data.HookFuncAddress, 0) = (UINT64)Hook_NtOpenProcess;
    DEF_PTR(UINT64, trampoline->Data.CallbackAddress, 0) = (UINT64)g_NtOpenProcessOriginal + originalCodeLen;
    DEF_PTR(UINT64, trampoline->Data.ReturnAddress, 0) = (UINT64)trampoline + SvmGetReturnOffset();

    //    保存原函数序言到蹦床
    memcpy(trampoline->Execute.OriginalCode, g_NtOpenProcessOriginal, originalCodeLen);

    // 7. 在 Shadow Page 1 中用 INT3 替换原函数序言
    UINT8 jmpCode[256] = { 0 };
    memset(jmpCode, 0x90, sizeof(jmpCode));
    jmpCode[0] = INT_3;  // INT3 断点指令
    SvmShadowCopyMemory(g_NtOpenProcessHookInfo, 1, (UINT64)g_NtOpenProcessOriginal, jmpCode, originalCodeLen);

    // 8. 对所有核心设置 Shadow Page 0 为执行页面
    for (UINT32 i = 0; i < CpuCount; i++)
    {
        SvmSetGuestShadowPage(SvmGetCpuContextIndex(i), g_NtOpenProcessHookInfo, 0, TRUE, FALSE);
    }

    DbgPrintEx(77, 0, "[+] NtOpenProcess hook installed successfully.\n");
    return STATUS_SUCCESS;
}
```

## 🔄 Hook 流程详解

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
   ├── 跳转到 HookFuncAddress（Hook 处理函数）
   │       └── Hook 函数可通过返回值控制行为
   ├── 执行原函数序言（OriginalCode）
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

## 🛡️ 数据页面保护

使用 `DataPage = TRUE` 的 Hook 信息可保护数据段不被 Guest 修改：

```c
// 方式一：使用便捷 API
SvmProtectDriverSection((UINT64)&g_MyVariable, PAGE_SIZE, TRUE, NULL, 0);

// 方式二：手动创建 Hook 信息并设置 DataPage
PHOOK_INFO dataProtect = SvmAddHookInfo("MyData", (UINT64)&g_MyVariable, PAGE_SIZE);
dataProtect->DataPage = TRUE;  // 标记为数据页面保护

for (UINT32 i = 0; i < CpuCount; i++)
{
    SvmSetGuestShadowPage(SvmGetCpuContextIndex(i), dataProtect, NO_SHADOW_PAGE, TRUE, FALSE);
}
```

## 🛠️ 构建要求

- Visual Studio 2026，项目文件当前记录的工具链版本为 14.51。
- WDK 28000.1761，目标平台为 Windows x64。
- 仅支持具备 AMD SVM/AMD-V 能力的 AMD 处理器；项目虽然保留 ARM64 配置项，但源码使用 AMD SVM 和 x64 汇编，不应将 ARM64 视为受支持目标。
- 需要测试签名、内核调试器或其他合法的驱动加载环境。

解决方案包含 `SVMHypervisor`、`TestDriver` 和 `Ring3_Test`。构建 `SVMHypervisor` 后会生成供内核驱动链接的 `SVMHypervisor.lib`。

`TestDriver` 导入该 LIB 时必须手动指定路径：打开 **TestDriver 属性 → 链接器 → 输入 → 附加依赖项**，将 `SVMHypervisor.lib` 设置为当前机器上的实际文件路径；如只填写文件名，还需在 **链接器 → 常规 → 附加库目录** 中加入 LIB 所在目录。请分别检查 Debug/Release 和 x64 配置，删除或替换项目中原有的绝对路径，否则项目移动到其他目录或机器后会链接失败。

第三方驱动还需要包含与当前 Hypervisor 版本匹配的 `test.h` 和 `amd_defs.h`。头文件、导入库与 `SVMHypervisor.sys` 必须来自同一次构建，避免导出结构或函数签名不一致。

## 🚀 启动顺序

1. 构建并加载 `SVMHypervisor.sys`。
2. 构建并加载 `TestDriver.sys`；它创建 `\\.\SVMTest` 设备，并启动测试线程。
3. 通过 `Ring3_Test` 或其他控制程序向 `SVMTest` 发送 `IOCTL_VM_START`，使 `g_VmStart` 变为 `TRUE`。
4. TestDriver 的测试线程安装 `NtOpenProcess` 和 `TestFunc` Hook，并执行页面保护与调用测试。

启动前应确认目标 CPU 支持 SVM。不要在未连接内核调试器、未准备恢复方式的机器上测试。

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
    InstallNtOpenProcessHook();

    DriverObject->DriverUnload = UnloadDriver;
    return STATUS_SUCCESS;
}
```

## ♻️ 生命周期与清理

Hook 创建后会被加入全局 Hook 列表。移除函数 Hook 的相关 API 只能在 Hypervisor 进入 Guest 状态之前调用；一旦进入 Guest 状态，禁止再调用 `SvmRemoveHookFuncInfo` 或相关移除、释放接口。

**默认不建议开启驱动卸载。** 常规测试中不要将 `g_Unload` 设为 `TRUE`，也不要在 Hypervisor 或 Hook 仍处于活动状态时卸载驱动。

当前 `TestDriver` 的 `UnloadDriver` 只删除设备和符号链接，没有移除已安装的 Hook，也没有释放其 MDL 和映射资源。只有在自行实现并验证完整的停机流程后，才应考虑允许卸载：停止 VM、撤销每个核心的映射、确认目标代码不会继续执行，并释放 Hook、蹦床、MDL 和页面资源。否则卸载可能留下指向已释放驱动代码或数据的引用，导致系统崩溃。

## ⚠️ 注意事项与限制

- Hook 处理函数必须位于 Guest 可执行且 Hypervisor 保护的代码段中；TestDriver 使用 `.hook` 段并通过 `SvmProtectDriverSection` 保护。
- `SvmAllocateJmpTrampoline` 分配非分页可执行内存，并由 NPT 设置为 Guest 可执行但不可写。
- `GetInstructionLength` 的结果必须是完整指令边界，且不得超过蹦床 `OriginalCode` 缓冲区；不能直接假设固定字节数适用于所有目标函数。
- `SvmSetGuestShadowPage` 的映射只对传入的 CPU 上下文生效。批量设置时必须遍历所有活动 CPU，并确保目标核心不会并发执行正在修改的映射。
- `SvmAddVmexitCallback` 的回调运行在 VM-Exit 路径中，不能调用可能阻塞的内核 API。
- Hypervisor 一旦进入 Guest 状态，不得再调用移除 Hook 或释放相关影子页的 API。
- 默认保持驱动不可卸载；不要仅通过设置 `g_Unload = TRUE` 允许卸载，除非所有虚拟化状态和关联资源都已可靠清理。
- 该项目当前是研究和演示代码，不是可直接用于生产环境的通用 Hook 框架。

## 🔒 安全说明

`TestDriver` 暴露的测试 IOCTL 包括内核内存读写、MSR 读写、进程终止和 SVM 控制。它们没有提供生产级访问控制，加载该驱动等同于向具有设备访问权限的调用方授予高权限内核测试能力。

请仅在隔离虚拟机或专用测试机中使用，并在测试结束后停止虚拟化、移除 Hook、卸载测试驱动和 Hypervisor。
