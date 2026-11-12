// Periodicity — pool: object storage, threads, buffers and process channels.
//
// The layer exists because a UI is not the only thread in the process, and
// because allocating per use is not free. It provides the things every
// off-thread or recycling feature needs, and nothing else:
//
//   BlockPool        fixed-size raw blocks, no lifetime
//   ObjectPool<T>    storage recycled, T constructed fresh on every create()
//   ThreadPool       work that must not run on the paint thread, recycling nodes
//   SpscRingBuffer   the lossless, lock-free hand-off between two threads
//   Channel          the closeable, multi-producer queue
//   IoThread         one thread whose clock is deadlines, not frames
//   Mailbox          the way a worker hands one closure back to the UI
//   ipc::*           the same-machine bridge: two pipes and a shared ring
//
// Everything is headless (Qt Core and the standard library), so the layer is
// usable by tests, by tools and by a headless server, not only by the widgets.

#pragma once

#include <md/pool/block_pool.h>
#include <md/pool/channel.h>
#include <md/pool/io_thread.h>
#include <md/pool/ipc/child_process.h>
#include <md/pool/ipc/frame.h>
#include <md/pool/ipc/shared_ring.h>
#include <md/pool/mailbox.h>
#include <md/pool/object_pool.h>
#include <md/pool/ring_buffer.h>
#include <md/pool/thread_pool.h>
