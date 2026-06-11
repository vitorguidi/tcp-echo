#include <cstdio>
#include <iostream>
#include <string.h>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in
#include <thread>
#include <signal.h>



int main() {
    signal(SIGPIPE, [](int) {
            std::cout << "caught SIGPIPE\n";

    });
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

    auto server_handler = [](int server_sock) {
        while (true) {
            // char buf[1024];
            // int rcv = recv(server_sock, buf, 1024, 0);
            // if (rcv < 0) {
            //     std::cout << "recv failed : " << strerror(errno) << std::endl;
            //     return;
            // }
            // std::cout << "[thread " << std::this_thread::get_id() << "]received " << buf << " from socket " << server_sock << std::endl;
    std::cout << "Hello" << std::endl;

            std::string hello = "hello";
            int sent = send(server_sock, hello.c_str(), 6, 0);
            if (sent < 0) {
                std::cout << "send failed : " << strerror(errno) << std::endl;
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    };

    while (true) {
        int new_sock = accept(sock, (struct sockaddr *) &addr, &addr_len);
        if (new_sock < 0) {
            std::cout << "accept failed : " << strerror(errno) << std::endl;
            return 1;
        }
        std::thread svr = std::thread(server_handler, new_sock);
        svr.detach();
    }
  
}
