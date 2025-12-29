#include <stdint.h>
#include <stddef.h>

void print(const char *s);
void print_hex(uint64_t num);

int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);

/* 简单 memcpy/memset 实现，避免依赖标准库 */
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


/* 依赖底层块设备 */
int virtio_blk_read_sector(uint64_t sector, void *buf);

struct fat32_bpb {
    uint8_t  jmp[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;   /* offset 11 */
    uint8_t  sectors_per_cluster;/* offset 13 */
    uint16_t reserved_sectors;   /* offset 14 */
    uint8_t  num_fats;           /* offset 16 */
    uint16_t root_entries;       /* offset 17 (FAT16) */
    uint16_t total_sectors_16;   /* offset 19 */
    uint8_t  media;              /* offset 21 */
    uint16_t fat_size_16;        /* offset 22 */
    uint16_t sectors_per_track;  /* offset 24 */
    uint16_t num_heads;          /* offset 26 */
    uint32_t hidden_sectors;     /* offset 28 */
    uint32_t total_sectors_32;   /* offset 32 */
    uint32_t fat_size_32;        /* offset 36 */
    uint16_t ext_flags;          /* offset 40 */
    uint16_t fs_version;         /* offset 42 */
    uint32_t root_cluster;       /* offset 44 */
    uint16_t fs_info;            /* offset 48 */
    uint16_t backup_boot;        /* offset 50 */
    uint8_t  reserved2[12];      /* offset 52 */
    uint8_t  drive_number;       /* offset 64 */
    uint8_t  reserved3;          /* offset 65 */
    uint8_t  boot_signature;     /* offset 66 */
    uint32_t volume_id;          /* offset 67 */
    uint8_t  volume_label[11];   /* offset 71 */
    uint8_t  fs_type[8];         /* offset 82 */
} __attribute__((packed));

struct fat_dir_entry {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

#define ATTR_LONG_NAME 0x0F

static struct fat32_ctx {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint32_t fat_start_sector;
} fat;

static uint8_t sector_buf[512];

static uint16_t le16(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t le32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint64_t cluster_to_sector(uint32_t cluster)
{
    return fat.first_data_sector + (uint64_t)(cluster - 2) * fat.sectors_per_cluster;
}

static char to_upper_simple(char c)
{
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static int build_short_name(const char *name, uint8_t out[11])
{
    /* 构造 8.3 短文件名（空格填充，忽略多余字符） */
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int pos = 0;      /* 0-7 */
    int ext_pos = 8;  /* 8-10 */
    int in_ext = 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (c == '.') { in_ext = 1; continue; }
        if (c == '/' || c == '\n' || c == '\r' || c == '\t') return -1;
        c = to_upper_simple(c);
        if (!in_ext) {
            if (pos >= 8) continue; /* 截断 */
            out[pos++] = (uint8_t)c;
        } else {
            if (ext_pos >= 11) continue; /* 截断 */
            out[ext_pos++] = (uint8_t)c;
        }
    }
    return 0;
}

static int short_name_match(const uint8_t disk_name[11], const uint8_t target[11])
{
    for (int i = 0; i < 11; i++) {
        if (disk_name[i] != target[i]) return 0;
    }
    return 1;
}

int fat32_init(void)
{
    if (virtio_blk_read_sector(0, sector_buf) != 0) {
        print("fat32: read bpb failed\n");
        return -1;
    }
    struct fat32_bpb bpb;
    memcpy_simple(&bpb, sector_buf, sizeof(struct fat32_bpb));

    fat.bytes_per_sector = le16(&bpb.bytes_per_sector);
    fat.sectors_per_cluster = bpb.sectors_per_cluster;
    fat.reserved_sectors = le16(&bpb.reserved_sectors);
    fat.num_fats = bpb.num_fats;
    fat.fat_size = bpb.fat_size_32;
    fat.root_cluster = bpb.root_cluster;
    fat.fat_start_sector = fat.reserved_sectors;
    fat.first_data_sector = fat.reserved_sectors + fat.num_fats * fat.fat_size;

    if (fat.bytes_per_sector != 512) {
        print("fat32: only 512-byte sectors supported\n");
        return -1;
    }
    return 0;
}

static int fat32_read_fat_entry(uint32_t cluster, uint32_t *out)
{
    /* FAT32 entry 4 bytes */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fat.fat_start_sector + (fat_offset / fat.bytes_per_sector);
    uint32_t ent_offset = fat_offset % fat.bytes_per_sector;

    if (virtio_blk_read_sector(fat_sector, sector_buf) != 0) return -1;
    uint32_t val = le32(&sector_buf[ent_offset]);
    *out = val & 0x0FFFFFFF;
    return 0;
}

static void print_short_name(const uint8_t name[11])
{
    char buf[14];
    size_t p = 0;
    /* name 0-7 */
    for (int i = 0; i < 8; i++) {
        if (name[i] == ' ') break;
        buf[p++] = name[i];
    }
    /* extension */
    int has_ext = 0;
    for (int i = 8; i < 11; i++) {
        if (name[i] != ' ') { has_ext = 1; break; }
    }
    if (has_ext && p < sizeof(buf)-1) {
        buf[p++] = '.';
        for (int i = 8; i < 11 && p < sizeof(buf)-1; i++) {
            if (name[i] == ' ') break;
            buf[p++] = name[i];
        }
    }
    buf[p] = '\0';
    print(buf);
    print("\n");
}

/* 将磁盘中的短文件名转换为以0结尾的字符串，例如 "TEST    TXT" -> "TEST.TXT" */
static void short_name_to_str(const uint8_t name[11], char *out, size_t out_len)
{
    size_t p = 0;
    for (int i = 0; i < 8 && p + 1 < out_len; i++) {
        if (name[i] == ' ') break;
        out[p++] = name[i];
    }
    int has_ext = 0;
    for (int i = 8; i < 11; i++) {
        if (name[i] != ' ') { has_ext = 1; break; }
    }
    if (has_ext && p + 1 < out_len) {
        out[p++] = '.';
        for (int i = 8; i < 11 && p + 1 < out_len; i++) {
            if (name[i] == ' ') break;
            out[p++] = name[i];
        }
    }
    out[p] = '\0';
}

static size_t tiny_strlen(const char *s)
{
    size_t l = 0; while (s[l]) l++; return l;
}

static int contains_keyword_case_insensitive(const char *name, const char *key)
{
    size_t nlen = tiny_strlen(name);
    size_t klen = tiny_strlen(key);
    if (klen == 0 || klen > nlen) return 0;
    for (size_t i = 0; i + klen <= nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < klen; j++) {
            char c1 = name[i + j];
            char c2 = key[j];
            if (c1 >= 'a' && c1 <= 'z') c1 = c1 - 'a' + 'A';
            if (c2 >= 'a' && c2 <= 'z') c2 = c2 - 'a' + 'A';
            if (c1 != c2) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

int fat32_list_root(void)
{
    uint32_t cluster = fat.root_cluster;
    while (cluster < 0x0FFFFFF8) {
        uint64_t first_sector = cluster_to_sector(cluster);
        for (uint32_t s = 0; s < fat.sectors_per_cluster; s++) {
            if (virtio_blk_read_sector(first_sector + s, sector_buf) != 0) {
                print("fat32: read sector failed\n");
                return -1;
            }
            for (int off = 0; off < 512; off += 32) {
                struct fat_dir_entry *ent = (struct fat_dir_entry *)(sector_buf + off);
                if (ent->name[0] == 0x00) {
                    return 0; /* 目录结束 */
                }
                if (ent->name[0] == 0xE5) continue; /* 已删除 */
                if (ent->attr == ATTR_LONG_NAME) continue; /* 跳过长文件名项 */
                print_short_name(ent->name);
            }
        }
        if (fat32_read_fat_entry(cluster, &cluster) != 0) {
            print("fat32: read FAT entry failed\n");
            return -1;
        }
    }
    return 0;
}

int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size)
{
    uint8_t target[11];
    if (build_short_name(name, target) != 0) return -1;

    uint32_t cluster = fat.root_cluster;
    while (cluster < 0x0FFFFFF8) {
        uint64_t first_sector = cluster_to_sector(cluster);
        for (uint32_t s = 0; s < fat.sectors_per_cluster; s++) {
            if (virtio_blk_read_sector(first_sector + s, sector_buf) != 0) {
                print("fat32: read sector failed\n");
                return -1;
            }
            for (int off = 0; off < 512; off += 32) {
                struct fat_dir_entry *ent = (struct fat_dir_entry *)(sector_buf + off);
                if (ent->name[0] == 0x00) return -1; /* not found */
                if (ent->name[0] == 0xE5) continue;
                if (ent->attr == ATTR_LONG_NAME) continue;
                if (ent->attr & 0x10) continue; /* skip directories */
                if (!short_name_match(ent->name, target)) continue;

                uint32_t file_cluster = ((uint32_t)ent->first_cluster_high << 16) | ent->first_cluster_low;
                uint32_t remaining = ent->file_size;
                if (out_size) *out_size = remaining;
                uint32_t copied = 0;

                while (file_cluster < 0x0FFFFFF8 && remaining > 0) {
                    uint64_t sec0 = cluster_to_sector(file_cluster);
                    for (uint32_t i = 0; i < fat.sectors_per_cluster && remaining > 0; i++) {
                        if (virtio_blk_read_sector(sec0 + i, sector_buf) != 0) return -1;
                        uint32_t chunk = remaining < 512 ? remaining : 512;
                        if (copied < buf_size) {
                            uint32_t to_copy = chunk;
                            if (copied + to_copy > buf_size) to_copy = buf_size - copied;
                            memcpy_simple(buf + copied, sector_buf, to_copy);
                        }
                        copied += chunk;
                        remaining -= chunk;
                    }
                    uint32_t next;
                    if (fat32_read_fat_entry(file_cluster, &next) != 0) return -1;
                    file_cluster = next;
                }
                return 0;
            }
        }
        if (fat32_read_fat_entry(cluster, &cluster) != 0) {
            print("fat32: read FAT entry failed\n");
            return -1;
        }
    }
    return -1;
}

/* 在根目录中查找短文件名包含关键字的普通文件，返回其短名字符串 */
int fat32_find_file_keyword(const char *keyword, char *out_name, size_t out_len, uint32_t *out_size)
{
    uint32_t cluster = fat.root_cluster;
    while (cluster < 0x0FFFFFF8) {
        uint64_t first_sector = cluster_to_sector(cluster);
        for (uint32_t s = 0; s < fat.sectors_per_cluster; s++) {
            if (virtio_blk_read_sector(first_sector + s, sector_buf) != 0) {
                print("fat32: read sector failed\n");
                return -1;
            }
            for (int off = 0; off < 512; off += 32) {
                struct fat_dir_entry *ent = (struct fat_dir_entry *)(sector_buf + off);
                if (ent->name[0] == 0x00) return -1; /* 目录结束 */
                if (ent->name[0] == 0xE5) continue;
                if (ent->attr == ATTR_LONG_NAME) continue;
                if (ent->attr & 0x10) continue; /* 目录跳过 */
                char name_buf[16];
                short_name_to_str(ent->name, name_buf, sizeof(name_buf));
                if (contains_keyword_case_insensitive(name_buf, keyword)) {
                    /* 命中 */
                    size_t need = tiny_strlen(name_buf) + 1;
                    if (need > out_len) need = out_len;
                    for (size_t i = 0; i < need - 1; i++) out_name[i] = name_buf[i];
                    out_name[need - 1] = '\0';
                    if (out_size) *out_size = ent->file_size;
                    return 0;
                }
            }
        }
        if (fat32_read_fat_entry(cluster, &cluster) != 0) {
            print("fat32: read FAT entry failed\n");
            return -1;
        }
    }
    return -1;
}

/* 获取根目录下第 index 个普通文件的文件名 */
int fat32_get_file_at_index(int index, char *out_name, size_t out_len, uint32_t *out_size)
{
    uint32_t cluster = fat.root_cluster;
    int count = 0;
    while (cluster < 0x0FFFFFF8) {
        uint64_t first_sector = cluster_to_sector(cluster);
        for (uint32_t s = 0; s < fat.sectors_per_cluster; s++) {
            if (virtio_blk_read_sector(first_sector + s, sector_buf) != 0) return -1;
            for (int off = 0; off < 512; off += 32) {
                struct fat_dir_entry *ent = (struct fat_dir_entry *)(sector_buf + off);
                if (ent->name[0] == 0x00) return -1; /* End of dir */
                if (ent->name[0] == 0xE5) continue;
                if (ent->attr == ATTR_LONG_NAME) continue;
                if (ent->attr & 0x10) continue; /* Skip directories */
                
                if (count == index) {
                    char name_buf[16];
                    short_name_to_str(ent->name, name_buf, sizeof(name_buf));
                    size_t need = tiny_strlen(name_buf) + 1;
                    if (need > out_len) need = out_len;
                    for (size_t i = 0; i < need - 1; i++) out_name[i] = name_buf[i];
                    out_name[need - 1] = '\0';
                    if (out_size) *out_size = ent->file_size;
                    return 0;
                }
                count++;
            }
        }
        if (fat32_read_fat_entry(cluster, &cluster) != 0) return -1;
    }
    return -1;
}
