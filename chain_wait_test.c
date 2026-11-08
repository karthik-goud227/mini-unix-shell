
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

/* Include the unfinished module to test its private wait helper. */
#include "chain.c"

static int checks;
static int failures;

static void check(const char *name, int passed)
{
    checks++;
    printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) {
        failures++;
    }
}

static pid_t start_child(int exit_code, int signal_number)
{
    pid_t child = fork();

    if (child == 0) {
        if (signal_number != 0) {
            signal(signal_number, SIG_DFL);
            raise(signal_number);
        }
        _exit(exit_code);
    }

    return child;
}

int main(void)
{
    Process single[1] = {0};
    single[0].pid = start_child(7, 0);
    if (single[0].pid == -1) {
        perror("fork");
        return 1;
    }

    int status = chain_wait_pipeline(single, 1);
    check("single child exit status preserved", status == 7);
    check("completed child has final resource measurements",
          single[0].completed && single[0].has_usage);

    Process stages[2] = {0};
    stages[0].pid = start_child(3, 0);
    if (stages[0].pid == -1) {
        perror("fork");
        return 1;
    }

    stages[1].pid = start_child(9, 0);
    if (stages[1].pid == -1) {
        perror("fork");
        waitpid(stages[0].pid, NULL, 0);
        return 1;
    }

    status = chain_wait_pipeline(stages, 2);
    check("last stage determines pipeline status", status == 9);
    check("both stages collected before return",
          stages[0].completed && stages[1].completed);
    check("both stages have resource measurements",
          stages[0].has_usage && stages[1].has_usage);

    Process terminated[1] = {0};
    terminated[0].pid = start_child(0, SIGTERM);
    if (terminated[0].pid == -1) {
        perror("fork");
        return 1;
    }

    status = chain_wait_pipeline(terminated, 1);
    check("signal termination returns 128 plus signal",
          status == 128 + SIGTERM);
    check("signal-terminated child marked completed",
          terminated[0].completed
          && WIFSIGNALED(terminated[0].wait_status));

    errno = 0;
    pid_t remaining = waitpid(-1, NULL, WNOHANG);
    check("all test children reaped",
          remaining == -1 && errno == ECHILD);

    printf("\nChecks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
