#include "message.h"
#include "shmlib.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    uint8_t point_type = XYZI;
    uint32_t point_count = 32;
    uint32_t seq = 0;

    size_t msg_len = msg_pointcloud_size(point_type, point_count);

    shmlib_t *producer = shmlib_init("demo_pc", msg_len);
    if (!producer) {
        perror("producer");
        return 1;
    }


    while (1) {
        MsgPointCloud *msg = (MsgPointCloud *)shmlib_write_begin(producer);
        if (!msg) {
            usleep(1000);
            continue;
        }

        msg->hdr.msg_type = MSG_TYPE_POINT_CLOUD;
        msg->hdr.msg_count = ++seq;
        msg->hdr.timestamp_ns = time_ns();
        msg->point_type = point_type;
        msg->point_count = point_count;

        for (uint32_t i = 0; i < point_count; i++) {
            size_t base = (size_t)i * point_type;
            float t = (float)seq * 0.01f;
            msg->points[base + 0u] = (float)i * 0.1f + t;
            msg->points[base + 1u] = (float)i * 0.2f + t;
            if (point_type >= 3u) msg->points[base + 2u] = (float)i * 0.3f + t;
            if (point_type >= 4u) msg->points[base + 3u] = (float)(i % 100u) / 100.0f;
        }

        if (shmlib_write_end(producer, msg_len) != 0) {
            perror("shmlib_write_end");
            shmlib_close(producer);
            return 1;
        }

        usleep(33000);
    }
}
