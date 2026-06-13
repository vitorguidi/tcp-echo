#include <cstdio>
#include <functional>
#include <queue>
#include <string>
#include <vector>

// ── EventLoop (from 5.2, provided) ───────────────────────────────────────────

struct EventLoop {
    struct Entry {
        int tick;
        std::function<void()> cb;
        bool operator>(const Entry& o) const { return tick > o.tick; }
    };

    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq_;
    int tick_ = 0;

    void schedule(int delay, std::function<void()> cb) {
        pq_.push({tick_ + delay, std::move(cb)});
    }

    void run() {
        while (!pq_.empty()) {
            tick_ = pq_.top().tick;
            while (!pq_.empty() && pq_.top().tick == tick_) {
                auto cb = pq_.top().cb;
                pq_.pop();
                cb();
            }
        }
    }
};

// ── Your work below ───────────────────────────────────────────────────────────

// Submit an async disk read. Completes after 3 ticks.
// Calls on_done with the raw data string.
void async_read_disk(EventLoop& loop, std::function<void(std::string)> on_done);

// Submit async processing of raw data. Completes after 2 ticks.
// Calls on_done with the processed result string.
void async_process(EventLoop& loop, std::string raw, std::function<void(std::string)> on_done);

// Wire the two operations in sequence using nested callbacks.
//
// The equivalent coroutine body (Stage 9 replaces this with actual co_await):
//
//   auto raw    = co_await async_read_disk(loop);
//   auto result = co_await async_process(loop, raw);
//   on_result(result);
//
// Each lambda you write is the "rest of the coroutine" after that suspend point.
// The loop resumes the coroutine by calling the lambda.
// The lambda's captures are the coroutine's saved stack frame.
void pipeline(EventLoop& loop, std::function<void(std::string)> on_result);

int main() {
    EventLoop loop;
    // Expected output:
    // [tick 0] read_disk: submitted (latency = 3 ticks)
    // [tick 3] process: submitted (latency = 2 ticks)
    // [tick 5] pipeline done: ...
    pipeline(loop, [&](std::string result) {
        printf("[tick %d] pipeline done: %s\n", loop.tick_, result.c_str());
    });
    loop.run();
}
