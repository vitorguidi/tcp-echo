#include <cstdio>
#include  <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/epoll.h>
#include <fcntl.h>
#include <thread>
#include <chrono>

void handle_child(int read_end_fd) {

    char buf[1024];

    // if we do not set as non blocking, read hangs until parent sends new data
    // or until it exits, then we get an EOF and read returns 0
    fcntl(read_end_fd, F_SETFL, O_NONBLOCK);


    int epoll_fd = epoll_create(1024);
    if (epoll_fd < 0) {
        std::cout << "epoll_create failed" << std::endl;
        return;
    }

    epoll_event event;
    // EPOLLIN ALONE IS LEVEL TRIGGERED, BOTH ARE EDGE TRIGGERED
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = read_end_fd;
    auto callback = []() {
        std::cout << "callback" << std::endl;
    };
    event.data.ptr = static_cast<void*>(&callback);

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, read_end_fd, &event);

    std::cout << "Starting epoll" << std::endl;

    while(epoll_wait(epoll_fd, &event, 1, 1000) <= 0) {
        std::cout << "fd not ready, sleeping." << std::endl;
    }

    (*static_cast<decltype(&callback)>(event.data.ptr))();


    int bytes_read = 0;
    while (true) {
        int rcv = read(read_end_fd, buf, 1024);
        if (rcv == 0) {
            std::cout << "read returned 0, breaking" << std::endl;
               break;
        }
        if (rcv < 0) {
            if (errno == EAGAIN) {
                std::cout << "Reached eagain, breaking" << std::endl;
                break;
            } else {
                std::cout << "read failed" << std::endl;
                return;
            }
        }
        bytes_read += rcv;
    }

    std::cout << "rcv = " << bytes_read << std::endl;
    std::cout << "buf = " << std::string(buf, bytes_read) << std::endl;

    int second_epoll = epoll_wait(epoll_fd, &event, 1 , 1);
    if (second_epoll > 0) {
        std::cout << "Expected EPOLLET to not notify after buffer is drained" << std::endl;
        return;
    }
    std::cout << "Second epoll wait returned no events, we are gucci" << std::endl;

}

void handle_parent(int write_end_fd) {


    std::string hello = "hello";
    const char *hello_ptr = hello.c_str();
    int sent = write(write_end_fd, hello_ptr, hello.size());

    if (sent == 0) {
        std::cout << "write failed" << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
    // std::this_thread::sleep_for(std::chrono::seconds(3));

    // sent = write(write_end_fd, hello_ptr, hello.size());

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
