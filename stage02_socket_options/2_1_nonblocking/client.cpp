#include <cstdio>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in
#include <string.h>
#include <iostream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <thread>
#include <chrono>

int socket_factory(int port, sockaddr_in *addr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0 ) {
        std::cout << "socket failed creation : " << strerror(errno) << std::endl;
        exit(1);
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        std::cout << "fcntl failed : " << strerror(errno) << std::endl;
        exit(1);
    }

    return sock;

}

int main() {

    bool connected = false;

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);


    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) < 0) {
        std::cout << "inet_pton failed : " << strerror(errno) << std::endl;
        return 1;
    }

    int sock = socket_factory(8080, &addr);

    while (!connected) {
        int error = connect(sock, (sockaddr *) &addr, sizeof(addr));
        // errno is only set if the function returns failure, if it returns zero
        // the old value is still set, so undefined behavior.
        int err = error ? errno : 0;
        switch (errno) {
            case 0:
                connected = true;
                std::cout << "connected" << std::endl;
                break;
            case EINPROGRESS:
            case EALREADY:
                std::cout << "in progress" << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            case EISCONN:
                std::cout << "already connected" << std::endl;
                connected = true;
                break;
            default:
                if (close(sock) < 0) {
                    std::cout << "close failed : " << strerror(errno) << std::endl;
                    return 1;
                }
                std::cout << "connect failed : " << strerror(errno) << std::endl;
                sock = socket_factory(8080, &addr);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
        }
    }

    int n =0;
    while (true) {
        n++;
        std::cout << "Iteration " << n << std::endl;

        char buf[1024];
        int rcv = recv(sock, buf, 1024, 0);
        if (rcv != 6) {
            std::cout << "recv failed : " << strerror(errno) << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::cout << buf << std::endl;
    }

    return 0;
}
