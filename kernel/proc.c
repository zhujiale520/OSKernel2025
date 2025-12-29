#include "proc.h"
#include <stdint.h>

struct proc procs[MAX_PROC];
struct proc *current = 0;
struct trapframe *current_trapframe = 0;

static uint8_t kstacks[MAX_PROC][KSTACK_SIZE] __attribute__((aligned(16)));
static int next_pid = 1000;

void proc_init(void)
{
    for (int i = 0; i < MAX_PROC; i++) {
        procs[i].state = P_UNUSED;
        procs[i].pid = 0;
        procs[i].ppid = 0;
        procs[i].kstack = (uint64_t)(kstacks[i] + KSTACK_SIZE);
        for (int j = 0; j < MAX_FD; j++) procs[i].fds[j] = -1;
    }
}

struct proc *proc_alloc(int ppid)
{
    for (int i = 0; i < MAX_PROC; i++) {
        if (procs[i].state == P_UNUSED) {
            struct proc *p = &procs[i];
            p->state = P_RUNNABLE;
            p->pid = ++next_pid;
            p->ppid = ppid;
            p->exit_code = 0;
            for (int j = 0; j < MAX_FD; j++) p->fds[j] = -1;
            return p;
        }
    }
    return 0;
}

void proc_free(struct proc *p)
{
    if (!p) return;
    p->state = P_UNUSED;
    p->pid = 0;
    p->ppid = 0;
    p->exit_code = 0;
    for (int j = 0; j < MAX_FD; j++) p->fds[j] = -1;
}

struct proc *proc_find_zombie_child(int ppid)
{
    for (int i = 0; i < MAX_PROC; i++) {
        if (procs[i].state == P_ZOMBIE && procs[i].ppid == ppid) return &procs[i];
    }
    return 0;
}

struct proc *proc_find_runnable_after(struct proc *p)
{
    int start = 0;
    if (p) start = (int)(p - procs) + 1;
    for (int i = 0; i < MAX_PROC; i++) {
        int idx = (start + i) % MAX_PROC;
        if (procs[idx].state == P_RUNNABLE) return &procs[idx];
    }
    return 0;
}

int fd_alloc(struct proc *p)
{
    for (int i = 0; i < MAX_FD; i++) {
        if (p->fds[i] == -1) { p->fds[i] = i; return i; }
    }
    return -1;
}

void fd_free(struct proc *p, int fd)
{
    if (fd >= 0 && fd < MAX_FD) p->fds[fd] = -1;
}

int proc_slot(struct proc *p)
{
    if (!p) return -1;
    int idx = (int)(p - procs);
    if (idx < 0 || idx >= MAX_PROC) return -1;
    return idx;
}

extern void user_restore(void);

void switch_to_user(struct proc *p)
{
    current = p;
    current_trapframe = &p->tf;
    asm volatile("csrw sscratch, %0" :: "r"(p->kstack));
    user_restore();
    while (1) {}
}

extern void run_next_test(void);

void schedule(void)
{
    while (1) {
        struct proc *next = proc_find_runnable_after(current);
        if (next) {
            next->state = P_RUNNING;
            switch_to_user(next);
        }
        /* 没有可运行进程，说明当前测试结束，加载下一个 */
        run_next_test();
    }
}
