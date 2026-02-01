#include <stdint.h>
#include <stddef.h>

/* Paging definitions */
#define SATP_MODE_SV39 (8ULL << 60)
#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)

/* Page tables - aligned to 4KB */
uint64_t root_pt[512] __attribute__((aligned(4096)));
uint64_t l1_pt_user[512] __attribute__((aligned(4096)));

/* User stack physical memory */
uint8_t user_stack_real[16384] __attribute__((aligned(4096)));
#define USER_STACK_VA 0xF0000000UL

void setup_paging(void)
{
    /* 1. Identity map Kernel (0x80000000 - 0xC0000000) using 1GB huge page */
    /* VPN[2] for 0x80000000 is 2 */
    /* PPN for 0x80000000 is 0x80000 */
    /* Attributes: RWX, Global, Valid, Accessed, Dirty */
    root_pt[2] = (0x80000ULL << 10) | PTE_R | PTE_W | PTE_X | PTE_G | PTE_V | PTE_A | PTE_D;

    /* 2. Map User Space [0, 1GB) using Level 1 PT */
    /* VPN[2] for 0x0 is 0. Point to l1_pt_user */
    root_pt[0] = ((((uint64_t)l1_pt_user) >> 12) << 10) | PTE_V;

    /* 3. Map User Code/Data [0, 128MB) -> [0x80400000, ...) using 2MB huge pages */
    /* 0x80400000 PPN is 0x80400 */
    /* We map 64 entries (64 * 2MB = 128MB) */
    uint64_t ppn_start = 0x80400;
    for (int i = 0; i < 64; i++) {
        l1_pt_user[i] = ((ppn_start + i * 512) << 10) | PTE_R | PTE_W | PTE_X | PTE_U | PTE_V | PTE_A | PTE_D;
    }

    /* 4. Map MMIO for Kernel (UART, VirtIO, PLIC) */
    /* UART: 0x10000000 (256MB) -> Entry 128 */
    /* VirtIO: 0x10001000 (256MB) -> Entry 128 */
    /* PLIC: 0x0C000000 (192MB) -> Entry 96 */
    /* CLINT: 0x02000000 (32MB) -> Entry 16. CONFLICT with User Space! */
    
    /* Map UART/VirtIO (Entry 128) */
    /* PPN for 0x10000000 is 0x10000 */
    l1_pt_user[128] = (0x10000ULL << 10) | PTE_R | PTE_W | PTE_G | PTE_V | PTE_A | PTE_D; /* No PTE_U */
    
    /* Map PLIC (Entry 96) */
    /* PPN for 0x0C000000 is 0x0C000 */
    l1_pt_user[96] = (0x0C000ULL << 10) | PTE_R | PTE_W | PTE_G | PTE_V | PTE_A | PTE_D; /* No PTE_U */
    
    /* Map CLINT (Entry 16) - This overwrites User Space 32MB-34MB! */
    /* If user program uses this area, it will crash. But Kernel needs it for timer? */
    /* Actually, S-mode kernel uses 'time' CSR, not CLINT directly. */
    /* So we might skip CLINT mapping if we don't access it. */
    /* But let's map it just in case, assuming user program is small (<32MB). */
    /* PPN for 0x02000000 is 0x02000 */
    /* l1_pt_user[16] = (0x02000ULL << 10) | PTE_R | PTE_W | PTE_G | PTE_V | PTE_A | PTE_D; */
    
    /* 5. Map User Stack at USER_STACK_VA (0xF0000000) */
    /* VPN[2] = 0x3C0 (0xF0000000 >> 30) */
    /* We need a L1 PT for this? 0xF0000000 is in the 3rd GB range (0xC0000000 - 1000000000)? No. */
    /* 0xF0000000:
       VPN[2] = (0xF0000000 >> 30) & 0x1FF = 3.
       So we need entry 3 in root_pt.
    */
    /* We can map a 1GB huge page? No, we only have 16KB stack. */
    /* We need a L1 PT for entry 3. Let's reuse l1_pt_user? No, that's for 0x0. */
    /* Let's allocate another L1 PT or just map it in the 0x0 range? */
    /* If we map stack at 0x10000000 (256MB), it falls into l1_pt_user (entry 128). */
    /* l1_pt_user covers 0x0 to 0x40000000 (1GB). */
    /* Entry 128 corresponds to 256MB. */
    /* Let's map entry 128 of l1_pt_user to user_stack_real (2MB huge page, though we only use 16KB) */
    /* uint64_t stack_ppn = ((uint64_t)user_stack_real) >> 12; */
    /* Note: user_stack_real is 4KB aligned, but for 2MB page we need 2MB alignment? */
    /* Yes, for huge page PPN must be aligned. user_stack_real might not be 2MB aligned. */
    /* So we should use 4KB pages for stack. We need L0 PT. */
    
    /* Alternative: Just map stack at 0x00000000 + offset? */
    /* No, user program is at 0x0. */
    
    /* Let's just use the memory at 0x80400000 + X as stack? */
    /* The user program is loaded at 0x80400000. */
    /* We can put stack at the end of the mapped 128MB region? */
    /* Say at 128MB - 4KB. */
    /* VA = 0x07FF0000. */
    /* This is inside the region we mapped. */
    /* So we don't need special mapping for stack! */
    /* We just point SP to a high address in the 0-128MB range. */
    /* But we need to make sure we don't overwrite program data. */
    /* Program is loaded at 0. */
    /* Let's use 0x04000000 (64MB) as stack top. */
    /* This maps to PA 0x80400000 + 64MB = 0x84400000. */
    /* This is safe. */
}

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

