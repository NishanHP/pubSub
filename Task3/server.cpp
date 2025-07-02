#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <signal.h>

#define BUFFER_SIZE 1024

int server_fd = -1;
bool server_running = true; // Flag to control server loop

std::map<std::string, std::vector<int>> topic_subscribers;
std::mutex topic_mutex;

void cleanup(int sig)
{
    std::cout << "\n[SERVER] Received signal " << sig << ". Shutting down gracefully..." << std::endl;

    server_running = false; // Stop the accept loop

    if (server_fd != -1)
    {
        close(server_fd);
        server_fd = -1;
    }

    // Close all subscriber connections
    {
        std::lock_guard<std::mutex> lock(topic_mutex);
        for (auto &topic_pair : topic_subscribers)
        {
            for (int subscriber_fd : topic_pair.second)
            {
                close(subscriber_fd);
            }
        }
        topic_subscribers.clear();
    }

    std::cout << "[SERVER] Cleanup completed. Exiting..." << std::endl;
    exit(0);
}

// Helper function to read from socket until delimiter is found
std::string read_until_delimiter(int socket_fd, char delimiter = '\n')
{
    std::string result;
    char ch;

    while (true)
    {
        int bytes_read = read(socket_fd, &ch, 1);
        if (bytes_read <= 0)
        {
            // Connection closed or error
            return "";
        }

        if (ch == delimiter)
        {
            // Found delimiter, return the accumulated string
            break;
        }

        result += ch;
    }

    return result;
}

// Broadcast to every subscriber of a specific topic
void broadcast_to_subscribers(const std::string &topic, const std::string &message)
{
    std::lock_guard<std::mutex> lock(topic_mutex);
    auto it = topic_subscribers.find(topic);
    if (it != topic_subscribers.end())
    {
        auto &subscribers = it->second;
        auto subscriber_it = subscribers.begin();

        while (subscriber_it != subscribers.end())
        {
            int subscriber_fd = *subscriber_it;
            // Send message with newline delimiter to maintain consistency
            std::string message_with_delimiter = message + "\n";
            int byte_sent = send(subscriber_fd, message_with_delimiter.c_str(), message_with_delimiter.length(), 0);

            if (byte_sent <= 0)
            {
                std::cout << "[SERVER] Subscriber disconnected: " << subscriber_fd << std::endl;
                close(subscriber_fd);
                subscriber_it = subscribers.erase(subscriber_it);
            }
            else
            {
                std::cout << "[SERVER] Broadcasted to subscriber: " << subscriber_fd << std::endl;
                ++subscriber_it;
            }
        }
    }
    else
    {
        std::cout << "[SERVER] No subscribers for the topic " << topic << std::endl;
    }
}

void add_subscriber(const std::string &topic, int subscriber_fd)
{
    std::lock_guard<std::mutex> lock(topic_mutex);
    topic_subscribers[topic].push_back(subscriber_fd);
    std::cout << "[SERVER] Added subscriber " << subscriber_fd << std::endl;
}

void remove_subscriber(const std::string &topic, int subscriber_fd)
{
    std::lock_guard<std::mutex> lock(topic_mutex);
    auto it = topic_subscribers.find(topic);
    if (it != topic_subscribers.end())
    {
        it->second.erase(std::remove(it->second.begin(), it->second.end(), subscriber_fd), it->second.end());
        std::cout << "[SERVER] Removed subscriber " << subscriber_fd << std::endl;
    }
    else
    {
        std::cout << "[SERVER] No subscriber found for the topic" << topic << std::endl;
    }
}

void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];

    // Step 1: Read role from client using delimiter
    std::string role = read_until_delimiter(client_fd, '\n');
    if (role.empty())
    {
        std::cerr << "[SERVER] Error reading role from client or connection closed" << std::endl;
        close(client_fd);
        return;
    }

    std::cout << "[SERVER] Client role: '" << role << std::endl;

    // Step 2: Send acknowledgment for role
    std::string role_ack = "ROLE_ACK";
    if (send(client_fd, role_ack.c_str(), role_ack.length(), 0) <= 0)
    {
        std::cerr << "[SERVER] Failed to send role acknowledgment" << std::endl;
        close(client_fd);
        return;
    }

    // Step 3: Read topic from client using delimiter
    std::string topic = read_until_delimiter(client_fd, '\n');
    if (topic.empty())
    {
        std::cerr << "[SERVER] Error reading topic from client or connection closed" << std::endl;
        close(client_fd);
        return;
    }

    std::cout << "[SERVER] Client topic: '" << topic << std::endl;

    // Step 4: Send acknowledgment for topic and proceed based on role
    if (role == "subscriber")
    {
        add_subscriber(topic, client_fd);

        std::string sub_ack = "SUBSCRIBER_READY";
        send(client_fd, sub_ack.c_str(), sub_ack.length(), 0);

        // Keep subscriber connection alive
        while (server_running) // Check server status
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            char dummy;
            int check = recv(client_fd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
            if (check == 0) // Client disconnected
            {
                std::cout << "[SERVER] Subscriber disconnected: " << client_fd << std::endl;
                break;
            }
            else if (check < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                std::cout << "[SERVER] Error checking subscriber connection: " << client_fd << std::endl;
                break;
            }
        }

        remove_subscriber(topic, client_fd);
        close(client_fd);
    }
    else if (role == "publisher")
    {
        std::cout << "[SERVER] Publisher connected for topic: " << topic << std::endl;

        std::string pub_ack = "PUBLISHER_READY";
        send(client_fd, pub_ack.c_str(), pub_ack.length(), 0);

        // Handle publisher messages
        while (true)
        {
            std::string message = read_until_delimiter(client_fd, '\n');
            if (message.empty())
            {
                std::cout << "[SERVER] Publisher disconnected from topic: " << topic << std::endl;
                break;
            }

            std::cout << "[SERVER] Received message for topic '" << topic << "': " << message << std::endl;

            // Broadcast message to all subscribers of this topic
            broadcast_to_subscribers(topic, message);

            // Send acknowledgment back to publisher
            std::string response = "MESSAGE_BROADCASTED";
            send(client_fd, response.c_str(), response.length(), 0);
        }

        close(client_fd);
    }
    else
    {
        std::string error_msg = "UNKNOWN_ROLE";
        send(client_fd, error_msg.c_str(), error_msg.length(), 0);
        std::cout << "[SERVER] Unknown role: " << role << std::endl;
        close(client_fd);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <IP> <Port>" << std::endl;
        return 1;
    }

    const char *ip = argv[1];
    int port = std::stoi(argv[2]);

    signal(SIGINT, cleanup);

    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    // Create socket and assign to global server_fd
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(server_fd);
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[SERVER] Listening on " << ip << ":" << port << std::endl;

    while (server_running) // Use flag instead of true
    {
        client_socket = accept(server_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_socket < 0)
        {
            if (server_running) // Only show error if server is still running
            {
                perror("accept failed");
            }
            continue;
        }

        if (!server_running) // Check flag after accept
        {
            close(client_socket);
            break;
        }

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
        std::cout << "[SERVER] Client connected" << std::endl;
    }

    std::cout << "[SERVER] Main loop ended" << std::endl;
    close(server_fd);
    return 0;
}
