#pragma once
#include <stdint.h>

/* Trapframe: general registers x0-31, sepc, sstatus */
struct trapframe {
    uint64_t regs[32];
    uint64_t sepc;
    uint64_t sstatus;
};

#define MAX_FD 16
#define MAX_PROC 8
#define KSTACK_SIZE 8192

enum proc_state {
    P_UNUSED = 0,
    P_RUNNABLE,
    P_RUNNING,
    P_ZOMBIE
};

struct proc {
    int pid;
    int ppid;
    enum proc_state state;
    int exit_code;
    struct trapframe tf;
    uint64_t kstack;
    int fds[MAX_FD];
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
void schedule(void) __attribute__((noreturn));
void switch_to_user(struct proc *p) __attribute__((noreturn));

/* helpers */
int fd_alloc(struct proc *p);
void fd_free(struct proc *p, int fd);
