#include "drp.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>

int main() {
    drp::init_network();
    
    std::ifstream file("../assets/input.jpg", std::ios::binary);
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    if (buffer.empty()) {
        std::cerr << "File not found!" << std::endl;
        return 1;
    }

    const size_t CHUNK_SIZE = 512; 
    size_t total_chunks = (buffer.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const int WINDOW_SIZE = 4;

    std::cout << "DRP TURBO: Sending " << total_chunks << " chunks with window size " << WINDOW_SIZE << std::endl;

    size_t next_chunk_to_send = 0;
    std::vector<std::unique_ptr<drp::RatSender>> workers;

    while (next_chunk_to_send < total_chunks || !workers.empty()) {
        while (workers.size() < WINDOW_SIZE && next_chunk_to_send < total_chunks) {
            size_t i = next_chunk_to_send++;
            size_t start = i * CHUNK_SIZE;
            size_t end = std::min(start + CHUNK_SIZE, buffer.size());

            std::vector<uint8_t> payload;
            payload.push_back((i >> 8) & 0xFF); payload.push_back(i & 0xFF);
            payload.push_back((total_chunks >> 8) & 0xFF); payload.push_back(total_chunks & 0xFF);
            payload.insert(payload.end(), buffer.begin() + start, buffer.begin() + end);

            auto sender = std::make_unique<drp::RatSender>("127.0.0.1", 8888);
            sender->set_payload(payload);
            
            sender->start((uint16_t)(i + 1)); 
            workers.push_back(std::move(sender));
            
            std::cout << "[+] Started Chunk " << i + 1 << "/" << total_chunks << std::endl;
        }

        for (auto it = workers.begin(); it != workers.end(); ) {
            if (!(*it)->isRunning()) {
                it = workers.erase(it);
                std::cout << "[-] Chunk Delivered. Slot freed." << std::endl;
            } else {
                ++it;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::cout << "\n[!] ALL CHUNKS DELIVERED! TURBO-DRP SUCCESS." << std::endl;
    drp::cleanup_network();
    return 0;
}