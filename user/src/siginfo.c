#include "../../os/ktest/ktest.h"
#include "../lib/user.h"
#include "signal-project-tests/basic.c"
// basic 1-3

struct test {
    void (*f)(char *);
    char *s;
} signaltests[] = {
    {siginfo_test1, "siginfo_test1"},
    {siginfo_test2, "siginfo_test2"},
    {siginfo_test3, "siginfo_test3"},
    {siginfo_test4, "siginfo_test4"},
    {NULL,    NULL     },
};

int run(void f(char *), char *s) {
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if ((pid = fork()) < 0) {
        printf("runtest: fork error\n");
        exit(1);
    }
    if (pid == 0) {
        f(s);
        exit(0);
    } else {
        wait(-1, &xstatus);
        if (xstatus != 0)
            printf("FAILED\n");
        else
            printf("OK\n");
        return xstatus == 0;
    }
}

int runtests(struct test *tests, char *whichone, int continuous) {
    for (struct test *t = tests; t->s != 0; t++) {
        if ((whichone == NULL) || strcmp(t->s, whichone) == 0) {
            if (!run(t->f, t->s)) {
                if (continuous != 2) {
                    printf("SOME TESTS FAILED\n");
                    return 1;
                }
            }
        }
    }
    return 0;
}

int drivetests(int continuous, char *whichone) {
    do {
        printf("signaltests starting\n");
        int freepg  = ktest(KTEST_GET_NRFREEPGS, 0, 0);
        int freebuf = ktest(KTEST_GET_NRSTRBUF, 0, 0);
        if (runtests(signaltests, whichone, continuous)) {
            if (continuous != 2) {
                return 1;
            }
        }
        int freepg1  = ktest(KTEST_GET_NRFREEPGS, 0, 0);
        int freebuf1 = ktest(KTEST_GET_NRSTRBUF, 0, 0);
        if (freepg1 < freepg || freebuf < freebuf1) {
            printf("FAILED -- lost some free pages %d (out of %d), kstrbuf: %d (out of %d)\n", freepg1, freepg, freebuf1, freebuf);
            if (continuous != 2) {
                return 1;
            }
        }
    } while (continuous);
    return 0;
}

int main(int argc, char *argv[]) {
    printf("=== TESTSUITE ===\n- Project: signal test suite\n");
    printf(" Usage: ./signal [testname]\n");
    printf(" - [testname] can be one of the following:\n");
    for (struct test *t = signaltests; t->s != 0; t++) {
        printf("  %s\n", t->s);
    }
    printf("\n");

    if (argc > 1) {
        printf("Running test %s\n", argv[1]);
        return drivetests(0, argv[1]);
    } else {
        printf("Running all tests\n");
        drivetests(0, NULL);
    }
    return 0;
}