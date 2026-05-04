#ifndef SHMLIB_H
#define SHMLIB_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CACHE_LINE 64
#define NUM_BUFFERS 3
#define SHMLIB_NO_BUFFER UINT32_MAX
#define SHMLIB_MAGIC 0x53484D4C
#define SHMLIB_PREFIX "/shmlib_"
#define SHMLIB_NAME_MAX 256

#ifdef __cplusplus
#define SHMLIB_ATOMIC(type) type
#define SHMLIB_MO_RELAXED __ATOMIC_RELAXED
#define SHMLIB_MO_ACQUIRE __ATOMIC_ACQUIRE
#define SHMLIB_MO_RELEASE __ATOMIC_RELEASE
#define SHMLIB_MO_ACQ_REL __ATOMIC_ACQ_REL
#define SHMLIB_ATOMIC_LOAD_U64(ptr, mo) __atomic_load_n((ptr), (mo))
#define SHMLIB_ATOMIC_LOAD_U32(ptr, mo) __atomic_load_n((ptr), (mo))
#define SHMLIB_ATOMIC_STORE_U64(ptr, val, mo) __atomic_store_n((ptr), (val), (mo))
#define SHMLIB_ATOMIC_STORE_U32(ptr, val, mo) __atomic_store_n((ptr), (val), (mo))
#define SHMLIB_ATOMIC_FETCH_ADD_U32(ptr, val, mo) __atomic_fetch_add((ptr), (val), (mo))
#define SHMLIB_ATOMIC_FETCH_SUB_U32(ptr, val, mo) __atomic_fetch_sub((ptr), (val), (mo))
#define SHMLIB_FENCE_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define SHMLIB_FENCE_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)
#define SHMLIB_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#include <stdatomic.h>
#define SHMLIB_ATOMIC(type) _Atomic type
#define SHMLIB_MO_RELAXED memory_order_relaxed
#define SHMLIB_MO_ACQUIRE memory_order_acquire
#define SHMLIB_MO_RELEASE memory_order_release
#define SHMLIB_MO_ACQ_REL memory_order_acq_rel
#define SHMLIB_ATOMIC_LOAD_U64(ptr, mo) atomic_load_explicit((ptr), (mo))
#define SHMLIB_ATOMIC_LOAD_U32(ptr, mo) atomic_load_explicit((ptr), (mo))
#define SHMLIB_ATOMIC_STORE_U64(ptr, val, mo) atomic_store_explicit((ptr), (val), (mo))
#define SHMLIB_ATOMIC_STORE_U32(ptr, val, mo) atomic_store_explicit((ptr), (val), (mo))
#define SHMLIB_ATOMIC_FETCH_ADD_U32(ptr, val, mo) atomic_fetch_add_explicit((ptr), (val), (mo))
#define SHMLIB_ATOMIC_FETCH_SUB_U32(ptr, val, mo) atomic_fetch_sub_explicit((ptr), (val), (mo))
#define SHMLIB_FENCE_ACQUIRE() atomic_thread_fence(memory_order_acquire)
#define SHMLIB_FENCE_RELEASE() atomic_thread_fence(memory_order_release)
#define SHMLIB_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

// Header in shared memory - snapshot slots for single producer / many consumers
typedef struct {
    uint32_t magic;
    SHMLIB_ATOMIC(uint32_t) version;
    uint64_t capacity;
    uint8_t _pad[CACHE_LINE - 16];
} shmlib_header_ro_t;

typedef struct {
    SHMLIB_ATOMIC(uint64_t) publish_seq;
    SHMLIB_ATOMIC(uint64_t) sizes[NUM_BUFFERS];
    uint8_t _pad[CACHE_LINE - 32];
} shmlib_header_prod_t;

typedef struct {
    SHMLIB_ATOMIC(uint32_t) slot_refcnt[NUM_BUFFERS];
    uint8_t _pad[CACHE_LINE - 12];
} shmlib_header_cons_t;

typedef struct {
    shmlib_header_ro_t ro;
    shmlib_header_prod_t prod;
    shmlib_header_cons_t cons;
} shmlib_header_t;

SHMLIB_STATIC_ASSERT(sizeof(shmlib_header_ro_t) == CACHE_LINE, "ro domain must be 64 bytes");
SHMLIB_STATIC_ASSERT(sizeof(shmlib_header_prod_t) == CACHE_LINE, "producer domain must be 64 bytes");
SHMLIB_STATIC_ASSERT(sizeof(shmlib_header_cons_t) == CACHE_LINE, "consumer domain must be 64 bytes");
SHMLIB_STATIC_ASSERT(offsetof(shmlib_header_t, ro) == 0, "ro domain must start at offset 0");
SHMLIB_STATIC_ASSERT(offsetof(shmlib_header_t, prod) == CACHE_LINE, "producer domain must start at offset 64");
SHMLIB_STATIC_ASSERT(offsetof(shmlib_header_t, cons) == (2 * CACHE_LINE), "consumer domain must start at offset 128");
SHMLIB_STATIC_ASSERT(sizeof(shmlib_header_t) == (3 * CACHE_LINE), "header must be 192 bytes");

