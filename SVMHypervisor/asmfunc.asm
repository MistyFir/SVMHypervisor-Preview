.code
extern VmExitHandler : proc
extern g_bSvmRunning : byte
extern g_CpuContexts : qword
extern g_SuspendGuest : byte
PUSH_ALL_XMM macro
    sub rsp, 100h
    movups xmmword ptr [rsp + 0F0h], xmm15
    movups xmmword ptr [rsp + 0E0h], xmm14
    movups xmmword ptr [rsp + 0D0h], xmm13
    movups xmmword ptr [rsp + 0C0h], xmm12
    movups xmmword ptr[rsp + 0B0h], xmm11
    movups xmmword ptr[rsp + 0A0h], xmm10
    movups xmmword ptr [rsp + 090h], xmm9
    movups xmmword ptr [rsp + 080h], xmm8
    movups xmmword ptr [rsp + 070h], xmm7
    movups xmmword ptr [rsp + 060h], xmm6
    movups xmmword ptr[rsp + 050h], xmm5
    movups xmmword ptr[rsp + 040h], xmm4
    movups xmmword ptr [rsp + 030h], xmm3
    movups xmmword ptr [rsp + 020h], xmm2
    movups xmmword ptr [rsp + 010h], xmm1
    movups xmmword ptr [rsp + 000h], xmm0
endm

POP_ALL_XMM macro
    movups xmm0, xmmword ptr [rsp + 000h]
    movups xmm1, xmmword ptr [rsp + 010h]
    movups xmm2, xmmword ptr [rsp + 020h]
    movups xmm3, xmmword ptr [rsp + 030h]
    movups xmm4, xmmword ptr [rsp + 040h]
    movups xmm5, xmmword ptr[rsp + 050h]
    movups xmm6, xmmword ptr[rsp + 060h]
    movups xmm7, xmmword ptr[rsp + 070h]
    movups xmm8, xmmword ptr [rsp + 080h]
    movups xmm9, xmmword ptr [rsp + 090h]
    movups xmm10, xmmword ptr [rsp + 0A0h]
    movups xmm11, xmmword ptr [rsp + 0B0h]
    movups xmm12, xmmword ptr [rsp + 0C0h]
    movups xmm13, xmmword ptr [rsp + 0D0h]
    movups xmm14, xmmword ptr [rsp + 0E0h]
    movups xmm15, xmmword ptr[rsp + 0F0h]
    add rsp, 100h
endm
asm_lar proc
	xor rax,rax
	lar rax,rcx
    shr rax, 8
	jnz @fail
	ret
@fail:
	xor rax,rax
	ret
asm_lar endp
__sgdt proc
	sgdt [rcx]
	ret
__sgdt endp
AsmGetCs proc
	mov ax, cs
	ret
AsmGetCs endp

AsmGetDs proc
	mov ax, ds
	ret
AsmGetDs endp

AsmGetEs proc
	mov ax, es
	ret
AsmGetEs endp

AsmGetSs proc
	mov ax, ss
	ret
AsmGetSs endp

; UINT64 AsmGetFs(void)
AsmGetFs proc
	mov ax, fs
	ret
AsmGetFs endp

; UINT64 AsmGetGs(void)
AsmGetGs proc
	mov ax, gs
	ret
AsmGetGs endp

; UINT64 AsmGetTr(void)
AsmGetTr proc
	str ax
	ret
AsmGetTr endp

; VOID AsmLaunchVm(PCPU_CONTEXT context)
; RCX=context
_VMASM SEGMENT ALIGN(4096) READ EXECUTE ALIAS('.vmasm')
VMASM_START::
AsmLaunchVm proc
    mov [rcx+0A8h], rsp
    mov rsp, [rcx+90h]
    add rsp, 1FFF8h
    mov rax, [rsp]
    mov rax, [rax+8]
@vm_loop:
    vmload rax
    vmrun rax
    clgi
    vmsave rax
    mov rax, [rsp]
    mov rax, [rax+35F8h]
    cmp rax, 0
    je @vm_cleanup
    mov rax, [rsp]
    mov rax, [rax+8h]
    sub rsp, 100h
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    sub rsp, 8
    push rbx
    push rdx
    push rcx
    push rax
    mov rdx, rsp
    mov rcx, [rsp+10h*8h+100h]
    PUSH_ALL_XMM
    sub rsp, 28h
    call VmExitHandler
    add rsp, 28h
    POP_ALL_XMM
    pop rax
    pop rcx
    pop rdx
    pop rbx
    add rsp, 8
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 100h
    jmp @vm_loop
@vm_cleanup:
    ;   rax = context
    mov rax, [rsp]
    ;   rsp = context->GuestVmcb.VirtualAddress
    mov rsp, [rax]
    ;   rsp = GuestVmcb->StateSaveArea.Rsp
    mov rsp, [rsp+5D8h]
    ;   rax = context->GuestVmcb.VirtualAddress
    mov rax, [rax]
    ;   rax = GuestVmcb->ControlArea.NRip
    mov rax, [rax+0C8h]
    jmp rax
