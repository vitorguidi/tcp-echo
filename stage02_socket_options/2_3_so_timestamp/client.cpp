#include <cstdio>
#include "sys/socket.h"
#include <netinet/in.h> // imports sockaddr_in
#include <string.h>
#include <iostream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <netinet/tcp.h>

int socket_factory() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0 ) {
        std::cout << "socket failed creation : " << strerror(errno) << std::endl;
        exit(1);
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
        std::cout << "fcntl failed : " << strerror(errno) << std::endl;
        exit(1);
    }

    int opt_val = 1;

    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val)) < 0) {
        std::cout << "setsockopt failed : " << strerror(errno) << std::endl;
        exit(1);
    }

    if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMP, &opt_val, sizeof(opt_val)) < 0) {
        std::cout << "setsockopt failed : " << strerror(errno) << std::endl;
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

    int sock = socket_factory();

    while (!connected) {
        int error = connect(sock, (sockaddr *) &addr, sizeof(addr));
        // errno is only set if the function returns failure, if it returns zero
        // the old value is still set, so undefined behavior.
        int err = error ? errno : 0;
        switch (err) {
            case 0:
                connected = true;
                std::cout << "connected" << std::endl;
                {
                    int quickack = 0;
                    setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
                }
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
                sock = socket_factory();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
        }
    }

    int n =0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        n++;
        char pt1[2], pt2[10];
        char cmgs_buff[1024];

        struct iovec iov[2];
        iov[0].iov_base = pt1;
        iov[0].iov_len = 2;
        iov[1].iov_base = pt2;
        iov[1].iov_len = 10;

        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        struct msghdr msg;

        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &client_addr;
        msg.msg_namelen = client_addr_len;
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        msg.msg_control = cmgs_buff;
        msg.msg_controllen = sizeof(cmgs_buff);


        ssize_t rcv = recvmsg(sock, &msg, 0);

        if (rcv < 0) {
            std::cout << "recvmsg failed : " << strerror(errno) << std::endl;
            continue;
        }

        std::cout << "rcv = " << rcv << std::endl;
        std::cout << "pt1 = " << pt1 << std::endl;
        std::cout << "pt2 = " << pt2 << std::endl;

        for(struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm != NULL; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SO_TIMESTAMP) {
                struct timeval *tv = (struct timeval *) CMSG_DATA(cm);
                std::cout << "Msg arrived at " << tv->tv_sec << "." << tv->tv_usec << std::endl;
            }
        }

    }

    return 0;
}
