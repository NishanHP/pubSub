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
#include <sys/socket.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define MAX_MESSAGE_LENGTH 4096
#define MAX_TOPIC_LENGTH 256
#define MAX_ROLE_LENGTH 64

int server_fd = -1;
bool server_running = true;

std::map<std::string, std::vector<int>> topic_subscribers;
std::mutex topic_mutex;

void cleanup(int sig)
{
    std::cout << "\n[SERVER] Received signal " << sig << ". Shutting down..." << std::endl;

    server_running = false;

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

// Secure function to read until delimiter with length limits
std::string read_until_delimiter(int socket_fd, char delimiter = '\n', size_t max_length = MAX_MESSAGE_LENGTH)
{
    std::string result;
    char ch;

    // Reserve space to avoid frequent reallocations
    result.reserve(std::min(max_length, static_cast<size_t>(256)));

    std::cout << "[SERVER] Starting to read until delimiter '" << delimiter << "'" << std::endl;

    while (result.length() < max_length)
    {
        int bytes_read = read(socket_fd, &ch, 1);
        if (bytes_read <= 0)
        {
            // Connection closed or error
            std::cerr << "[SERVER] Read error or connection closed. Bytes read: " << bytes_read
                      << ", errno: " << errno << " (" << strerror(errno) << ")" << std::endl;
            std::cout << "[SERVER] Partial data received: '" << result << "'" << std::endl;
            return "";
        }

        std::cout << "[SERVER] Read char: '" << ch << "' (0x" << std::hex << (unsigned char)ch << std::dec << ")" << std::endl;

        if (ch == delimiter)
        {
            // Found delimiter, return the accumulated string
            std::cout << "[SERVER] Found delimiter, returning: '" << result << "'" << std::endl;
            break;
        }

        // Only add printable characters and common whitespace (except delimiter)
        if ((ch >= 32 && ch <= 126) || ch == '\t' || ch == '\r')
        {
            result += ch;
        }
        else
        {
            std::cout << "[SERVER] Ignoring non-printable character: 0x" << std::hex << (unsigned char)ch << std::dec << std::endl;
        }
    }

    // If we reached max_length without finding delimiter, it's suspicious
    if (result.length() >= max_length)
    {
        std::cerr << "[SERVER] Warning: Message exceeded maximum length limit" << std::endl;
        return "";
    }

    std::cout << "[SERVER] Final result: '" << result << "'" << std::endl;
    return result;
}

// Secure send function with error handling
bool secure_send(int socket_fd, const std::string &message)
{
    if (message.empty() || message.length() > MAX_MESSAGE_LENGTH)
    {
        std::cerr << "[SERVER] Invalid message length for sending" << std::endl;
        return false;
    }

    size_t total_sent = 0;
    size_t message_len = message.length();
    const char *data = message.c_str();

    while (total_sent < message_len)
    {
        int bytes_sent = send(socket_fd, data + total_sent, message_len - total_sent, MSG_NOSIGNAL);
        if (bytes_sent <= 0)
        {
            if (errno == EPIPE || errno == ECONNRESET)
            {
                std::cerr << "[SERVER] Client disconnected during send" << std::endl;
            }
            else
            {
                std::cerr << "[SERVER] Send error: " << strerror(errno) << std::endl;
            }
            return false;
        }
        total_sent += bytes_sent;
    }

    return true;
}

// Input validation function
bool validate_input(const std::string &input, size_t max_length, const std::string &field_name)
{
    if (input.empty())
    {
        std::cerr << "[SERVER] Empty " << field_name << " received" << std::endl;
        return false;
    }

    if (input.length() > max_length)
    {
        std::cerr << "[SERVER] " << field_name << " exceeds maximum length (" << max_length << ")" << std::endl;
        return false;
    }

    // Check for valid characters (alphanumeric, underscore, hyphen, dot)
    for (char c : input)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
        {
            std::cerr << "[SERVER] Invalid character in " << field_name << ": " << c << std::endl;
            return false;
        }
    }

    return true;
}

void broadcast_to_subscribers(const std::string &topic, const std::string &message)
{
    // Validate inputs
    if (!validate_input(topic, MAX_TOPIC_LENGTH, "topic") ||
        message.empty() || message.length() > MAX_MESSAGE_LENGTH)
    {
        std::cerr << "[SERVER] Invalid topic or message for broadcasting" << std::endl;
        return;
    }

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

            if (!secure_send(subscriber_fd, message_with_delimiter))
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
    if (!validate_input(topic, MAX_TOPIC_LENGTH, "topic"))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(topic_mutex);
    topic_subscribers[topic].push_back(subscriber_fd);
    std::cout << "[SERVER] Added subscriber " << subscriber_fd << " to topic: " << topic << std::endl;
}

