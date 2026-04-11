#include "drp.hpp"
#include <iostream>
#include <vector>

int main() {
    drp::init_network();

    std::cout << "--- DRP Single Shot Test ---" << std::endl;
    
    drp::RatSender sender("127.0.0.1", 8888);
    std::string msg = "Hello from DRP!";
    sender.set_payload(std::vector<uint8_t>(msg.begin(), msg.end())); 

    std::cout << "Sending: " << msg << std::endl;
    sender.start();
    std::cout << "Waiting for ACK... (Press Ctrl+C to abort)" << std::endl;
    
    while(sender.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Message delivered successfully!" << std::endl;

    drp::cleanup_network();
    return 0;
}