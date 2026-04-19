#include "drp.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define DRP_CLOSE(s)    closesocket(s)
    #define DRP_SET_NONBLOCK(s) do { u_long m = 1; ioctlsocket((s), FIONBIO, &m); } while(0)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define DRP_CLOSE(s)    close(s)
    #define DRP_SET_NONBLOCK(s) do { \
        int fl = fcntl((s), F_GETFL, 0); \
        fcntl((s), F_SETFL, fl | O_NONBLOCK); \
    } while(0)
#endif

namespace drp {

static std::atomic<uint16_t> global_msg_id{1};
class RawSocket {
public:
    RawSocket() {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    ~RawSocket() {
        if (isValid()) DRP_CLOSE(sock_);
    }

    bool isValid() const { return sock_ != INVALID_SOCKET; }

    drp_socket_t get() const { return sock_; }

    bool send(const char* ip, int port, const void* data, int len) const {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port   = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, ip, &dest.sin_addr);
        return sendto(sock_, static_cast<const char*>(data), len, 0,
                      reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) != SOCKET_ERROR;
    }

private:
    drp_socket_t sock_;
};

// -----------------------------------------------------------------------
// Network init / cleanup
// -----------------------------------------------------------------------
bool init_network() {
#ifdef _WIN32
    WSADATA wd;
    return WSAStartup(MAKEWORD(2, 2), &wd) == 0;
#else
    return true;
#endif
}

void cleanup_network() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// -----------------------------------------------------------------------
// RatSender
// -----------------------------------------------------------------------
RatSender::RatSender(const char* target_ip, int target_port)
    : ip(target_ip), port(target_port) {}

RatSender::~RatSender() { stop(); }

void RatSender::set_payload(const std::vector<uint8_t>& data) {
    current_payload = data;
}

void RatSender::start(uint16_t manual_id) {
    if (running.load()) return;
    assigned_id = (manual_id != 0) ? manual_id : global_msg_id.fetch_add(1);

    running.store(true);
    worker_thread = std::thread(&RatSender::send_loop, this);
}

void RatSender::stop() {
    running.store(false);
    if (worker_thread.joinable()) worker_thread.join();
}

void RatSender::send_loop() {
    RawSocket sock;
    if (!sock.isValid()) {
        running.store(false);
        return;
    }
    DRP_SET_NONBLOCK(sock.get());

    const uint16_t mid = assigned_id;
    if (current_payload.size() > static_cast<size_t>(MAX_CHUNKS) * CHUNK_SIZE) {
        fprintf(stderr, "[DRP ID:%u] ERROR: payload too large (max %u bytes). "
                        "Split at application layer.\n",
                mid, static_cast<unsigned>(MAX_CHUNKS) * CHUNK_SIZE);
        running.store(false);
        return;
    }

    std::vector<std::vector<uint8_t>> chunks;
    for (size_t offset = 0; offset < current_payload.size(); offset += CHUNK_SIZE) {
        size_t sz = std::min(static_cast<size_t>(CHUNK_SIZE),
                             current_payload.size() - offset);
        chunks.emplace_back(current_payload.begin() + static_cast<ptrdiff_t>(offset),
                            current_payload.begin() + static_cast<ptrdiff_t>(offset + sz));
    }
    if (chunks.empty()) {
        chunks.emplace_back();
    }

    const auto total_chunks = static_cast<uint8_t>(chunks.size());
    std::vector<bool> confirmed(total_chunks, false);

    auto last_ack_time = std::chrono::steady_clock::now();

    while (running.load()) {
        char ack_buf[sizeof(AckHeader)];
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        int res = recvfrom(sock.get(), ack_buf, sizeof(ack_buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &from_len);
        if (res == static_cast<int>(sizeof(AckHeader))) {
            auto* ah = reinterpret_cast<AckHeader*>(ack_buf);
            if (ah->flags == 1 && ntohs(ah->message_id) == mid) {
                if (ah->chunk_index < total_chunks && !confirmed[ah->chunk_index]) {
                    confirmed[ah->chunk_index] = true;
                    last_ack_time = std::chrono::steady_clock::now();
                }
            }
        }

        bool all_done = true;
        for (bool c : confirmed) if (!c) { all_done = false; break; }
        if (all_done) {
            printf("[DRP ID:%u] Message fully delivered (%u chunk(s)).\n",
                   mid, total_chunks);
            break;
        }

        auto now = std::chrono::steady_clock::now();
        int idle_sec = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - last_ack_time).count());

        if (idle_sec >= FATAL_TIMEOUT) {
            fprintf(stderr, "[DRP ID:%u] FATAL: connection lost after %ds.\n",
                    mid, FATAL_TIMEOUT);
            break;
        }

        int sleep_ms = BASE_INTERVAL;
        if (idle_sec >= 10) {
            sleep_ms = std::min(MAX_INTERVAL, BASE_INTERVAL + idle_sec * 10);
            if (idle_sec % 10 == 0) {
                printf("[DRP ID:%u] Warning: no ACK for %ds, throttling to %dms.\n",
                       mid, idle_sec, sleep_ms);
            }
        }

        for (uint8_t i = 0; i < total_chunks; ++i) {
            if (confirmed[i]) continue;

            Header h{};
            h.message_id   = htons(mid);
            h.chunk_index  = i;
            h.total_chunks = total_chunks;
            h.flags        = 0;
            h.payload_size = htons(static_cast<uint16_t>(chunks[i].size()));
            h.reserved     = 0;
            std::vector<uint8_t> packet(sizeof(Header) + chunks[i].size());
            std::memcpy(packet.data(), &h, sizeof(Header));
            if (!chunks[i].empty())
                std::memcpy(packet.data() + sizeof(Header),
                            chunks[i].data(), chunks[i].size());

            sock.send(ip.c_str(), port,
                      packet.data(), static_cast<int>(packet.size()));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    running.store(false);
}

// -----------------------------------------------------------------------
// RatReceiver
// -----------------------------------------------------------------------
RatReceiver::RatReceiver(int port) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    DRP_SET_NONBLOCK(sock);
}

RatReceiver::~RatReceiver() {
    if (sock != INVALID_SOCKET) DRP_CLOSE(sock);
}

bool RatReceiver::poll(std::vector<uint8_t>& out_data, uint16_t& out_id) {
    static constexpr int BUF_SIZE = static_cast<int>(sizeof(Header) + CHUNK_SIZE);
    char buf[BUF_SIZE];

    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    int res = recvfrom(sock, buf, BUF_SIZE, 0,
                       reinterpret_cast<sockaddr*>(&from), &from_len);

    if (res < static_cast<int>(sizeof(Header))) return false;

    auto* h = reinterpret_cast<Header*>(buf);
    if (h->flags != 0) return false; // ignore no-DATA

    const uint16_t msg_id     = ntohs(h->message_id);
    const uint16_t p_size     = ntohs(h->payload_size);
    const uint8_t  chunk_idx  = h->chunk_index;
    const uint8_t  total      = h->total_chunks;
    if (p_size > CHUNK_SIZE || total == 0 || chunk_idx >= total) return false;
    if (static_cast<int>(sizeof(Header) + p_size) > res)         return false;

    // --- ACK burst ---
    AckHeader ah{};
    ah.message_id  = h->message_id;
    ah.chunk_index = chunk_idx;
    ah.flags       = 1;
    for (int i = 0; i < ACK_BURST; ++i) {
        sendto(sock, reinterpret_cast<const char*>(&ah), sizeof(AckHeader), 0,
               reinterpret_cast<sockaddr*>(&from), from_len);
    }
    if (processed_ids.count(msg_id)) return false;
    auto& msg_map = reassembly_buffer[msg_id];
    if (msg_map.find(chunk_idx) == msg_map.end()) {
        const uint8_t* payload_start =
            reinterpret_cast<uint8_t*>(buf) + sizeof(Header);
        msg_map[chunk_idx] =
            std::vector<uint8_t>(payload_start, payload_start + p_size);
    }
    if (msg_map.size() != static_cast<size_t>(total)) return false;
    out_data.clear();
    for (uint8_t i = 0; i < total; ++i) {
        auto it = msg_map.find(i);
        if (it == msg_map.end()) return false;
        out_data.insert(out_data.end(), it->second.begin(), it->second.end());
    }
    out_id = msg_id;
    reassembly_buffer.erase(msg_id);
    if (processed_ids.size() >= MAX_KNOWN_IDS) {
        processed_ids.erase(processed_ids.begin());
    }
    processed_ids.insert(msg_id);

    return true;
}

} // namespace drp