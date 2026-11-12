# pool — object storage, threads, buffers and process channels

A widget library still has to allocate nodes, read a file, decode an image, wait
for a child process and hand the result back to the paint thread. This module is
the headless set of primitives for all of it: storage that is recycled instead of
reallocated, work that runs off the paint thread, and channels that carry values
and messages between threads and between processes.

It is headless on purpose: Qt Core and the standard library, no widgets, no
tokens, no painting. The gallery is the only thing that draws its numbers.

| Facility | Header | What it is for |
| --- | --- | --- |
| `BlockPool` | `block_pool.h` | fixed-size raw blocks, no lifetime |
| `ObjectPool<T>` | `object_pool.h` | recycled storage, `T` constructed fresh per use |
| `ThreadPool` | `thread_pool.h` | work that must not run on the paint thread |
| `SpscRingBuffer<T>` | `ring_buffer.h` | the lossless, lock-free hand-off between two threads |
| `Channel<T>` | `channel.h` | the closeable, multi-producer / multi-consumer queue |
| `IoThread` | `io_thread.h` | one thread whose clock is deadlines, not frames |
| `Mailbox` | `mailbox.h` | the way a worker hands a closure back to the UI thread |
| `ipc::ChildProcess` | `ipc/child_process.h` | a child process over two pipes |
| `ipc::SharedRingBuffer` | `ipc/shared_ring.h` | a named ring in shared memory, mapped by both processes |
| `ipc::FrameDecoder` | `ipc/frame.h` | length-prefixed messages over a byte stream |

`pool.h` includes all of them.

## Object pools

`BlockPool` is the engine: fixed-size blocks, carved in chunks, threaded on an
intrusive free list. `ObjectPool<T>` puts an object on top of each block:
`create()` placement-constructs, `destroy()` runs the destructor, and the block
is reused. Storage is recycled; no state survives between users.

```cpp
md::pool::ObjectPool<Node> nodes;
{
    auto handle = nodes.createHandle();   // destroys itself at scope exit
    handle->value = 42;
}
```

* `create(args...)` starts a fresh `T`; a constructor that throws returns the
  block before the exception escapes.
* `destroy(nullptr)` is a no-op, so it composes with a capped pool.
* A capped pool (`maxBlocks`) makes `create()` return `nullptr` instead of
  growing — the caller applies backpressure instead of allocating.
* Nothing may be live when the pool is destroyed; a debug build asserts it.
* Reuse is LIFO, so the block returned last is handed out next.
* The pool is **not** thread-safe: it belongs to one thread, like the rest of
  the GUI-side layer. `ThreadPool` is the exception, and it serialises its own
  pool behind the queue mutex.

These two classes used to live in `structure`; they moved here so that storage
reuse and the code that reuses storage sit in one layer.

## ThreadPool

```cpp
md::pool::ThreadPool pool({.workers = 4, .queueCapacity = 1024});

pool.post([]{ /* fire and forget */ });

auto future = pool.submit([] { return 42; });   // future<int>
pool.parallelFor(1000, [](std::size_t index) { /* index runs once */ });
```

* The queue is bounded by default. When it is full, `post()` returns `false`
  and counts a rejection — it never blocks the caller and never grows without
  limit.
* `submit()` returns a `std::future`; if the task could not be queued it throws
  `std::runtime_error` rather than returning a future that never becomes ready.
* `parallelFor()` splits `[0, n)` into at most `workers × 4` chunks, blocks
  until every index has run, and asserts when called from a worker.
* `stop(true)` drains the queue and joins; `stop(false)` destroys the queued
  tasks and joins. `pause()` keeps the queue but runs nothing; `resume()`
  restarts it; `start()` after `stop()` rebuilds the worker set.
* A task that throws is counted in `failed`; the worker survives.

The task nodes themselves come from `md::pool::ObjectPool<TaskNode>`. The pool is
not thread-safe, so every `create`/`destroy` happens under the queue mutex; the
payoff is visible in `Stats::tasks` (capacity, live, high-water mark, chunks) and
in the gallery page.

## SpscRingBuffer

One producer, one consumer, fixed capacity (rounded up to a power of two), no
mutex. The producer owns `head`, the consumer owns `tail`, and each publishes its
cursor with a release store. A full ring rejects the write and increments
`dropped()`; it never overwrites and never blocks.

Counters are free-running `size_t`, so wrap-around is handled by unsigned
arithmetic. Each cursor lives on its own cache line.

## Channel

The general counterpart: any number of senders and receivers, an optional bound,
and `close()` as the termination signal.

