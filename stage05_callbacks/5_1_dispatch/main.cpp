#include <cstdio>
#include <functional>
#include <queue>

struct EventLoop {
    std::queue<std::function<void()>> ready_;

    void post(std::function<void()> cb);  // enqueue cb; it runs on the next run() iteration
    void run();                            // drain ready_ until empty; callbacks may post more
};

// Print the current step, then post itself back to the loop if more steps remain.
// A and B share the same loop — interleaving emerges from FIFO order.
void task(EventLoop& loop, char name, int step, int total);

int main() {
    EventLoop loop;
    // Expected output: A1 B1 A2 B2 A3 B3
    loop.post([&] { task(loop, 'A', 1, 3); });
    loop.post([&] { task(loop, 'B', 1, 3); });
    loop.run();
}
