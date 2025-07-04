import socket
import threading
import time

PORT = 12345
SERVER = socket.gethostbyname(socket.gethostname())
ADDR = (SERVER, PORT)

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(ADDR)

def handle_client(conn, addr):
    print(f"[NEW CONNECTTION] {addr} connected")

    connected = True
    while connected:
        try:
            msg_length = conn.recv(64).decode('utf-8')
            if msg_length:
                msg_length = int(msg_length)
                msg = conn.recv(msg_length).decode('utf-8')
                if msg == "DISCONNECT":
                    connected = False
                print(f"[{addr}] {msg}")
                conn.send("Message received".encode('utf-8'))
        except Exception as e:
            print(f"[ERROR] {e}")
            connected = False

    conn.close()
    print(f"[DISCONNECT] {addr} disconnected")


def start():
    server.listen()
    print(f"[LISTENING] Server is listening on {SERVER}:{PORT}")
    while True:
        conn, addr = server.accept()
        thread = threading.Thread(target=handle_client, args=(conn, addr))
        thread.start()
        print("[ACTIVE CONNECTIONS] " + str(threading.active_count() - 1))

print("[STARTING] Server is starting...")
start()
