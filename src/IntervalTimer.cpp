#include "IntervalTimer.h"

#include <cstdlib>
#include <cstdio> //perror
#include <cerrno> //errno

const itimerval stop_interval{{0, 0},
                              {0, 0}};

IntervalTimer::IntervalTimer(long seconds, long microseconds) {
    this->interval = {{seconds, microseconds},
                      {0,       1000}};

    // Register alarm signal
    sigemptyset(&alarm_signal);
    sigaddset(&alarm_signal, SIGALRM);
}

void IntervalTimer::start() {
    // Create timer: https://stackoverflow.com/questions/25327519/how-to-send-udp-packet-every-1-ms
    if (setitimer(ITIMER_REAL, &(interval), nullptr) < 0) {
        perror("Failed to set timer");
        exit(errno);
    }
}

void IntervalTimer::stop() {
    if (setitimer(ITIMER_REAL, &stop_interval, nullptr) < 0) {
        perror("Failed to stop timer");
        exit(errno);
    }
}
