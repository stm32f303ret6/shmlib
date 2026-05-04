#ifndef MESSAGE_H_
#define MESSAGE_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define MSGS_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define MSGS_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    XYZI = 4,
    XYZ = 3,
    XY = 2
};

enum {
    MSG_JOINT_COUNT = 8
};

typedef enum {
    MSG_TYPE_UNKNOWN     = 0,
    MSG_TYPE_POSE2D      = 1,
    MSG_TYPE_JOINT       = 2,
    MSG_TYPE_POINT_CLOUD = 3,
    MSG_TYPE_TRANSFORM   = 4,
    MSG_TYPE_POSITION    = 5,
    MSG_TYPE_SENSOR      = 6,
    MSG_TYPE_STATUS      = 7,
    MSG_TYPE_TWIST2D     = 8,
    MSG_TYPE_ACCEL2D     = 9,
    MSG_TYPE_ROBOT_STATE = 10,
    MSG_TYPE_ACTUATOR_CMD = 11,
} MsgType;

enum {
    SWERVE_MODULE_COUNT = 4
};

typedef struct __attribute__((packed)) {
    uint32_t msg_type;
    uint32_t msg_count;
    uint64_t timestamp_ns;
} MsgHeader;

typedef struct {
    MsgHeader hdr;
    double x;      /* meters */
    double y;      /* meters */
    double theta;  /* radians (yaw) */
} MsgPose2D;

typedef struct {
    MsgHeader hdr;
    double vx;     /* m/s */
    double vy;     /* m/s */
    double omega;  /* rad/s */
} MsgTwist2D;

typedef struct {
    MsgHeader hdr;
    double ax;     /* m/s^2 */
    double ay;     /* m/s^2 */
    double alpha;  /* rad/s^2 */
} MsgAccel2D;

MSGS_STATIC_ASSERT(offsetof(MsgPose2D, hdr) == 0, "MsgPose2D.hdr must be at offset 0");
MSGS_STATIC_ASSERT(offsetof(MsgTwist2D, hdr) == 0, "MsgTwist2D.hdr must be at offset 0");
MSGS_STATIC_ASSERT(offsetof(MsgAccel2D, hdr) == 0, "MsgAccel2D.hdr must be at offset 0");

typedef struct __attribute__((packed)) {
    MsgHeader hdr;
    uint32_t sensor_id;
    float value;
    float min;
    float max;
} MsgSensor;

typedef struct __attribute__((packed)) {
    MsgHeader hdr;
    uint32_t status_code;
    uint32_t flags;
} MsgStatus;

typedef struct {
    MsgHeader hdr;
    uint8_t point_type;
    uint32_t point_count;
    float points[];
} MsgPointCloud;

MSGS_STATIC_ASSERT(offsetof(MsgPointCloud, hdr) == 0, "MsgPointCloud.hdr must be at offset 0");
MSGS_STATIC_ASSERT(offsetof(MsgPointCloud, point_type) == sizeof(MsgHeader),
                   "MsgPointCloud.point_type offset changed");
MSGS_STATIC_ASSERT(offsetof(MsgPointCloud, point_count) == (sizeof(MsgHeader) + 4),
                   "MsgPointCloud.point_count offset changed");
MSGS_STATIC_ASSERT(offsetof(MsgPointCloud, points) == (sizeof(MsgHeader) + 8),
                   "MsgPointCloud.points offset changed");
MSGS_STATIC_ASSERT(sizeof(MsgPointCloud) == (sizeof(MsgHeader) + 8),
                   "MsgPointCloud base size changed");
MSGS_STATIC_ASSERT((offsetof(MsgPointCloud, points) % sizeof(float)) == 0,
                   "MsgPointCloud.points must be float-aligned");

typedef struct __attribute__((packed)) {
    MsgHeader hdr;
    float x;
    float y;
    float z;
    float qx;
    float qy;
    float qz;
    float qw;
} MsgTransform;

typedef struct __attribute__((packed)) {
    MsgHeader hdr;
    uint8_t joint_id;
    float value;
} MsgJoint;

typedef struct {
    MsgHeader hdr;
    double base_x, base_y, base_yaw;               /* pose (m, m, rad) */
    double base_vx, base_vy, base_omega;            /* velocity (m/s, m/s, rad/s) */
    double steering_pos[SWERVE_MODULE_COUNT];       /* joint positions (rad) */
    double steering_vel[SWERVE_MODULE_COUNT];       /* joint velocities (rad/s) */
    double wheel_vel[SWERVE_MODULE_COUNT];          /* wheel velocities (rad/s) */
    double sim_time;                                /* MuJoCo time (s) */
} MsgRobotState;

typedef struct {
    MsgHeader hdr;
    double steering[SWERVE_MODULE_COUNT];           /* steering angle commands (rad) */
    double wheels[SWERVE_MODULE_COUNT];             /* wheel speed commands (rad/s) */
} MsgActuatorCmd;

MSGS_STATIC_ASSERT(offsetof(MsgRobotState, hdr) == 0, "MsgRobotState.hdr must be at offset 0");
MSGS_STATIC_ASSERT(offsetof(MsgActuatorCmd, hdr) == 0, "MsgActuatorCmd.hdr must be at offset 0");

#ifdef __cplusplus
}
#endif

#endif // MESSAGE_H_
