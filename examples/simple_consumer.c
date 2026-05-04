#include "message.h"
#include "shmlib.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *channel = (argc > 1) ? argv[1] : "demo";

    shmlib_t *consumer = NULL;
    while (!consumer) {
        consumer = shmlib_open(channel);
        usleep(10000);
    }

    while (1) {
        size_t len = 0;
        MsgPose2D *msg = (MsgPose2D *)shmlib_read_begin(consumer, &len);
        if (!msg) {
            usleep(1000);
            continue;
        }

        if (len == sizeof(MsgPose2D)) {
            printf("pose2d=(x=%.2f y=%.2f theta=%.2f rad)\n",
                   msg->x,
                   msg->y,
                   msg->theta);
        }

        shmlib_read_end(consumer);
    }
}