/* Setup stack in the mapped region (0-128MB). Use per-slot top for isolation. */
struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

static uint64_t setup_user_stack(uint64_t stack_top, const char *prog_name, uint64_t entry)
{
    /* We write to PA, but calculate VA for SP */
    uintptr_t sp_va = (uintptr_t)stack_top;
    uintptr_t sp_pa = (uintptr_t)(stack_top + 0x80400000UL); /* PA offset for user mapping */

    /* Place 16 bytes of random data for AT_RANDOM */
    sp_pa -= 16;
    sp_va -= 16;
    uint64_t rand_ptr_va = (uint64_t)sp_va;
    {
        uint8_t *r = (uint8_t *)sp_pa;
        for (int i = 0; i < 16; i++) r[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
    }
    
    /* 放置程序名字符串 */
    size_t len = 0; while (prog_name[len]) len++;
    sp_pa -= (len + 1);
    sp_va -= (len + 1);
    
    uint64_t arg0_ptr_va = sp_va;
    memcpy_simple((void *)sp_pa, prog_name, len + 1);
    
    /* 对齐 */
    sp_pa &= ~((uintptr_t)0xF);
    sp_va &= ~((uintptr_t)0xF);
    
    /* Auxiliary vector for static-PIE (musl) */
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)(uintptr_t)0x80400000UL;
    uint64_t phoff = eh->e_phoff;
    uint64_t phentsz = eh->e_phentsize;
    uint64_t phnum = eh->e_phnum;

    const uint64_t AT_NULL   = 0;
    const uint64_t AT_PHDR   = 3;
    const uint64_t AT_PHENT  = 4;
    const uint64_t AT_PHNUM  = 5;
    const uint64_t AT_PAGESZ = 6;
    const uint64_t AT_ENTRY  = 9;
    const uint64_t AT_RANDOM = 25;

    /* Stack layout: argc, argv[], NULL, envp[], NULL, auxv (key,val)..., AT_NULL */
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_NULL;   ((uint64_t *)sp_pa)[1] = 0;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_RANDOM; ((uint64_t *)sp_pa)[1] = rand_ptr_va;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_ENTRY;  ((uint64_t *)sp_pa)[1] = entry;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_PAGESZ; ((uint64_t *)sp_pa)[1] = 4096;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_PHNUM;  ((uint64_t *)sp_pa)[1] = phnum;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_PHENT;  ((uint64_t *)sp_pa)[1] = phentsz;
    sp_pa -= 8 * 2; sp_va -= 8 * 2; ((uint64_t *)sp_pa)[0] = AT_PHDR;   ((uint64_t *)sp_pa)[1] = phoff;

    /* envp terminator */
    sp_pa -= 8; sp_va -= 8; *(uint64_t *)sp_pa = 0;

    /* argv[1]=NULL */
    sp_pa -= 8; sp_va -= 8; *(uint64_t *)sp_pa = 0;
    /* argv[0] */
    sp_pa -= 8; sp_va -= 8; *(uint64_t *)sp_pa = arg0_ptr_va;
    /* argc */
    sp_pa -= 8; sp_va -= 8; *(uint64_t *)sp_pa = 1;
    
    return sp_va;
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
        int slot = proc_slot(p);
        uint64_t stack_top = user_stack_top_for_slot(slot);
        p->tf.regs[2] = setup_user_stack(stack_top, target_name, entry); /* sp */
        uint64_t sstatus = 0;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        sstatus &= ~(1ULL << 8); /* SPP=0 (User) */
        sstatus |= (1ULL << 5);  /* SPIE=1 (Enable interrupts after sret) */
        sstatus |= (1ULL << 18); /* SUM=1 (Allow Supervisor User Memory access) */
        /* Enable FP for userland (FS=01 Initial) to run rv64gc/lp64d binaries */
        sstatus |= (1ULL << 13);
        p->tf.sstatus = sstatus;
        /* stdout fd 1 preset */
        p->fds[0] = 0; p->fds[1] = 1; p->fds[2] = 2;
        p->state = P_RUNNABLE;
        current = p;
        current_trapframe = &p->tf;
        
        /* Enable Paging */
        uint64_t satp_val = SATP_MODE_SV39 | (((uint64_t)root_pt) >> 12);
        asm volatile("csrw satp, %0" :: "r"(satp_val));
        asm volatile("sfence.vma");
        
        return;
    }
}

void main()
{
    /* 设置 stvec 到 trap_entry */
    asm volatile("csrw stvec, %0" :: "r"((uint64_t)trap_entry));

    /* 允许用户态读取 cycle/time/instret（否则 libc 的时间函数可能触发 illegal instruction） */
    asm volatile("csrw scounteren, %0" :: "r"(7ULL));

    // 1. 输出启动信息
    print("\n================================\n");
    print("RISC-V OS Kernel Boot Success!\n");
    print("Team: 404notfound\n");
    print("School: Xi'an Jiaotong University\n");
    print("Kernel is now running in QEMU...\n");
    print("================================\n\n");
    
    setup_paging();
    
    // 2. 初始化 virtio-blk 与 FAT32，查找并运行包含 WRITE 关键字的测试文件
    if (virtio_blk_init() != 0) {
        print("virtio-blk init failed\n");
        shutdown();
    }
    if (fat32_init() != 0) {
        print("FAT32 init failed\n");
        shutdown();
    }

    extern void syscall_init(void);
    syscall_init();
    proc_init();
    schedule();
}

/* 当 syscall_action 请求回到内核调度时由 trap.S 调用 */
void user_return(void)
{
    schedule();
}
