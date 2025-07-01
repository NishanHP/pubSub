#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

using namespace std;

int main(int argc, char *argv[])
{
    string ip = argv[1];
    int port = atoi(argv[2]);
    string role = argv[3];

    if (argc != 4)
    {
        cout << "Usage: ./client <IP> <Port> <Role>" << endl;
        return 1;
    }

    if (strcasecmp("publisher", role.c_str()) != 0 && strcasecmp("subscriber", role.c_str()) != 0)
    {
        cerr << "Invalid role. Use 'publisher' or 'subscriber'." << endl;
        return 1;
    }

    if (port < 1024 || port > 65535)
    {
        cerr << "Port number must be between 1024 and 65535." << endl;
        return 1;
    }

    // variable declaration
    int socket_fd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("socket creation failed");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0)
    {
        cerr << "Invalid address" << endl;
        return 1;
    }

    if (connect(socket_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection failed");
        cout << "Trying to connect to " << ip << ":" << port << endl;
        return 1;
    }

    cout << "[CLIENT] Successfully connected to server" << endl;

    // Send role to server first
    send(socket_fd, role.c_str(), role.length(), 0);

    if (strcasecmp("publisher", role.c_str()) == 0)
    {
        // Read server acknowledgment
        memset(buffer, 0, BUFFER_SIZE);
        read(socket_fd, buffer, BUFFER_SIZE - 1);
        cout << "[CLIENT] " << buffer << endl;

        while (true)
        {
            string message;
            cout << "Enter message to send: ";
            getline(cin, message);

            if (strcmp(message.c_str(), "exit") == 0)
            {
                cout << "Exiting client" << endl;
                break;
            }
            send(socket_fd, message.c_str(), message.length(), 0);

            // Read server response
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = read(socket_fd, buffer, BUFFER_SIZE - 1);
            if (bytes_read > 0)
            {
                cout << "[CLIENT] Server response: " << buffer << endl;
            }
        }
    }
    else
    {
        cout << "[CLIENT] Subscriber connected" << endl;
        memset(buffer, 0, BUFFER_SIZE);
        while (true)
        {
            int bytes_read = read(socket_fd, buffer, BUFFER_SIZE - 1);
            if (bytes_read <= 0)
            {
                std::cerr << "[CLIENT] Server disconnected" << std::endl;
                break; // Exit the loop if server disconnects
            }
            buffer[bytes_read] = '\0'; // Null-terminate the string
            std::cout << "[CLIENT] Received: " << buffer << std::endl;
            memset(buffer, 0, BUFFER_SIZE); // Clear the buffer for the next read
        }
    }

    close(socket_fd);
    return 0;
}