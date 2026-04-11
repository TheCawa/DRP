#ifndef DRP_HPP
#define DRP_HPP

#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #define SOCKET int
#endif

namespace drp {

#pragma pack(push, 1)
struct Header {
    uint16_t message_id;
    uint8_t  chunk_index;
    uint8_t  flags;         // 0 = DATA, 1 = ACK
    uint8_t  payload_size; 
    uint32_t reserved;     
};
#pragma pack(pop)

class RatSender {
public:
    RatSender(const char* target_ip, int target_port);
    ~RatSender();

    void start(uint16_t manual_id = 0); 
    void stop();
    void set_payload(const std::vector<uint8_t>& data);

    bool isRunning() const { return running; }

private:
    void send_loop();

    std::atomic<bool> running{false};
    std::thread worker_thread;
    
    std::string ip;
    int port;
    std::vector<uint8_t> current_payload;
    Header current_header;
};

class RatReceiver {
public:
    RatReceiver(int port);
    ~RatReceiver();

    bool poll(std::vector<uint8_t>& out_data, uint16_t& out_id);

private:
    SOCKET sock;      
    uint16_t last_msg_id;
};

bool init_network();
void cleanup_network();

} // namespace drp

#endif