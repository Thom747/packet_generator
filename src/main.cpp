#include "argparse.h"
#include "constants.h"
#include "IntervalTimer.h"
#include "signal_handling.h"

#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>


struct arguments {
    std::string dest_ip;
    unsigned int dest_port;
    double packet_freq;
    unsigned int packet_size;
    uint8_t packet_dscp;
    unsigned int timeout;
    bool verbose;
    std::string interface;
    uint8_t label_byte;
    bool csv;
};

volatile uint32_t packet_num{0};
extern volatile bool keyboard_interrupt;
uint32_t successful_packet_num{0};
const int socket_fd{socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
sockaddr_in out_addr{};
void *msg_buffer;

auto parse_args(int argc,
                char *argv[]) -> struct arguments { // NOLINT(modernize-avoid-c-arrays) // Disabled as argv has to be of dynamic length
    // Register arguments
    argparse::ArgumentParser parser("Packet Generator");
    parser.add_description("Send UDP packets to a destination at a specific frequency.\n"
                           "Packet structure:\n"
                           "Label byte               (1B)\n"
                           "Packet sequence number   (4B)\n"
                           "Padding zero bytes       (remaining bytes)");
    parser.add_argument("dest_IP").help("IPv4 address to send packets to");
    parser.add_argument("dest_port").help("Port to send packets to").scan<'u', unsigned int>();
    parser.add_argument("packet_freq").help("Frequency in Hz to send packets").scan<'f', double>();
    parser.add_argument("packet_size").help("Size of packet payload in bytes").scan<'u', unsigned int>();
    parser.add_argument("packet_dscp").help(
            "IP DSCP code for packet, see https://www.speedguide.net/articles/quality-of-service-tos-dscp-wmm-3477").scan<'u', uint8_t>();
    parser.add_argument("-t", "--timeout").help(
            "Timeout to send packets for in whole seconds. If omitted or 0, runs indefinitely.").nargs(1).default_value(
            (unsigned int) 0).scan<'u', unsigned int>();
    parser.add_argument("-v", "--verbose").help("Print debugging information").default_value(false).implicit_value(
            true);
    parser.add_argument("-i", "--interface").help("Interface to send packets over").nargs(1).default_value(
            (std::string) "");
    parser.add_argument("-l", "--label").help("Byte to label transmissions with").nargs(1).default_value(
            (uint8_t) 0).scan<'u', uint8_t>();
    parser.add_argument("-c", "--csv").help("Output packet start and end times in csv format").default_value(
            false).implicit_value(true);

    // Attempt to parse the arguments provided
    try {
        parser.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        std::exit(1);
    }

    uint8_t dscp{(uint8_t) (parser.get<uint8_t>("packet_dscp") << 2)};
    struct arguments res{parser.get("dest_IP"), parser.get<unsigned int>("dest_port"),
                         parser.get<double>("packet_freq"), parser.get<unsigned int>("packet_size"), dscp,
                         parser.get<unsigned int>("--timeout"), parser.get<bool>("--verbose"),
                         parser.get("--interface"), parser.get<uint8_t>("--label"), parser.get<bool>("--csv")};

    if (res.verbose) {
        std::cout << "Sending UDP packets to " << res.dest_ip << ":" << res.dest_port << " at " << res.packet_freq
                  << "Hz." << std::endl;
        std::cout << "Packet size is " << res.packet_size << "B, DSCP is " << (unsigned int) (res.packet_dscp >> 2)
                  << ", and label is " << (unsigned int) res.label_byte << "." << std::endl;
        if (res.timeout)
            std::cout << "Timeout in " << res.timeout << " seconds." << std::endl;
        else
            std::cout << "Timeout not specified, running indefinitely." << std::endl;
        if (!res.interface.empty()) {
            std::cout << "Binding to interface " << res.interface << "." << std::endl;
        } else {
            std::cout << "Not bound to an interface." << std::endl;
        }
    }

    return res;
}

auto inline await_and_send(const struct arguments &args, IntervalTimer &intervalTimer) -> int {
    // Wait for interrupt
    intervalTimer.await();

    // Fill buffer with packet_num
    packet_num++;
    const uint32_t my_packet_num{packet_num};
    {
        const uint32_t network_packet_num{htonl(my_packet_num)};
        std::memcpy(&(((char *) msg_buffer)[1]), &network_packet_num, 4);
    }

    // Send packet
    auto pre_send_timestamp = std::chrono::system_clock::now();
    auto retval = sendto(socket_fd, msg_buffer, args.packet_size, 0, (sockaddr *) &out_addr, sizeof(out_addr));
    auto post_send_timestamp = std::chrono::system_clock::now();
    if (retval < 0) {
        perror("Failed to send following packet");
    } else {
        successful_packet_num++;
    }

    // Report start and end times for transmit call
    if (args.csv) {
        std::cout << my_packet_num << ", " << double(std::chrono::duration_cast<std::chrono::microseconds>(
                pre_send_timestamp.time_since_epoch()).count()) / S_TO_US << ", " <<
                  double(std::chrono::duration_cast<std::chrono::microseconds>(
                          post_send_timestamp.time_since_epoch()).count()) / S_TO_US << std::endl;
    } else {
        std::cout << "Sent packet " << my_packet_num << ": start " <<
                  double(std::chrono::duration_cast<std::chrono::microseconds>(
                          pre_send_timestamp.time_since_epoch()).count()) / S_TO_US << ", end " <<
                  double(std::chrono::duration_cast<std::chrono::microseconds>(
                          post_send_timestamp.time_since_epoch()).count()) / S_TO_US << std::endl;
    }

    return 0;
}


void report_stats(std::chrono::duration<double, std::micro> duration) {
    double successful_percent = successful_packet_num * 100.0 / packet_num;
    std::cout << "Ran for " << duration.count() / S_TO_US << " seconds." << std::endl << "Attempted to send "
              << packet_num << " packets, of which " << successful_packet_num << " (" << successful_percent
              << "%) were successful." << std::endl << "Attempt frequency: "
              << packet_num / (duration.count() / S_TO_US) << "Hz." << std::endl << "Successful attempt frequency: "
              << successful_packet_num / (duration.count() / S_TO_US) << "Hz." << std::endl;
    if (successful_percent < 95) {
        std::cerr << "Less than 95% successful, aborting..." << std::endl;
        exit(-95);
    }
}

auto set_and_start_timer(const struct arguments &args) -> int {
    long us_per_packet{(long) floor(S_TO_US / args.packet_freq)};
    const long s_per_packet{(long) us_per_packet / S_TO_US};
    us_per_packet -= s_per_packet * S_TO_US;
    if (args.verbose) {
        std::cout << "Sending packets every " << (double) s_per_packet + ((double) us_per_packet) / S_TO_US
                  << " seconds." << std::endl;
    }

    register_handlers();

    // Upgrade process to RT
    if (geteuid() == 0) {
        if (args.verbose) {
            std::cout << "Running as root, switching to SCHED_FIFO scheduler.\n" << std::endl;
        }
        const struct sched_param schedParam = {sched_get_priority_max(SCHED_FIFO)};
        if (sched_setscheduler(0, SCHED_FIFO, &schedParam) < 0) {
            perror("Failed to change to SCHED_FIFO scheduler");
            exit(errno);
        }
    } else if (args.verbose) {
        std::cout << "Not running as root, using default scheduler.\n" << std::endl;
    }

    std::chrono::duration<double, std::micro> diff{0};
    IntervalTimer intervalTimer{s_per_packet, us_per_packet};
    intervalTimer.start();

    if (args.timeout) {
        const std::chrono::duration<double, std::micro> timeout_duration{args.timeout * S_TO_US};
        const auto start_time = std::chrono::high_resolution_clock::now();

        do {
            await_and_send(args, intervalTimer);
            diff = std::chrono::high_resolution_clock::now() - start_time;
        } while (!keyboard_interrupt && diff < timeout_duration);

    } else {
        const auto start_time = std::chrono::high_resolution_clock::now();
        while (!keyboard_interrupt) {
            await_and_send(args, intervalTimer);
        }
        const auto end_time = std::chrono::high_resolution_clock::now();
        diff = end_time - start_time;
    }

    IntervalTimer::stop();
    report_stats(diff);

    return 0;
}

auto main(int argc, char *argv[]) -> int {
    try {
        // Turn off scientific notation for std::cout
        std::cout << std::fixed;

        if (socket_fd < 0) {
            perror("Can't open socket");
            exit(errno);
        }

        struct arguments args{parse_args(argc, argv)};

        if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, args.interface.c_str(), args.interface.length() + 1) <
            0) {
            perror("Can't bind to interface");
            exit(errno);
        }

        if (setsockopt(socket_fd, SOL_IP, IP_TOS, &args.packet_dscp, 1) < 0) {
            perror("Cant set ToS");
            exit(errno);
        }

        msg_buffer = calloc(args.packet_size, sizeof(char));
        if (msg_buffer == nullptr) {
            perror("Can't calloc msg_buffer");
            exit(errno);
        }
        ((uint8_t *) msg_buffer)[0] = args.label_byte;

        out_addr.sin_family = AF_INET;
        out_addr.sin_addr.s_addr = inet_addr(args.dest_ip.c_str());
        out_addr.sin_port = htons(args.dest_port);

        set_and_start_timer(args);

        close(socket_fd);
        free(msg_buffer);
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << std::endl;
        std::exit(1);
    }

}
