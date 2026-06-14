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

// Simulate reading a raw CSV record from disk: "42,alice,99"
// Completes after 3 ticks; calls on_done with the raw record.
void async_read_disk(EventLoop& loop, std::function<void(std::string)> on_done) {
    printf("[tick %d] read_disk: submitted (latency = 3 ticks)\n", loop.tick_);
    loop.schedule(3, [on_done]() { on_done("42,alice,99"); });
}

// Parse "id,name,score" into a human-readable summary.
// Completes after 2 ticks; calls on_done with the result.
void async_process(EventLoop& loop, std::string read_result, std::function<void(std::string)> on_done) {
    printf("[tick %d] process: submitted read_result=\"%s\" (latency = 2 ticks)\n", loop.tick_, read_result.c_str());
    loop.schedule(2, [read_result, on_done]() {
        auto p1    = read_result.find(',');
        auto p2    = read_result.find(',', p1 + 1);
        auto id    = read_result.substr(0, p1);
        auto name  = read_result.substr(p1 + 1, p2 - p1 - 1);
        auto score = read_result.substr(p2 + 1);
        on_done(name + " (id=" + id + ") scored " + score);
    });
    
}

// Wire the two operations in sequence using nested callbacks.
//
// The equivalent coroutine body (Stage 9 replaces this with actual co_await):
//
//   auto raw    = co_await async_read_disk(loop);
//   auto result = co_await async_process(loop, raw);
//   on_result(result);
//
// Each lambda is the "rest of the coroutine" after that suspend point.
// The captured `raw` is the coroutine's saved stack frame — it keeps the
// value alive across the tick gap between the two stages.
void pipeline(EventLoop& loop, std::function<void(std::string)> on_result) {
    async_read_disk(loop, [&](std::string raw) {
        async_process(loop, raw, [&](std::string result) {
            on_result(result);
        });
    });
}

int main() {
    EventLoop loop;
    // Expected output:
    // [tick 0] read_disk: submitted (latency = 3 ticks)
    // [tick 3] process: submitted raw="42,alice,99" (latency = 2 ticks)
    // [tick 5] pipeline done: alice (id=42) scored 99
    pipeline(loop, [&](std::string result) {
        printf("[tick %d] pipeline done: %s\n", loop.tick_, result.c_str());
    });
    loop.run();
}
