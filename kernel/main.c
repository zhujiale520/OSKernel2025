#include <stdint.h>
#include <stddef.h>

void print(const char *str);
void print_hex(uint64_t num);

int virtio_blk_init(void);
int fat32_init(void);
int fat32_list_root(void);
int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);
int fat32_find_file_keyword(const char *keyword, char *out_name, size_t out_len, uint32_t *out_size);
int load_elf_from_fat32(const char *name, uint64_t *entry_out);
void enter_user(uint64_t entry);
void trap_entry(void);
uint64_t syscall_handle(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);
extern volatile uint64_t syscall_exit_code;

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

void main()
{
    char target_name[32];
    uint32_t file_size = 0;
    uint64_t entry = 0;

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

    print("Scanning FAT32 root for WRITE test...\n");
    if (fat32_find_file_keyword("WRITE", target_name, sizeof(target_name), &file_size) != 0) {
        print("No WRITE test found.\n");
        shutdown();
    }

    print("Loading ELF: ");
    print(target_name);
    print("\n");
    if (load_elf_from_fat32(target_name, &entry) != 0) {
        print("ELF load failed\n");
        shutdown();
    }

    print("Jumping to user mode...\n");
    enter_user(entry);

    // 不期望执行到此处
    print("returned from user (unexpected)\n");
    shutdown();
    while (1) {}
}

/* 用户态 exit 回到此处，保持内核栈 */
void user_return(void)
{
    print("User program exited, code=");
    print_hex(syscall_exit_code);
    print("\nShutting down...\n");
    shutdown();
    while (1) {}
}
