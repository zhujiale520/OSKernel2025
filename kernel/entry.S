.section .text.entry
.global _start
.global boot_stack_top
.global boot_stack
.global user_stack_top
.global user_stack

_start:
    /* 只允许第一个进入的 hart 执行 main，其余 hart 进入待机 */
    la   t0, boot_claimed
    li   t1, 1
    amoswap.w t2, t1, (t0)   /* t2=旧值，0 表示本 hart 获得执行权 */
    bnez t2, secondary_hart

    /* 设置栈指针（分配16KB栈） */
    la sp, boot_stack_top
    
    /* 跳转到C语言主函数 */
    call main
    
    /* 如果main返回，则死循环 */
halt:
    wfi
    j halt

secondary_hart:
    /* 其他 hart 简单休眠 */
    wfi
    j secondary_hart

/* 内核栈空间 */
.section .bss.stack
.align 12
boot_stack:
    .space 16384  /* 16KB */
boot_stack_top:

.section .bss.bootflag
.align 3
boot_claimed:
    .dword 0

/* 用户态栈空间 */
.section .bss.ustack
.align 12
user_stack:
    .space 16384
user_stack_top:
