#include <cstdio>
#include <iostream>
#include <thread>
#include <coroutine>

template <typename T>
struct Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
public:
    handle_type h_;
    struct promise_type {
        T return_value_;
        std::coroutine_handle<> continuation_;
        Task get_return_object() {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }
        std::suspend_always initial_suspend() { return {};}
        struct ContinuationWaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                if (h.promise().continuation_) {
                    h.promise().continuation_.resume();
                }
            }
            void await_resume() noexcept {}
        };
        ContinuationWaiter final_suspend() noexcept {return {};}
        void unhandled_exception() {}
        void return_value(T val) {return_value_ = val;}
    };

    bool await_ready() {return false;}
    void await_suspend(std::coroutine_handle<> cont) {
        h_.promise().continuation_ = cont;
        h_.resume();
    }

    T await_resume() {
        return h_.promise().return_value_;
    }

    Task(handle_type h) : h_(h) {}
};

Task<int> inner() {
    co_return 42;
}

Task<int> outer() {
    auto val = co_await inner();
    co_return 2*val;
}


int main() {
    // TODO: implement stage09_coroutines/9_2_awaiter
    auto t = outer();
    t.h_.resume();
    std::cout << "awoke main: " << t.h_.promise().return_value_ << std::endl;
    return 0;
}
