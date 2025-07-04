#include <iostream>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <algorithm>

class PubSubLoadBalancer
{
private:
    struct BackendServer
    {
        std::string ip;
        int port;
        bool is_healthy;
        std::chrono::steady_clock::time_point last_health_check;

        BackendServer(const std::string &ip, int port)
            : ip(ip), port(port), is_healthy(true), last_health_check(std::chrono::steady_clock::now()) {}
    };

    // to store backend server information in a vector
    // use vector for dynamic sizing
    std::vector<BackendServer> backends;
    std::atomic<int> current_backend{0}; // use to keep track on which backend server to use, atomic for thread safety
    std::mutex backend_mutex;            // mutex to protect access to backend server list

    // Load balancer settings
    std::string lb_ip;
    int lb_port;
    int lb_fd;
    std::atomic<bool> running{true};

    std::chrono::seconds health_check_interval{30}; // Reduced frequency
    std::thread health_check_thread;

public:
    // Constructor to initialize the load balancer with IP and port
    PubSubLoadBalancer(const std::string &ip, int port)
        : lb_ip(ip), lb_port(port), lb_fd(-1) {}

    // Desctructor to clean up resources
    ~PubSubLoadBalancer()
    {
        stop();
    }

    void add_backend(const std::string &ip, int port)
    {

        if (ip.empty() || port <= 1024 || port > 65535)
        {
            std::cerr << "[LOAD BALANCER] Invalid backend server IP or port." << std::endl;
            return;
        }

        // lock the mutex to ensure thread safety when adding a backend server
        std::lock_guard<std::mutex> lock(backend_mutex);
        backends.emplace_back(ip, port);
        std::cout << "[LOAD BALANCER] Added backend server: " << ip << ":" << port << std::endl;
    }

    BackendServer *get_next_healthy_backend()
    {
        // lock the mutex to ensure thread safety when accessing backend server list
        std::lock_guard<std::mutex> lock(backend_mutex);

        if (backends.empty())
            return nullptr;

        int attempts = 0;
        int max_attempts = backends.size();

        // Round robin logic to get next healthy backend server
        while (attempts < max_attempts)
        {
            int index = current_backend++ % backends.size();
            BackendServer &backend = backends[index];

            if (backend.is_healthy)
                return &backend;

            attempts++;
        }
        return nullptr;
    }

