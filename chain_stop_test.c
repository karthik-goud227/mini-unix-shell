
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "chain.c"

int main(void)
{
    fflush(NULL);
    pid_t supervisor = fork();

    if (supervisor == -1) {
        perror("fork supervisor");
        return 1;
    }

    if (supervisor == 0) {
        Process stage[1] = {0};
        stage[0].pid = fork();

        if (stage[0].pid == -1) {
            _exit(90);
        }

        if (stage[0].pid == 0) {
            raise(SIGSTOP);
            _exit(5);
        }

        int result = chain_wait_pipeline(stage, 1);

        if (result != 5 || !stage[0].completed
            || !stage[0].has_usage) {
            _exit(91);
        }

        errno = 0;
        if (waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD) {
            _exit(92);
        }

        _exit(5);
    }

    int status;
    pid_t result;
    do {
        result = waitpid(supervisor, &status, WUNTRACED);
    } while (result == -1 && errno == EINTR);

    int stopped = result == supervisor
        && WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP;

    printf("%s: supervisor stops when its unfinished stage stops\n",
           stopped ? "PASS" : "FAIL");

    if (!stopped) {
        return 1;
    }

    int resumed = kill(supervisor, SIGCONT) == 0;
    printf("%s: supervisor accepts SIGCONT\n",
           resumed ? "PASS" : "FAIL");

    if (!resumed) {
        return 1;
    }

    do {
        result = waitpid(supervisor, &status, 0);
    } while (result == -1 && errno == EINTR);

    int completed = result == supervisor
        && WIFEXITED(status) && WEXITSTATUS(status) == 5;

    printf("%s: resumed supervisor continues and reaps its stage\n",
           completed ? "PASS" : "FAIL");

    int failures = completed ? 0 : 1;
    printf("\nChecks: 3, failures: %d\n", failures);
    return failures ? 1 : 0;
}
