
.section .text
.extern syscall_handle
.extern print
.extern print_hex
.extern boot_stack_top
.extern current_trapframe
.extern syscall_action
.extern user_return

.global trap_entry
.global enter_user

trap_entry:
    /* 切换到内核栈：sp <-> sscratch */
    csrrw sp, sscratch, sp

    /* 保存 t6 到内核栈，以便使用 t6 */
    addi sp, sp, -8
    sd t6, 0(sp)

    /* 获取 trapframe 指针 */
    la t6, current_trapframe
    ld t6, 0(t6)              # t6 = trapframe*
    
    sd x0,   0(t6)
    sd ra,   8(t6)
    sd gp,  24(t6)
    sd tp,  32(t6)
    sd t0,  40(t6)
    sd t1,  48(t6)
    sd t2,  56(t6)
    sd s0,  64(t6)
    sd s1,  72(t6)
    sd a0,  80(t6)
    sd a1,  88(t6)
    sd a2,  96(t6)
    sd a3, 104(t6)
    sd a4, 112(t6)
    sd a5, 120(t6)
    sd a6, 128(t6)
    sd a7, 136(t6)
    sd s2, 144(t6)
    sd s3, 152(t6)
    sd s4, 160(t6)
    sd s5, 168(t6)
    sd s6, 176(t6)
    sd s7, 184(t6)
    sd s8, 192(t6)
    sd s9, 200(t6)
    sd s10,208(t6)
    sd s11,216(t6)
    sd t3, 224(t6)
    sd t4, 232(t6)
    sd t5, 240(t6)
    
    /* 恢复用户 t6 并保存到 trapframe */
    ld t5, 0(sp)
    sd t5, 248(t6)
    addi sp, sp, 8
    
    /* 保存用户 sp (在 sscratch 中) */
    csrr t5, sscratch
    sd t5, 16(t6)

    /* 恢复 sscratch 为内核栈指针 */
    csrw sscratch, sp

    csrr t0, sepc
    sd t0, 256(t6)            # save sepc
    csrr t1, sstatus
    sd t1, 264(t6)

    csrr t0, scause
    li t1, 8                  # user-mode ecall
    beq t0, t1, trap_syscall
    # li t1, 9                  # supervisor-mode ecall (未用)
    # beq t0, t1, trap_syscall
    # li t1, 11                 # machine-mode ecall
    # beq t0, t1, trap_syscall
    j trap_other

trap_syscall:
    # advance sepc in trapframe to skip ecall
    ld t2, 256(t6)
    addi t2, t2, 4
    sd t2, 256(t6)

    # syscall: pass a0-a7 to C handler, return in a0
    mv a0, a0
    mv a1, a1
    mv a2, a2
    mv a3, a3
    mv a4, a4
    mv a5, a5
    mv a6, a6
    mv a7, a7
    call syscall_handle

    /* 将返回值写回 trapframe 的 a0 */
    la t6, current_trapframe
    ld t6, 0(t6)
    sd a0, 80(t6)

    /* 检查是否需要切回内核调度（exit 等） */
    la t3, syscall_action
    ld t4, 0(t3)
    beqz t4, trap_sys_ret_user
    sd zero, 0(t3)
    
    /* 切换到 boot_stack 以便安全地重新调度和重用进程槽 */
    la sp, boot_stack_top
    
    la t5, user_return
    jalr t5

trap_sys_ret_user:
    /* 恢复寄存器并返回用户态 */
    j user_restore

trap_other:
    # unknown trap: print scause and halt
    la a0, msg_trap
    call print
    csrr a0, scause
    call print_hex
    la a0, msg_sepc
    call print
    csrr a0, sepc
    call print_hex
    la a0, msg_nl
    call print
halt_loop:
    j halt_loop

/* 从 trapframe 恢复寄存器并 sret 到用户态 */
.global user_restore
user_restore:
    la t6, current_trapframe
    ld t6, 0(t6)

    /* Restore CSRs first using t0, t1 as scratch */
    ld t0, 256(t6)          # sepc
    csrw sepc, t0
    ld t1, 264(t6)          # sstatus
    csrw sstatus, t1

    ld ra,   8(t6)
    ld sp,  16(t6)
    ld gp,  24(t6)
    ld tp,  32(t6)
    ld t0,  40(t6)
    ld t1,  48(t6)
    ld t2,  56(t6)
    ld s0,  64(t6)
    ld s1,  72(t6)
    ld a0,  80(t6)
    ld a1,  88(t6)
    ld a2,  96(t6)
    ld a3, 104(t6)
    ld a4, 112(t6)
    ld a5, 120(t6)
    ld a6, 128(t6)
    ld a7, 136(t6)
    ld s2, 144(t6)
    ld s3, 152(t6)
    ld s4, 160(t6)
    ld s5, 168(t6)
    ld s6, 176(t6)
    ld s7, 184(t6)
    ld s8, 192(t6)
    ld s9, 200(t6)
    ld s10,208(t6)
    ld s11,216(t6)
    ld t3, 224(t6)
    ld t4, 232(t6)
    ld t5, 240(t6)
    
    /* Restore t6 last */
    ld t6, 248(t6)
    
    sret

.section .rodata
msg_trap:
    .asciz "trap scause="
msg_sepc:
    .asciz " sepc="
msg_nl:
    .asciz "\n"
