#ifndef UDSIM_TIMER_H
#define UDSIM_TIMER_H
#include <functional>

void timer_start(std::function<bool(void)> func, unsigned int interval);

bool can_periodic_send(void); // start in different thread

#endif

