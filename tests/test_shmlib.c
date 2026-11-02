#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Use isolated /tmp files with real MAP_SHARED mappings, avoiding /dev/shm
 * permissions and allowing a deterministic pause before ftruncate. */
static char test_dir[] = "/tmp/shmlib-test-XXXXXX";
static int creation_notify = -1;
static int creation_continue = -1;
static int release_creator = -1;

static int test_shm_open(const char *name, int flags, mode_t mode) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", test_dir, name);
    int fd = open(path, flags, mode);
    if (fd >= 0 && (flags & O_EXCL) && creation_notify >= 0) {
        assert(write(creation_notify, "x", 1) == 1);
        char byte;
        assert(read(creation_continue, &byte, 1) == 1);
    }
    return fd;
}

static int test_shm_unlink(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", test_dir, name);
    return unlink(path);
}

static int test_fstat(int fd, struct stat *st) {
    int result = fstat(fd, st);
    if (result == 0 && st->st_size == 0 && release_creator >= 0) {
        assert(write(release_creator, "x", 1) == 1);
        release_creator = -1;
    }
    return result;
}

/* Resume the creator only after init has observed its zero-length file. */
#include <sys/mman.h>
#define shm_open test_shm_open
#define shm_unlink test_shm_unlink
#define fstat test_fstat
#include "shmlib.h"

static void publish(shmlib_t *q, uint64_t value) {
    uint64_t *p = (uint64_t *)shmlib_write_begin(q);
    assert(p);
    *p = value;
    assert(shmlib_write_end(q, sizeof(*p)) == 0);
}

static void test_bounds(void) {
    shmlib_t *q = shmlib_init("bounds", 1);
    assert(q);
    size_t capacity = shmlib_capacity(q);
    assert(!shmlib_init("bounds", capacity + 1) && errno == EMSGSIZE);
    shmlib_t *r = shmlib_init("bounds", capacity);
    assert(r);
    publish(q, 42);
    size_t len;
    uint64_t *p = (uint64_t *)shmlib_read_begin(r, &len);
    assert(p && len == sizeof(*p) && *p == 42);
    shmlib_read_end(r);

    publish(q, 43);
    uint32_t idx = (uint32_t)(atomic_load(&q->hdr->prod.publish_seq) % NUM_BUFFERS);
    atomic_store(&q->hdr->prod.sizes[idx], capacity + 1);
    assert(!shmlib_read_begin(r, &len) && errno == EMSGSIZE);
    assert(atomic_load(&q->hdr->cons.slot_refcnt[idx]) == 0);
    atomic_store(&q->hdr->prod.sizes[idx], sizeof(uint64_t));
    assert(shmlib_read_begin(r, &len));
    shmlib_read_end(r);
    shmlib_close(r);

    const uint64_t invalid[] = {0, 1, capacity + 1, capacity * 2, UINT64_MAX};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        q->hdr->ro.capacity = invalid[i];
        assert(!shmlib_open("bounds") && errno == EINVAL);
    }
    q->hdr->ro.capacity = capacity;
    atomic_store(&q->hdr->ro.version, 4);
    assert(!shmlib_init("bounds", 1) && errno == EINVAL);
    int fd = test_shm_open("/shmlib_bounds", O_RDWR, 0600);
    struct stat original, current;
    assert(fd >= 0 && fstat(fd, &current) == 0 && fstat(q->fd, &original) == 0);
    assert(current.st_ino == original.st_ino);
    close(fd);
    atomic_store(&q->hdr->ro.version, 5);
    assert(ftruncate(q->fd, (off_t)shmlib__get_page_size()) == 0);
    assert(!shmlib_open("bounds") && errno == EINVAL);
    shmlib_close(q);
    shmlib_unlink("bounds");

    const size_t excessive[] = {SIZE_MAX, SIZE_MAX / 3 + 4096};
    for (size_t i = 0; i < sizeof(excessive) / sizeof(excessive[0]); i++) {
        assert(!shmlib_init("overflow", excessive[i]) && errno == EOVERFLOW);
        assert(test_shm_open("/shmlib_overflow", O_RDWR, 0600) < 0 && errno == ENOENT);
    }
    if (sizeof(off_t) <= sizeof(size_t))
        assert(!shmlib_init("overflow", SIZE_MAX / 4) && errno == EOVERFLOW);
}

