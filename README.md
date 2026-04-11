# 🐀 Drowning Rat Protocol (DRP)

> *"Better to receive the same thing a hundred times than to never receive it at all."*

DRP is a lightweight, application-layer transport protocol built for one thing: **guaranteed message delivery over terrible channels**.

50% packet loss? Fine. 80%? Still works.

---

## Why does this exist?

TCP backs off when the channel degrades — exactly when you need it to push harder. UDP doesn't try at all. Neither is right for sending a critical command to a drone flying through RF interference, or reading a sensor buried in a concrete shaft.

DRP takes a different approach: **aggressive, persistent retransmission** until the message gets through or the link is declared dead. No politeness. No congestion fairness. Just delivery.

---

## How it works

1. **Segment** — Message is split into chunks (default 512 bytes)
2. **Blast** — All unconfirmed chunks are sent in a continuous loop
3. **ACK burst** — Receiver sends 10 ACKs per received chunk to overcome return-path loss
4. **Confirm** — Sender removes confirmed chunks from the retransmission queue
5. **Done** — When all chunks are ACK'd, message is complete

A configurable **sliding window** (default: 4 parallel senders) allows multiple chunks to be in-flight simultaneously, significantly improving throughput.

---

## Benchmarks

Tested on localhost with a bidirectional chaos proxy (random packet drop):

| Packet Loss | File Size | Time   | Throughput |
|-------------|-----------|--------|------------|
| 50%         | ~113 KB   | ~9.9s  | ~11.5 KB/s |
| 80%         | ~113 KB   | ~19s   | ~6 KB/s    |

At 80% loss, every packet requires ~5 transmission attempts on average. DRP accepts this overhead as the cost of delivery guarantees.

---

## Quick Start

DRP is a single header-only C++ library.

```cpp
#include "drp.hpp"

// Sender
drp::init_network();
drp::RatSender sender("192.168.1.10", 9999);
sender.set_payload({0x01, 0x02, 0x03});
sender.start();

// Wait for delivery...
while (sender.isRunning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

drp::cleanup_network();
```

```cpp
#include "drp.hpp"

// Receiver
drp::init_network();
drp::RatReceiver receiver(9999);

std::vector<uint8_t> data;
uint16_t msg_id;

while (true) {
    if (receiver.poll(data, msg_id)) {
        // message received
    }
}
```

### Sending a file

```cpp
const size_t CHUNK_SIZE = 512;
const int WINDOW_SIZE = 4;  // parallel senders

// split file into chunks, launch up to WINDOW_SIZE RatSenders in parallel
// see sender_turbo.cpp for full example
```

---

## Testing with the Chaos Proxy

A bidirectional UDP proxy with configurable packet loss is included for testing:

```bash
python chaos_proxy.py
# Bidir Chaos Proxy on 8888 -> 127.0.0.1:9999 (loss: 50%)
```

Point your sender at port `8888`, run your receiver on port `9999`, adjust `LOSS_CHANCE` in the script.

---

## Use cases

✅ UAV / robotics control commands under RF interference  
✅ IoT sensors in shielded environments (mines, basements, concrete structures)  
✅ LoRa networks requiring guaranteed command delivery  
✅ Satellite uplinks with high and unpredictable loss  
✅ Any scenario where *missing* a message is more expensive than *extra* bandwidth  

❌ High-throughput file transfer over shared networks  
❌ Real-time streaming (use UDP)  
❌ Scenarios requiring congestion fairness  

---

## Limitations

| Limitation | Notes |
|------------|-------|
| **Bandwidth hogging** | DRP consumes aggressively. Use only on dedicated control channels. |
| **No security** | No encryption or replay protection in v1.x. Wrap with Noise Protocol or AES-GCM. |
| **ACK volume** | 10 ACKs per chunk; large messages generate significant ACK traffic. |
| **message_id rollover** | 16-bit ID space (65535 IDs). Handle rollover at application layer for high-frequency use. |
| **Single receiver** | Not designed for multicast or broadcast topologies. |

---

## Comparison

| | TCP | UDP | DRP |
|---|---|---|---|
| Reliability | High | None | Extreme |
| On loss | Backs off | Ignores | Persists |
| Bandwidth | Efficient | Efficient | Expensive |
| Congestion control | Yes | No | No |

---

## Roadmap

- [x] Core protocol + C++ header-only implementation
- [x] Bidirectional chaos proxy for loss simulation  
- [x] Sliding window parallel sender
- [ ] Persistent ACK loop (v1.2)
- [ ] message_id rollover handling (v1.2)
- [ ] Optional FEC layer (v1.3)
- [ ] Security layer integration — Noise Protocol (v2.0)

---

## Protocol Specification

See [`SPEC.md`](./SPEC.md) for the full technical specification including packet format, algorithm pseudocode, and performance analysis.

---

## License

MIT

---

*TheCawa, 2026*
