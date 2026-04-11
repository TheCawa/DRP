#include "drp.hpp"
#include <fstream>
#include <iostream>
#include <vector>

int main() {
    drp::init_network();
    drp::RatReceiver rec(9999);
    
    std::vector<std::vector<uint8_t>> file_chunks;
    std::vector<bool> received_mask;
    uint16_t total_expected = 0;
    uint16_t chunks_captured = 0;

    std::cout << "Receiver ready (Turbo Mode)..." << std::endl;

    while (true) {
        std::vector<uint8_t> raw_payload;
        uint16_t msg_id;

        if (rec.poll(raw_payload, msg_id)) {
            if (raw_payload.size() < 4) continue;

            uint16_t chunk_idx = (raw_payload[0] << 8) | raw_payload[1];
            uint16_t total = (raw_payload[2] << 8) | raw_payload[3];

            if (total_expected == 0) {
                total_expected = total;
                file_chunks.resize(total_expected);
                received_mask.resize(total_expected, false);
            }

            if (chunk_idx < total_expected && !received_mask[chunk_idx]) {
                std::vector<uint8_t> data(raw_payload.begin() + 4, raw_payload.end());
                file_chunks[chunk_idx] = std::move(data);
                received_mask[chunk_idx] = true;
                chunks_captured++;
                if (chunks_captured % 10 == 0 || chunks_captured == total_expected) {
                    std::cout << "\rProgress: " << chunks_captured << " / " << total_expected << std::flush;
                }
            }

            if (chunks_captured == total_expected && total_expected > 0) {
                std::cout << "\nAll chunks received! Saving..." << std::endl;
                std::ofstream out("output.jpg", std::ios::binary);
                for (const auto& chunk : file_chunks) {
                    out.write((char*)chunk.data(), chunk.size());
                }
                out.close();
                std::cout << "Image saved!" << std::endl;
                break;
            }
        }

    }

    drp::cleanup_network();
    return 0;
}