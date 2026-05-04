#ifndef UTILS_H_
#define UTILS_H_

#include "message.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

static inline uint64_t time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline size_t msg_pointcloud_size(uint8_t point_type, uint32_t point_count) {
    size_t total_floats = (size_t)point_count * point_type;
    return offsetof(MsgPointCloud, points) + total_floats * sizeof(float);
}

#endif // UTILS_H_
