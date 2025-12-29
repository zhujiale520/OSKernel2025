#include <stdint.h>
#include <stddef.h>

void print(const char *str);
void print_hex(uint64_t num);

int virtio_blk_init(void);
int fat32_init(void);
int fat32_list_root(void);
int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);
int fat32_find_file_keyword(const char *keyword, char *out_name, size_t out_len, uint32_t *out_size);
int fat32_get_file_at_index(int index, char *out_name, size_t out_len, uint32_t *out_size);
int load_elf_from_fat32(const char *name, uint64_t *entry_out);
uint64_t syscall_handle(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
extern volatile uint64_t syscall_exit_code;

#include "proc.h"

extern uint8_t user_stack_top[];
extern uint8_t user_stack[];
uint64_t user_stack_ptr = 0;

static void *memcpy_simple(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static uint64_t setup_user_stack(const char *prog_name)
{
    uintptr_t sp = (uintptr_t)user_stack_top;
    /* 放置程序名字符串 */
    size_t len = 0; while (prog_name[len]) len++;
    sp -= (len + 1);
    uint64_t arg0_ptr = sp;
    memcpy_simple((void *)sp, prog_name, len + 1);
    /* 对齐 */
    sp &= ~((uintptr_t)0xF);
    /* argc/argv 布局：sp[0]=argc, sp[1]=argv0, sp[2]=NULL */
    sp -= 8 * 3;
    uint64_t *p = (uint64_t *)sp;
    p[0] = 1;
    p[1] = arg0_ptr;
    p[2] = 0;
    return sp;
}

/* 通过SBI关机 */
void shutdown()
{
    // SBI system reset 扩展：EID=0x53525354, FID=0（关机），a0=reset_type=0，a1=reason=0
    asm volatile(
        "li a0, 0\n\t"          // reset_type: shutdown
        "li a1, 0\n\t"          // reset_reason: none
        "li a6, 0\n\t"          // FID: system reset
        "li a7, 0x53525354\n\t" // EID: system reset
        "ecall"
    );
}

extern void trap_entry(void);

static int test_index = 0;

void run_next_test(void)
{
    char target_name[32];
    uint32_t file_size = 0;

    while (1) {
        if (fat32_get_file_at_index(test_index, target_name, sizeof(target_name), &file_size) != 0) {
            print("All selected tests finished. Shutdown.\n");
            shutdown();
            while (1) {}
        }
        test_index++;

        print("Running test: "); print(target_name); print("\n");

        uint64_t entry = 0;
        if (load_elf_from_fat32(target_name, &entry) != 0) {
            print("load failed\n");
            continue;
        }
        struct proc *p = proc_alloc(0);
        if (!p) { print("no proc slot\n"); break; }
        for (int r = 0; r < 32; r++) p->tf.regs[r] = 0;
        p->tf.sepc = entry;
        p->tf.regs[2] = setup_user_stack(target_name); /* sp */
        uint64_t sstatus = 0;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        sstatus &= ~(1ULL << 8); /* SPP=0 (User) */
        sstatus |= (1ULL << 5);  /* SPIE=1 (Enable interrupts after sret) */
        p->tf.sstatus = sstatus;
        /* stdout fd 1 preset */
        p->fds[0] = 0; p->fds[1] = 1; p->fds[2] = 2;
        p->state = P_RUNNABLE;
        current = p;
        current_trapframe = &p->tf;
        schedule();
    }
}

void main()
{
    /* 设置 stvec 到 trap_entry */
    asm volatile("csrw stvec, %0" :: "r"((uint64_t)trap_entry));

    // 1. 输出启动信息
    print("\n================================\n");
    print("RISC-V OS Kernel Boot Success!\n");
    print("Team: 404notfound\n");
    print("School: Xi'an Jiaotong University\n");
    print("Kernel is now running in QEMU...\n");
    print("================================\n\n");
    
    // 2. 初始化 virtio-blk 与 FAT32，查找并运行包含 WRITE 关键字的测试文件
    if (virtio_blk_init() != 0) {
        print("virtio-blk init failed\n");
        shutdown();
    }
    if (fat32_init() != 0) {
        print("FAT32 init failed\n");
        shutdown();
    }

    proc_init();
    run_next_test();
}

/* 当 syscall_action 请求回到内核调度时由 trap.S 调用 */
void user_return(void)
{
    schedule();
}
