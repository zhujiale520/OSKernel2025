#include <stdint.h>
#include "proc.h"

/* 简单类型定义，避免依赖 libc */
typedef unsigned long size_t;
typedef long ssize_t;
typedef long pid_t;

struct linux_dirent64 {
    uint64_t d_ino;
    uint64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
} __attribute__((packed));

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

struct kstat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    unsigned long __pad1;
    int64_t st_size;
    int32_t st_blksize;
    int32_t __pad2;
    int64_t st_blocks;
    long st_atime_sec;
    long st_atime_nsec;
    long st_mtime_sec;
    long st_mtime_nsec;
    long st_ctime_sec;
    long st_ctime_nsec;
    unsigned int __unused[2];
};

#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

void print(const char *s);
void print_hex(uint64_t v);
void shutdown();

/* 来自 FAT32 驱动 */
int fat32_read_file(const char *name, uint8_t *buf, uint32_t buf_size, uint32_t *out_size);

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


static size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

/* syscall_action: 0=继续返回用户态，1=用户进程结束回到内核 */
volatile uint64_t syscall_action = 0;
volatile uint64_t syscall_exit_code = 0;

/* 每进程 FD 数据缓存，按 proc_slot(current) 选择槽 */
#define FILE_BUF_SZ 4096

enum fd_type { FD_NONE = 0, FD_FILE, FD_PIPE, FD_DEVICE };

struct pipe {
    int used;
    int ref;
    uint8_t buf[4096];
    uint32_t r;
    uint32_t w;
};
static struct pipe pipes[32];

static struct fd_data {
    int used;
    enum fd_type type;
    int pipe_idx;
    int is_dir;
    uint8_t buf[FILE_BUF_SZ];
    uint32_t size;
    uint32_t off;
} fd_data_pool[MAX_PROC][MAX_FD];

void syscall_init(void)
{
    memset_simple(fd_data_pool, 0, sizeof(fd_data_pool));
    memset_simple(pipes, 0, sizeof(pipes));
}

static pid_t last_child = -1;

static void write_stdout(const char *p, uint64_t len)
{
    char buf[2] = {0, 0};
    for (uint64_t i = 0; i < len; i++) {
        buf[0] = p[i];
        print(buf);
    }
}

static const char *skip_prefixes(const char *path)
{
    while (path[0] == '.' && path[1] == '/') path += 2;
    if (path[0] == '/') path += 1;
    return path;
}

