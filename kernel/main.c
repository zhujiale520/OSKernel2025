#include <stdint.h>

void print(const char *str);
void print_hex(uint64_t num);

int virtio_blk_init(void);
int fat32_init(void);
int fat32_list_root(void);
int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);
int load_elf_from_fat32(const char *name, uint64_t *entry_out);
void enter_user(uint64_t entry);
void trap_entry(void);
uint64_t syscall_handle(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7);

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
    static uint8_t file_buf[4096];
    uint32_t file_size = 0;
    uint64_t entry = 0;

    // 1. 输出启动信息
    print("\n================================\n");
    print("RISC-V OS Kernel Boot Success!\n");
    print("Team: 404notfound\n");
    print("School: Xi'an Jiaotong University\n");
    print("Kernel is now running in QEMU...\n");
    print("================================\n\n");
    
    // 2. 初始化 virtio-blk 与 FAT32，并列出根目录文件名
    if (virtio_blk_init() == 0) {
        if (fat32_init() == 0) {
            print("FAT32 root entries:\n");
            fat32_list_root();

            /* 读取示例文件 TEXT.TXT ，验证文件读路径 */
            if (fat32_read_file("TEXT.TXT", file_buf, sizeof(file_buf) - 1, &file_size) == 0) {
                if (file_size >= sizeof(file_buf)) file_size = sizeof(file_buf) - 1;
                file_buf[file_size] = '\0';
                print("\nTEXT.TXT content (first 4KB):\n");
                print((char *)file_buf);
                print("\n");
            } else {
                print("read TEXT.TXT failed\n");
            }

            /* 尝试加载一个 ELF 测试程序（短文件名 TEST_E~1 对应 TEST_ELF?），仅加载不跳转 */
                if (load_elf_from_fat32("TEST_E~1", &entry) == 0) {
                    print("ELF load success, entry="); print_hex(entry); print("\n");
                    print("Jumping to user mode...\n");
                    enter_user(entry);
                    print("returned from user (unexpected)\n");
            } else {
                print("ELF load failed\n");
            }
        } else {
            print("FAT32 init failed\n");
        }
    } else {
        print("virtio-blk init failed\n");
    }
    
    // 3. 测试完成，关机
    print("Kernel initialization complete.\n");
    print("Shutting down via SBI call...\n");
    
    shutdown();
    
    // 4. 永远不会执行到这里
    while (1) {}
}
