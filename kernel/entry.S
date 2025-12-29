.section .text.entry
.global _start
.global boot_stack_top
.global boot_stack
.global user_stack_top
.global user_stack

_start:
    /* 设置栈指针（分配16KB栈） */
    la sp, boot_stack_top
    
    /* 跳转到C语言主函数 */
    call main
    
    /* 如果main返回，则死循环 */
halt:
    wfi
    j halt

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
