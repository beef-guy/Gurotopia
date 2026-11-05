#include "pch.hpp"

#include "time.hpp"

u_int ticks()
{
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts); // @note wall clock, so saved tree timers survive a reboot
    return (u_int)(ts.tv_sec);
}
