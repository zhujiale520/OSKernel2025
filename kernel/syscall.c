#include <stdint.h>

void print(const char *s);
void print_hex(uint64_t v);
void shutdown();

static void write_stdout(const char *p, uint64_t len)
{
    for (uint64_t i = 0; i < len; i++) {
        char c = p[i];
        if (c == '\0') break;
        print((const char *)&c);
    }
}

uint64_t syscall_handle(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    switch (a7) {
    case 93: /* exit */
        print("sys_exit code="); print_hex(a0); print("\n");
        shutdown();
        break;
    case 64: /* write */
        if (a0 == 1) { /* stdout */
            write_stdout((const char *)a1, a2);
            return a2;
        }
        return (uint64_t)-1;
    default:
        print("syscall unknown: "); print_hex(a7); print("\n");
        return (uint64_t)-1;
    }
    return 0;
}
