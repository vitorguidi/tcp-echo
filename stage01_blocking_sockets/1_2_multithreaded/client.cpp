#include <cstdio>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in
#include <string.h>
#include <iostream>
#include <arpa/inet.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0 ) {
        std::cout << "socket failed creation : " << strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) < 0) {
        std::cout << "inet_pton failed : " << strerror(errno) << std::endl;
        return 1;
    }

    if (connect(sock, (sockaddr *) &addr, sizeof(addr)) < 0) {
        std::cout << "connect failed : " << strerror(errno) << std::endl;
        return 1;
    }

    int n =0;
    while (true) {
        n++;
        char buf[1024];
        int sent = send(sock, "hello", 6, 0);
        if (sent != 6) {
            std::cout << "send failed : " << strerror(errno) << std::endl;
            return 1;
        }
        int rcv = recv(sock, buf, 1024, 0);
        if (rcv != 6) {
            std::cout << "recv failed : " << strerror(errno) << std::endl;
            return 1;
        }
        std::cout << "Iteration " << n << std::endl;
        std::cout << buf << std::endl;
    }

    return 0;
}