AsmLaunchVm endp

__readdr6 proc
    mov rax,dr6
    ret
__readdr6 endp
__readdr7 proc
    mov rax,dr7
    ret
__readdr7 endp
__readrsp proc
    mov rax,rsp
    ret
__readrsp endp
__jump proc
    jmp rcx
__jump endp
__writerax proc
mov rax, rcx
ret
__writerax endp

__readrax proc
mov qword ptr [rcx], 0
mov qword ptr [rcx], rax
ret
__readrax endp
__svm_vmmcall proc
    xor rax, rax
    mov rax, rcx
    vmmcall
    ret
__svm_vmmcall endp

asm_add proc
	xor rax,rax
	mov rax,rcx
	add rax,rdx
	ret
asm_add endp
JMP_CODE_START::
    jmp qword ptr [JMP_CODE_DATA]
JMP_CODE_DATA::
dq 0FFFFFFFFFFFFFFFFh
JMP_CODE_END::
JMP_DATA::
dq 0FFFFFFFFFFFFFFFFh
dq 0FFFFFFFFFFFFFFFFh
dq 0FFFFFFFFFFFFFFFFh
JMP_FUNC_START::
    nop
    ;   Hook Func
    sub rsp, 108h
    mov [rsp+100h], rax
    mov [rsp+38h], rcx
    mov [rsp+38h+8h], rdx
    mov [rsp+38h+10h], r8
    mov [rsp+38h+18h], r9
    mov rax,[rsp+108h+20h+8h]
    mov [rsp+38h+20h], rax
    mov rax,[rsp+108h+28h+8h]
    mov [rsp+38h+28h], rax
    mov rax, [rsp+108h+30h+8h]
    mov [rsp+38h+30h], rax
    lea rcx, [rsp+38h]
    lea rax, [@hook_next]
    push rax
    mov rax, [rsp+108h]
    jmp qword ptr [JMP_DATA]
@hook_next:
    mov rcx, [rsp+38h+20h]
    mov [rsp+108h+20h+8h], rcx
    mov rcx, [rsp+38h+28h]
    mov [rsp+108h+28h+8h], rcx
    mov rcx, [rsp+38h+30h]
    mov [rsp+108h+30h+8h], rcx
    mov rcx, [rsp+38h]
    mov rdx, [rsp+38h+8h]
    mov r8, [rsp+38h+10h]
    mov r9, [rsp+38h+18h]
    cmp eax, 0C0000022h
    je @hook_block
    mov rax, [rsp+100h]
    add rsp, 108h
    ;   Original Func
    sub rsp, 78h
    mov [rsp+70h], rax
    mov [rsp], rcx
    mov [rsp+8h], rdx
    mov [rsp+10h], r8
    mov [rsp+18h], r9
    mov rax,[rsp+78h+20h+8h]
    mov [rsp+20h], rax
    mov rax,[rsp+78h+28h+8h]
    mov [rsp+28h], rax
    mov rax, [rsp+78h+30h+8h]
    mov [rsp+30h], rax
    mov rax, [rsp+70h]
    push qword ptr [JMP_DATA+10h]
    ;   Original Code Start
    db 90 dup(90h)
    ;   Original Code End
    jmp qword ptr [JMP_DATA+8]
Next::
    add rsp, 78h
    jmp @hook_complete
@hook_block:
    add rsp, 108h
@hook_complete:
    push rax
    mov rax, 5E3Bh
    vmmcall
    pop rax
    nop
    ret
JMP_FUNC_END::

AsmGetJmpCodeFuncBase proc
    lea rax, [JMP_DATA]
    ret
AsmGetJmpCodeFuncBase endp

AsmGetJmpCodeFuncLength proc
    push rcx
    lea rcx, [JMP_DATA]
    lea rax, [JMP_FUNC_END]
    sub rax, rcx
    pop rcx
    ret
AsmGetJmpCodeFuncLength endp

AsmGetJmpCodeBase proc
    lea rax, [JMP_CODE_START]
    ret
AsmGetJmpCodeBase endp

AsmGetJmpCodeLength proc
    push rcx
    lea rcx, [JMP_CODE_START]
    lea rax, [JMP_CODE_END]
    sub rax, rcx
    pop rcx
    ret
AsmGetJmpCodeLength endp

AsmGetReturnOffset proc
    xor rax, rax
    push rcx
    lea rcx, [JMP_DATA]
    lea rax, [Next]
    sub rax, rcx
    pop rcx
    ret
AsmGetReturnOffset endp

AsmResetShadow proc
    push rax
    xor rax, rax
    mov rax, 5E3Dh
    vmmcall
    pop rax
    ret
AsmResetShadow endp

AsmGetVmAsmStartAddress proc
lea rax, [VMASM_START]
ret
AsmGetVmAsmStartAddress endp

AsmGetVmAsmEndAddress proc
lea rax, [VMASM_END]
ret
AsmGetVmAsmEndAddress endp
VMASM_END::
_VMASM ENDS
end