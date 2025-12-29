
.section .text
.extern syscall_handle
.extern print
.extern print_hex
.extern boot_stack_top
.extern user_stack_top
.extern syscall_action
.extern user_return

.global trap_entry
.global enter_user

trap_entry:
    /* 切换到内核栈：sp <-> mscratch */
    csrrw sp, mscratch, sp

    csrr t0, mcause
    li t1, 8                  # user-mode ecall
    beq t0, t1, trap_syscall
    li t1, 9                  # supervisor-mode ecall (未用)
    beq t0, t1, trap_syscall
    li t1, 11                 # machine-mode ecall
    beq t0, t1, trap_syscall
    j trap_other

trap_syscall:
    # advance mepc to skip ecall
    csrr t2, mepc
    addi t2, t2, 4
    csrw mepc, t2

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

    /* 检查是否需要直接回到内核（exit 等） */
    la t3, syscall_action
    ld t4, 0(t3)
    beqz t4, trap_sys_ret_user

    /* 清除动作标志后直接跳转到 user_return */
    sd zero, 0(t3)
    la t5, user_return
    jr t5

trap_sys_ret_user:
    /* 恢复用户栈返回用户态 */
    csrrw sp, mscratch, sp
    mret

trap_other:
    # unknown trap: print mcause and halt
    la a0, msg_trap
    call print
    mv a0, t0
    call print_hex
1:  j 1b

/* 进入用户态运行 ELF，参数 a0=entry */
enter_user:
    /* 设置 trap 向量 */
    la t0, trap_entry
    csrw mtvec, t0

    /* 内核栈保存到 mscratch，trap 时可切换 */
    la t1, boot_stack_top
    csrw mscratch, t1

    /* 配置 mepc = entry */
    csrw mepc, a0

    /* 切换到用户栈 */
    la sp, user_stack_top

    /* mstatus.MPP = U */
    csrr t2, mstatus
    li t3, ~(3 << 11)
    and t2, t2, t3
    li t3, (0 << 11)        # U-mode
    or t2, t2, t3
    csrw mstatus, t2

    mret

.section .rodata
msg_trap:
    .asciz "trap mcause="
