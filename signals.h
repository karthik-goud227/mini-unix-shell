#ifndef MSH_SIGNALS_H
#define MSH_SIGNALS_H

int signals_init(void);
int signals_event_fd(void);
void signals_drain(void);

int signals_take_interrupt(void);
void signals_close(void);

#endif
