#include <iostream>
#include "argparse.h"

int main(int argc, char *argv[]) {
    argparse::ArgumentParser parser("Packet Generator");
    parser.add_description("Send UDP packets to a destination at a specific frequency.");
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
            .help("Timeout to send packets for in seconds. If omitted or <=0, runs indefinitely.")
            .default_value(0.0)
            .nargs(1)
            .scan<'f', double>();
    parser.add_argument("-v", "--verbose")
            .help("Print debugging information")
            .default_value(false)
            .implicit_value(true);

    try {
        parser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        std::exit(1);
    }

    std::string dest_ip{parser.get("dest_IP")};
    unsigned int dest_port{parser.get<unsigned int>("dest_port")};
    double packet_freq{parser.get<double>("packet_freq")};
    unsigned int packet_size{parser.get<unsigned int>("packet_size")};
    uint8_t packet_tos{parser.get<uint8_t>("packet_tos")};
    double timeout{parser.get<double>("-t")};
    bool verbose{parser.get<bool>("--verbose")};

    if (verbose) {
        std::cout << "Sending UDP packets to " << dest_ip << ":" << dest_port << " at " << packet_freq << "Hz."
                  << std::endl;
        std::cout << "Packet size is " << packet_size << "B and ToS is " << (unsigned int) packet_tos << "."
                  << std::endl;
        if (timeout > 0)
            std::cout << "Timeout in " << timeout << "seconds." << std::endl;
        else
            std::cout << "Timeout not specified, running indefinitely." << std::endl;
    }

    return 0;
}
