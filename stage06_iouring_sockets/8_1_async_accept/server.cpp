#include <cstdio>
#include <sys/socket.h>
#include <liburing.h>
#include <iostream>
#include <netinet/in.h>
#include <fcntl.h>

int main() {

    struct io_uring ring;
    int ret = io_uring_queue_init(1024, &ring, 0);
    if (ret < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

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

    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, conn_sock, NULL, NULL, 0);
    io_uring_submit(&ring);
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    io_uring_cqe_seen(&ring, cqe);

    int client_fd = cqe->res;

    send(client_fd, "Hello, world!", 13, 0);
    close(client_fd);
    io_uring_queue_exit(&ring);


    return 0;
}
