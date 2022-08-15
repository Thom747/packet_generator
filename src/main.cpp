#include <iostream>
#include <csignal>
// Excluded as I can't find new setitimer in ctime
#include <sys/time.h> // NOLINT(modernize-deprecated-headers)
#include <cmath>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

#include "argparse.h"

// Amount of microseconds in a second
#define S_TO_US (1000000)

struct arguments {
    std::string dest_ip;
    unsigned int dest_port;
    double packet_freq;
    unsigned int packet_size;
    uint8_t packet_tos;
    unsigned int timeout;
    bool verbose;
    std::string interface;
    uint8_t label_byte;
};

volatile bool keyboard_interrupt{false};
volatile uint32_t packet_num{0};
uint32_t succesful_packet_num{0};
const int socket_fd{socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};
sockaddr_in out_addr{};
void *msg_buffer;

struct arguments parse_args(int argc, char *argv[]) {
    // Register arguments
    argparse::ArgumentParser parser("Packet Generator");
    parser.add_description("Send UDP packets to a destination at a specific frequency.\n"
                           "Packet structure:\n"
                           "Label byte               (1B)\n"
                           "Packet sequence number   (4B)\n"
                           "Padding zero bytes       (remaining bytes)");
    parser.add_argument("dest_IP")
            .help("IPv4 address to send packets to");
    parser.add_argument("dest_port")
            .help("Port to send packets to")
            .scan<'u', unsigned int>();
    parser.add_argument("packet_freq")
            .help("Frequency in Hz to send packets")
            .scan<'f', double>();
    parser.add_argument("packet_size")
            .help("Size of packet payload in bytes")
            .scan<'u', unsigned int>();
    parser.add_argument("packet_tos")
            .help("IP ToS code for packet, see https://www.speedguide.net/articles/quality-of-service-tos-dscp-wmm-3477")
            .scan<'u', uint8_t>();
    parser.add_argument("-t", "--timeout")
            .help("Timeout to send packets for in whole seconds. If omitted or 0, runs indefinitely.")
            .nargs(1)
            .default_value((unsigned int) 0)
            .scan<'u', unsigned int>();
    parser.add_argument("-v", "--verbose")
            .help("Print debugging information")
            .default_value(false)
            .implicit_value(true);
    parser.add_argument("-i", "--interface")
            .help("Interface to send packets over")
            .nargs(1)
            .default_value((std::string) "");
    parser.add_argument("-l", "--label")
            .help("Byte to label transmissions with")
            .nargs(1)
            .default_value((uint8_t) 0)
            .scan<'u', uint8_t>();

    // Attempt to parse the arguments provided
    try {
        parser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        std::exit(1);
    }

    struct arguments res{
            parser.get("dest_IP"),
            parser.get<unsigned int>("dest_port"),
            parser.get<double>("packet_freq"),
            parser.get<unsigned int>("packet_size"),
            parser.get<uint8_t>("packet_tos"),
            parser.get<unsigned int>("--timeout"),
            parser.get<bool>("--verbose"),
            parser.get("--interface"),
            parser.get<uint8_t>("--label"),
    };

    if (res.verbose) {
        std::cout << "Sending UDP packets to " << res.dest_ip << ":" << res.dest_port << " at " << res.packet_freq
                  << "Hz."
                  << std::endl;
        std::cout << "Packet size is " << res.packet_size << "B, ToS is " << (unsigned int) res.packet_tos
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

int inline await_and_send(const struct arguments &args, sigset_t *alarm_sig, int *signum) {
    // Wait for interrupt
    sigwait(alarm_sig, signum);

    // Fill buffer with packet_num
    const uint32_t my_packet_num{packet_num};
    packet_num++;
    {
        const uint32_t network_packet_num{htonl(my_packet_num)};
        std::memcpy(&(((char *) msg_buffer)[1]), &network_packet_num, 4);
    }

    // Send packet
    printf("Sending packet %u\n", my_packet_num);
    if (sendto(socket_fd, msg_buffer, args.packet_size, 0, (sockaddr *) &out_addr, sizeof(out_addr)) < 0) {
        perror("Failed to send packet");
    } else {
        succesful_packet_num++;
    }

    return 0;
}

void keyboard_interrupt_handler([[maybe_unused]] int signum) {
    keyboard_interrupt = true;
}

void missed_alarm_handler([[maybe_unused]] int signum) {
    packet_num++;
    if (write(1, "Missed alarm!\n", 14) < 0) {
        perror("Failed to write to stdout");
    }
}

void report_stats(std::chrono::duration<double, std::micro> duration) {
    std::cout << "Ran for " << duration.count() / S_TO_US << " seconds." << std::endl
              << "Attempted to send " << packet_num << " packets, of which " << succesful_packet_num << " ("
              << succesful_packet_num * 100.0 / packet_num
              << "%) were successful." << std::endl
              << "Attempt frequency: " << packet_num / (duration.count() / S_TO_US) << "Hz." << std::endl
              << "Successful attempt frequency: " << succesful_packet_num / (duration.count() / S_TO_US) << "Hz."
              << std::endl;
}

int set_and_start_timer(const struct arguments &args) {
    long us_per_packet{(long) floor(S_TO_US / args.packet_freq)};
    long s_per_packet{(long) us_per_packet / S_TO_US};
    us_per_packet -= s_per_packet * S_TO_US;
    printf("%lds, %ldus\n", s_per_packet, us_per_packet);

    // Handle keyboard interrupts
    std::signal(SIGINT, keyboard_interrupt_handler);
    std::signal(SIGTERM, keyboard_interrupt_handler);

    // Handle missed alarms
    std::signal(SIGALRM, missed_alarm_handler);

    // Register alarm
    sigset_t alarm_sig;
    int signum{0};
    sigemptyset(&alarm_sig);
    sigaddset(&alarm_sig, SIGALRM);

    // Create timer: https://stackoverflow.com/questions/25327519/how-to-send-udp-packet-every-1-ms
    struct itimerval timer{0, us_per_packet, 0, 1000};
    if (setitimer(ITIMER_REAL, &timer, nullptr) < 0) {
        perror("Failed to set timer");
        exit(errno);
    }

    if (args.timeout) {
        const std::chrono::duration<double, std::micro> timeout_duration{args.timeout * S_TO_US};
        std::chrono::duration<double, std::micro> diff{0};
        const auto start_time = std::chrono::high_resolution_clock::now();

        do {
            await_and_send(args, &alarm_sig, &signum);
            diff = std::chrono::high_resolution_clock::now() - start_time;
        } while (!keyboard_interrupt && diff < timeout_duration);

        report_stats(diff);

    } else {
        const auto start_time = std::chrono::high_resolution_clock::now();
        while (!keyboard_interrupt) {
            await_and_send(args, &alarm_sig, &signum);
        }
        const auto end_time = std::chrono::high_resolution_clock::now();

        report_stats(end_time - start_time);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (socket_fd < 0) {
        perror("Can't open socket");
        exit(errno);
    }

    struct arguments args{parse_args(argc, argv)};

    if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE,
                   args.interface.c_str(), args.interface.length() + 1) < 0) {
        perror("Can't bind to interface");
        exit(errno);
    }

    if (setsockopt(socket_fd, SOL_IP, IP_TOS,
                   &args.packet_tos, 1) < 0) {
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
}
