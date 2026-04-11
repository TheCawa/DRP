# Drowning Rat Protocol (DRP) — Technical Specification
**Version:** 1.1-draft  
**Author:** TheCawa  
**Status:** Experimental  
**Date:** 2026

---

## 1. Overview

DRP (Drowning Rat Protocol) is an application-layer transport protocol designed for reliable delivery of messages over channels with extreme packet loss (>20–80%) and unstable RTT.

**Core philosophy:** *"Better to receive the same thing a hundred times than to never receive it at all."*  
Guaranteed delivery is prioritized over bandwidth efficiency.

### Target scenarios

- UAV and robotics control under RF interference
- Sensor communication in shielded environments (basements, mines, tunnels)
- LoRa networks and satellite links with low bandwidth but strict delivery requirements
- Any channel where losing a control command is more costly than consuming extra bandwidth

---

## 2. Design Decisions

### 2.1 Why not TCP?

TCP's congestion control (exponential backoff, sliding window reduction) is designed to be *fair* to other participants in a shared network. In an already-degraded channel, TCP's politeness causes it to back off exactly when aggressive retransmission is needed most.

### 2.2 Why not UDP?

Raw UDP provides no delivery guarantees. Application-level retransmission logic still needs to be implemented on top of it, which is exactly what DRP provides.

### 2.3 Why not QUIC/SCTP?

Both are excellent protocols but carry significant implementation overhead and assume relatively stable channels. DRP is intentionally minimal — the full implementation fits in a single header file.

### 2.4 The sliding window addition

The reference implementation uses a configurable parallel sender window (`WINDOW_SIZE`, default: 4). This is a pragmatic addition to the original single-stream design: instead of waiting for each chunk to be ACK'd before sending the next, multiple chunks are transmitted concurrently. This significantly improves throughput without abandoning the core retransmission model.

---

## 3. Protocol Mechanics

### 3.1 Message segmentation

A message is split into fixed-size chunks (default: 512 bytes). Each chunk is assigned a sequential `chunk_index` and sent independently.

### 3.2 Sender behavior

The sender transmits all unconfirmed chunks in a loop with a configurable interval. Upon receiving an ACK for a chunk, that chunk is removed from the retransmission queue. The sender applies **linear throttling** (not exponential backoff) when no ACKs are received:

```
if (no_ack_for > 10s):
    interval = min(20 + elapsed_seconds * 10, MAX_INTERVAL_MS)
else:
    interval = BASE_INTERVAL_MS  // 20ms default
```

This keeps the protocol aggressive during normal degradation while preventing infinite flooding during complete link failure.

### 3.3 Receiver behavior

Upon receiving a DATA packet, the receiver:
1. Stores the chunk in its reassembly buffer
2. Immediately sends 10 ACK packets for that chunk (burst ACK to overcome return-path loss)
3. Checks if all chunks for the message are received; if so, dispatches to the application

When all chunks for a `message_id` have been received, the receiver considers the message complete and stops processing further packets for that ID.

### 3.4 Message completion

A message is considered fully delivered when all `total_chunks` for a given `message_id` have been received and buffered. The reassembled payload is then delivered to the application layer in-order.

---

## 4. Packet Format (PDU)

Minimum header size: **4 bytes** (DATA) / **4 bytes** (ACK).  
Chunk metadata is embedded in the payload for DATA packets.

#### 4.1 Base header

| Field       | Size   | Description                                  |
|-------------|--------|----------------------------------------------|
| message_id  | 16 bit | Unique message identifier (network byte order) |
| chunk_index | 8 bit  | Chunk index (0-based) for multi-chunk messages |
| flags       | 8 bit  | `0x00` = DATA, `0x01` = ACK                  |
| payload_size| 8 bit  | Length of payload (DATA only)                |
| reserved    | 32 bit | Reserved for future use (set to 0)           |

Total header size: **9 bytes**

### 4.2 ACK packet

ACK packets contain only the base header (4 bytes) with `flags = 0x01` and the `message_id` of the chunk being acknowledged. No payload.

---

## 5. Algorithms

