// test_client.cpp
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port> <role>" << std::endl;
        std::cerr << "Role: 'subscriber' or 'publisher'" << std::endl;
        return 1;
    }

    const char *server_ip = argv[1];
    int server_port = std::stoi(argv[2]);
    std::string role = argv[3];

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connection failed");
        close(client_fd);
        return 1;
    }

    std::cout << "[CLIENT] Connected to server" << std::endl;

    // Send role
    std::string role_msg = role + "\n";
    send(client_fd, role_msg.c_str(), role_msg.length(), 0);
    std::cout << "[CLIENT] Sent role: " << role << std::endl;

    // Wait for acknowledgment
    char buffer[1024];
    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0';
        std::cout << "[CLIENT] Server response: " << buffer;
    }

    if (role == "subscriber")
    {
        std::cout << "[CLIENT] Waiting for messages..." << std::endl;
        while (true)
        {
            bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0)
            {
                std::cout << "[CLIENT] Disconnected from server" << std::endl;
                break;
            }
            buffer[bytes_read] = '\0';
            std::cout << "[CLIENT] Received: " << buffer;
        }
    }
    else if (role == "publisher")
    {
        std::string message;
        while (true)
        {
            std::cout << "[CLIENT] Enter message (or 'quit' to exit): ";
            std::getline(std::cin, message);

            if (message == "quit")
            {
                break;
            }

            std::string msg_with_newline = message + "\n";
            send(client_fd, msg_with_newline.c_str(), msg_with_newline.length(), 0);
            std::cout << "[CLIENT] Sent: " << message << std::endl;

            // Wait for acknowledgment
            bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                std::cout << "[CLIENT] Server response: " << buffer;
            }
        }
    }

    close(client_fd);
    return 0;
}