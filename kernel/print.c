#include <stdint.h>

/* QEMU virt机器的串口地址 */
#define UART0 0x10000000L

/* 输出单个字符到串口 */
void uart_putc(char ch)
{
    volatile uint8_t *uart = (uint8_t *)UART0;
    *uart = ch;
}

/* 输出字符串 */
void print(const char *str)
{
    while (*str) {
        uart_putc(*str++);
    }
}

/* 十六进制数字输出（调试用） */
void print_hex(uint64_t num)
{
    const char *hex = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex[(num >> i) & 0xF]);
    }
}

/* 十进制数字输出 */
void print_dec(uint64_t num)
{
    if (num == 0) {
        uart_putc('0');
        return;
    }
    
    char buffer[20];
    int i = 0;
    
    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        uart_putc(buffer[--i]);
    }
}
