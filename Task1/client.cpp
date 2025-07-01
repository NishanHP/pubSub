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

    if (argc != 3)
    {
        cout << "Usage: ./client <IP> <Port>" << endl;
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
        return 1;
    }

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

        memset(buffer, 0, BUFFER_SIZE);
        read(socket_fd, buffer, BUFFER_SIZE);
        cout << "[CLIENT] Received: " << buffer << endl;
    }

    close(socket_fd);
    return 0;
}