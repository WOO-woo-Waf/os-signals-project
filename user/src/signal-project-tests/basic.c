#include "../../os/ktest/ktest.h"
#include "../lib/user.h"

// Base Checkpoint 1: sigaction, sigkill, and sigreturn

// send SIGUSR0 to a child process, which default action is to terminate it.
void basic1(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sleep(10);
        exit(1);
    } else {
        // parent
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGUSR0);
    }
}

// send SIGUSR0 to a child process, but should be ignored.
void basic2(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = SIG_IGN,
            .sa_mask      = 0,
            .sa_restorer  = NULL,
        };
        sigaction(SIGUSR0, &sa, 0);
        sleep(10);
        sleep(10);
        sleep(10);
        exit(1);
    } else {
        // parent
        sleep(5);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert(ret == 1);
    }
}

void handler3(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    getpid();
    sleep(1);
    exit(103);
}

// set handler for SIGUSR0, which call exits to terminate the process.
//  this handler will not return, so sigreturn should not be called.
void basic3(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler3,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (1);
        exit(1);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 103);
    }
}

volatile int handler4_flag = 0;
void handler4(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    sleep(1);
    sleep(1);
    fprintf(1, "handler4 triggered\n");
    handler4_flag = 1;
}

// set handler for SIGUSR0, and return from handler.
void basic4(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler4,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (handler4_flag == 0);
        exit(104);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 104);
    }
}

static volatile int handler5_cnt = 0;
void handler5(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    static volatile int nonreentrace = 0;
    assert(!nonreentrace);    // non-reentrance
    nonreentrace = 1;
    sleep(5);
    sleep(5);
    if (handler5_cnt < 5)
        sigkill(getpid(), SIGUSR0, 0);
    sleep(5);
    sleep(5);
    fprintf(1, "handler5 triggered\n");
    nonreentrace = 0;
    handler5_cnt++;
}

// signal handler itself should not be reentrant.
//  when the signal handler is called for SIGUSR0, it should block all SIGUSR0.
//  after the signal handler returns, the signal should be unblocked.
//   then, the signal handler should be called again. (5 times)
// set handler for SIGUSR0, kernel should block it from re-entrance.
void basic5(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler5,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        while (handler5_cnt < 5);
        exit(105);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 105);
    }
}

volatile int handler6_flag = 0;
void handler6(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    handler6_flag = 1;
    fprintf(1, "handler6 triggered due to %d\n", signo);
    sleep(30);
    assert(handler6_flag == 2);
    handler6_flag = 3;
}

void handler6_2(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR1);
    assert(handler6_flag == 1);
    handler6_flag = 2;
    fprintf(1, "handler6_2 triggered due to %d\n", signo);
}

// signal handler can be nested.
void basic6(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler6,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR0, &sa, 0);
        sigaction_t sa2 = {
            .sa_sigaction = handler6_2,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa2.sa_mask);
        sigaction(SIGUSR1, &sa2, 0);
        while (handler6_flag != 3);
        exit(106);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        sleep(5);
        sigkill(pid, SIGUSR1, 0);
        sleep(5);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 106);
    }
}

volatile int handler7_flag = 0;
void handler7(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR0);
    handler7_flag = 1;
    fprintf(1, "handler7 triggered due to %d\n", signo);
    sleep(30);
    sigset_t pending;
    sigpending(&pending);
    assert_eq(pending, sigmask(SIGUSR1));
    assert(handler7_flag == 1); // handler7 should not interrupted by SIGUSR1 (handler7_2)
    handler7_flag = 2;
}

void handler7_2(int signo, siginfo_t* info, void* ctx2) {
    assert(signo == SIGUSR1);
    assert(handler7_flag == 2);
    handler7_flag = 3;
    fprintf(1, "handler7_2 triggered due to %d\n", signo);
}

// signal handler can be nested.
void basic7(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler7,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, SIGUSR1); // block SIGUSR1 when handling SIGUSR0
        sigaction(SIGUSR0, &sa, 0);

        sigaction_t sa2 = {
            .sa_sigaction = handler7_2,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa2.sa_mask);
        sigaction(SIGUSR1, &sa2, 0);

        while (handler7_flag != 3);
        exit(107);
    } else {
        // parent
        sleep(10);
        sigkill(pid, SIGUSR0, 0);
        sleep(5);
        sigkill(pid, SIGUSR1, 0);
        sleep(5);
        int ret;
        wait(0, &ret);
        assert_eq(ret, 107);
    }
}

