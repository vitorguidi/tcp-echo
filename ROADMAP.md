# Networking Progression: Sockets, epoll, io_uring, and Coroutines

## Dependency Graph

```
1 → 2 → 3 → 4 → 5          (core path: sockets → epoll → callbacks)
              ↓
              6              (io_uring + sockets: needs epoll for motivation)
         5 → 7               (coroutine basics: needs callbacks from Stage 5)
    4 + 7 → 8                (coroutines + epoll)
    6 + 7 → 9                (coroutines + io_uring)
         4 → 10              (full echo server: epoll)
         6 → 11              (full echo server: io_uring)
         8 → 12              (full echo server: coroutines + epoll)
         9 → 13              (full echo server: coroutines + io_uring)
```

## Stage 1 — Raw TCP Sockets (blocking)

**Goal:** understand the syscall lifecycle for a TCP connection with no abstractions.

### Exercise 1.1 — Echo server (blocking)
Write a single-threaded blocking TCP echo server and client from scratch.

- Server: `socket()` → `bind()` → `listen()` → `accept()` → `read()` → `write()` loop
- Client: `socket()` → `connect()` → `write()` → `read()`
- No helper functions, no classes — raw syscalls only
- Print every syscall return value

**What you learn:** the exact sequence of calls needed to establish a TCP connection and exchange data.

### Exercise 1.2 — Multiple clients (blocking, multi-thread)
Extend 1.1 so the server spawns a `std::thread` per accepted connection.

- Each thread handles one client: read → echo → close
- Server main thread loops on `accept()`
- Verify with 3 simultaneous clients using `telnet` or `nc`

**What you learn:** why blocking + threads doesn't scale — one thread per connection is untenable at thousands of connections.

---

## Stage 2 — Socket Options

**Goal:** understand the knobs that matter for low-latency before introducing non-blocking I/O.

### Exercise 2.1 — Non-blocking flag
Take the client from 1.1. Set `O_NONBLOCK` via `fcntl()`. Call `read()` immediately after `connect()`.

- Observe `EWOULDBLOCK` / `EINPROGRESS`
- Add a busy-wait loop that retries until data arrives
- Compare behaviour with and without the flag

**What you learn:** what non-blocking actually means — the call returns immediately instead of sleeping.

### Exercise 2.2 — TCP_NODELAY
Write a client that sends 10 single-byte messages with and without `TCP_NODELAY`.

- Use `gettimeofday()` to timestamp each send/recv pair
- Print the round-trip latency for each message
- Observe Nagle batching in the non-NODELAY case

**What you learn:** Nagle's algorithm adds latency by coalescing small packets — critical to disable in trading systems.

### Exercise 2.3 — SO_TIMESTAMP
Receive a message and extract the kernel receive timestamp from the `cmsg` ancillary data.

- Enable `SO_TIMESTAMP` via `setsockopt()`
- Use `recvmsg()` with a `msghdr` to read the `SCM_TIMESTAMP` control message
- Print: kernel timestamp, `clock_gettime(CLOCK_REALTIME)` at recv, difference

**What you learn:** kernel timestamps are more accurate than userspace timestamps — the kernel stamps the packet when it hits the socket buffer, before your thread even wakes up.

---

## Stage 3 — epoll in Isolation

**Goal:** understand the epoll API with zero network complexity using pipes.

### Exercise 3.1 — epoll with a single pipe
Create a pipe. Register the read end with epoll. Write to the write end from a thread. Read in the epoll loop.

```
pipe(fds) → epoll_create(1) → epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &ev{EPOLLIN})
→ loop: epoll_wait() → read(fds[0])
```

- Use `epoll_wait(..., timeout=0)` (non-blocking poll)
- Use `epoll_wait(..., timeout=-1)` (blocking wait)
- Observe the difference

**What you learn:** `epoll_wait` with timeout=0 is a poll — it returns immediately. timeout=-1 blocks until an event fires. Low-latency code uses timeout=0 in a spin loop.

### Exercise 3.2 — EPOLLET (edge-triggered) vs EPOLLIN (level-triggered)
Write 5 bytes to a pipe. Call `epoll_wait` twice without reading.

- Level-triggered (default): both calls return an event
- Edge-triggered (`EPOLLET`): only the first call returns an event

