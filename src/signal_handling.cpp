#include "signal_handling.h"

#include <csignal>
#include <cstdio>
#include <unistd.h>

extern volatile uint32_t packet_num;
volatile bool keyboard_interrupt{false};

void keyboard_interrupt_handler([[maybe_unused]]int signum) {
    keyboard_interrupt = true;
}

void missed_alarm_handler([[maybe_unused]]int signum) {
    packet_num++;
    if (write(1, "Missed alarm!\n", 14) < 0) {
        perror("Failed to write to stdout");
    }
}

void register_handlers() {
    // Handle keyboard interrupts
    std::signal(SIGINT, keyboard_interrupt_handler);
    std::signal(SIGTERM, keyboard_interrupt_handler);

    // Handle missed alarms
    std::signal(SIGALRM, missed_alarm_handler);
}
