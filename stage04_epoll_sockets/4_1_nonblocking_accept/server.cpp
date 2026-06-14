#include <cstdio>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in
#include <string.h>
#include <iostream>
#include <sys/epoll.h>
#include <vector>
#include  <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>


int main() {
    // TODO: implement stage05_epoll_sockets/5_1_nonblocking_accept/server
    int conn_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_sock == -1) {
        perror("socket");
        return 1;
    }

    if (fcntl(conn_sock, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        return 1;
    }

    int opt = 1;

    if (setsockopt(conn_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(conn_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(conn_sock, 1024) < 0) {
        perror("listen");
        return 1;
    }

    epoll_event conn_event;
    conn_event.events = EPOLLIN;
    conn_event.data.fd = conn_sock;

    epoll_event events[1024];

    int epoll_fd = epoll_create(1024);
    if (epoll_fd == -1) {
        perror("epoll_create");
        return 1;
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &conn_event) < 0) {
        perror("epoll_ctl");
        return 1;
    }


    while (true) {
        int nr_events = epoll_wait(epoll_fd, events, 1024, -1);
        for (int i = 0; i < nr_events; i++) {
            if (events[i].data.fd == conn_sock && (events[i].events & EPOLLIN)) {
                int new_sock = accept(conn_sock, NULL, NULL);
                if (new_sock == -1) {
                    perror("accept");
                    return 1;
                }
                fcntl(new_sock, F_SETFL, O_NONBLOCK);
                epoll_event new_event;
                new_event.events = EPOLLIN | EPOLLHUP;
                new_event.data.fd = new_sock;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_sock, &new_event) < 0) {
                    perror("epoll_ctl");
                    return 1;
                }
                continue;
            } else if (events[i].data.fd != conn_sock && events[i].events & EPOLLHUP) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                std::cout << "Client abruptly disconnected on fd" << events[i].data.fd << std::endl;
                continue;
            } else if (events[i].data.fd != conn_sock && events[i].events & EPOLLIN) {
                char buffer[1024];
                int read = recv(events[i].data.fd, buffer, 1024, 0);
                if (read == -1) {
                    if (errno == EAGAIN) continue;
                    perror("recv");
                    return 1;
                }
                if (read == 0) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                    close(events[i].data.fd);
                    std::cout << "Client disconnected" << std::endl;
                    continue;
                }
                std::cout << buffer << std::endl;
                send(events[i].data.fd, buffer, read, 0);
            } else {
                std::cout << "Unknown event" << std::endl;
            }
        } 
    }


}
