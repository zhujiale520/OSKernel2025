#include "proc.h"
#include <stdint.h>

void print(const char *s);
void print_hex(uint64_t v);

struct proc procs[MAX_PROC];
struct proc *current = 0;
struct trapframe *current_trapframe = 0;

static uint8_t kstacks[MAX_PROC][KSTACK_SIZE] __attribute__((aligned(16)));
static int next_pid = 1000;

static inline uint64_t get_time_ticks(void)
{
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static inline uint64_t clamp_slot(int slot) { return slot < 0 ? 0 : (uint64_t)slot; }

uint64_t user_stack_top_for_slot(int slot)
{
    return USER_STACK_TOP_BASE - clamp_slot(slot) * USER_STACK_STRIDE;
}

uint64_t user_stack_base_for_slot(int slot)
{
    return user_stack_top_for_slot(slot) - USER_STACK_SIZE;
}

void proc_init(void)
{
    for (int i = 0; i < MAX_PROC; i++) {
        procs[i].state = P_UNUSED;
        procs[i].pid = 0;
        procs[i].ppid = 0;
        procs[i].pgid = 0;
        procs[i].exit_code = 0;
        procs[i].exited_by_signal = 0;
        procs[i].term_signal = 0;
        procs[i].kstack = (uint64_t)(kstacks[i] + KSTACK_SIZE);
        for (int j = 0; j < MAX_FD; j++) {
            procs[i].fds[j] = -1;
            procs[i].fd_flags[j] = 0;
        }
        procs[i].waiting = 0;
        procs[i].wait_target = 0;
        procs[i].wait_status_ptr = 0;

        procs[i].wait_is_waitid = 0;
        procs[i].wait_infop_ptr = 0;
        procs[i].wait_idtype = 0;
        procs[i].wait_id = 0;
        procs[i].wait_options = 0;

        procs[i].sleep_until = 0;
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
            p->pgid = p->pid;
            p->exit_code = 0;
            p->exited_by_signal = 0;
            p->term_signal = 0;
            p->heap_top = 0x00200000; /* Initial heap top */
            /* Reserve stdin/stdout/stderr */
            /* We assume file_table[0,1,2] are pre-allocated for stdio */
            p->fds[0] = 0; p->fds[1] = 1; p->fds[2] = 2;
            p->fd_flags[0] = 0; p->fd_flags[1] = 0; p->fd_flags[2] = 0;
            for (int j = 3; j < MAX_FD; j++) p->fds[j] = -1;
            for (int j = 3; j < MAX_FD; j++) p->fd_flags[j] = 0;
            p->cwd[0] = '/'; p->cwd[1] = '\0';
            p->waiting = 0;
            p->wait_target = 0;
            p->wait_status_ptr = 0;

            p->wait_is_waitid = 0;
            p->wait_infop_ptr = 0;
            p->wait_idtype = 0;
            p->wait_id = 0;
            p->wait_options = 0;

            p->sleep_until = 0;
            return p;
        }
    }
    return 0;
}

extern void file_close(int fidx);

void proc_free(struct proc *p)
{
    if (!p) return;
    p->state = P_UNUSED;
    p->pid = 0;
    p->ppid = 0;
    p->pgid = 0;
    p->exit_code = 0;
    p->exited_by_signal = 0;
    p->term_signal = 0;
    for (int j = 0; j < MAX_FD; j++) {
        if (p->fds[j] != -1) {
            file_close(p->fds[j]);
            p->fds[j] = -1;
        }
        p->fd_flags[j] = 0;
    }
    p->waiting = 0;
    p->wait_target = 0;
    p->wait_status_ptr = 0;

    p->wait_is_waitid = 0;
    p->wait_infop_ptr = 0;
    p->wait_idtype = 0;
    p->wait_id = 0;
    p->wait_options = 0;

    p->sleep_until = 0;
}

struct proc *proc_find_by_pid(int pid)
{
    if (pid <= 0) return 0;
    for (int i = 0; i < MAX_PROC; i++) {
        if (procs[i].state != P_UNUSED && procs[i].pid == pid) return &procs[i];
    }
    return 0;
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
        if (p->fds[i] == -1) { return i; }
    }
    return -1;
}

void fd_free(struct proc *p, int fd)
{
    if (fd >= 0 && fd < MAX_FD) {
        p->fds[fd] = -1;
        p->fd_flags[fd] = 0;
    }
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
    print("BUG: user_restore returned\n");
    while (1) {}
}

extern void run_next_test(void);

void schedule(void)
{
    while (1) {
        /* Wake sleeping processes if their deadline has passed */
        uint64_t now = get_time_ticks();
        for (int i = 0; i < MAX_PROC; i++) {
            if (procs[i].state == P_SLEEPING && now >= procs[i].sleep_until) {
                procs[i].state = P_RUNNABLE;
            }
        }

        /* Reap orphan zombies to avoid leaking proc slots */
        for (int i = 0; i < MAX_PROC; i++) {
            if (procs[i].state != P_ZOMBIE) continue;
            if (procs[i].ppid == 0) {
                proc_free(&procs[i]);
                continue;
            }
            if (!proc_find_by_pid(procs[i].ppid)) {
                proc_free(&procs[i]);
            }
        }

        struct proc *next = proc_find_runnable_after(current);
        if (next) {
            next->state = P_RUNNING;
            switch_to_user(next);
        }

        /* No runnable process: only start next test if nothing is alive */
        int any_alive = 0;
        for (int i = 0; i < MAX_PROC; i++) {
            if (procs[i].state != P_UNUSED) { any_alive = 1; break; }
        }
        if (!any_alive) {
            run_next_test();
        } else {
            static int dbg_left = 5;
            if (dbg_left > 0) {
                dbg_left--;
                print("schedule: no runnable, alive; cur pid=");
                if (current) print_hex((uint64_t)current->pid); else print_hex(0);
                print(" state=");
                if (current) print_hex((uint64_t)current->state); else print_hex(0);
                print("\n");
            }
        }
    }
}