// SIG_IGN and SIG_DFL
void basic8(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = SIG_IGN,
            .sa_restorer  = NULL,
        };
        sigaction(SIGUSR0, &sa, 0);
        sigkill(getpid(), SIGUSR0, 0); // should have no effect

        sigaction_t sa2 = {
            .sa_sigaction = SIG_DFL,
            .sa_restorer  = NULL,
        };
        sigaction(SIGUSR1, &sa2, 0);
        sigkill(getpid(), SIGUSR1, 0); // should terminate the process

        exit(1);
    } else {
        // parent
        sigkill(pid, SIGUSR0, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGUSR1); // child terminated by SIGUSR1
    }
}


// Base Checkpoint 2: SIGKILL

void handler10(int signo, siginfo_t* info, void* ctx2) {
    exit(2);
}

// child process is killed by signal: SIGKILL, which cannot be handled, ignored and blocked.
void basic10(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigaction_t sa = {
            .sa_sigaction = handler10,
            .sa_restorer  = NULL,
        };
        sigaction(SIGKILL, &sa, 0); 
        // set handler for SIGKILL, which should not be called
        while (1);
        exit(1);
    } else {
        // parent
        sleep(20);
        sigkill(pid, SIGKILL, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGKILL);
    }
}

// child process is killed by signal: SIGKILL, which cannot be handled, ignored and blocked.
void basic11(char* s) {
    int pid = fork();
    if (pid == 0) {
        // child
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGKILL);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        // set handler for SIGKILL, which should not be called
        while (1);
        exit(1);
    } else {
        // parent
        sleep(20);
        sigkill(pid, SIGKILL, 0);
        int ret;
        wait(0, &ret);
        assert(ret == -10 - SIGKILL);
    }
}

// Base Checkpoint 3: signals under fork & exec

void basic20(char *s) {
    // our modification does not affect our parent process.
    // because `run` method in the testsuite will do fork for us.

    sigaction_t sa = {
        .sa_sigaction = SIG_IGN,
        .sa_restorer  = NULL,
    };
    sigaction(SIGUSR0, &sa, 0);
    // ignore SIGUSR0.

    int pid = fork();
    if (pid == 0) {
        // child
        sigkill(getpid(), SIGUSR0, 0); 
        // should have no effect, because parent ignores it.
        exit(1);
    } else {
        // parent
        int ret;
        wait(0, &ret);
        assert(ret == 1); // child should not be terminated by SIGUSR0
    }
}

// basic12: alarm 触发 handler
void alarm_handler_basic12(int sig, siginfo_t *info, void *ctx) {
    printf("[basic12] SIGALRM received\n");
    exit(99);  // 用于父进程判断 handler 被执行
}

void basic12(char *s) {
    int pid = fork();
    if (pid == 0) {
        sigaction_t sa = {
            .sa_sigaction = alarm_handler_basic12,
            .sa_restorer = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGALRM, &sa, 0) < 0) {
            printf("[basic12] sigaction failed\n");
            exit(111);
        }

        alarm(1);      // 设置 1 秒后触发
        sleep(5);      // 等待足够时间
        exit(1);       // 如果没被 handler 中断，则失败
    } else {
        int status;
        wait(0, &status);
        if (status == 99)
            printf("[basic12] OK\n");
        else
            printf("[basic12] FAILED (status=%d)\n", status);
    }
}

// basic13: alarm 取消
void alarm_handler_basic13(int sig, siginfo_t *info, void *ctx) {
    printf("[basic13] handler should NOT be called\n");
    exit(200);  // 如果触发了说明失败
}

void basic13(char *s) {
    int pid = fork();
    if (pid == 0) {
        sigaction_t sa = {
            .sa_sigaction = alarm_handler_basic13,
            .sa_restorer = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, 0);

        alarm(2);      // 设置 alarm
        sleep(1);      // 在它触发前取消
        alarm(0);      // 取消 alarm
        sleep(3);      // 等待看是否触发
        exit(123);     // handler 没触发则成功
    } else {
        int status;
        wait(0, &status);
        if (status == 123)
            printf("[basic13] OK\n");
        else
            printf("[basic13] FAILED (status=%d)\n", status);
    }
}

