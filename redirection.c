#define _POSIX_C_SOURCE 200809L

#include "redirection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int apply_redirections(const Command *command)
{
    for (size_t i = 0; i < command->redirection_count; i++) {
        const Redirection *redirect = &command->redirections[i];
        int target;
        int flags;

        switch (redirect->type) {
        case REDIRECT_IN:
            target = STDIN_FILENO;
            flags = O_RDONLY;
            break;

        case REDIRECT_OUT:
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;

        case REDIRECT_APPEND:
            target = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_APPEND;
            break;

        default:
            fprintf(stderr, "msh: invalid redirection type\n");
            return -1;
        }

        int fd = open(redirect->path, flags, 0666);

        if (fd == -1) {
            fprintf(stderr, "msh: %s: %s\n",
                    redirect->path, strerror(errno));
            return -1;
        }

        if (fd != target) {
            if (dup2(fd, target) == -1) {
                int saved_errno = errno;
                close(fd);
                fprintf(stderr, "msh: redirection: %s\n",
                        strerror(saved_errno));
                return -1;
            }

            close(fd);
        }
    }

    return 0;
}
