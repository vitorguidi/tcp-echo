#include <cstdio>
#include  <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/epoll.h>
#include <thread>
#include <chrono>

void handle_child(int read_end_fd) {

    char buf[1024];

    int epoll_fd = epoll_create(1024);
    if (epoll_fd < 0) {
        std::cout << "epoll_create failed" << std::endl;
        return;
    }

    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = read_end_fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, read_end_fd, &event);

    while(epoll_wait(epoll_fd, &event, 1, 1) <= 0) {
        std::cout << "fd not ready, sleeping." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    int rcv = read(read_end_fd, buf, 1024);

    if (rcv == 0) {
        std::cout << "read failed" << std::endl;
        return;
    }

    std::cout << "rcv = " << rcv << std::endl;
    std::cout << "buf = " << std::string(buf, rcv) << std::endl;

    epoll_wait(epoll_fd, &event, 1 , -1);

    rcv = read(read_end_fd, buf, 1024);

    if (rcv == 0) {
        std::cout << "read failed" << std::endl;
        return;
    }

    std::cout << "rcv = " << rcv << std::endl;
    std::cout << "buf = " << std::string(buf, rcv) << std::endl;

}

void handle_parent(int write_end_fd) {

    std::string hello = "hello";
    const char *hello_ptr = hello.c_str();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    int sent = write(write_end_fd, hello_ptr, hello.size());

    if (sent == 0) {
        std::cout << "write failed" << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));

    sent = write(write_end_fd, hello_ptr, hello.size());

}


int main() {

    int pipe_fd[2];

    if (pipe(pipe_fd) < 0) {
        std::cout << "pipe failed : " << strerror(errno) << std::endl;
        return 1;
    }

    int proc = fork();

    if (proc < 0) {
        std::cout << "fork failed : " << strerror(errno) << std::endl;
        return 1;
    }

    if (proc == 0) {
        if(close(pipe_fd[1]) < 0) {
            std::cout << "close on write end failed on child: " << strerror(errno) << std::endl;
            return 1;
        }
        handle_child(pipe_fd[0]);
    } else {
        if(close(pipe_fd[0]) < 0) {
            std::cout << "close on read end failed on parent: " << strerror(errno) << std::endl;

        }
        handle_parent(pipe_fd[1]);
    }

    return 0;

}
