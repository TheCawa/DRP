import socket
import time

start_time = time.time()
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(('127.0.0.1', 9999)) # Попробуй подключиться
    s.sendall(b"CRITICAL COMMAND")
    print(f"TCP Delivered in: {time.time() - start_time:.3f}s")
except Exception as e:
    print(f"TCP Failed or Timed out: {e}")