// Handle for local process
typedef struct {
    shmlib_header_t *hdr;
    uint8_t *buffers[NUM_BUFFERS];
    uint32_t write_idx;
    uint32_t held_read_idx;
    uint64_t write_seq;
    uint64_t next_write_seq;
    uint64_t last_read_seq;
    int fd;
    size_t map_size;
} shmlib_t;

// Get system page size
static inline size_t shmlib__get_page_size(void) {
    static size_t page_size = 0;
    if (!page_size) page_size = (size_t)sysconf(_SC_PAGESIZE);
    return page_size;
}

// Round up to page size
static inline size_t shmlib__page_align(size_t size) {
    size_t page = shmlib__get_page_size();
    return (size + page - 1) & ~(page - 1);
}

// Sleep helper that remains available under strict POSIX feature profiles.
static inline void shmlib__sleep_us(uint64_t usec) {
    struct timespec req;
    req.tv_sec = (time_t)(usec / 1000000ULL);
    req.tv_nsec = (long)((usec % 1000000ULL) * 1000ULL);

    while (nanosleep(&req, &req) < 0 && errno == EINTR) {
    }
}

// Build POSIX shm name "/shmlib_<channel>" from bare channel name
static inline int shmlib__build_name(const char *channel, char *buf, size_t bufsize) {
    int n;
    if (!channel || !buf || bufsize == 0) {
        errno = EINVAL;
        return -1;
    }

    n = snprintf(buf, bufsize, "%s%s", SHMLIB_PREFIX, channel);
    if (n < 0 || (size_t)n >= bufsize) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

// Open existing shared memory (channel is bare name, e.g. "video")
static inline shmlib_t *shmlib_open(const char *channel) {
    char name[SHMLIB_NAME_MAX];
    int fd;
    struct stat st;
    size_t file_size;
    size_t hdr_size;
    void *mem;
    shmlib_header_t *hdr;
    shmlib_t *q;
    size_t buf_size;
    uint8_t *base;
    uint32_t version;

    if (shmlib__build_name(channel, name, sizeof(name)) < 0) return NULL;

    fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) return NULL;

    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }

    file_size = (size_t)st.st_size;
    hdr_size = shmlib__page_align(sizeof(shmlib_header_t));

    mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    hdr = (shmlib_header_t *)mem;
    version = SHMLIB_ATOMIC_LOAD_U32(&hdr->ro.version, SHMLIB_MO_ACQUIRE);
    if (version == 0) {
        munmap(mem, file_size);
        close(fd);
        errno = EAGAIN;
        return NULL;
    }

    if (hdr->ro.magic != SHMLIB_MAGIC || version != 5u) {
        munmap(mem, file_size);
        close(fd);
        errno = EINVAL;
        return NULL;
    }

    SHMLIB_FENCE_ACQUIRE();

    q = (shmlib_t *)malloc(sizeof(shmlib_t));
    if (!q) {
        munmap(mem, file_size);
        close(fd);
        return NULL;
    }

    buf_size = (size_t)hdr->ro.capacity;
    if (buf_size == 0) {
        free(q);
        munmap(mem, file_size);
        close(fd);
        errno = EINVAL;
        return NULL;
    }

    q->hdr = hdr;
    base = (uint8_t *)mem + hdr_size;
    q->buffers[0] = base;
    q->buffers[1] = base + buf_size;
    q->buffers[2] = base + (buf_size * 2);
    q->write_idx = SHMLIB_NO_BUFFER;
    q->held_read_idx = SHMLIB_NO_BUFFER;
    q->write_seq = 0;
    {
        uint64_t cur_seq = SHMLIB_ATOMIC_LOAD_U64(&hdr->prod.publish_seq, SHMLIB_MO_ACQUIRE);
        q->next_write_seq = cur_seq + 1;
        /* Seed last_read_seq so that messages written before this open()
         * are invisible to the consumer.  Without this, stale shared memory
         * from a previous producer session would be delivered immediately. */
        q->last_read_seq = cur_seq;
    }
    q->fd = fd;
    q->map_size = file_size;

    return q;
}

