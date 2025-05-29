#include "ksignal.h"

#include "defs.h"
#include "proc.h"
#include "trap.h"
#include "riscv.h"
#include "string.h"
#include "vm.h"  // for walkaddr, PA_TO_KVA, etc

#include "timer.h"



/**
 * @brief init the signal struct inside a PCB.
 * 
 * @param p 
 * @return int 
 */
int siginit(struct proc *p) {
    memset(&p->signal, 0, sizeof(p->signal));
    return 0;
}

int siginit_fork(struct proc *parent, struct proc *child) {
    child->signal.sigmask = parent->signal.sigmask;
    memmove(child->signal.sa, parent->signal.sa, sizeof(parent->signal.sa));
    memset(child->signal.siginfos, 0, sizeof(child->signal.siginfos));
    child->signal.sigpending = 0;
    return 0;
}

int siginit_exec(struct proc *p) {
    for (int i = 1; i <= SIGMAX; ++i) {
        if (p->signal.sa[i].sa_sigaction != SIG_IGN)
            memset(&p->signal.sa[i], 0, sizeof(sigaction_t));
    }
    return 0;
}

int do_signal(void) {
    assert(!intr_get());
    struct proc *p = curr_proc();  // FIXED

    for (int signo = 1; signo <= SIGMAX; ++signo) {
        if ((p->signal.sigpending & (1ULL << signo)) &&
            !(p->signal.sigmask & (1ULL << signo))) {

            p->signal.sigpending &= ~(1ULL << signo);

            sigaction_t *act = &p->signal.sa[signo];

            if (act->sa_sigaction == SIG_IGN)
                continue;
            else if (act->sa_sigaction == SIG_DFL) {
                setkilled(p, -10 - signo);
                return 0;
            }

            setup_signal_handler(p, signo);
            return 0;
        }
    }

    return 0;
}

int setup_signal_handler(struct proc *p, int signo) {
    struct trapframe *tf = p->trapframe;
    uint64 sp = tf->sp;

    sp -= sizeof(struct ucontext);
    sp &= ~0xfUL;
    struct ucontext *uctx = (struct ucontext *)sp;

    struct ucontext context = {0};
    context.uc_sigmask = p->signal.sigmask;

    for (int i = 0; i < 31; i++) {
        context.uc_mcontext.regs[i] = ((uint64 *)&tf->ra)[i];  // x1-x31
    }
    context.uc_mcontext.epc = tf->epc;
    if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
    if (copy_to_user(p->mm, (uint64)uctx, (char *)&context, sizeof(context)) < 0)
    {
        release(&p->mm->lock);
        return -1;
    }
    sp -= sizeof(siginfo_t);
    siginfo_t *info = (siginfo_t *)sp;
    if (copy_to_user(p->mm, (uint64)info, (char *)&p->signal.siginfos[signo], sizeof(siginfo_t)) < 0)
    {
        release(&p->mm->lock);
        return -1;
    }
        
    release(&p->mm->lock);
    tf->a0 = signo;
    tf->a1 = (uint64)info;
    tf->a2 = (uint64)uctx;
    tf->epc = (uint64)p->signal.sa[signo].sa_sigaction;
    tf->ra  = (uint64)p->signal.sa[signo].sa_restorer;

    p->signal.sigmask |= p->signal.sa[signo].sa_mask;
    p->signal.sigmask |= sigmask(signo);

    tf->sp = sp;
    return 0;
}

int sys_sigaction(int signo, const sigaction_t __user *act, sigaction_t __user *oldact) {
    if (signo == SIGKILL || signo == SIGSTOP) return -1;
    if (signo <= 0 || signo > SIGMAX) return -1;
    struct proc *p = curr_proc();

    if (oldact) {
        if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
        if (copy_to_user(p->mm, (uint64)oldact, (char *)&p->signal.sa[signo], sizeof(sigaction_t)) < 0)
        {
            release(&p->mm->lock);
            return -1;
        }
        release(&p->mm->lock);
    }
    if (act) {
        if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
        if (copy_from_user(p->mm, (char *)&p->signal.sa[signo], (uint64)act, sizeof(sigaction_t)) < 0)
        {
            release(&p->mm->lock);
            return -1;
        }
        release(&p->mm->lock);
    }

    return 0;
}

