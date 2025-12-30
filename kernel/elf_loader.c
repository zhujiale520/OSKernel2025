#include <stdint.h>
#include <stddef.h>

void print(const char *s);
void print_hex(uint64_t v);

int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);

#define LOAD_BASE 0x80400000UL
#define MAX_ELF_SIZE (1024 * 1024) /* 1MB buffer */

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

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

static void *memcpy_simple(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *memset_simple(void *dst, int v, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)v;
    return dst;
}

static int load_elf_from_buffer(const uint8_t *img, uint32_t size, uint64_t *entry_out)
{
    if (size < sizeof(struct elf64_ehdr)) {
        print("elf: too small\n");
        return -1;
    }
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)img;
    if (!(eh->e_ident[0] == 0x7F && eh->e_ident[1] == 'E' && eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F')) {
        print("elf: bad magic\n");
        return -1;
    }
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1) { /* ELFCLASS64 + little endian */
        print("elf: unsupported class/endian\n");
        return -1;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size) {
        print("elf: bad ph table\n");
        return -1;
    }

    const struct elf64_phdr *ph = (const struct elf64_phdr *)(img + eh->e_phoff);
    uint64_t base_vaddr = (uint64_t)-1;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != 1 /* PT_LOAD */) continue;
        if (ph[i].p_vaddr < base_vaddr) base_vaddr = ph[i].p_vaddr;
    }
    if (base_vaddr == (uint64_t)-1) {
        print("elf: no loadable segments\n");
        return -1;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != 1) continue;
        /* 
         * Load to Physical Address (mapped in Kernel at 0x80400000+offset)
         * We assume User VA 0 maps to PA LOAD_BASE.
         */
        uint64_t dest = LOAD_BASE + ph[i].p_vaddr;
        
        if (ph[i].p_offset + ph[i].p_filesz > size) {
            print("elf: segment oob\n");
            return -1;
        }
        memcpy_simple((void *)(uintptr_t)dest, img + ph[i].p_offset, (size_t)ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz) {
            memset_simple((void *)(uintptr_t)(dest + ph[i].p_filesz), 0, (size_t)(ph[i].p_memsz - ph[i].p_filesz));
        }
    }

    if (entry_out) {
        *entry_out = eh->e_entry; /* Return Virtual Address */
    }
    return 0;
}

int load_elf_from_fat32(const char *name, uint64_t *entry_out)
{
    static uint8_t elf_buf[MAX_ELF_SIZE];
    uint32_t file_size = 0;
    if (fat32_read_file(name, elf_buf, sizeof(elf_buf), &file_size) != 0) {
        print("elf: read file failed\n");
        return -1;
    }
    if (file_size > MAX_ELF_SIZE) {
        print("elf: file too big\n");
        return -1;
    }
    return load_elf_from_buffer(elf_buf, file_size, entry_out);
}
