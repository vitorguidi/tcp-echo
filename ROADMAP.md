# Networking Progression: Sockets, epoll, io_uring, and Coroutines

## Dependency Graph

```
1 → 2 → 3 → 4 → 5 → 6      (core path: sockets → epoll → callbacks → full pattern)
              ↓
              7 → 8          (io_uring: needs epoll for motivation)
         5 → 9               (coroutine basics: needs callbacks from Stage 5)
    4 + 9 → 10               (coroutines + epoll: needs Stage 4 + Stage 9)
    8 + 9 → 11               (coroutines + io_uring: needs Stage 8 + Stage 9)
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

### Exercise 4.1 — Non-blocking accept loop
Adapt the server from Stage 1 to use epoll on the listener socket.

- Register the listener fd with `EPOLLIN | EPOLLET`
- On event, loop `accept()` until `EAGAIN` (drain completely — required for edge-triggered)
- For each accepted fd, register it with epoll too
- On data event, `read()` until `EAGAIN`, echo back

**What you learn:** a single thread can handle many clients with no blocking. This is the event loop pattern.

### Exercise 4.2 — EPOLLHUP / EPOLLERR handling
Kill a client mid-transfer. Detect the disconnection on the server via `EPOLLHUP`.

- On `EPOLLERR | EPOLLHUP`: close the fd, remove from epoll with `EPOLL_CTL_DEL`
- Verify the server keeps running and accepts new clients

**What you learn:** broken connections must be explicitly removed from epoll or you'll get infinite error events.

---

## Stage 5 — Callbacks and Event Loops

**Goal:** learn `std::function` and inversion-of-control through a pure-C++ event loop — no sockets, no threads, no system clock.

**Core idea:** instead of a function *returning* a value, it accepts a callback — a `std::function` to call *when the value is ready*. The event loop owns the *when*; your code owns the *what*.

This directly motivates Stage 6 (epoll wires the same callback pattern to real kernel events) and Stage 9 (C++20 coroutines are structured callbacks with a compiler-generated state machine).

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
// Completes after 3 ticks; calls on_done with the raw data string.
void async_read_disk(EventLoop& loop, std::function<void(std::string)> on_done);

// Completes after 2 ticks; calls on_done with the processed result string.
void async_process(EventLoop& loop, std::string raw, std::function<void(std::string)> on_done);

// Chains the two operations using nested callbacks, then calls on_result.
void pipeline(EventLoop& loop, std::function<void(std::string)> on_result);
```

The equivalent coroutine body is already in the file as a comment. Your job is to write the callback version that produces the same result:

```
[tick 0] read_disk: submitted (latency = 3 ticks)
[tick 3] process: submitted (latency = 2 ticks)
[tick 5] pipeline done: ...
```

**What you learn:** each nested lambda in `pipeline` is a *resume point* — the code the coroutine executes after it wakes up. The lambda's capture list is the coroutine's saved stack frame. `co_await` in Stage 9 has the compiler generate exactly this nesting automatically; here you write it by hand so the transformation is not magic.

---

## Stage 6 — Full Pattern: epoll + Sockets + Callbacks

**Goal:** arrive at something equivalent to the book's `TCPSocket` / `TCPServer` but built bottom-up.

### Exercise 6.1 — Socket wrapper with recv_callback
Take the non-blocking socket from 4.1. Wrap it in a struct with a `recv_callback_`.

```cpp
struct Socket {
    int fd_;
    std::function<void(Socket*, std::string_view)> on_recv_;
    bool recv();  // reads available data, calls on_recv_, returns true if data read
};
```

**What you learn:** the callback wrapper is just a struct holding an fd and a `std::function`. All the book's complexity follows from this.

### Exercise 6.2 — Server with per-socket callbacks
Write a `Server` that:
1. Listens with epoll
2. On new connection: creates a `Socket`, assigns a callback, registers with epoll
3. On data: calls the socket's `on_recv_`
4. On disconnect: cleans up

**What you learn:** this is exactly `TCPServer::poll()` + `TCPServer::sendAndRecv()` — you've now reconstructed the book's design from first principles.

### Exercise 6.3 — Send buffering
Add outbound buffering: `Socket::send(data, len)` copies into a buffer. `Socket::flush()` writes it out.

- Call `flush()` from the epoll loop after processing all reads
- Observe that batching sends reduces syscall count

**What you learn:** this is `TCPSocket::send()` + `sendAndRcv()`. Decoupling "I want to send" from "actually write to the wire" lets you coalesce multiple logical messages into one syscall.

---

## Stage 7 — io_uring in Isolation

**Goal:** understand the io_uring submission/completion model using pipes, before any sockets.

**Prerequisite:** Stage 3 (epoll). The motivation for io_uring is understanding what epoll can't do: true async I/O where the kernel does the work and notifies you via a completion queue, without any syscall per operation.

### Exercise 7.1 — Basic SQE/CQE cycle
Submit a single async `read` on a pipe using io_uring. Wait for the completion.

```cpp
io_uring ring;
io_uring_queue_init(8, &ring, 0);

io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(sqe, pipe_read_fd, buf, sizeof(buf), 0);
io_uring_submit(&ring);

io_uring_cqe *cqe;
io_uring_wait_cqe(&ring, &cqe);   // blocks until done
// cqe->res is the number of bytes read
io_uring_cqe_seen(&ring, cqe);
```

- Write to the pipe from a thread, confirm the read completes
- Print `cqe->res` (bytes read) and `cqe->user_data`

**What you learn:** the core io_uring contract — submit ops to the SQ (submission queue), kernel executes them, completions appear in the CQ (completion queue). Zero syscalls after `io_uring_submit` until you call `io_uring_wait_cqe`.