void remove_subscriber(const std::string &topic, int subscriber_fd)
{
    std::lock_guard<std::mutex> lock(topic_mutex);
    auto it = topic_subscribers.find(topic);
    if (it != topic_subscribers.end())
    {
        it->second.erase(std::remove(it->second.begin(), it->second.end(), subscriber_fd), it->second.end());
        std::cout << "[SERVER] Removed subscriber " << subscriber_fd << " from topic: " << topic << std::endl;
    }
    else
    {
        std::cout << "[SERVER] No subscriber found for the topic: " << topic << std::endl;
    }
}

void handle_client(int client_fd)
{
    // Step 1: Read role from client using delimiter with length limit
    std::string role = read_until_delimiter(client_fd, '\n', MAX_ROLE_LENGTH);
    if (role.empty() || !validate_input(role, MAX_ROLE_LENGTH, "role"))
    {
        std::cerr << "[SERVER] Error reading role from client or invalid role" << std::endl;
        close(client_fd);
        return;
    }

    std::cout << "[SERVER] Client role: '" << role << "'" << std::endl;

    // Step 2: Send acknowledgment for role
    std::string role_ack = "ROLE_ACK\n";
    if (!secure_send(client_fd, role_ack))
    {
        std::cerr << "[SERVER] Failed to send role acknowledgment" << std::endl;
        close(client_fd);
        return;
    }

    // Step 3: Read topic from client using delimiter with length limit
    std::string topic = read_until_delimiter(client_fd, '\n', MAX_TOPIC_LENGTH);
    if (topic.empty() || !validate_input(topic, MAX_TOPIC_LENGTH, "topic"))
    {
        std::cerr << "[SERVER] Error reading topic from client or invalid topic" << std::endl;
        close(client_fd);
        return;
    }

    std::cout << "[SERVER] Client topic: '" << topic << "'" << std::endl;

    // Step 4: Send acknowledgment for topic and proceed based on role
    if (role == "subscriber")
    {
        add_subscriber(topic, client_fd);

        std::string sub_ack = "SUBSCRIBER_READY\n";
        if (!secure_send(client_fd, sub_ack))
        {
            std::cerr << "[SERVER] Failed to send subscriber acknowledgment" << std::endl;
            remove_subscriber(topic, client_fd);
            close(client_fd);
            return;
        }

        // Keep subscriber connection alive
        while (server_running)
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

        std::string pub_ack = "PUBLISHER_READY\n";
        if (!secure_send(client_fd, pub_ack))
        {
            std::cerr << "[SERVER] Failed to send publisher acknowledgment" << std::endl;
            close(client_fd);
            return;
        }

        // Handle publisher messages
        while (server_running)
        {
            std::string message = read_until_delimiter(client_fd, '\n', MAX_MESSAGE_LENGTH);
            if (message.empty())
            {
                std::cout << "[SERVER] Publisher disconnected from topic: " << topic << std::endl;
                break;
            }

            std::cout << "[SERVER] Received message for topic '" << topic << "': " << message << std::endl;

            // Broadcast message to all subscribers of this topic
            broadcast_to_subscribers(topic, message);

            // Send acknowledgment back to publisher
            std::string response = "MESSAGE_BROADCASTED\n";
            if (!secure_send(client_fd, response))
            {
                std::cout << "[SERVER] Failed to send acknowledgment to publisher" << std::endl;
                break;
            }
        }

        close(client_fd);
    }
    else
    {
        std::string error_msg = "UNKNOWN_ROLE\n";
        secure_send(client_fd, error_msg);
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

    // Validate IP address format
    struct sockaddr_in sa;
    int ip_valid = inet_pton(AF_INET, ip, &(sa.sin_addr));
    if (ip_valid != 1)
    {
        std::cerr << "Invalid IP address format: " << ip << std::endl;
        return 1;
    }

    int port;
    try
    {
        port = std::stoi(argv[2]);
        if (port <= 0 || port > 65535)
        {
            std::cerr << "Invalid port number. Must be between 1 and 65535." << std::endl;
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Invalid port number: " << argv[2] << std::endl;
        return 1;
    }

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    int client_socket;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    // Create socket and assign to global server_fd
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    // tells the kernel we want to reuse the address
    //  This is useful for development to avoid "Address already in use" errors
    //  when restarting the server quickly.
    //  This is doing by SO_REUSEADDR option.
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(server_fd);
        return 1;
    }

    // To user kernel load balancer
    // Note: this does not reinitiate connection that lost. only the server instance available
    // Client have to reconnect to the server manually.
    // if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    // {
    //     perror("setsockopt failed");
    //     close(server_fd);
    //     return 1;
    // }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
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

    while (server_running)
    {
        client_socket = accept(server_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_socket < 0)
        {
            if (server_running)
            {
                perror("accept failed");
            }
            continue;
        }

        if (!server_running)
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