#ifndef PACKET_GENERATOR_INTERVALTIMER_H
#define PACKET_GENERATOR_INTERVALTIMER_H

#include <sys/time.h>
#include <csignal>

/**
 * Timer that will unlock at a specific interval.
 * Causes an uncaught SIGALRM signal if the timer unlocks and no-one is waiting.
 */
class IntervalTimer {
private:
    /**
     * Stores the interval to wait for.
     */
    struct itimerval interval{{0, 0},
                              {0, 0}};
    /**
     * Stores the signal to wait for.
     */
    sigset_t alarm_signal{};
    /**
     * Stores the signal number that triggered an unlock.
     */
    int signum{0};

public:
    /**
     * Create an IntervalTimer, but do not start it.
     * @param seconds Seconds in each interval.
     * @param microseconds Microseconds in each interval.
     */
    IntervalTimer(long seconds, long microseconds);

    /**
     * Start the timer.
     * First unlock will happen in 1000 microseconds.
     * Can be used to resume a previously stopped timer.
     */
    void start();

    /**
     * Stop the timer.
     */
    static void stop();

    /**
     * Blocking call that waits until the next time the timer unlocks.
     */
    void await() {
        sigwait(&alarm_signal, &signum);
    }
};

#endif //PACKET_GENERATOR_INTERVALTIMER_H
