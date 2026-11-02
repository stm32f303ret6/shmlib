# shmlib

A small, header-only C library for **single-producer / multi-consumer (SPMC)** "latest-snapshot" IPC over POSIX shared memory. Designed for low-latency robotics-style workloads where consumers always want the freshest message and old messages can be dropped.

Producers never block on consumers, consumers never block each other, and there are no syscalls or locks on the hot path — just atomic loads/stores against a triple-buffered ring in `/dev/shm`.

## Highlights

- **Header-only.** Drop `include/shmlib.h` into your project and link `-lrt`.
- **Lock-free.** All synchronization is via C11 atomics with explicit memory orders.
- **Zero-copy reads.** `shmlib_read_begin` returns a direct pointer into shared memory.
- **Latest-snapshot semantics.** A slow consumer never holds back the producer; it just sees newer messages on its next read.
- **Triple-buffered (3 slots).** Producer can always make progress as long as ≥ 1 non-latest slot is free.
- **C and C++ compatible.** The atomic primitives switch between `<stdatomic.h>` and GCC `__atomic_*` intrinsics automatically.
- **Cache-line aware.** Producer, consumer, and read-only fields live on separate 64 B cache lines to avoid false sharing.

## Requirements

- Linux (or any POSIX system with `shm_open` / `mmap`)
- A C11-capable compiler (GCC, Clang) — or any C++ compiler if you include from C++
- Link against `-lrt`

## Quick start

**producer.c**

```c
#include "message.h"
#include "shmlib.h"
#include <unistd.h>

int main(void) {
    shmlib_t *q = shmlib_init("demo", sizeof(MsgPose2D));
    if (!q) return 1;

    for (uint32_t seq = 1; ; seq++) {
        MsgPose2D *m = shmlib_write_begin(q);
        if (!m) { usleep(1000); continue; }      // all slots pinned by readers

        m->hdr.msg_type     = MSG_TYPE_POSE2D;
        m->hdr.msg_count    = seq;
        m->hdr.timestamp_ns = 0;
        m->x = 1.0; m->y = 2.0; m->theta = 0.0;

        shmlib_write_end(q, sizeof(*m));
        usleep(16000);                            // ~60 Hz
    }
}
```

**consumer.c**

```c
#include "message.h"
#include "shmlib.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    shmlib_t *q = NULL;
    while (!q) { q = shmlib_open("demo"); if (!q) usleep(10000); }

    for (;;) {
        size_t len;
        MsgPose2D *m = shmlib_read_begin(q, &len);
        if (!m) { usleep(1000); continue; }       // no new message

        if (len == sizeof(*m))
            printf("pose=(%.2f, %.2f, %.2f)\n", m->x, m->y, m->theta);

        shmlib_read_end(q);                        // mandatory
    }
}
```

Compile and run:

```bash
gcc -O2 -I include producer.c -lrt -o /tmp/producer
gcc -O2 -I include consumer.c -lrt -o /tmp/consumer
/tmp/producer & /tmp/consumer
```

The shared segment lives at `/dev/shm/shmlib_demo` until you call `shmlib_unlink("demo")` or remove it manually.

## Build

### Using shmlib in your CMake project

```cmake
add_subdirectory(third_party/shmlib)
target_link_libraries(your_target PRIVATE shmlib)
```

`shmlib` is an `INTERFACE` library that exposes `include/` and links `rt`.

### Building the bundled examples

The examples are not registered with CMake. Compile them directly:

```bash
gcc -O2 -Wall -I include examples/simple_producer.c            -lrt              -o /tmp/simple_producer
gcc -O2 -Wall -I include examples/simple_consumer.c            -lrt              -o /tmp/simple_consumer
gcc -O2 -Wall -I include examples/simple_pointcloud_producer.c -lrt              -o /tmp/simple_pointcloud_producer
gcc -O2 -Wall -I include examples/simple_pointcloud_consumer.c -lrt              -o /tmp/simple_pointcloud_consumer
gcc -O2 -Wall -I include examples/pointcloud_consumer.c        -lrt -lpthread    -o /tmp/pointcloud_consumer
```

Run the regression tests (using isolated files in `/tmp` for shared mappings):

```bash
gcc -std=gnu11 -O2 -Wall -Wextra -I include tests/test_shmlib.c -pthread -lrt -o /tmp/shmlib-test
/tmp/shmlib-test
```

## How it works

The shared region is laid out as:

```
+--------------------------+ <- offset 0
|  ro      (64 B)          |   magic, version, capacity   (set once)
+--------------------------+ <- offset 64
|  prod    (64 B)          |   publish_seq, sizes[3]      (producer-only writes)
+--------------------------+ <- offset 128
|  cons    (64 B)          |   slot_refcnt[3]             (consumer-only writes)
+--------------------------+ <- page-aligned
|  slot 0  (capacity B)    |
|  slot 1  (capacity B)    |
|  slot 2  (capacity B)    |
+--------------------------+
```