```cpp
md::pool::Channel<Job> channel(64);
// producer: channel.send(std::move(job));
// consumer: while (auto job = channel.receive()) { ... }
```

`send()` blocks while a bounded channel is full; `trySend()` drops instead;
`close()` wakes every waiter, makes further sends fail, and lets receivers drain
what was already queued.

## IoThread

One thread and a deadline-ordered timer set:

```cpp
io.post(task);
io.postAfter(250ms, task);
const auto id = io.postEvery(1s, task);
io.cancel(id);
io.readFile(path, [](QByteArray bytes, QString error) { ... });
```

A due timer never waits behind a task that is not due yet. The loop sleeps until
the earliest deadline. Latency statistics are measured from the moment a task
became eligible, which is what tells a UI whether the thread is keeping up.

## Mailbox

`Mailbox` lives on the UI thread; any thread may post a closure:

```cpp
worker: mailbox.post([this, result] { label->setText(result); });
```

The wake-up is coalesced — a burst of a thousand posts costs one queued
meta-call — and closures run in post order on the owning thread. The queue lives
in a shared state object and the meta-call is addressed to the mailbox, so a post
that races destruction is dropped instead of touching freed memory.

## Process communication

Two same-machine transports, both parent/child:

**Pipes.** `ChildProcess::spawn()` creates two pipes, passes only those two
handles to the child (a `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` on Windows), and
reads its stdout on an internal reader thread. `write()` sends to its stdin.
Messages are framed by `encodeFrame()` / `FrameDecoder`: a 4-byte little-endian
length followed by the payload, capped at 1 MiB so a corrupt peer cannot make the
reader allocate arbitrarily.

**Shared memory.** `SharedRingBuffer::create(name, capacity)` maps a named
segment; `open(name, capacity)` maps the same one in the child. The header holds
magic, version, capacity and two atomic cursors; the payload follows. Cursors are
offsets, not pointers, because the two processes may map at different addresses.
`push()` writes a whole frame or nothing; a full ring counts a drop. Names are
per-session and carry the creating PID, so a crashed run cannot collide with the
next one.

The demo protocol is deliberately tiny:

```
PING <n>   -> PONG <n> <2n>      over the pipe
TICK <n>                          streamed into the shared ring
SHUTDOWN   -> exit 0
```

`apps/ipc_echo` is the child half. It is a console tool, not a widget.

## The gallery page

`apps/gallery` builds the module once, in `PoolDemo`, and the **Pool** page reads
it through a 120 ms ticker. Six cards: thread pool, object pool, ring buffer, IO
thread, thread communication, process communication. Nothing on the page is a
mock — submit a job, start the child, send a ping and the counters move.

`periodicity_gallery --pool-smoke out/shots/pool-live.png` drives every control
without a mouse, waits for the counters to move, and writes a screenshot; the
exit code is non-zero if anything stayed at zero.

## Costs

| Operation | Cost |
| --- | --- |
| `ObjectPool::create/destroy` | O(1) amortised; one chunk allocation per `chunkBlocks` objects |
| `ThreadPool::post` | one mutex, one pointer push |
| `ThreadPool::parallelFor` | chunks × post + one wait |
| `SpscRingBuffer::push/pop` | one atomic load, one atomic store |
| `Channel::send/receive` | one mutex, one condition-variable vote |
| `IoThread::postAfter` | one insertion into a `multimap` |
| `Mailbox::post` | one mutex push, one CAS for the wake-up |
| `SharedRingBuffer::push/pop` | two atomic cursors and a `memcpy` |

## Building and testing

The module is part of the repository build (`framework.md` §12.1). Its own test
suites live in `pool/tests/` and are registered by that directory's
`CMakeLists.txt`:

```bash
cmake --preset x64-debug
cmake --build --preset x64-debug --target periodicity_pool
ctest --preset x64-debug -R "tst_pool|tst_thread_pool|tst_ring_channel|tst_ipc"
```

| Suite | Covers |
| --- | --- |
| `tst_pool.cpp` | `BlockPool`, `ObjectPool`: reuse, lifetime, handle, bounds, allocation failure |
| `tst_thread_pool.cpp` | every task runs once, rejection, pause, both stop modes, `parallelFor` coverage, task-node reuse, a throwing task, restart |
| `tst_ring_channel.cpp` | 200k-item SPSC ordering, full/dropped, 4-producer channel, close wake-up, IO delay/repeat/cancel, file read, mailbox ordering |
| `tst_ipc.cpp` | split frames, oversized length, shared-ring create/open contract, a real child echo round trip, cross-process TICK stream |

The IPC suite skips its process tests when `periodicity_ipc_echo` is not built.
