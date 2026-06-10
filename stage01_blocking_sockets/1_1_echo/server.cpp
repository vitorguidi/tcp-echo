#include <cstdio>
#include <iostream>
#include <string.h>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (sock < 0) {
        std::cout << "socket failed creation : " << strerror(errno) << std::endl;
        return 1;
    }
    size_t opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))  < 0 ) {
        std::cout << "setsocketopt failed : " << strerror(errno) << std::endl;
        return 1;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        std::cout << "bind failed : " << strerror(errno) << std::endl;
        return 1;
    }
    if (listen(sock, 20) < 0) {
        std::cout << "listen failed : " << strerror(errno) << std::endl;
        return 1;
    }
    int new_sock = accept(sock, (struct sockaddr *) &addr, &addr_len);
    if (new_sock < 0) {
        std::cout << "accept failed : " << strerror(errno) << std::endl;
        return 1;
    }
    while (true) {
        char buf[1024];
        int rcv = recv(new_sock, buf, 1024, 0);
        if (rcv < 0) {
            std::cout << "recv failed : " << strerror(errno) << std::endl;
            return 1;
        }
        int sent = send(new_sock, buf, rcv, 0);
        if (sent < 0) {
            std::cout << "send failed : " << strerror(errno) << std::endl;
            return 1;
        }
    }
}
