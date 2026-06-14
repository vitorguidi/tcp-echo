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

void EventLoop::schedule(int delay, std::function<void()> cb) {
    pq_.push({tick_ + delay, cb});
}

void EventLoop::run() {
    while (!pq_.empty()) {
        auto e = pq_.top();
        int next_tick = e.tick;
        tick_ = std::max(tick_, next_tick);
        while(!pq_.empty() && pq_.top().tick <= tick_) {
            e = pq_.top();
            pq_.pop();
            e.cb();
            tick_++;
        }
    }
}

// Print ping #n, then re-schedule itself until n == 3.
void ping(EventLoop& loop, int n) {
    printf("[tick %d] ping #%d\n", loop.tick_, n);
    if (n < 3) {
        loop.schedule(1, [&loop, n] { ping(loop, n + 1); });
    }
}

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