    // health check function
    bool check_backend_health(const std::string &ip, int port)
    {
        int test_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (test_fd < 0)
            return false;

        struct timeval timeout;
        timeout.tv_sec = 1; // Shorter timeout for health checks
        timeout.tv_usec = 0;
        setsockopt(test_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        setsockopt(test_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
        server_addr.sin_port = htons(port);

        bool healthy = (connect(test_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0);

        // Immediately close the connection to avoid it being treated as a client
        if (healthy)
        {
            shutdown(test_fd, SHUT_RDWR);
        }
        close(test_fd);

        return healthy;
    }

    void health_checker_loop()
    {
        while (running)
        {
            {
                std::lock_guard<std::mutex> lock(backend_mutex);
                for (auto &backend : backends)
                {
                    bool was_healthy = backend.is_healthy;
                    backend.is_healthy = check_backend_health(backend.ip, backend.port);

                    if (was_healthy != backend.is_healthy)
                    {
                        std::cout << "[LOAD BALANCER] Backend server " << backend.ip << ":" << backend.port
                                  << (backend.is_healthy ? " is healthy." : " is unhealthy.") << std::endl;
                    }
                }
            }
            std::this_thread::sleep_for(health_check_interval);
        }
    }

    int connect_to_backend(const BackendServer &backend)
    {
        int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (backend_fd < 0)
            return -1;

        struct sockaddr_in backend_addr;
        memset(&backend_addr, 0, sizeof(backend_addr));
        backend_addr.sin_family = AF_INET;
        backend_addr.sin_addr.s_addr = inet_addr(backend.ip.c_str());
        backend_addr.sin_port = htons(backend.port);

        if (connect(backend_fd, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0)
        {
            close(backend_fd);
            return -1;
        }

        return backend_fd;
    }

    // Helper function to send all data
    bool send_all(int fd, const char *data, int len)
    {
        int total_sent = 0;
        while (total_sent < len)
        {
            int sent = send(fd, data + total_sent, len - total_sent, MSG_NOSIGNAL);
            if (sent <= 0)
                return false;
            total_sent += sent;
        }
        return true;
    }

    void proxy_data(int client_fd, int backend_fd)
    {
        fd_set read_fds;
        int max_fd = std::max(client_fd, backend_fd);
        char buffer[4096];

        std::cout << "[LOAD BALANCER] Starting bidirectional proxy between client and backend" << std::endl;

        while (running)
        {
            FD_ZERO(&read_fds);
            FD_SET(client_fd, &read_fds);
            FD_SET(backend_fd, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

            if (activity < 0)
            {
                std::cerr << "[LOAD BALANCER] Select error: " << strerror(errno) << std::endl;
                break;
            }

            if (activity == 0)
            {
                // Timeout, continue to check running flag
                continue;
            }

            // Check if client sent data
            if (FD_ISSET(client_fd, &read_fds))
            {
                int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
                if (bytes <= 0)
                {
                    std::cout << "[LOAD BALANCER] Client disconnected" << std::endl;
                    break;
                }

                // Debug: print what we're forwarding from client with hex dump
                std::cout << "[LOAD BALANCER] Forwarding " << bytes << " bytes from client to backend" << std::endl;
                if (bytes < 100) // Only print short messages
                {
                    std::string data_str(buffer, bytes);
                    std::cout << "[LOAD BALANCER] Client data: '" << data_str << "'" << std::endl;

                    // Also print as hex for debugging
                    std::cout << "[LOAD BALANCER] Client data hex: ";
                    for (int i = 0; i < bytes; i++)
                    {
                        printf("%02x ", (unsigned char)buffer[i]);
                    }
                    std::cout << std::endl;
                }

                if (!send_all(backend_fd, buffer, bytes))
                {
                    std::cerr << "[LOAD BALANCER] Failed to send data to backend" << std::endl;
                    break;
                }
                std::cout << "[LOAD BALANCER] Successfully forwarded " << bytes << " bytes to backend" << std::endl;
            }

            // Check if backend sent data
            if (FD_ISSET(backend_fd, &read_fds))
            {
                int bytes = recv(backend_fd, buffer, sizeof(buffer), 0);
                if (bytes <= 0)
                {
                    std::cout << "[LOAD BALANCER] Backend disconnected" << std::endl;
                    break;
                }

                // Debug: print what we're forwarding from backend
                std::cout << "[LOAD BALANCER] Forwarding " << bytes << " bytes from backend to client" << std::endl;
                if (bytes < 100) // Only print short messages
                {
                    std::string data_str(buffer, bytes);
                    std::cout << "[LOAD BALANCER] Backend data: '" << data_str << "'" << std::endl;
                }

                if (!send_all(client_fd, buffer, bytes))
                {
                    std::cerr << "[LOAD BALANCER] Failed to send data to client" << std::endl;
                    break;
                }
            }
        }

        std::cout << "[LOAD BALANCER] Proxy loop ended" << std::endl;
    }

    void handle_client(int client_fd)
    {
        std::cout << "[LOAD BALANCER] New client connected." << std::endl;

        // Try to get a healthy backend server
        BackendServer *backend = get_next_healthy_backend();
        if (!backend)
        {
            std::cout << "[LOAD BALANCER] No healthy backend servers available." << std::endl;
            close(client_fd);
            std::cout << "[LOAD BALANCER] Client disconnected (no backends)." << std::endl;
            return;
        }

        // Connect to the selected backend
        int backend_fd = connect_to_backend(*backend);
        if (backend_fd < 0)
        {
            std::cout << "[LOAD BALANCER] Failed to connect to backend server: " << backend->ip << ":" << backend->port << std::endl;

            {
                std::lock_guard<std::mutex> lock(backend_mutex);
                backend->is_healthy = false; // Mark backend as unhealthy if connection fails
            }

            close(client_fd);
            std::cout << "[LOAD BALANCER] Client disconnected (backend connection failed)." << std::endl;
            return;
        }

        std::cout << "[LOAD BALANCER] Connected client to backend server: " << backend->ip << ":" << backend->port << std::endl;

        // Proxy data between client and backend - this will block until connection ends
        proxy_data(client_fd, backend_fd);

        // Close connections
        close(backend_fd);
        close(client_fd);
        std::cout << "[LOAD BALANCER] Connection closed with backend server: " << backend->ip << ":" << backend->port << std::endl;
        std::cout << "[LOAD BALANCER] Client disconnected." << std::endl;
    }

    void start()
    {
        lb_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (lb_fd < 0)
            throw std::runtime_error("Failed to create load balancer socket");

        // set socket options
        int opt = 1;
        if (setsockopt(lb_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            throw std::runtime_error("Failed to set socket options");
        }

        // bind socket
        struct sockaddr_in lb_addr;
        memset(&lb_addr, 0, sizeof(lb_addr));
        lb_addr.sin_family = AF_INET;
        lb_addr.sin_addr.s_addr = inet_addr(lb_ip.c_str());
        lb_addr.sin_port = htons(lb_port);

        if (bind(lb_fd, (struct sockaddr *)&lb_addr, sizeof(lb_addr)) < 0)
        {
            close(lb_fd);
            throw std::runtime_error("Failed to bind load balancer socket");
        }

        // Listen for connections
        if (listen(lb_fd, 5) < 0)
        {
            close(lb_fd);
            throw std::runtime_error("Failed to listen on load balancer socket");
        }

        std::cout << "[LOAD BALANCER] Load balancer started on " << lb_ip << ":" << lb_port << std::endl;

        // start health check thread - DISABLED FOR DEBUGGING
        // health_check_thread = std::thread(&PubSubLoadBalancer::health_checker_loop, this);

        // Accept incoming connections
        while (running)
        {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(lb_fd, (struct sockaddr *)&client_addr, &addr_len);

            if (client_fd < 0)
            {
                if (running)
                    std::cerr << "[LOAD BALANCER] Failed to accept client connection." << std::endl;
                continue;
            }

            // handle client connection in a separate thread
            std::thread client_thread(&PubSubLoadBalancer::handle_client, this, client_fd);
            client_thread.detach(); // Detach the thread to handle multiple clients concurrently
        }
    }

    void stop()
    {
        running = false;

        if (lb_fd != -1)
        {
            close(lb_fd);
            lb_fd = -1;
        }

        if (health_check_thread.joinable())
            health_check_thread.join();

        std::cout << "[LOAD BALANCER] Load balancer stopped." << std::endl;
    }
};

PubSubLoadBalancer *load_balancer = nullptr;

void signal_handler(int signal)
{
    std::cout << "\n[LOAD BALANCER] Received signal " << signal << ". Stopping load balancer..." << std::endl;
    if (load_balancer)
        load_balancer->stop();
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <load_balancer_ip> <load_balancer_port>" << std::endl;
        return 1;
    }

    std::string lb_ip = argv[1];
    int lb_port = std::stoi(argv[2]);

    try
    {
        PubSubLoadBalancer lb(lb_ip, lb_port);
        load_balancer = &lb;

        // for ctrl+c and termination signals
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        lb.add_backend("127.0.0.1", 8081);
        // lb.add_backend("127.0.0.1", 8082);  // Temporarily disable second backend

        std::cout << "[LB] Starting load balancer with " << 1 << " backend servers" << std::endl;

        // Start load balancer
        lb.start();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[LOAD BALANCER] Error: " << e.what() << std::endl;
        delete load_balancer;
        return 1;
    }

    delete load_balancer;
    return 0;
}