ENTRY(_start)

SECTIONS
{
    . = 0x80200000;  /* QEMU virt机器内核加载地址 */
    
    .text : {
        *(.text.entry)
        *(.text .text.*)
    }
    
    .rodata : {
        *(.rodata .rodata.*)
    }
    
    .data : {
        *(.data .data.*)
    }
    
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /DISCARD/ : {
        *(.eh_frame)
        *(.note.*)
    }
}
