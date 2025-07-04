import socket
import argparse

SERVER = "127.0.0.1"
PORT = 12345


def send_message(client_socket,message):
    msg = message.encode('utf-8')
    client_socket.send(str(len(msg)).encode('utf-8').ljust(64))
    client_socket.send(msg)
    response = client_socket.recv(1024).decode('utf-8')
    print(f"Server response: {response}")

def main():
    parser = argparse.ArgumentParser(description="Simple TCP Client")
    parser.add_argument('ip', help="server ip address", default=SERVER)
    parser.add_argument('port', type=int, help="server port", default=PORT)
    parser.add_argument('--role', '-r', type=str, help="role of the client (publisher/subscriber)", choices=['publisher', 'subscriber'], default='subscriber')
    parser.add_argument('topic', type=str, help="topic to subscribe to", default="default_topic")
    parser.add_argument('--message', '-m', type=str, help="message to send to the server", default="Hello, Server!")

    args = parser.parse_args()

    server_ip = args.ip
    server_port = args.port
    server_role = args.role
    server_topic = args.topic
    server_message = args.message
    ADDR = (server_ip, server_port)

    print(f"Connecting to server {server_ip}:{server_port}...")

    try:
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect(ADDR)
        print("Connected to server.")

        send_message(client_socket, f"Role: {server_role}")

        send_message(client_socket, f"Topic: {server_topic}")

        send_message(client_socket, server_message)
    
    except ConnectionRefusedError:
        print(f"Connection to {server_ip}:{server_port} failed.")
    except socket.gaierror as e:
        print(f"Invalid address {server_ip}:{server_port}. {e}")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        client_socket.close()
        print("Connection closed.")

if __name__ == "__main__":
    main()