### 5.1 Sender (pseudocode)

```
BASE_INTERVAL    = 20ms
MAX_INTERVAL     = 2000ms
FATAL_TIMEOUT    = 300s
WINDOW_SIZE      = 4       // parallel concurrent senders

split message into chunks of CHUNK_SIZE bytes
assign sequential message_ids starting from 1

for each chunk (up to WINDOW_SIZE in parallel):
    loop:
        send DATA packet
        check for incoming ACK
        if ACK received for this chunk_id:
            mark chunk as confirmed
            exit loop
        if elapsed > FATAL_TIMEOUT:
            abort("Connection Lost")
        apply linear throttle if no ACK received for >10s
        sleep(current_interval)
```

### 5.2 Receiver (pseudocode)

```
on receive(packet):
    if packet.flags == DATA:
        extract chunk_index, total_chunks from payload
        if chunk not already received:
            store chunk in reassembly buffer
            send 10x ACK burst for this chunk
        if all chunks received:
            reassemble and dispatch to application
    
    if packet.flags == ACK:
        mark corresponding chunk as confirmed
        remove from retransmission queue
```

---

## 6. Performance Characteristics

Benchmarked on localhost with a bidirectional chaos proxy:

| Loss Rate | File Size | Transfer Time | Effective Throughput | Traffic Overhead |
|-----------|-----------|---------------|----------------------|------------------|
| 50%       | ~113 KB   | ~9.9 s        | ~11.5 KB/s           | ~2x              |
| 80%       | ~113 KB   | ~19 s         | ~6 KB/s              | ~5x              |

**Notes:**
- At 80% loss each packet requires ~5 attempts on average
- Actual wire traffic at 80% loss: ~550–600 KB to deliver 113 KB
- For control commands (<100 bytes), delivery latency is in the range of milliseconds even under heavy loss

---

## 7. Known Limitations

### 7.1 Bandwidth hogging

DRP makes no attempt at congestion control and will aggressively consume available bandwidth. It is designed exclusively for dedicated control channels, not shared networks.

### 7.2 ACK storms (multi-chunk messages)

When transmitting large messages with many chunks, the volume of ACK bursts (10 per received chunk) can contribute to channel congestion. This is partially mitigated by the sliding window cap.

### 7.3 message_id rollover

The 16-bit `message_id` field supports 65535 unique IDs. In high-frequency command scenarios, rollover handling should be implemented at the application layer.

### 7.4 No security

DRP v1.0–1.1 contains no encryption, authentication, or replay protection. It is strongly recommended to wrap DRP traffic in an external security layer:
- **Noise Protocol Framework** — for authenticated key exchange + encryption
- **AES-GCM** — for symmetric encryption with integrity

### 7.5 Single receiver assumption

The current implementation assumes one sender and one receiver per session. Multicast or broadcast scenarios are not supported and may cause ACK storms.

---

## 8. Comparison

| Feature            | TCP                   | UDP         | DRP                    |
|--------------------|-----------------------|-------------|------------------------|
| Reliability        | High (with latency)   | None        | Extreme                |
| On packet loss     | Exponential backoff   | Ignore      | Linear persistence     |
| Bandwidth usage    | Minimal               | Minimal     | Very high              |
| Congestion control | Yes (fair)            | No          | No (bully-style)       |
| Suitable for       | General data transfer | Streaming   | Control commands, IoT  |

---

## 9. Roadmap

- [x] v1.0 — Core protocol design and specification
- [x] v1.1 — Reference C++ implementation (header-only)
- [x] v1.1 — Bidirectional chaos proxy for testing
- [x] v1.1 — Sliding window sender (`WINDOW_SIZE`)
- [ ] v1.2 — Persistent ACK loop (replace burst-10 with true loop)
- [ ] v1.2 — Per-session message_id rollover handling
- [ ] v1.3 — Optional FEC (Forward Error Correction) layer
- [ ] v1.3 — Formal timing analysis and worst-case delivery bounds
- [ ] v2.0 — Optional security layer integration (Noise Protocol)

---

*TheCawa, 2026*