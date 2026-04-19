import socket
import random

PROXY_PORT = 8888
TARGET_ADDR = ('127.0.0.1', 9999)
LOSS_CHANCE = 0.5
SENDER_ADDR = None

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if hasattr(socket, 'SIO_UDP_CONNRESET'):
    sock.ioctl(socket.SIO_UDP_CONNRESET, False)

sock.bind(('127.0.0.1', PROXY_PORT))
print(f"Bidir Chaos Proxy on {PROXY_PORT} -> {TARGET_ADDR} (Immune to 10054)")

while True:
    try:
        data, addr = sock.recvfrom(2048)
        
        if addr != TARGET_ADDR:
            SENDER_ADDR = addr
            target = TARGET_ADDR
            label = "CMD"
        else:
            target = SENDER_ADDR
            label = "ACK"

        if target and random.random() > LOSS_CHANCE:
            sock.sendto(data, target)
            print(f"[OK] {label} forwarded")
        else:
            print(f"[DROP] {label} lost")

    except ConnectionResetError:
        continue
    except Exception as e:
        print(f"Unexpected error: {e}")