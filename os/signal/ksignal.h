#ifndef __KSIGNAL_H__
#define __KSIGNAL_H__

#include <vm.h>
#include "signal.h"
#include <string.h>

struct ksignal {
    sigaction_t sa[SIGMAX + 1];
    siginfo_t siginfos[SIGMAX + 1];
    sigset_t sigmask;       // signal mask, when set to 1, the signal is blocked
    sigset_t sigpending;

    uint64 alarm_start_ticks;   // 设置 alarm 时的 ticks 值
    uint64 alarm_ticks;         // 当前 alarm 持续 tick 数，0 表示未设置

};

struct proc;  // forward declaration
int siginit(struct proc *p);
int siginit_fork(struct proc *parent, struct proc* child);
int siginit_exec(struct proc *p);

int do_signal(void);

// syscall handler:
int sys_sigaction(int signo, const sigaction_t __user *act, sigaction_t __user *oldact);
int sys_sigreturn();
int sys_sigprocmask(int how, const sigset_t __user *set, sigset_t __user *oldset);
int sys_sigpending(sigset_t __user *set);
int sys_sigkill(int pid, int signo, int code);
int sigreturn(struct proc *p);
int setup_signal_handler(struct proc *p, int signo);
int sys_alarm(int seconds);
#endif