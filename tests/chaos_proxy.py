import socket
import random
import time
import threading
from collections import deque

PROXY_PORT = 8888
TARGET_ADDR = ('127.0.0.1', 9999)

# ==== MODE ====
MODE = "wifi"  # "wifi", "rf", "satellite"

PROFILES = {
    "wifi": {
        "loss_cmd": 0.1,
        "loss_ack": 0.15,
        "delay": (5, 30),
        "jitter": True,
        "burst_chance": 0.02,
        "burst_len": (2, 5),
        "dup": 0.02,
        "reorder": 0.05,
        "max_pps": 500
    },
    "rf": {
        "loss_cmd": 0.4,
        "loss_ack": 0.5,
        "delay": (20, 200),
        "jitter": True,
        "burst_chance": 0.1,
        "burst_len": (3, 15),
        "dup": 0.05,
        "reorder": 0.1,
        "max_pps": 200
    },
    "satellite": {
        "loss_cmd": 0.2,
        "loss_ack": 0.3,
        "delay": (200, 800),
        "jitter": True,
        "burst_chance": 0.05,
        "burst_len": (2, 10),
        "dup": 0.03,
        "reorder": 0.05,
        "max_pps": 100
    }
}

cfg = PROFILES[MODE]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('127.0.0.1', PROXY_PORT))

print(f"Chaos Proxy [{MODE}] on {PROXY_PORT} -> {TARGET_ADDR}")

SENDER_ADDR = None

reorder_buffer = deque()
last_send_time = 0
burst_active = False
burst_left = 0

lock = threading.Lock()

def should_drop(is_ack):
    global burst_active, burst_left

    loss = cfg["loss_ack"] if is_ack else cfg["loss_cmd"]

    # burst logic
    if burst_active:
        burst_left -= 1
        if burst_left <= 0:
            burst_active = False
        return True

    if random.random() < cfg["burst_chance"]:
        burst_active = True
        burst_left = random.randint(*cfg["burst_len"])
        return True

    return random.random() < loss


def apply_delay():
    delay = random.uniform(*cfg["delay"]) / 1000
    time.sleep(delay)


def send_with_limits(data, target):
    global last_send_time

    now = time.time()
    min_interval = 1.0 / cfg["max_pps"]

    if now - last_send_time < min_interval:
        return False

    last_send_time = now
    sock.sendto(data, target)
    return True


def process_packet(data, addr):
    global SENDER_ADDR

    is_ack = (addr == TARGET_ADDR)

    if not is_ack:
        SENDER_ADDR = addr
        target = TARGET_ADDR
        label = "CMD"
    else:
        target = SENDER_ADDR
        label = "ACK"

    if not target:
        return

    if should_drop(is_ack):
        print(f"[DROP] {label}")
        return

    if cfg["jitter"]:
        apply_delay()

    # reorder
    if random.random() < cfg["reorder"]:
        reorder_buffer.append((data, target, label))
        print(f"[BUFFER] {label}")
        return

    # normal send
    if send_with_limits(data, target):
        print(f"[OK] {label}")
    else:
        print(f"[THROTTLE] {label}")

    # duplicate
    if random.random() < cfg["dup"]:
        sock.sendto(data, target)
        print(f"[DUP] {label}")


def reorder_worker():
    while True:
        if reorder_buffer:
            data, target, label = reorder_buffer.popleft()
            sock.sendto(data, target)
            print(f"[REORDER] {label}")
        time.sleep(random.uniform(0.01, 0.1))


threading.Thread(target=reorder_worker, daemon=True).start()

while True:
    try:
        data, addr = sock.recvfrom(2048)
        process_packet(data, addr)
    except Exception as e:
        print("ERR:", e)