**What you learn:** edge-triggered fires once per state change. You must drain the fd completely or you'll miss data. The book uses `EPOLLET` — this is why the server loops on `accept()` until it gets `EAGAIN`.

### Exercise 3.3 — void* payload in epoll_event
Store a pointer to a struct in `epoll_event.data.ptr`. Retrieve it on event and call a method on it.

```cpp
struct Handler { std::string name_; void handle() { ... } };
Handler h{"pipe-0"};
ev.data.ptr = &h;
// on event:
reinterpret_cast<Handler*>(event.data.ptr)->handle();
```

**What you learn:** this is the exact trick `TCPServer` uses to recover a `TCPSocket*` from an epoll event — zero-overhead polymorphism via void pointer round-trip.

---

## Stage 4 — epoll + Non-blocking Sockets

**Goal:** replace the pipes from Stage 3 with real TCP sockets.

### Exercise 4.1 — Non-blocking accept loop with disconnect handling
Adapt the server from Stage 1 to use epoll on the listener socket.

- Register the listener fd with `EPOLLIN | EPOLLET`
- On event, loop `accept()` until `EAGAIN` (drain completely — required for edge-triggered)
- For each accepted fd, register it with epoll too
- On data event, `read()` until `EAGAIN`, echo back
- On `EPOLLERR | EPOLLHUP`: close the fd, remove from epoll with `EPOLL_CTL_DEL`
- On `recv()` returning 0 (clean FIN): same cleanup as above
- Force an abrupt disconnect with `ss -K dst 127.0.0.1 dport = <port>` and verify the server keeps running

**What you learn:** a single thread can handle many clients with no blocking. Broken connections must be explicitly removed from epoll or you'll get infinite error events. Clean shutdown (FIN) arrives as `EPOLLIN` with `recv() == 0`; abrupt teardown (RST) arrives as `EPOLLHUP`.

---

## Stage 5 — Callbacks and Event Loops

**Goal:** learn `std::function` and inversion-of-control through a pure-C++ event loop — no sockets, no threads, no system clock.

**Core idea:** instead of a function *returning* a value, it accepts a callback — a `std::function` to call *when the value is ready*. The event loop owns the *when*; your code owns the *what*.

This directly motivates Stage 7 (C++20 coroutines are structured callbacks with a compiler-generated state machine).

---

### Exercise 5.1 — FIFO dispatch loop

Implement `EventLoop::post()` and `EventLoop::run()`.

```cpp
struct EventLoop {
    std::queue<std::function<void()>> ready_;

    void post(std::function<void()> cb);  // enqueue cb; runs on the next run() iteration
    void run();                            // drain ready_ until empty; callbacks may post more
};
```

Implement `task(loop, name, step, total)`: prints its step, then posts itself back to the loop if more steps remain.

Expected output:
```
[A] step 1/3
[B] step 1/3
[A] step 2/3
[B] step 2/3
[A] step 3/3
[B] step 3/3
```

**What you learn:** `post()` is a cooperative yield — the caller relinquishes control back to the loop, which decides what runs next. Two logical tasks share one thread with zero synchronization. This is how every async runtime (Node.js, Tokio, asyncio) works at its core.

### Exercise 5.2 — Tick-based scheduler (priority queue)

Extend `EventLoop` with `schedule(delay, cb)` and a tick counter.

```cpp
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
```

- `schedule(delay, cb)` stores `(tick_ + delay, cb)` in the min-heap
- `run()` jumps `tick_` to the next due deadline, fires all callbacks at that tick, repeats
- Callbacks may call `schedule()` — newly added items land in the heap for future ticks

Implement the recurring `ping`: prints its number and re-schedules itself until `n == 3`.

Expected output (registration order does not matter — only deadlines do):
```
[tick 1] ping #1
[tick 2] retry
[tick 4] ping #2
[tick 5] heartbeat
[tick 7] ping #3
[tick 8] timeout
```

**What you learn:** a priority queue is a scheduler. The same structure underlies every timer wheel, `setTimeout` implementation, and deadline-aware event loop. Jumping `tick_` to the next due entry — rather than advancing one tick at a time — is the "skip idle ticks" optimisation all real runtimes use.

### Exercise 5.3 — Simulated coroutine via continuation-passing