Each publish advances `publish_seq` (monotonic) and chooses slot `seq % 3`. Readers atomically increment `slot_refcnt[seq % 3]` to *pin* the latest slot, then re-check `publish_seq` to confirm the producer didn't race past — if so they release the pin and can retry. Publication and reference-count operations use sequentially consistent ordering so the producer and reader cannot both miss each other's updates. The producer's `write_begin` searches for a slot that is neither the latest nor pinned, returning `EAGAIN` if none is available.

A consumer that exits without releasing a pin can permanently reserve a slot; two abandoned slots can stop publishing. Recovery requires stopping all users of the channel, unlinking it, and recreating it. Rebuild both producers and consumers to use the corrected atomic ordering; the shared-memory layout is unchanged.

A fresh `shmlib_open` seeds its `last_read_seq` to the producer's current `publish_seq`, so consumers don't see stale messages from a previous producer session.

## API

```c
shmlib_t *shmlib_init  (const char *channel, size_t capacity);  // create or attach
shmlib_t *shmlib_open  (const char *channel);                   // attach only
void      shmlib_close (shmlib_t *q);
void      shmlib_unlink(const char *channel);                   // remove from /dev/shm
size_t    shmlib_capacity(const shmlib_t *q);

void *shmlib_write_begin (shmlib_t *q);              // NULL + EAGAIN if no slot free
int   shmlib_write_end   (shmlib_t *q, size_t len);  // 0 on success
void  shmlib_write_cancel(shmlib_t *q);

void *shmlib_read_begin(shmlib_t *q, size_t *len);   // NULL if no new message
void  shmlib_read_end  (shmlib_t *q);                // mandatory after a successful read_begin
```

`channel` is a bare name (e.g. `"demo"`); the on-disk object is `/dev/shm/shmlib_<channel>`.

A handle has at most one outstanding `write_begin` *or* `read_begin` at a time — a second call without the matching `_end` returns `EBUSY`.

`shmlib_init` rejects an existing channel smaller than the requested capacity with `EMSGSIZE`, and rejects incompatible or malformed layouts with `EINVAL`. It never automatically unlinks an existing channel. Incomplete initialization is retried up to 1000 times with 1 ms sleeps, then returns `EAGAIN`; callers may retry. Unrepresentable creation sizes return `EOVERFLOW`. `shmlib_open` returns `EAGAIN` immediately for incomplete initialization. Always keep writes within `shmlib_capacity(q)`; `write_end` cannot undo an out-of-bounds write.

## Messages

`include/message.h` defines a common `MsgHeader { msg_type, msg_count, timestamp_ns }` and a small set of typed payloads:

| `MsgType`              | Struct           | Notes                              |
|------------------------|------------------|------------------------------------|
| `MSG_TYPE_POSE2D`      | `MsgPose2D`      | x, y, theta                        |
| `MSG_TYPE_TWIST2D`     | `MsgTwist2D`     | vx, vy, omega                      |
| `MSG_TYPE_ACCEL2D`     | `MsgAccel2D`     | ax, ay, alpha                      |
| `MSG_TYPE_JOINT`       | `MsgJoint`       | joint_id, value                    |
| `MSG_TYPE_TRANSFORM`   | `MsgTransform`   | xyz + quaternion                   |
| `MSG_TYPE_SENSOR`      | `MsgSensor`      | sensor_id, value, min, max         |
| `MSG_TYPE_STATUS`      | `MsgStatus`      | status_code, flags                 |
| `MSG_TYPE_POINT_CLOUD` | `MsgPointCloud`  | flexible array; size with `msg_pointcloud_size()` |
| `MSG_TYPE_ROBOT_STATE` | `MsgRobotState`  | base pose/vel + 4 swerve modules   |
| `MSG_TYPE_ACTUATOR_CMD`| `MsgActuatorCmd` | steering + wheel commands          |

`include/msg_desc.h` provides parallel `FieldDesc` tables so a generic tool (logger, debugger) can walk a message by `msg_type` without recompiling. When you add a new message struct, also add its `MSG_*_FIELDS` array and a `MSG_DESCS` entry.

`include/utils.h` has `time_ns()` (CLOCK_MONOTONIC) and `msg_pointcloud_size(point_type, point_count)` for sizing variable-length point cloud payloads — pass the same value to `shmlib_write_end`.

## Examples

| File | What it does |
|------|--------------|
| `examples/simple_producer.c`            | Publishes `MsgPose2D` at ~60 Hz on channel `demo` (or `argv[1]`) |
| `examples/simple_consumer.c`            | Reads `MsgPose2D` from channel `demo` (or `argv[1]`) |
| `examples/simple_pointcloud_producer.c` | Publishes a 32-point XYZI cloud at ~30 Hz on `demo_pc` |
| `examples/simple_pointcloud_consumer.c` | Reads from `demo_pc` and validates `point_type` / `len` |
| `examples/pointcloud_consumer.c`        | Threaded reader of two channels (`cloud_fl`, `cloud_fr`) — run two pointcloud producers against those channel names yourself |

