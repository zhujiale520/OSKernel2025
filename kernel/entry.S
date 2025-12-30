.section .text.entry
.global _start
.global boot_stack_top
.global boot_stack
.global user_stack_top
.global user_stack

_start:
    /* 只允许 hart0 继续，其他 hart 进入低功耗等待；a0 已由 OpenSBI 传入 hartid */
    bnez a0, secondary_hart

    /* 设置栈指针（分配16KB栈） */
    la sp, boot_stack_top
    
    /* 跳转到C语言主函数 */
    call main
    
    /* 如果main返回，则死循环 */
halt:
    wfi
    j halt

secondary_hart:
    /* 停在低功耗循环，避免二核重入内核初始化 */
    wfi
    j secondary_hart

/* 内核栈空间 */
.section .bss.stack
.align 12
boot_stack:
    .space 16384  /* 16KB */
boot_stack_top:

/* 用户态栈空间 */
.section .bss.ustack
.align 12
user_stack:
    .space 16384
user_stack_top:
