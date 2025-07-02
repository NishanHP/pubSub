#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include <chrono>

#define BUFFER_SIZE 1024

int server_fd = -1;
std::vector<int> subscribers; // Store subscriber socket descriptors
std::mutex subscribers_mutex; // Protect access to subscribers list

void cleanup(int sig)
{
    if (server_fd != -1)
    {
        close(server_fd);
        return;
    }
}

// Function to broadcast message to all subscribers
void broadcast_to_subscribers(const std::string &message)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);

    auto it = subscribers.begin();
    while (it != subscribers.end())
    {
        int subscriber_fd = *it;
        int bytes_sent = send(subscriber_fd, message.c_str(), message.length(), 0);

        if (bytes_sent <= 0)
        {
            // Subscriber disconnected, remove from list
            std::cout << "[SERVER] Removing disconnected subscriber" << std::endl;
            close(subscriber_fd);
            it = subscribers.erase(it);
        }
        else
        {
            ++it;
        }
    }
    std::cout << "[SERVER] Broadcasted to " << subscribers.size() << " subscribers" << std::endl;
}

// Function to add subscriber to the list
void add_subscriber(int client_socket)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);
    subscribers.push_back(client_socket);
    std::cout << "[SERVER] Subscriber added. Total subscribers: " << subscribers.size() << std::endl;
}

// Function to remove subscriber from the list
void remove_subscriber(int client_socket)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);
    auto it = std::find(subscribers.begin(), subscribers.end(), client_socket);
    if (it != subscribers.end())
    {
        subscribers.erase(it);
        std::cout << "[SERVER] Subscriber removed. Total subscribers: " << subscribers.size() << std::endl;
    }
}

void handle_client(int client_socket)
{
    char buffer[BUFFER_SIZE];

    std::memset(buffer, 0, BUFFER_SIZE);
    int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0)
    {
        std::cerr << "[SERVER] Client disconnected during role identification" << std::endl;
        close(client_socket);
        return;
    }

    std::string role(buffer, bytes_read);
    std::cout << "[SERVER] Client role: " << role << std::endl;

    if (role == "subscriber")
    {
        // Add to subscribers list
        add_subscriber(client_socket);

        // Keep the subscriber connection alive
        // Subscribers don't send messages, they just receive
        while (true)
        {
            // Just keep the connection alive
            // The subscriber will receive messages via broadcast_to_subscribers
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Check if subscriber is still connected by trying to read (non-blocking would be better)
            char dummy;
            int check = recv(client_socket, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
            if (check == 0)
            {
                // Subscriber disconnected
                break;
            }
        }

        // Remove subscriber when they disconnect
        remove_subscriber(client_socket);
    }
    else if (role == "publisher")
    {
        std::cout << "[SERVER] Publisher connected" << std::endl;

        // Send acknowledgment to publisher
        std::string ack = "Publisher connected";
        send(client_socket, ack.c_str(), ack.length(), 0);

        // Handle publisher messages
        while (true)
        {
            std::memset(buffer, 0, BUFFER_SIZE);
            bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

            if (bytes_read <= 0)
            {
                std::cout << "[SERVER] Publisher disconnected" << std::endl;
                break;
            }

            std::string message(buffer, bytes_read);
            std::cout << "[SERVER] Received from publisher: " << message << std::endl;

            // Broadcast message to all subscribers
            broadcast_to_subscribers(message);

            // Send acknowledgment back to publisher
            std::string response = "Message broadcasted";
            send(client_socket, response.c_str(), response.length(), 0);
        }
        close(client_socket);
    }
    else
    {
        std::cerr << "[SERVER] Unknown role: " << role << std::endl;
        close(client_socket);
        return;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "usage: ./server <IP> <port>" << std::endl;
        return 1;
    }

    const char *ip = argv[1];
    int port = std::stoi(argv[2]);

    signal(SIGINT, cleanup);

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

    while (true)
    {
        client_socket = accept(server_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_socket < 0)
        {
            perror("accept failed");
            continue; // Continue to accept next client
        }

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
        std::cout << "[SERVER] Client connected" << std::endl;
    }
}