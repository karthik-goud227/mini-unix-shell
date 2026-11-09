#ifndef MSH_TERMINAL_H
#define MSH_TERMINAL_H

#include <sys/types.h>

int terminal_init(void);
int terminal_init_mode(int allow_interactive);
int terminal_is_interactive(void);
int terminal_give_to(pid_t pgid);
int terminal_reclaim(void);
int terminal_prepare_child(void);

#endif