Using the tick-based `EventLoop` (provided at the top of the file), implement three functions:

```cpp
// Reads a raw CSV record "id,name,score" from disk. Completes after 3 ticks.
void async_read_disk(EventLoop& loop, std::function<void(std::string)> on_done);

// Parses "id,name,score" into a human-readable summary. Completes after 2 ticks.
// `raw` is the value produced by async_read_disk and passed into this stage.
void async_process(EventLoop& loop, std::string raw, std::function<void(std::string)> on_done);

// Chains the two operations using nested callbacks, then calls on_result.
void pipeline(EventLoop& loop, std::function<void(std::string)> on_result);
```

The data flows through explicitly: `async_read_disk` produces `"42,alice,99"`, which `pipeline` captures and passes as the `raw` argument to `async_process`. Inside `async_process`, the lambda captures `raw` by value so it stays alive across the 2-tick gap. That capture is what a coroutine frame does for you automatically.

Expected output:
```
[tick 0] read_disk: submitted (latency = 3 ticks)
[tick 3] process: submitted raw="42,alice,99" (latency = 2 ticks)
[tick 5] pipeline done: alice (id=42) scored 99
```

**What you learn:** each nested lambda in `pipeline` is a *resume point*. The value produced by one stage (`raw`) flows to the next as a captured variable — the closure keeps it alive across the tick gap, exactly as a coroutine frame keeps local variables alive across a `co_await`. In Stage 7, the compiler generates this closure and nesting automatically; here you write it by hand so the transformation is not magic.

---

## Stage 6 — io_uring + Sockets

**Goal:** learn io_uring by building directly on TCP sockets — the completion model contrasted against the readiness model from Stage 4.

**Prerequisite:** Stage 3 (epoll). The motivation for io_uring is understanding what epoll can't do: true async I/O where the kernel does the work and notifies you via a completion queue, without any syscall per operation.

### Exercise 6.1 — Async accept
Use `io_uring_prep_accept` to accept connections without blocking.

```cpp
io_uring ring;
io_uring_queue_init(8, &ring, 0);

io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
io_uring_submit(&ring);

io_uring_cqe *cqe;
io_uring_wait_cqe(&ring, &cqe);  // blocks until a connection arrives
// cqe->res is the accepted fd
io_uring_cqe_seen(&ring, cqe);
```

- On completion, re-submit another accept SQE immediately (chain accepts)
- Print each accepted fd from the CQE

**What you learn:** the core io_uring contract — submit ops to the SQ, kernel executes them, completions appear in the CQ. With io_uring, accept is just another async op — no special listener loop, no `EAGAIN` handling.

### Exercise 6.2 — Async echo server
Build a full echo server using only io_uring ops: `prep_accept` → `prep_recv` → `prep_send`.

- On accept completion: submit a recv SQE for the new fd
- On recv completion: submit a send SQE with the same data
- On send completion: submit another recv (loop)
- Use `user_data` to carry state between completions (connection id or a pointer)

**What you learn:** io_uring turns the event loop inside-out — instead of "wait for readiness then call syscall", you submit the I/O op upfront and react to completion. This matches how hardware DMA works.

---

## Stage 7 — Coroutine Basics

**Goal:** understand C++20 coroutines as a language feature, in complete isolation from networking.

**Prerequisite:** Stage 5 (callbacks). Coroutines are structured callbacks — instead of passing a function to call when work is done, you write code that suspends itself and resumes later. Same idea, radically different syntax.

### Exercise 7.1 — Generator with co_yield
Write a coroutine that generates a Fibonacci sequence using `co_yield`.

```cpp
Generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto tmp = a + b; a = b; b = tmp;
    }
}
```

- Implement `Generator<T>` with `promise_type`, `yield_value`, `coroutine_handle`
- Iterate with a range-for loop

**What you learn:** the coroutine machinery — `promise_type` controls suspension/resumption, `coroutine_handle` is the resume token, `co_yield` suspends and hands a value to the caller.

### Exercise 7.2 — Custom awaiter with co_await
Write an `Awaitable` that suspends a coroutine and resumes it after a condition is met.

