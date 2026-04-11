#include "drp.hpp"
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace drp {

static std::atomic<uint16_t> global_msg_id{1};

class RawSocket {
    SOCKET sock;
public:
    RawSocket() { sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); }
    bool isValid() { return sock != INVALID_SOCKET; }
    bool send(const char* ip, int port, const char* data, int len) {
        sockaddr_in dest;
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        inet_pton(AF_INET, ip, &dest.sin_addr);
        return sendto(sock, data, len, 0, (struct sockaddr*)&dest, sizeof(dest)) != SOCKET_ERROR;
    }
    SOCKET getInternal() { return sock; }
    ~RawSocket() {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }
};

bool init_network() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#endif
    return true;
}

void cleanup_network() {
#ifdef _WIN32
    WSACleanup();
#endif
}

RatSender::RatSender(const char* target_ip, int target_port) 
    : ip(target_ip), port(target_port), running(false) {
    current_header = {0, 0, 0, 0, 0}; 
}

RatSender::~RatSender() { stop(); }

void RatSender::set_payload(const std::vector<uint8_t>& data) {
    current_payload = data;
}

void RatSender::start(uint16_t manual_id) {
    if (running) return;
    current_header.message_id = (manual_id == 0) ? global_msg_id++ : manual_id;
    current_header.flags = 0; // DATA
    running = true;
    worker_thread = std::thread(&RatSender::send_loop, this);
}

void RatSender::stop() {
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void RatSender::send_loop() {
    RawSocket sock;
    if (!sock.isValid()) return;

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock.getInternal(), FIONBIO, &mode); 
#endif

    uint16_t mid = current_header.message_id;
    auto last_ack_time = std::chrono::steady_clock::now();
    int current_sleep = 20;
    const int MAX_SLEEP = 2000;
    const int FATAL_TIMEOUT = 300;
    int last_warned_second = 0;

    while (running) {
        char ack_buf[sizeof(Header)];
        sockaddr_in from;
        int from_len = sizeof(from);
        int res = recvfrom(sock.getInternal(), ack_buf, sizeof(ack_buf), 0, (struct sockaddr*)&from, &from_len);

        auto now = std::chrono::steady_clock::now();
        auto idle_seconds = (int)std::chrono::duration_cast<std::chrono::seconds>(now - last_ack_time).count();

        if (idle_seconds >= FATAL_TIMEOUT) {
            printf("\n[DRP ID:%d] FATAL: Connection lost.\n", mid);
            running = false;
            break;
        }

        if (res == (int)sizeof(Header)) {
            Header* h = reinterpret_cast<Header*>(ack_buf);
            if (h->flags == 1 && ntohs(h->message_id) == mid) {
                printf("[DRP ID:%d] SUCCESS! ACK received.\n", mid);
                running = false;
                break;
            }
        }

        if (idle_seconds >= 10) {
            current_sleep = std::min(MAX_SLEEP, 20 + (idle_seconds * 10)); 
            if (idle_seconds % 10 == 0 && idle_seconds != last_warned_second) {
                printf("[DRP ID:%d] Warning: No ACK for %ds. Throttling...\n", mid, idle_seconds);
                last_warned_second = idle_seconds;
            }
        } else {
            current_sleep = 20;
        }

        if (!running) break;
        std::vector<uint8_t> packet;
        Header net_header = current_header;
        net_header.message_id = htons(current_header.message_id);
        net_header.payload_size = (current_payload.size() > 255) ? 255 : (uint8_t)current_payload.size();
        net_header.reserved = 0;
        uint8_t* h_ptr = reinterpret_cast<uint8_t*>(&net_header);
        packet.insert(packet.end(), h_ptr, h_ptr + sizeof(Header));
        packet.insert(packet.end(), current_payload.begin(), current_payload.end());

        sock.send(ip.c_str(), port, (const char*)packet.data(), (int)packet.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(current_sleep));
    }
}

RatReceiver::RatReceiver(int port) : last_msg_id(0) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#endif
}

RatReceiver::~RatReceiver() {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

bool RatReceiver::poll(std::vector<uint8_t>& out_data, uint16_t& out_id) {
    char buf[4096];
    sockaddr_in from;
    int from_len = sizeof(from);
    int res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    
    if (res >= (int)sizeof(Header)) {
        Header* h = reinterpret_cast<Header*>(buf);
        uint16_t received_id = ntohs(h->message_id);

        if (h->flags == 0) { // DATA
            Header ack;
            ack.message_id = h->message_id;
            ack.chunk_index = h->chunk_index;
            ack.flags = 1;
            for(int i = 0; i < 10; i++) {
                sendto(sock, (const char*)&ack, sizeof(Header), 0, (struct sockaddr*)&from, from_len);
            }

            if (received_id != last_msg_id) {
                last_msg_id = received_id;
                out_id = received_id;
                out_data.assign(buf + sizeof(Header), buf + res);
                return true;
            }
        }
    }
    return false;
}

} // namespace drp