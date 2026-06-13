#include <cstdio>
#include <functional>
#include <queue>
#include <vector>

struct EventLoop {
    struct Entry {
        int tick;
        std::function<void()> cb;
        bool operator>(const Entry& o) const { return tick > o.tick; }
    };

    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq_;
    int tick_ = 0;

    void schedule(int delay, std::function<void()> cb);  // enqueue at tick_ + delay
    void run();  // jump to next due tick, fire all callbacks there, repeat
};

// Print ping #n, then re-schedule itself until n == 3.
void ping(EventLoop& loop, int n);

int main() {
    EventLoop loop;

    // Three one-shot timers registered in arbitrary order.
    // run() must fire them sorted by deadline.
    loop.schedule(5, [&] { printf("[tick %d] heartbeat\n", loop.tick_); });
    loop.schedule(2, [&] { printf("[tick %d] retry\n",     loop.tick_); });
    loop.schedule(8, [&] { printf("[tick %d] timeout\n",   loop.tick_); });

    loop.schedule(1, [&] { ping(loop, 1); });

    // Expected output:
    // [tick 1] ping #1
    // [tick 2] retry
    // [tick 4] ping #2
    // [tick 5] heartbeat
    // [tick 7] ping #3
    // [tick 8] timeout
    loop.run();
}