```cpp
struct ReadyAfter {
    int ticks_;
    bool await_ready() { return ticks_ == 0; }
    void await_suspend(std::coroutine_handle<> h) { /* store h, resume later */ }
    void await_resume() {}
};

Task<void> my_coro() {
    co_await ReadyAfter{3};   // suspends here
    std::cout << "resumed\n";
}
```

- Implement `Task<T>` (the return type)
- Implement a simple scheduler that calls `resume()` on stored handles

**What you learn:** `await_ready/suspend/resume` are the three hooks the compiler calls at a `co_await`. The coroutine handle is the "callback" — you store it and call it when you're ready. This is callbacks with better syntax.

### Exercise 7.3 — Task<T> with return value
Implement `Task<int>` where `co_return 42` stores the value and the caller retrieves it.

- Chain two coroutines: `Task<int> inner()` returns a value, `Task<void> outer()` does `int x = co_await inner()`
- Print the value from `main`

**What you learn:** `co_await task` suspends the outer coroutine, runs the inner, then resumes with the result. This is structured concurrency — no callbacks, no shared state, sequential-looking code that is async underneath.

---

## Stage 8 — Coroutines + epoll

**Goal:** wire the coroutine machinery from Stage 7 to the epoll event loop from Stage 4.

**Prerequisites:** Stage 4 (epoll + sockets) + Stage 7 (coroutine basics).

### Exercise 8.1 — Awaitable fd read
Write an `AsyncRead` awaitable that suspends a coroutine until epoll signals a fd is readable.

```cpp
Task<ssize_t> async_read(EventLoop& loop, int fd, char* buf, size_t len) {
    co_await loop.wait_readable(fd);   // suspends here
    co_return ::read(fd, buf, len);    // resumes when epoll fires
}
```

- `wait_readable(fd)` registers the fd with epoll and stores the coroutine handle
- When epoll fires, the event loop calls `handle.resume()`

**What you learn:** epoll becomes the scheduler. The coroutine handle replaces a registered callback — instead of registering a lambda, you register a suspended coroutine.

### Exercise 8.2 — Coroutine echo server
Rewrite the echo server from Stage 4 using coroutines. Each connection gets its own coroutine.

```cpp
Task<void> handle_connection(EventLoop& loop, int fd) {
    char buf[1024];
    while (true) {
        auto n = co_await async_read(loop, fd, buf, sizeof(buf));
        if (n <= 0) break;
        co_await async_write(loop, fd, buf, n);
    }
}
```

- Compare code length and structure with the raw epoll version from Stage 4
- Observe how coroutines eliminate manual fd-to-state routing

**What you learn:** in Stage 4 you manually tracked which fd was readable and dispatched to the right handler. Here the coroutine frame *is* the per-connection state — no routing map, no split across multiple functions. The sequential-looking code IS the state machine.

---

## Stage 9 — Coroutines + io_uring

**Goal:** replace the epoll event loop with io_uring as the coroutine scheduler.

**Prerequisites:** Stage 6 (io_uring + sockets) + Stage 7 (coroutine basics).

### Exercise 9.1 — Awaitable io_uring op
Write an `IoUringAwaitable` that submits an SQE and suspends until the CQE arrives.

```cpp
Task<ssize_t> async_read(IoUringLoop& loop, int fd, char* buf, size_t len) {
    co_return co_await loop.read(fd, buf, len);
    // submits SQE with user_data = coroutine_handle
    // resumes when CQE arrives with user_data
}
```

- Store the coroutine handle in `sqe->user_data`
- In the event loop: `io_uring_wait_cqe` → cast `cqe->user_data` back to `coroutine_handle` → `resume()`

**What you learn:** io_uring and coroutines are a natural fit — the CQE is a completion notification, the coroutine handle is the continuation. `user_data` is the glue. No callbacks, no lambdas.

### Exercise 9.2 — Coroutine echo server with io_uring
Port the coroutine echo server from 8.2 to use io_uring instead of epoll.

- `async_accept`, `async_recv`, `async_send` all submit SQEs
- The loop just drains CQEs and resumes handles
- Try chaining: submit the next recv SQE in the send completion (linked SQEs via `IOSQE_IO_LINK`)

**What you learn:** with io_uring + coroutines, the server is: submit work → sleep → work arrives → continue. No readiness polling, no manual non-blocking loops, no `EAGAIN`. The closest thing to blocking code that is actually async.

---