### Exercise 7.2 — Batching: multiple ops in one submit
Submit 3 reads on 3 pipes in a single `io_uring_submit` call. Collect all 3 completions.

- Set `sqe->user_data` to the pipe index on each SQE
- Loop `io_uring_peek_cqe` to drain completions without blocking
- Compare total syscall count vs 3 separate `read()` calls (use `strace -c`)

**What you learn:** io_uring's key advantage — N operations submitted with 1 syscall. epoll requires a syscall per `epoll_ctl` add and then `epoll_wait`. io_uring amortises this.

### Exercise 7.3 — io_uring vs epoll: head-to-head
Implement the same pipe fan-out from Exercise 3.2 using io_uring instead of epoll.

- Write 10,000 messages across 3 pipes
- Measure throughput and latency with both approaches using `clock_gettime(CLOCK_MONOTONIC)`
- Try `IORING_SETUP_SQPOLL` flag (kernel-side polling — zero syscalls entirely)

**What you learn:** `SQPOLL` eliminates the `io_uring_submit` syscall too — the kernel thread polls the SQ continuously. Latency drops but CPU usage increases. Same tradeoff as busy-spin vs sleep in epoll.

---

## Stage 8 — io_uring + Sockets

**Goal:** replace pipes with TCP sockets, same pattern as the epoll→socket progression in Stages 3→4.

### Exercise 8.1 — Async accept
Use `io_uring_prep_accept` to accept connections without blocking.

- Submit an accept SQE on the listener fd
- On completion, re-submit another accept immediately (chain accepts)
- Print each accepted fd from the CQE

**What you learn:** with io_uring, accept is just another async op — no special listener loop, no `EAGAIN` handling. The kernel queues the next accept for you.

### Exercise 8.2 — Async echo server
Build a full echo server using only io_uring ops: `prep_accept` → `prep_recv` → `prep_send`.

- On accept completion: submit a recv SQE for the new fd
- On recv completion: submit a send SQE with the same data
- On send completion: submit another recv (loop)
- Use `user_data` to carry state between completions (connection id or a pointer)

**What you learn:** io_uring turns the event loop inside-out — instead of "wait for readiness then call syscall", you submit the I/O op upfront and react to completion. This matches how hardware DMA works.

### Exercise 8.3 — Fixed buffers
Register a buffer pool with `io_uring_register_buffers`. Use `prep_read_fixed` instead of `prep_read`.

- Allocate 4 buffers of 4KB each, register them
- Round-robin buffer assignment per connection
- Compare throughput with and without fixed buffers

**What you learn:** registered buffers avoid per-op kernel/user memory mapping — the kernel pins the memory once at registration. Important for high-frequency small messages.

---

## Stage 9 — Coroutine Basics

**Goal:** understand C++20 coroutines as a language feature, in complete isolation from networking.

**Prerequisite:** Stage 5 (callbacks). Coroutines are structured callbacks — instead of passing a function to call when work is done, you write code that suspends itself and resumes later. Same idea, radically different syntax.

### Exercise 9.1 — Generator with co_yield
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

### Exercise 9.2 — Custom awaiter with co_await
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

### Exercise 9.3 — Task<T> with return value
Implement `Task<int>` where `co_return 42` stores the value and the caller retrieves it.

- Chain two coroutines: `Task<int> inner()` returns a value, `Task<void> outer()` does `int x = co_await inner()`
- Print the value from `main`

**What you learn:** `co_await task` suspends the outer coroutine, runs the inner, then resumes with the result. This is structured concurrency — no callbacks, no shared state, sequential-looking code that is async underneath.

---

## Stage 10 — Coroutines + epoll

**Goal:** wire the coroutine machinery from Stage 9 to the epoll event loop from Stage 4.

**Prerequisites:** Stage 4 (epoll + sockets) + Stage 9 (coroutine basics).

### Exercise 10.1 — Awaitable fd read
Write an `AsyncRead` awaitable that suspends a coroutine until epoll signals a fd is readable.

```cpp
Task<ssize_t> async_read(EventLoop& loop, int fd, char* buf, size_t len) {
    co_await loop.wait_readable(fd);   // suspends here
    co_return ::read(fd, buf, len);    // resumes when epoll fires
}
```

- `wait_readable(fd)` registers the fd with epoll and stores the coroutine handle
- When epoll fires, the event loop calls `handle.resume()`

**What you learn:** epoll becomes the scheduler. The coroutine handle replaces the `recv_callback_` — instead of registering a lambda, you register a suspended coroutine.

### Exercise 10.2 — Coroutine echo server
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

- Compare the code length and structure with the callback version from Stage 6
- Both do the same thing — observe how coroutines eliminate the state machine

**What you learn:** callbacks require you to manually split your logic across multiple functions and store intermediate state externally. Coroutines let the compiler generate that state machine for you. The sequential-looking code IS the state machine.

---

## Stage 11 — Coroutines + io_uring

**Goal:** replace the epoll event loop with io_uring as the coroutine scheduler.

**Prerequisites:** Stage 8 (io_uring + sockets) + Stage 9 (coroutine basics).

### Exercise 11.1 — Awaitable io_uring op
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

### Exercise 11.2 — Coroutine echo server with io_uring
Port the coroutine echo server from 10.2 to use io_uring instead of epoll.

- `async_accept`, `async_recv`, `async_send` all submit SQEs
- The loop just drains CQEs and resumes handles
- Try chaining: submit the next recv SQE in the send completion (linked SQEs via `IOSQE_IO_LINK`)

**What you learn:** with io_uring + coroutines, the server is: submit work → sleep → work arrives → continue. No readiness polling, no manual non-blocking loops, no `EAGAIN`. The closest thing to blocking code that is actually async.