// basic.c
void siginfo_test1_handler(int signo, siginfo_t *info, void *ctx) {
    printf("Received signal %d from PID %d\n", info->si_signo, info->si_pid);
    assert(info->si_signo == SIGUSR1);
    assert(info->si_pid == getppid());  // 应来自父进程
    assert(info->si_code == 123);       // 验证自定义代码
    exit(0);
}

void siginfo_test1(char* s) {
    int pid = fork();
    if (pid == 0) {
        // 子进程
        sigaction_t sa = {
            .sa_sigaction = siginfo_test1_handler,
            .sa_restorer = sigreturn
        };
        sigaction(SIGUSR1, &sa, 0);
        while(1) sleep(1);  // 等待信号
    } else {
        // 父进程
        sleep(1);
        sigkill(pid, SIGUSR1, 123);  // 发送带自定义代码的信号
        int status;
        wait(0, &status);
        assert(status == 0);  // 验证子进程正常退出
    }
}

// basic.c
void siginfo_test2_handler(int signo, siginfo_t *info, void *ctx) {
    printf("Segfault at %p from kernel\n", info->addr);
    assert(info->si_signo == SIGKILL);
    assert(info->si_pid == -1);       // 内核发送的信号
    assert(info->addr == (void*)0xDEADBEEF);  // 验证错误地址
}

void siginfo_test2(char* s) {
    int pid = fork();
    if (pid == 0) {
        // 子进程
        sigaction_t sa = {
            .sa_sigaction = siginfo_test2_handler,
            .sa_restorer = sigreturn
        };
        sigaction(SIGUSR1, &sa, 0);
        
        // 故意触发页错误
        *(volatile int*)0xDEADBEEF = 42;
        while(1);  // 不应执行到这里
    } else {
        // 父进程
        int status;
        wait(0, &status);
        assert(status == -10 - SIGKILL);  // 验证信号处理正常
    }
}

// basic.c
volatile int siginfo_test3_count = 0;
void siginfo_test3_handler(int signo, siginfo_t *info, void *ctx) {
    printf("Signal %d from %d (count=%d)\n", 
           info->si_signo, info->si_pid, siginfo_test3_count);
    
    if (siginfo_test3_count == 0) {
        assert(info->si_pid == getppid());  // 第一次来自父进程
    } else {
        assert(info->si_pid == getpid());   // 后续来自自身
    }
    
    if (++siginfo_test3_count < 3) {
        sigkill(getpid(), SIGUSR1, 0);  // 再次发送信号
    } else {
        exit(0);
    }
}

void siginfo_test3(char* s) {
    int pid = fork();
    if (pid == 0) {
        // 子进程
        sigaction_t sa = {
            .sa_sigaction = siginfo_test3_handler,
            .sa_restorer = sigreturn
        };
        sigaction(SIGUSR1, &sa, 0);
        while(siginfo_test3_count < 3) sleep(1);
    } else {
        // 父进程
        sleep(1);
        sigkill(pid, SIGUSR1, 0);  // 发送初始信号
        int status;
        wait(0, &status);
        assert(status == 0);
    }
}


void siginfo_test4_handler(int signo, siginfo_t *info, void *ctx) {
    printf("Special signal %d from %d\n", info->si_signo, info->si_pid);
    assert(info->si_signo == SIGTERM);
    assert(info->si_pid == getppid());
    exit(0);
}

void siginfo_test4(char* s) {
    int pid = fork();
    if (pid == 0) {
        // 子进程
        sigaction_t sa = {
            .sa_sigaction = siginfo_test4_handler,
            .sa_restorer = sigreturn
        };
        sigaction(SIGTERM, &sa, 0);
        while(1) sleep(1);
    } else {
        // 父进程
        sleep(1);
        sigkill(pid, SIGTERM, 456);  // 发送特殊信号
        int status;
        wait(0, &status);
        assert(status == 0);
    }
}