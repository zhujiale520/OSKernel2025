#pragma once
#include <stdint.h>

/* Simple per-process user stack layout helpers */
#define USER_STACK_TOP_BASE 0x04000000UL
#define USER_STACK_SIZE     0x00004000UL /* 16KB */
#define USER_STACK_STRIDE   0x00010000UL /* 64KB spacing between stacks */

/* Trapframe: general registers x0-31, sepc, sstatus */
struct trapframe {
    uint64_t regs[32];
    uint64_t sepc;
    uint64_t sstatus;
};

#define MAX_FD 128
#define MAX_PROC 8
#define KSTACK_SIZE 8192

enum proc_state {
    P_UNUSED = 0,
    P_RUNNABLE,
    P_RUNNING,
    P_ZOMBIE,
    P_WAITING,
    P_SLEEPING
};

struct proc {
    int pid;
    int ppid;
    enum proc_state state;
    int exit_code;
    struct trapframe tf;
    uint64_t kstack;
    uint64_t heap_top;
    int fds[MAX_FD]; /* Values are indices into global file_table, -1 if closed */
    uint8_t fd_flags[MAX_FD]; /* Per-fd flags (e.g. FD_CLOEXEC) */
    char cwd[256];
    /* Waiting information for wait/waitpid */
    int waiting;
    int wait_target;
    uint64_t wait_status_ptr;

    /* Process group and termination metadata */
    int pgid;
    int exited_by_signal;
    int term_signal;

    /* waitid metadata (used when blocking in kernel) */
    int wait_is_waitid;
    uint64_t wait_infop_ptr;
    int wait_idtype;
    int wait_id;
    int wait_options;

    /* Sleep support (nanosleep/sleep) */
    uint64_t sleep_until;
};

extern struct proc *current;
extern struct trapframe *current_trapframe;
extern struct proc procs[MAX_PROC];

int proc_slot(struct proc *p);

void proc_init(void);
struct proc *proc_alloc(int ppid);
void proc_free(struct proc *p);
struct proc *proc_find_zombie_child(int ppid);
struct proc *proc_find_runnable_after(struct proc *p);
struct proc *proc_find_by_pid(int pid);
void schedule(void) __attribute__((noreturn));
void switch_to_user(struct proc *p) __attribute__((noreturn));

/* stack helpers */
uint64_t user_stack_top_for_slot(int slot);
uint64_t user_stack_base_for_slot(int slot);

/* helpers */
int fd_alloc(struct proc *p);
void fd_free(struct proc *p, int fd);
