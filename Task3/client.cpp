#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

using namespace std;

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        cout << "Usage: ./client <IP> <Port> <Role> <Topic>" << endl;
        return 1;
    }

    string ip = argv[1];
    int port = atoi(argv[2]);
    string role = argv[3];
    string topic = argv[4];

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

    // Step 1: Send role to server first (with newline delimiter)
    string role_msg = role + "\n";
    send(socket_fd, role_msg.c_str(), role_msg.length(), 0);
    cout << "[CLIENT] Sent role: " << role << endl;

    // Step 2: Wait for role acknowledgment
    memset(buffer, 0, BUFFER_SIZE);
    int ack_bytes = read(socket_fd, buffer, BUFFER_SIZE - 1);
    if (ack_bytes <= 0)
    {
        cerr << "[CLIENT] Failed to receive role acknowledgment" << endl;
        close(socket_fd);
        return 1;
    }
    cout << "[CLIENT] Role acknowledged: " << buffer << endl;

    // Step 3: Send topic to server (with newline delimiter)
    string topic_msg = topic + "\n";
    send(socket_fd, topic_msg.c_str(), topic_msg.length(), 0);
    cout << "[CLIENT] Sent topic: " << topic << endl;

    // Step 4: Wait for final acknowledgment based on role

    if (strcasecmp("publisher", role.c_str()) == 0)
    {
        cout << "[CLIENT] Publisher mode for topic: " << topic << endl;

        // Read final acknowledgment for publisher
        memset(buffer, 0, BUFFER_SIZE);
        ack_bytes = read(socket_fd, buffer, BUFFER_SIZE - 1);
        if (ack_bytes > 0)
        {
            cout << "[SERVER] " << buffer << endl;
        }
        else
        {
            cerr << "[CLIENT] Failed to receive publisher acknowledgment" << endl;
            close(socket_fd);
            return 1;
        }

        while (true)
        {
            string message;
            cout << "Enter message for topic '" << topic << "' (or 'exit' to quit): ";
            getline(cin, message);

            if (strcmp(message.c_str(), "exit") == 0)
            {
                cout << "Exiting publisher" << endl;
                break;
            }

            // Send message with newline delimiter for consistency
            string msg_with_delimiter = message + "\n";
            int bytes_sent = send(socket_fd, msg_with_delimiter.c_str(), msg_with_delimiter.length(), 0);
            if (bytes_sent <= 0)
            {
                cerr << "[CLIENT] Failed to send message" << endl;
                break;
            }

            // Read server response
            memset(buffer, 0, BUFFER_SIZE);
            int bytes_read = read(socket_fd, buffer, BUFFER_SIZE - 1);
            if (bytes_read > 0)
            {
                cout << "[SERVER] " << buffer << endl;
            }
            else
            {
                cerr << "[CLIENT] No response from server" << endl;
            }
        }
    }
    else
    {
        cout << "[CLIENT] Subscriber mode for topic: " << topic << endl;

        // Read final acknowledgment for subscriber
        memset(buffer, 0, BUFFER_SIZE);
        ack_bytes = read(socket_fd, buffer, BUFFER_SIZE - 1);
        if (ack_bytes > 0)
        {
            cout << "[SERVER] " << buffer << endl;
        }
        else
        {
            cerr << "[CLIENT] Failed to receive subscriber acknowledgment" << endl;
        }

        cout << "[CLIENT] Waiting for messages on topic '" << topic << "'..." << endl;

        // Read messages line by line (with newline delimiter)
        string received_message;
        char ch;
        while (true)
        {
            int bytes_read = read(socket_fd, &ch, 1);
            if (bytes_read <= 0)
            {
                cerr << "[CLIENT] Server disconnected or connection lost" << endl;
                break;
            }

            if (ch == '\n')
            {
                // End of message reached
                if (!received_message.empty())
                {
                    cout << "[TOPIC: " << topic << "] Received: " << received_message << endl;
                    received_message.clear();
                }
            }
            else
            {
                received_message += ch;
            }
        }
    }

    close(socket_fd);
    return 0;
}