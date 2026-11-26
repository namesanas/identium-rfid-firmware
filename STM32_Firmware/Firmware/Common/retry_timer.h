#ifndef RETRY_TIMER_H_
#define RETRY_TIMER_H_

#include <stdint.h>

/* Disarm once elapsed: an inactive timer must not become pending again
 * when the millisecond counter crosses its signed half-range. */
typedef struct
{
    uint32_t start;
    uint32_t delay;
    uint8_t armed;
} RetryTimer_t;

static inline void RetryTimer_Arm(RetryTimer_t *timer, uint32_t now, uint32_t delay)
{
    timer->start = now;
    timer->delay = delay;
    timer->armed = (delay != 0U);
}

static inline uint8_t RetryTimer_Ready(RetryTimer_t *timer, uint32_t now)
{
    if(timer->armed && ((uint32_t)(now - timer->start) >= timer->delay))
        timer->armed = 0U;
    return (timer->armed == 0U);
}

#endif
