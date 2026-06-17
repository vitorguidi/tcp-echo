#include <cstdio>
#include <iostream>
#include <thread>
#include <coroutine>

struct Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
private:
    handle_type h_;
public:
    struct promise_type {
        int current_answer_ = 0;
        Task get_return_object() {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
        std::suspend_always initial_suspend() { return {};}
        std::suspend_always final_suspend() noexcept {return {};}
        void unhandled_exception() {}
        void return_void() {}
        std::suspend_always yield_value(int x) {
            current_answer_ = x;
            return {};
        }
    };
    Task(handle_type h) : h_(h) {}
    ~Task() { if(h_) h_.destroy(); }

    void resume() {h_.resume();}
    bool done() const {return h_.done();}
    int value() {return h_.promise().current_answer_;}

};

struct Awaiter {
    std::coroutine_handle<> _h;
    int n_;
    void await_suspend(std::coroutine_handle<> h) {
        _h = h;
        std::this_thread::sleep_for(std::chrono::seconds(n_));
        h.resume();
    }
    // does not call h.resume, it runs as a reaction to h.resume
    int await_resume() {return 2;}
    bool await_ready() {return n_<=0;}
    Awaiter(int n) : n_(n) {}
};

Task sleep(int x) {
    int result = co_await Awaiter(x);
    std::cout << "sleep got back " << result << std::endl;
    co_return;
};

int main() {
    // TODO: implement stage09_coroutines/9_2_awaiter
    auto t = sleep(3);
    t.resume();
    std::cout << "awoke main" << std::endl;
    return 0;
}
