#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define BUFFER_SIZE 1024

int server_fd = -1; // Global for signal handler

void cleanup(int sig)
{
    if (server_fd != -1)
    {
        close(server_fd);
    }
    std::cout << "\n[SERVER] Shutting down" << std::endl;
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "usage: ./ server <IP> <port>" << std::endl;
        return 1;
    }

    const char *ip = argv[1];
    int port = std::stoi(argv[2]);

    signal(SIGINT, cleanup);

    // declaring variables
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    // create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    // address reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(server_fd);
        return 1;
    }

    // Bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        return 1;
    }

    // Listen
    if (listen(server_fd, 5) < 0)
    {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[SERVER] Listening on " << ip << ":" << port << std::endl;

    // Accept loop
    while (true)
    {
        client_socket = accept(server_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_socket < 0)
        {
            perror("accept failed");
            continue;
        }

        std::cout << "[SERVER] Client connected" << std::endl;

        // Handle multiple messages from the same client
        while (true)
        {
            std::memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

            if (bytes_read <= 0)
            {
                std::cout << "[SERVER] Client disconnected" << std::endl;
                break; // Client disconnected
            }

            std::cout << "[SERVER] Received: " << buffer << std::endl;

            std::string response = "Message received";
            send(client_socket, response.c_str(), response.length(), 0);
        }

        close(client_socket);
    }

    close(server_fd);
    return 0;
}