static void test_startup(void) {
    int ready[2], resume[2];
    assert(pipe(ready) == 0 && pipe(resume) == 0);
    creation_notify = ready[1];
    creation_continue = resume[0];
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(ready[0]);
        close(resume[1]);
        shmlib_t *q = shmlib_init("startup", 1);
        struct stat st;
        assert(q && fstat(q->fd, &st) == 0);
        assert(write(ready[1], &st.st_ino, sizeof(st.st_ino)) == sizeof(st.st_ino));
        shmlib_close(q);
        _exit(0);
    }
    creation_notify = -1;
    close(ready[1]);
    close(resume[0]);
    char byte;
    assert(read(ready[0], &byte, 1) == 1);
    assert(!shmlib_open("startup") && errno == EAGAIN);
    release_creator = resume[1];
    shmlib_t *q = shmlib_init("startup", 1);
    close(resume[1]);
    struct stat st;
    ino_t child_inode;
    assert(q && fstat(q->fd, &st) == 0);
    assert(read(ready[0], &child_inode, sizeof(child_inode)) == sizeof(child_inode));
    assert(st.st_ino == child_inode);
    close(ready[0]);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    shmlib_close(q);
    shmlib_unlink("startup");

    int fd = test_shm_open("/shmlib_orphan", O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(fd >= 0);
    assert(!shmlib_init("orphan", 1) && errno == EAGAIN);
    assert(ftruncate(fd, (off_t)shmlib__get_page_size()) == 0);
    assert(!shmlib_init("orphan", 1) && errno == EAGAIN);
    close(fd);
    shmlib_unlink("orphan");
}

static void test_pins(void) {
    shmlib_t *q = shmlib_init("pins", 1);
    shmlib_t *a = shmlib_open("pins"), *b = shmlib_open("pins");
    assert(q && a && b);
    size_t len;
    publish(q, 1);
    uint64_t *first = (uint64_t *)shmlib_read_begin(a, &len);
    publish(q, 2);
    uint64_t *second = (uint64_t *)shmlib_read_begin(b, &len);
    publish(q, 3);
    assert(first && second && *first == 1 && *second == 2);
    assert(!shmlib_write_begin(q) && errno == EAGAIN);
    shmlib_close(a); /* Closing must release its outstanding pin. */
    publish(q, 4);
    assert(*second == 2);
    shmlib_read_end(b);
    shmlib_close(b);
    shmlib_close(q);
    shmlib_unlink("pins");
}

enum { READERS = 4, WORDS = 128, MESSAGES = 200000 };
static pthread_barrier_t start;
static atomic_int done;

static void *reader(void *arg) {
    size_t *reads = (size_t *)arg;
    shmlib_t *q = shmlib_open("stress");
    assert(q);
    pthread_barrier_wait(&start);
    while (!atomic_load(&done)) {
        size_t len;
        uint64_t *p = (uint64_t *)shmlib_read_begin(q, &len);
        if (!p) continue;
        assert(len == WORDS * sizeof(*p));
        uint64_t value = p[0];
        if ((*reads % 64) == 0) sched_yield();
        for (size_t j = 0; j < WORDS; j++) assert(p[j] == value);
        shmlib_read_end(q);
        ++*reads;
    }
    shmlib_close(q);
    return NULL;
}

static void test_stress(void) {
    pthread_t threads[READERS];
    size_t reads[READERS] = {0};
    shmlib_t *q = shmlib_init("stress", WORDS * sizeof(uint64_t));
    assert(q && pthread_barrier_init(&start, NULL, READERS + 1) == 0);
    for (int i = 0; i < READERS; i++)
        assert(pthread_create(&threads[i], NULL, reader, &reads[i]) == 0);
    pthread_barrier_wait(&start);
    for (uint64_t n = 1; n <= MESSAGES; n++) {
        uint64_t *p;
        while (!(p = (uint64_t *)shmlib_write_begin(q))) {
            assert(errno == EAGAIN);
            sched_yield();
        }
        for (size_t j = 0; j < WORDS; j++) p[j] = n;
        assert(shmlib_write_end(q, WORDS * sizeof(*p)) == 0);
    }
    atomic_store(&done, 1);
    for (int i = 0; i < READERS; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(reads[i] > 0);
    }
    pthread_barrier_destroy(&start);
    shmlib_close(q);
    shmlib_unlink("stress");
}

int main(void) {
    alarm(30);
    assert(mkdtemp(test_dir));
    test_bounds();
    test_startup();
    test_pins();
    test_stress();
    assert(rmdir(test_dir) == 0);
    puts("shmlib regression tests passed");
    return 0;
}
