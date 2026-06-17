#include <cstdio>
#include <coroutine>
#include <iostream>


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

Task test(int n) {
    if (n<0)    co_return;
    int a=0, b =1;
    for (int i=0; i<=n; ++i) {
        co_yield a;
        a+= b;
        std::swap(a,b);
    }
}

int main() {
    auto t = test(20);
    for(int i=0;i<=20;i++) {
        t.resume();
        std::cout << t.value() << std::endl;
    }
    return 0;
}
