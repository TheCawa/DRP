#ifndef DRP_HPP
#define DRP_HPP

#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <set>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using drp_socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using drp_socket_t = int;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
#endif

namespace drp {

// maximum 255 chunks * 512 bytes = ~127 KB per message.
// for larger data, chunk at the application level.
static constexpr uint16_t CHUNK_SIZE      = 512;
static constexpr uint8_t  MAX_CHUNKS      = 255;
static constexpr int      BASE_INTERVAL   = 20;    // ms
static constexpr int      MAX_INTERVAL    = 2000;  // ms
static constexpr int      FATAL_TIMEOUT   = 300;   // sec
static constexpr int      ACK_BURST       = 10;
static constexpr size_t   MAX_KNOWN_IDS   = 1024;  // limit processed_ids

#pragma pack(push, 1)
struct Header {
    uint16_t message_id;   // network byte order
    uint8_t  chunk_index;  // 0-based
    uint8_t  total_chunks; // all chunks
    uint8_t  flags;        // 0 = DATA
    uint16_t payload_size; // network byte order
    uint16_t reserved;     // 0
};

struct AckHeader {
    uint16_t message_id;   // network byte order
    uint8_t  chunk_index;
    uint8_t  flags;        // 1 = ACK
};
#pragma pack(pop)


class RatSender {
public:
    RatSender(const char* target_ip, int target_port);
    ~RatSender();

    void start(uint16_t manual_id = 0);
    void stop();
    void set_payload(const std::vector<uint8_t>& data);
    bool isRunning() const { return running.load(); }

private:
    void send_loop();

    std::atomic<bool>    running{false};
    std::thread          worker_thread;
    std::string          ip;
    int                  port;
    std::vector<uint8_t> current_payload;
    uint16_t             assigned_id{0};
};

class RatReceiver {
public:
    explicit RatReceiver(int port);
    ~RatReceiver();
    bool poll(std::vector<uint8_t>& out_data, uint16_t& out_id);

private:
    drp_socket_t sock;
    std::map<uint16_t, std::map<uint8_t, std::vector<uint8_t>>> reassembly_buffer;
    std::set<uint16_t> processed_ids;
};

bool init_network();
void cleanup_network();

} // namespace drp

#endif // DRP_HPP