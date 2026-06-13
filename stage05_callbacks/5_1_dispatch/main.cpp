#include <cstdio>
#include <functional>
#include <queue>
#include <iostream>

struct EventLoop {
    std::queue<std::function<void()>> ready_;

    void post(std::function<void()> cb);  // enqueue cb; it runs on the next run() iteration
    void run();                            // drain ready_ until empty; callbacks may post more
};

void EventLoop::post(std::function<void()> cb) {
    ready_.push(cb);
}

void EventLoop::run() {
    while (!ready_.empty()) {
        auto cb = ready_.front();
        ready_.pop();
        cb();
    }
}

// Print the current step, then post itself back to the loop if more steps remain.
// A and B share the same loop — interleaving emerges from FIFO order.
void task(EventLoop& loop, char name, int step, int total) {
    std::cout << name << step << std::endl;
    if (step < total) {
        loop.post([&loop, name, step, total] { task(loop, name, step + 1, total); });
    }
}

int main() {
    EventLoop loop;
    // Expected output: A1 B1 A2 B2 A3 B3
    loop.post([&] { task(loop, 'A', 1, 3); });
    loop.post([&] { task(loop, 'B', 1, 3); });
    loop.run();
}