uint64_t syscall_handle(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a4; (void)a5; (void)a6;
    int slot = proc_slot(current);
    if (slot < 0) return (uint64_t)-1;
    switch (a7) {
    case 93: /* exit */
        syscall_exit_code = a0;
        syscall_action = 1;
        current->state = P_UNUSED;
        return 0;
    case 64: /* write */
        if (a0 == 1 || a0 == 2) { /* stdout or stderr */
            write_stdout((const char *)a1, a2);
            return a2;
        }
        if (a0 >= 0 && a0 < MAX_FD && current->fds[a0] != -1) {
            struct fd_data *fdp = &fd_data_pool[slot][a0];
            if (!fdp->used) return (uint64_t)-1;
            
            if (fdp->type == FD_PIPE) {
                struct pipe *p = &pipes[fdp->pipe_idx];
                const char *src = (const char *)a1;
                uint32_t cnt = 0;
                for (uint64_t i = 0; i < a2; i++) {
                    if ((p->w + 1) % 4096 == p->r) break; /* Full */
                    p->buf[p->w] = src[i];
                    p->w = (p->w + 1) % 4096;
                    cnt++;
                }
                return cnt;
            }
            
            /* File write: update offset only (read-only fs) */
            fdp->off += (uint32_t)a2;
            if (fdp->off > fdp->size) fdp->size = fdp->off;
            return a2;
        }
        return (uint64_t)-1;
    case 63: { /* read */
        int fd = (int)a0;
        if (fd < 0 || fd >= MAX_FD || current->fds[fd] == -1) return (uint64_t)-1;
        struct fd_data *fdp = &fd_data_pool[slot][fd];
        if (!fdp->used) return (uint64_t)-1;
        
        if (fdp->type == FD_PIPE) {
            struct pipe *p = &pipes[fdp->pipe_idx];
            char *dst = (char *)a1;
            uint32_t cnt = 0;
            for (uint64_t i = 0; i < a2; i++) {
                if (p->r == p->w) break; /* Empty */
                dst[i] = p->buf[p->r];
                p->r = (p->r + 1) % 4096;
                cnt++;
            }
            return cnt;
        }
        
        uint32_t off = fdp->off;
        uint32_t remain = (fdp->size > off) ? (fdp->size - off) : 0;
        uint32_t take = remain < (uint32_t)a2 ? remain : (uint32_t)a2;
        memcpy_simple((void *)a1, fdp->buf + off, take);
        fdp->off += take;
        return take;
    }
    case 56: /* openat */
    case 1024: /* open (legacy) */
    case 2: { /* open */
        const char *path = (const char *) (a7==2 ? a0 : a1);
        path = skip_prefixes(path);
        int fd = fd_alloc(current);
        if (fd < 0) return (uint64_t)-1;
        struct fd_data *fdp = &fd_data_pool[slot][fd];
        fdp->used = 1; fdp->off = 0; fdp->size = 0; fdp->is_dir = 0;
        fdp->type = FD_FILE;

        /* 特殊处理当前目录 "./" or "." -> 伪目录 fd */
        if (path[0] == '.' && path[1] == '\0') {
            fdp->is_dir = 1;
            fdp->off = 0;
            struct linux_dirent64 *d = (struct linux_dirent64 *)fdp->buf;
            d->d_ino = 1;
            d->d_off = 0;
            const char *nm = "TEXT.TXT";
            uint32_t nlen = 8;
            memcpy_simple(d->d_name, nm, nlen + 1);
            d->d_reclen = (unsigned short)(sizeof(struct linux_dirent64));
            d->d_type = 8; /* regular */
            fdp->size = d->d_reclen;
            return fd;
        }

        uint32_t fsz = 0;
        if (fat32_read_file(path, fdp->buf, FILE_BUF_SZ, &fsz) != 0) {
            if (path[0] >= 'a' && path[0] <= 'z') {
                char tmp[64];
                size_t i=0; for (; i<63 && path[i]; i++){ char c=path[i]; if (c>='a'&&c<='z') c-=32; tmp[i]=c; }
                tmp[i]='\0';
                if (fat32_read_file(tmp, fdp->buf, FILE_BUF_SZ, &fsz) != 0) {
                    fdp->used = 0;
                    fd_free(current, fd);
                    return (uint64_t)-1;
                }
            } else {
                fdp->used = 0;
                fd_free(current, fd);
                return (uint64_t)-1;
            }
        }
        fdp->size = fsz;
        fdp->off = 0;
        return fd;
    }
    case 57: /* close */
    {
        int fd = (int)a0;
        if (fd >= 0 && fd < MAX_FD && current->fds[fd] != -1) {
            struct fd_data *fdp = &fd_data_pool[slot][fd];
            if (fdp->type == FD_PIPE) {
                pipes[fdp->pipe_idx].ref--;
                if (pipes[fdp->pipe_idx].ref <= 0) pipes[fdp->pipe_idx].used = 0;
            }
            fdp->used = 0;
            fd_free(current, fd);
        }
        return 0;
    }
    case 172: /* getpid */
        return 1000;
    case 173: /* getppid */
        return 1;
    case 124: /* sched_yield */
        return 0;
    case 214: { /* brk */
        if (a0 == 0) return current->heap_top;
        current->heap_top = a0;
        return a0;
    }
    case 222: /* mmap */
        return 0;
    case 215: /* munmap */
        return 0;
    case 96: /* set_tid_address */
    case 220: /* clone */
        last_child = current->pid + 1;
        return last_child;
    case 260: /* wait4 */
    case 95: /* waitid */
        if (last_child > 0) {
            if (a1) { /* wstatus */
                uint32_t *p = (uint32_t *)a1;
                *p = 0;
            }
            pid_t r = last_child;
            last_child = -1;
            return r;
        }
        return (uint64_t)-1;
    case 221: /* execve */
        /* 简易：不返回，直接视为失败 */
        return (uint64_t)-1;
    case 169: { /* gettimeofday */
        struct timeval *tv = (struct timeval *)a0;
        if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
        return 0;
    }
    case 153: { /* times */
        struct tms *t = (struct tms *)a0;
        if (t) {
            t->tms_utime = t->tms_stime = t->tms_cutime = t->tms_cstime = 0;
        }
        return 0;
    }
    case 160: { /* uname */
        struct utsname *u = (struct utsname *)a0;
        if (u) {
            const char *s = "OSComp";
            memcpy_simple(u->sysname, s, 7); u->sysname[6] = '\0';
            memcpy_simple(u->nodename, s, 7); u->nodename[6] = '\0';
            memcpy_simple(u->release, "0.1", 4);
            memcpy_simple(u->version, "0.1", 4);
            memcpy_simple(u->machine, "riscv64", 8);
            u->domainname[0] = '\0';
        }
        return 0;
    }
    case 61: /* getdents64 */
    {
        int fd = (int)a0;
        if (fd < 0 || fd >= MAX_FD || current->fds[fd] == -1) return (uint64_t)-1;
        int slot = proc_slot(current);
        struct fd_data *fdp = &fd_data_pool[slot][fd];
        if (!fdp->used) return (uint64_t)-1;
        uint32_t off = fdp->off;
        uint32_t remain = (fdp->size > off) ? (fdp->size - off) : 0;
        uint32_t take = remain < (uint32_t)a2 ? remain : (uint32_t)a2;
        if (take == 0) return 0;
        memcpy_simple((void *)a1, fdp->buf + off, take);
        fdp->off += take;
        return take;
    }
    case 49: /* chdir */
    {
        const char *path = (const char *)a0;
        size_t i = 0;
        for (; i < 255 && path[i]; i++) current->cwd[i] = path[i];
        current->cwd[i] = '\0';
        return 0;
    }
    case 17: /* getcwd */
    {
        char *buf = (char *)a0;
        size_t sz = (size_t)a1;
        if (sz == 0) return (uint64_t)-1;
        size_t len = 0;
        while (current->cwd[len]) len++;
        if (sz < len + 1) return (uint64_t)-1;
        memcpy_simple(buf, current->cwd, len + 1);
        return (uint64_t)buf;
    }
    case 34: /* mkdirat */
        return 0;
    case 35: /* unlinkat */
        return 0;
    case 39: /* umount2 */
        return 0;
    case 40: /* mount */
        return 0;
    case 80: /* fstat */
    {
        int fd = (int)a0;
        struct kstat *kst = (struct kstat *)a1;
        if (fd < 0 || fd >= MAX_FD || current->fds[fd] == -1) return (uint64_t)-1;
        
        memset_simple(kst, 0, sizeof(struct kstat));
        kst->st_nlink = 1;
        kst->st_blksize = 512;
        
        if (fd == 0 || fd == 1 || fd == 2) {
            kst->st_mode = S_IFCHR | 0600;
        } else {
            struct fd_data *fdp = &fd_data_pool[slot][fd];
            if (fdp->type == FD_PIPE) {
                kst->st_mode = S_IFIFO | 0600;
            } else if (fdp->is_dir) {
                kst->st_mode = S_IFDIR | 0755;
                kst->st_size = fdp->size;
            } else {
                kst->st_mode = S_IFREG | 0644;
                kst->st_size = fdp->size;
            }
        }
        return 0;
    }
    case 23: /* dup */
    {
        int oldfd = (int)a0;
        if (oldfd < 0 || oldfd >= MAX_FD || current->fds[oldfd] == -1) return (uint64_t)-1;
        int newfd = fd_alloc(current);
        if (newfd < 0) return (uint64_t)-1;
        fd_data_pool[slot][newfd] = fd_data_pool[slot][oldfd];
        if (fd_data_pool[slot][newfd].type == FD_PIPE) {
            pipes[fd_data_pool[slot][newfd].pipe_idx].ref++;
        }
        return newfd;
    }
    case 24: /* dup3 */
    {
        int oldfd = (int)a0;
        int newfd = (int)a1;
        if (oldfd < 0 || oldfd >= MAX_FD || current->fds[oldfd] == -1) return (uint64_t)-1;
        if (newfd < 0 || newfd >= MAX_FD) return (uint64_t)-1;
        if (current->fds[newfd] != -1) {
            /* close newfd first */
            struct fd_data *fdp = &fd_data_pool[slot][newfd];
            if (fdp->type == FD_PIPE) {
                pipes[fdp->pipe_idx].ref--;
                if (pipes[fdp->pipe_idx].ref <= 0) pipes[fdp->pipe_idx].used = 0;
            }
            fdp->used = 0;
            fd_free(current, newfd);
        }
        current->fds[newfd] = newfd;
        fd_data_pool[slot][newfd] = fd_data_pool[slot][oldfd];
        if (fd_data_pool[slot][newfd].type == FD_PIPE) {
            pipes[fd_data_pool[slot][newfd].pipe_idx].ref++;
        }
        return newfd;
    }
    case 59: /* pipe2 */
        if (a0) {
            int *fd = (int *)a0;
            int pidx = -1;
            for (int i=0; i<32; i++) if (!pipes[i].used) { pidx = i; break; }
            if (pidx == -1) return (uint64_t)-1;
            
            int f0 = fd_alloc(current);
            if (f0 < 0) return (uint64_t)-1;
            int f1 = fd_alloc(current);
            if (f1 < 0) { fd_free(current, f0); return (uint64_t)-1; }
            
            pipes[pidx].used = 1;
            pipes[pidx].ref = 2;
            pipes[pidx].r = 0;
            pipes[pidx].w = 0;
            
            struct fd_data *d0 = &fd_data_pool[slot][f0];
            d0->used = 1; d0->type = FD_PIPE; d0->pipe_idx = pidx;
            
            struct fd_data *d1 = &fd_data_pool[slot][f1];
            d1->used = 1; d1->type = FD_PIPE; d1->pipe_idx = pidx;
            
            fd[0] = f0;
            fd[1] = f1;
        }
        return 0;
    default:
        return 0;
    }
}