int sys_sigreturn() {
    struct proc *p = curr_proc();
    return sigreturn(p);
}

int sigreturn(struct proc *p) {
    struct trapframe *tf = p->trapframe;
    uint64 sp = tf->sp + sizeof(siginfo_t);
    struct ucontext context;
    if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
    if (copy_from_user(p->mm, (char *)&context, sp, sizeof(context)) < 0)
    {
        release(&p->mm->lock);
        return -1;
    }
    release(&p->mm->lock);
    for (int i = 0; i < 31; i++) {
        ((uint64 *)&tf->ra)[i] = context.uc_mcontext.regs[i];
    }
    tf->epc = context.uc_mcontext.epc;

    p->signal.sigmask = context.uc_sigmask;
    tf->sp = sp + sizeof(context);
    return 0;
}

int sys_sigprocmask(int how, const sigset_t __user *set, sigset_t __user *oldset) {
    struct proc *p = curr_proc();
    if (oldset) {
        if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
        if (copy_to_user(p->mm, (uint64)oldset, (char *)&p->signal.sigmask, sizeof(sigset_t)) < 0)
        {
            release(&p->mm->lock);
            return -1;
        }
        release(&p->mm->lock);
    }
    if (set) {
        sigset_t new;
        if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
        if (copy_from_user(p->mm, (char *)&new, (uint64)set, sizeof(sigset_t)) < 0)
        {
            release(&p->mm->lock);
            return -1;
        }
        release(&p->mm->lock);

        if (how == SIG_BLOCK)
            p->signal.sigmask |= new;
        else if (how == SIG_UNBLOCK)
            p->signal.sigmask &= ~new;
        else if (how == SIG_SETMASK)
            p->signal.sigmask = new;
        else
            return -1;
    }
    return 0;
}

int sys_sigpending(sigset_t __user *set) {
    struct proc *p = curr_proc();
    sigset_t result = p->signal.sigpending & p->signal.sigmask;
    if (!holding(&p->mm->lock)) acquire(&p->mm->lock);
    if (copy_to_user(p->mm, (uint64)set, (char *)&result, sizeof(sigset_t)) < 0)
    {
        release(&p->mm->lock);
        return -1;
    }
    release(&p->mm->lock);
    return 0;
}

int sys_sigkill(int pid, int signo, int code) {
    if (signo <= 0 || signo > SIGMAX)
        return -1;

    struct proc *target = 0;
    struct proc *sender = curr_proc();
    // 遍历全局进程表，查找目标进程
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = pool[i];
        acquire(&p->lock);
        if (p->pid == pid) {
            target = p;
            release(&p->lock);
            break;
        }
        release(&p->lock);
    }

    if (!target) return -1;

    if (signo == SIGKILL || signo == SIGSTOP) {
        setkilled(target, -10 - signo);
        if (holding(&target->lock)) release(&target->lock);
        return 0;
    }

    acquire(&target->lock);
    target->signal.sigpending |= sigmask(signo);

    siginfo_t *info = &target->signal.siginfos[signo];
    memset(info, 0, sizeof(siginfo_t));
    info->si_signo = signo;
    info->si_code = code;
    info->si_pid = sender->pid;
    release(&target->lock);
    return 0;
}

int sys_alarm(int seconds) {
    struct proc *p = curr_proc();
    int old = 0;

    acquire(&p->lock);
    infof("[kernel] sys_alarm: seconds=%d, ticks=%d", seconds, ticks);
    if (p->signal.alarm_ticks > 0 && ticks >= p->signal.alarm_start_ticks) {
        int remaining = (p->signal.alarm_start_ticks + p->signal.alarm_ticks - ticks) / TICKS_PER_SEC;
        old = remaining > 0 ? remaining : 0;
    }

    if (seconds == 0) {
        p->signal.alarm_ticks = 0;  // 取消 alarm
    } else {
        p->signal.alarm_ticks = seconds * TICKS_PER_SEC;
        p->signal.alarm_start_ticks = ticks;
    }

    release(&p->lock);
    return old;
}


