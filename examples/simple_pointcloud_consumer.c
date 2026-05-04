#include "message.h"
#include "shmlib.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    shmlib_t *consumer = NULL;
    while (!consumer) {
        consumer = shmlib_open("demo_pc");
        usleep(10000);
    }

    while (1) {
        size_t len = 0;
        MsgPointCloud *msg = (MsgPointCloud *)shmlib_read_begin(consumer, &len);
        if (!msg) {
            usleep(1000);
            continue;
        }

        if (msg->point_type < XY || msg->point_type > XYZI) {
            printf("msg_count=%u invalid point_type=%u len=%zu\n",
                   msg->hdr.msg_count,
                   (unsigned)msg->point_type,
                   len);
        } else {
            size_t expected_len = msg_pointcloud_size(msg->point_type, msg->point_count);
            if (len != expected_len) {
                printf("msg_count=%u len mismatch got=%zu expected=%zu type=%u count=%u\n",
                       msg->hdr.msg_count,
                       len,
                       expected_len,
                       (unsigned)msg->point_type,
                       msg->point_count);
            } else if (msg->point_count > 0u) {
                size_t base = (size_t)(msg->point_count - 1u) * msg->point_type;
                float x0 = msg->points[0u];
                float y0 = msg->points[1u];
                float xn = msg->points[base + 0u];
                float yn = msg->points[base + 1u];
                printf("msg_count=%u type=%u count=%u first=(%.2f %.2f) last=(%.2f %.2f) len=%zu\n",
                       msg->hdr.msg_count,
                       (unsigned)msg->point_type,
                       msg->point_count,
                       x0,
                       y0,
                       xn,
                       yn,
                       len);
            }
        }

        shmlib_read_end(consumer);
    }
}
