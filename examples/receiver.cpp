#include "drp.hpp"
#include <iostream>

int main() {
    drp::init_network();
    drp::RatReceiver receiver(9999);
    
    std::vector<uint8_t> data;
    uint16_t id;

    std::cout << "DRP Receiver is listening..." << std::endl;

    while (true) {
        if (receiver.poll(data, id)) {
            std::cout << "NEW COMMAND [ID: " << id << "] Data size: " << data.size() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    drp::cleanup_network();
    return 0;
}