#include "../../os/ktest/ktest.h"
#include "../lib/user.h"
#include "signal-project-tests/basic.c"

int test_passed = 0;
int test_failed = 0;

void count_pass(const char *name) {
    printf("✓ PASS: %s\n", name);
    test_passed++;
}

void count_fail(const char *name) {
    printf("✗ FAIL: %s\n", name);
    test_failed++;
}

// ================= ALARM TEST 1 =================
// SIGALRM handler triggers within tight loop
int triggered1 = 0;
void alarm1_handler(int sig, siginfo_t *info, void *ctx) {
    printf(">> [alarm1_handler] SIGALRM triggered\n");
    triggered1 = 1;
    exit(42);
}

void alarm1() {
    int pid = fork();
    if (pid == 0) {
        sigaction_t sa = {
            .sa_sigaction = alarm1_handler,
            .sa_restorer  = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, 0);

        alarm(2);

        while (1); 
    } else {
        int status;
        wait(0, &status);
        if (status == 42) {
            count_pass("alarm1 (trigger in busy loop)");
        } else {
            count_fail("alarm1 (trigger in busy loop)");
        }
    }
}

// ================= ALARM TEST 2 =================
// Check return value of alarm()
void alarm2() {
    int old = alarm(5);
    if (old != 0) {
        count_fail("alarm2 (first set should return 0)");
        return;
    }

    old = alarm(3); // reset before first one expires
    if (old != 5) {
        count_fail("alarm2 (second set should return 5)");
        return;
    }

    alarm(0); // cancel

    count_pass("alarm2 (return value correct)");
}

// ================= ALARM TEST 3 =================
// Cancel alarm before trigger
void alarm3_handler(int sig, siginfo_t *info, void *ctx) {
    printf(">> [alarm3_handler] SHOULD NOT BE CALLED!\n");
    exit(-1);
}

void alarm3() {
    int pid = fork();
    if (pid == 0) {
        sigaction_t sa = {
            .sa_sigaction = alarm3_handler,
            .sa_restorer = sigreturn,
        };
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, 0);

        alarm(2);
        for (volatile int i = 0; i < 100000000; i++); // burn some time
        alarm(0); // cancel alarm
        for (volatile int i = 0; i < 100000000; i++); // wait more

        exit(99); // expected normal exit
    } else {
        int status;
        wait(0, &status);
        if (status == 99) {
            count_pass("alarm3 (cancel before trigger)");
        } else {
            count_fail("alarm3 (cancel before trigger)");
        }
    }
}

// ================= MAIN =================
int main(int argc, char *argv[]) {
    printf("== alarm system call tests ==\n");

    alarm1();
    alarm2();
    alarm3();

    printf("\n[Result] %d passed, %d failed.\n", test_passed, test_failed);
    return test_failed > 0 ? 1 : 0;
}