// Open existing shared memory, or create it if missing.
// Safe against concurrent creators. (channel is bare name, e.g. "video")
static inline shmlib_t *shmlib_init(const char *channel, size_t capacity) {
    char name[SHMLIB_NAME_MAX];

    if (shmlib__build_name(channel, name, sizeof(name)) < 0) return NULL;

    for (;;) {
        shmlib_t *q;
        size_t page;
        size_t hdr_size;
        size_t buf_size;
        size_t file_size;
        int fd;
        void *mem;
        shmlib_header_t *hdr;
        uint8_t *base;

        q = shmlib_open(channel);
        if (q) return q;  
        if (errno == EAGAIN) {
            shmlib__sleep_us(1000ULL);
            continue;
        }

        if (errno == EINVAL) {
            // Latest-only policy: remove stale/incompatible segment and recreate.
            if (shm_unlink(name) < 0 && errno != ENOENT) {
                fprintf(stderr, "Error removing incompatible shared memory '%s': %s\n", name, strerror(errno));
                return NULL;
            }
            continue;
        }

        if (errno != ENOENT) {
            fprintf(stderr, "Error opening shared memory '%s': %s\n", name, strerror(errno));
            return NULL;
        }

        page = shmlib__get_page_size();
        if (capacity < page) capacity = page;

        hdr_size = shmlib__page_align(sizeof(shmlib_header_t));
        buf_size = shmlib__page_align(capacity);
        file_size = hdr_size + (buf_size * NUM_BUFFERS);

        fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            fprintf(stderr, "Error creating shared memory '%s': %s\n", name, strerror(errno));
            return NULL;
        }

        if (ftruncate(fd, (off_t)file_size) < 0) {
            int err = errno;
            close(fd);
            shm_unlink(name);
            fprintf(stderr, "Error sizing shared memory '%s': %s\n", name, strerror(err));
            return NULL;
        }

        mem = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED) {
            int err = errno;
            close(fd);
            shm_unlink(name);
            fprintf(stderr, "Error mapping shared memory '%s': %s\n", name, strerror(err));
            return NULL;
        }

        hdr = (shmlib_header_t *)mem;
        memset(hdr, 0, sizeof(*hdr));
        hdr->ro.magic = SHMLIB_MAGIC;
        hdr->ro.capacity = buf_size;
        SHMLIB_ATOMIC_STORE_U64(&hdr->prod.publish_seq, 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U64(&hdr->prod.sizes[0], 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U64(&hdr->prod.sizes[1], 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U64(&hdr->prod.sizes[2], 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U32(&hdr->cons.slot_refcnt[0], 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U32(&hdr->cons.slot_refcnt[1], 0, SHMLIB_MO_RELAXED);
        SHMLIB_ATOMIC_STORE_U32(&hdr->cons.slot_refcnt[2], 0, SHMLIB_MO_RELAXED);
        SHMLIB_FENCE_RELEASE();
        SHMLIB_ATOMIC_STORE_U32(&hdr->ro.version, 5u, SHMLIB_MO_RELEASE);

        q = (shmlib_t *)malloc(sizeof(shmlib_t));
        if (!q) {
            munmap(mem, file_size);
            close(fd);
            shm_unlink(name);
            fprintf(stderr, "Error allocating shared memory handle: %s\n", strerror(errno));
            return NULL;
        }

        q->hdr = hdr;
        base = (uint8_t *)mem + hdr_size;
        q->buffers[0] = base;
        q->buffers[1] = base + buf_size;
        q->buffers[2] = base + (buf_size * 2);
        q->write_idx = SHMLIB_NO_BUFFER;
        q->held_read_idx = SHMLIB_NO_BUFFER;
        q->write_seq = 0;
        q->next_write_seq = 1;
        q->last_read_seq = 0;
        q->fd = fd;
        q->map_size = file_size;

        return q;
    }
}

// Cleanup
static inline void shmlib_close(shmlib_t *q) {
    if (!q) return;

    if (q->held_read_idx != SHMLIB_NO_BUFFER) {
        SHMLIB_ATOMIC_FETCH_SUB_U32(&q->hdr->cons.slot_refcnt[q->held_read_idx], 1, SHMLIB_MO_ACQ_REL);
        q->held_read_idx = SHMLIB_NO_BUFFER;
    }

    munmap(q->hdr, q->map_size);
    close(q->fd);
    free(q);
}

static inline void shmlib_unlink(const char *channel) {
    char name[SHMLIB_NAME_MAX];
    if (shmlib__build_name(channel, name, sizeof(name)) < 0) return;
    shm_unlink(name);
}

static inline size_t shmlib_capacity(const shmlib_t *q) {
    if (!q || !q->hdr) return 0;
    return (size_t)q->hdr->ro.capacity;
}

// --- Writer primitives ---

// Get pointer to write slot (non-blocking, drop-new semantics).
// Returns NULL/EAGAIN if all non-latest slots are currently reserved by readers.
static inline void *shmlib_write_begin(shmlib_t *q) {
    uint64_t published_seq;
    uint32_t latest_idx;
    uint32_t offset;

    if (!q || !q->hdr) {
        errno = EINVAL;
        return NULL;
    }

    if (q->write_idx != SHMLIB_NO_BUFFER) {
        errno = EBUSY;
        return NULL;
    }

    published_seq = SHMLIB_ATOMIC_LOAD_U64(&q->hdr->prod.publish_seq, SHMLIB_MO_ACQUIRE);
    latest_idx = (published_seq == 0) ? SHMLIB_NO_BUFFER : (uint32_t)(published_seq % NUM_BUFFERS);

    for (offset = 0; offset < NUM_BUFFERS; offset++) {
        uint64_t candidate_seq = q->next_write_seq + offset;
        uint32_t idx = (uint32_t)(candidate_seq % NUM_BUFFERS);
        uint32_t refs;

        if (idx == latest_idx) continue;

        refs = SHMLIB_ATOMIC_LOAD_U32(&q->hdr->cons.slot_refcnt[idx], SHMLIB_MO_ACQUIRE);
        if (refs == 0) {
            q->write_idx = idx;
            q->write_seq = candidate_seq;
            return q->buffers[idx];
        }
    }

    q->write_idx = SHMLIB_NO_BUFFER;
    q->write_seq = 0;
    errno = EAGAIN;
    return NULL;
}

// Publish written data.
// Returns 0 on success, -1 on failure and sets errno.
static inline int shmlib_write_end(shmlib_t *q, size_t len) {
    uint32_t idx;

    if (!q || !q->hdr) {
        errno = EINVAL;
        return -1;
    }

    if (q->write_idx == SHMLIB_NO_BUFFER) {
        errno = EINVAL;
        return -1;
    }

    if (len > shmlib_capacity(q)) {
        q->write_idx = SHMLIB_NO_BUFFER;
        q->write_seq = 0;
        errno = EMSGSIZE;
        return -1;
    }

    idx = q->write_idx;

    SHMLIB_ATOMIC_STORE_U64(&q->hdr->prod.sizes[idx], len, SHMLIB_MO_RELAXED);
    SHMLIB_ATOMIC_STORE_U64(&q->hdr->prod.publish_seq, q->write_seq, SHMLIB_MO_RELEASE);
    q->next_write_seq = q->write_seq + 1;
    q->write_idx = SHMLIB_NO_BUFFER;
    q->write_seq = 0;
    return 0;
}

// Cancel an in-progress write reservation if one exists.
static inline void shmlib_write_cancel(shmlib_t *q) {
    if (!q) return;
    q->write_idx = SHMLIB_NO_BUFFER;
    q->write_seq = 0;
}

// --- Reader primitives ---

// Try to read latest message (non-blocking, zero-copy).
// Returns direct pointer to buffer in shared memory, or NULL if no new data.
// Caller must invoke shmlib_read_end() after every successful read_begin().
static inline void *shmlib_read_begin(shmlib_t *q, size_t *len) {
    uint64_t seq;
    uint32_t idx;
    uint64_t seq_check;
    uint64_t size;

    if (!q || !q->hdr || !len) {
        errno = EINVAL;
        return NULL;
    }

    if (q->held_read_idx != SHMLIB_NO_BUFFER) {
        errno = EBUSY;
        return NULL;
    }

    seq = SHMLIB_ATOMIC_LOAD_U64(&q->hdr->prod.publish_seq, SHMLIB_MO_ACQUIRE);
    if (seq == 0 || seq == q->last_read_seq) return NULL;

    idx = (uint32_t)(seq % NUM_BUFFERS);

    SHMLIB_ATOMIC_FETCH_ADD_U32(&q->hdr->cons.slot_refcnt[idx], 1, SHMLIB_MO_ACQ_REL);

    seq_check = SHMLIB_ATOMIC_LOAD_U64(&q->hdr->prod.publish_seq, SHMLIB_MO_ACQUIRE);
    if (seq_check != seq) {
        SHMLIB_ATOMIC_FETCH_SUB_U32(&q->hdr->cons.slot_refcnt[idx], 1, SHMLIB_MO_ACQ_REL);
        return NULL;
    }

    size = SHMLIB_ATOMIC_LOAD_U64(&q->hdr->prod.sizes[idx], SHMLIB_MO_RELAXED);
    *len = (size_t)size;
    q->last_read_seq = seq;
    q->held_read_idx = idx;
    return q->buffers[idx];
}

// Mandatory completion for successful shmlib_read_begin().
static inline void shmlib_read_end(shmlib_t *q) {
    if (!q || q->held_read_idx == SHMLIB_NO_BUFFER) return;

    SHMLIB_ATOMIC_FETCH_SUB_U32(&q->hdr->cons.slot_refcnt[q->held_read_idx], 1, SHMLIB_MO_ACQ_REL);
    q->held_read_idx = SHMLIB_NO_BUFFER;
}

#ifdef __cplusplus
}
#endif

#endif // SHMLIB_H
