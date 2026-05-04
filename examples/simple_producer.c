#include "message.h"
#include "shmlib.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *channel = (argc > 1) ? argv[1] : "demo";

    shmlib_t *producer = shmlib_init(channel, sizeof(MsgPose2D));
    if (!producer) {
        perror("producer");
        return 1;
    }

    double pos = 0.0;
    uint32_t seq = 0;

    while (1) {
        MsgPose2D *msg = (MsgPose2D *)shmlib_write_begin(producer);
        if (!msg) {
            usleep(1000);
            continue;
        }

        msg->hdr.msg_type = MSG_TYPE_POSE2D;
        msg->hdr.msg_count = ++seq;
        msg->hdr.timestamp_ns = (uint64_t)time(NULL) * 1000000000ULL;
        msg->x = pos;
        msg->y = pos * 0.5;
        msg->theta = pos * 0.1;

        if (shmlib_write_end(producer, sizeof(MsgPose2D)) != 0) {
            perror("shmlib_write_end");
            shmlib_close(producer);
            return 1;
        }

        pos += 0.1;
        usleep(16000);
    }
}
