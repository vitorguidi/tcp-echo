#include <cstdio>
#include <sys/socket.h>
#include <liburing.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

enum op_type { OP_ACCEPT, OP_RECV, OP_SEND };

struct EventData {
    op_type op_;
    char buffer_[1024];
    int fd_;
    EventData(op_type op, int fd) : op_(op), fd_(fd) {}
};

int main() {
    struct io_uring ring;
    if (io_uring_queue_init(1024, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listen_fd, 1024) < 0) { perror("listen"); return 1; }

    {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
        io_uring_sqe_set_data(sqe, new EventData(OP_ACCEPT, listen_fd));
        io_uring_submit(&ring);
    }

    while (true) {
        io_uring_submit_and_wait(&ring, 1);

        io_uring_cqe *cqe;
        unsigned head, count = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            EventData *ev = (EventData*)io_uring_cqe_get_data(cqe);
            switch (ev->op_) {
                case OP_ACCEPT: {
                    delete ev;
                    if (cqe->res >= 0) {
                        EventData *client = new EventData(OP_RECV, cqe->res);
                        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                        io_uring_prep_recv(sqe, client->fd_, client->buffer_, sizeof(client->buffer_), 0);
                        io_uring_sqe_set_data(sqe, client);
                        std::cout << "started conn on fd " << client->fd_ << std::endl;
                    }
                    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
                    io_uring_sqe_set_data(sqe, new EventData(OP_ACCEPT, listen_fd));
                    break;
                }
                case OP_RECV: {
                    if (cqe->res <= 0) {
                        std::cout << "closed conn on fd " << ev->fd_ << std::endl;
                        close(ev->fd_);
                        delete ev;
                        break;
                    }
                    ev->op_ = OP_SEND;
                    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_send(sqe, ev->fd_, ev->buffer_, cqe->res, 0);
                    io_uring_sqe_set_data(sqe, ev);
                    std::cout << "received " << std::string(ev->buffer_, cqe->res) << std::endl;
                    break;
                }
                case OP_SEND: {
                    ev->op_ = OP_RECV;
                    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, ev->fd_, ev->buffer_, sizeof(ev->buffer_), 0);
                    io_uring_sqe_set_data(sqe, ev);
                    break;
                }
                default:
                    _exit(1);
            }
            count++;
        }
        io_uring_cq_advance(&ring, count);
    }

    io_uring_queue_exit(&ring);
    return 0;
}
