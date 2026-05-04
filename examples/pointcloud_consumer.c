#include "message.h"
#include "shmlib.h"
#include "utils.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    shmlib_t *fl_consumer;
    shmlib_t *fr_consumer;
    uint32_t fl_seq;
    uint32_t fr_seq;
    uint32_t fl_count;
    uint32_t fr_count;
} PointcloudConsumer;

static void read_pointcloud(shmlib_t *consumer, uint32_t *seq, uint32_t *count)
{
    size_t len = 0;
    MsgPointCloud *msg = (MsgPointCloud *)shmlib_read_begin(consumer, &len);
    if (!msg) {
        return;
    }

    if (msg->hdr.msg_type == MSG_TYPE_POINT_CLOUD &&
        msg->point_type >= XY &&
        msg->point_type <= XYZI &&
        len == msg_pointcloud_size(msg->point_type, msg->point_count)) {
        *seq = msg->hdr.msg_count;
        *count = msg->point_count;
    }

    shmlib_read_end(consumer);
}

static void *consumer_thread(void *arg)
{
    PointcloudConsumer *consumer = (PointcloudConsumer *)arg;

    while (1) {
        read_pointcloud(consumer->fl_consumer, &consumer->fl_seq, &consumer->fl_count);
        read_pointcloud(consumer->fr_consumer, &consumer->fr_seq, &consumer->fr_count);

        printf("fl_seq=%u fl_count=%u fr_seq=%u fr_count=%u\n",
               consumer->fl_seq,
               consumer->fl_count,
               consumer->fr_seq,
               consumer->fr_count);

        usleep(100000);
    }

    return NULL;
}

int main(void)
{
    PointcloudConsumer consumer = {0};
    pthread_t thread_id;

    while (!consumer.fl_consumer || !consumer.fr_consumer) {
        if (!consumer.fl_consumer) {
            consumer.fl_consumer = shmlib_open("cloud_fl");
        }
        if (!consumer.fr_consumer) {
            consumer.fr_consumer = shmlib_open("cloud_fr");
        }
        if (!consumer.fl_consumer || !consumer.fr_consumer) {
            usleep(10000);
        }
    }

    if (pthread_create(&thread_id, NULL, consumer_thread, &consumer) != 0) {
        perror("pthread_create");
        shmlib_close(consumer.fl_consumer);
        shmlib_close(consumer.fr_consumer);
        return 1;
    }

    pthread_join(thread_id, NULL);
    shmlib_close(consumer.fl_consumer);
    shmlib_close(consumer.fr_consumer);

    return 0;
}
