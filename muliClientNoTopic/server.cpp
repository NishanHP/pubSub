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
#include <string>
#include <sstream>

#define BUFFER_SIZE 1024

int server_fd = -1;
std::vector<int> subscribers; // Store subscriber socket descriptors
std::mutex subscribers_mutex; // Protect access to subscribers list
bool server_running = true;

void cleanup(int sig)
{
    server_running = false;
    if (server_fd != -1)
    {
        close(server_fd);
        server_fd = -1;
    }
}

// Function to trim whitespace and newlines from string
std::string trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Function to broadcast message to all subscribers
void broadcast_to_subscribers(const std::string &message)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);

    // Add message delimiter for proper framing
    std::string formatted_message = "[MESSAGE] " + message + "\n";

    auto it = subscribers.begin();
    while (it != subscribers.end())
    {
        int subscriber_fd = *it;
        int bytes_sent = send(subscriber_fd, formatted_message.c_str(), formatted_message.length(), 0);

        if (bytes_sent <= 0)
        {
            // Subscriber disconnected, remove from list
            std::cout << "[SERVER] Removing disconnected subscriber (fd: " << subscriber_fd << ")" << std::endl;
            close(subscriber_fd);
            it = subscribers.erase(it);
        }
        else
        {
            ++it;
        }
    }
    std::cout << "[SERVER] Broadcasted message to " << subscribers.size() << " subscribers" << std::endl;
}

// Function to add subscriber to the list
void add_subscriber(int client_socket)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);
    subscribers.push_back(client_socket);
    std::cout << "[SERVER] Subscriber added (fd: " << client_socket << "). Total subscribers: " << subscribers.size() << std::endl;
}

// Function to remove subscriber from the list
void remove_subscriber(int client_socket)
{
    std::lock_guard<std::mutex> lock(subscribers_mutex);
    auto it = std::find(subscribers.begin(), subscribers.end(), client_socket);
    if (it != subscribers.end())
    {
        subscribers.erase(it);
        std::cout << "[SERVER] Subscriber removed (fd: " << client_socket << "). Total subscribers: " << subscribers.size() << std::endl;
    }
}

void handle_client(int client_socket)
{
    char buffer[BUFFER_SIZE];

    // Read role from client
    std::memset(buffer, 0, BUFFER_SIZE);
    int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0)
    {
        std::cerr << "[SERVER] Client disconnected during role identification" << std::endl;
        close(client_socket);
        return;
    }

    // Trim the role string to remove newlines and whitespace
    std::string role = trim(std::string(buffer, bytes_read));
    std::cout << "[SERVER] Client role: '" << role << "'" << std::endl;

    if (role == "subscriber")
    {
        // Add to subscribers list
        add_subscriber(client_socket);

        // Send acknowledgment to subscriber
        std::string ack = "SUBSCRIBER_READY\n";
        send(client_socket, ack.c_str(), ack.length(), 0);

        // Keep the subscriber connection alive
        // Use blocking read to wait for disconnect
        char dummy_buffer[BUFFER_SIZE];
        while (server_running)
        {
            int check = read(client_socket, dummy_buffer, BUFFER_SIZE);
            if (check <= 0)
            {
                // Subscriber disconnected
                std::cout << "[SERVER] Subscriber disconnected (fd: " << client_socket << ")" << std::endl;
                break;
            }
            // If subscriber sends any data, we just ignore it
            // Subscribers should only receive messages
        }

        // Remove subscriber when they disconnect
        remove_subscriber(client_socket);
        close(client_socket);
    }
    else if (role == "publisher")
    {
        std::cout << "[SERVER] Publisher connected (fd: " << client_socket << ")" << std::endl;

        // Send acknowledgment to publisher
        std::string ack = "PUBLISHER_READY\n";
        send(client_socket, ack.c_str(), ack.length(), 0);

        // Handle publisher messages
        while (server_running)
        {
            std::memset(buffer, 0, BUFFER_SIZE);
            bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);

            if (bytes_read <= 0)
            {
                std::cout << "[SERVER] Publisher disconnected (fd: " << client_socket << ")" << std::endl;
                break;
            }

            // Trim the message
            std::string message = trim(std::string(buffer, bytes_read));
            if (message.empty())
            {
                continue; // Skip empty messages
            }

            std::cout << "[SERVER] Received from publisher: '" << message << "'" << std::endl;

            // Broadcast message to all subscribers
            broadcast_to_subscribers(message);

            // Send acknowledgment back to publisher
            std::string response = "MESSAGE_BROADCASTED\n";
            send(client_socket, response.c_str(), response.length(), 0);
        }
        close(client_socket);
    }
    else
    {
        std::cerr << "[SERVER] Unknown role: '" << role << "'" << std::endl;
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

    while (server_running)
    {
        client_socket = accept(server_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_socket < 0)
        {
            if (server_running)
            {
                perror("accept failed");
            }
            continue; // Continue to accept next client
        }

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
        std::cout << "[SERVER] Client connected (fd: " << client_socket << ")" << std::endl;
    }

    return 0;
}