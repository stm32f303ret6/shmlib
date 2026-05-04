#ifndef MSG_DESC_H_
#define MSG_DESC_H_

#include "message.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FIELD_U8,
    FIELD_U32,
    FIELD_U64,
    FIELD_F32,
    FIELD_F64
} FieldType;

typedef struct {
    const char *name;
    size_t offset;
    FieldType type;
} FieldDesc;

typedef struct {
    uint32_t msg_type;
    const FieldDesc *fields;
    int field_count;
} MsgDesc;

#define FIELD_COUNT(arr) ((int)(sizeof(arr) / sizeof((arr)[0])))

static const FieldDesc MSG_JOINT_FIELDS[] = {
    { "joint_id", offsetof(MsgJoint, joint_id), FIELD_U8 },
    { "value", offsetof(MsgJoint, value), FIELD_F32 },
};

static const FieldDesc MSG_POSE2D_FIELDS[] = {
    { "x", offsetof(MsgPose2D, x), FIELD_F64 },
    { "y", offsetof(MsgPose2D, y), FIELD_F64 },
    { "theta", offsetof(MsgPose2D, theta), FIELD_F64 },
};

static const FieldDesc MSG_TWIST2D_FIELDS[] = {
    { "vx", offsetof(MsgTwist2D, vx), FIELD_F64 },
    { "vy", offsetof(MsgTwist2D, vy), FIELD_F64 },
    { "omega", offsetof(MsgTwist2D, omega), FIELD_F64 },
};

static const FieldDesc MSG_ACCEL2D_FIELDS[] = {
    { "ax", offsetof(MsgAccel2D, ax), FIELD_F64 },
    { "ay", offsetof(MsgAccel2D, ay), FIELD_F64 },
    { "alpha", offsetof(MsgAccel2D, alpha), FIELD_F64 },
};

static const FieldDesc MSG_TRANSFORM_FIELDS[] = {
    { "x", offsetof(MsgTransform, x), FIELD_F32 },
    { "y", offsetof(MsgTransform, y), FIELD_F32 },
    { "z", offsetof(MsgTransform, z), FIELD_F32 },
    { "qx", offsetof(MsgTransform, qx), FIELD_F32 },
    { "qy", offsetof(MsgTransform, qy), FIELD_F32 },
    { "qz", offsetof(MsgTransform, qz), FIELD_F32 },
    { "qw", offsetof(MsgTransform, qw), FIELD_F32 },
};

static const FieldDesc MSG_SENSOR_FIELDS[] = {
    { "sensor_id", offsetof(MsgSensor, sensor_id), FIELD_U32 },
    { "value", offsetof(MsgSensor, value), FIELD_F32 },
    { "min", offsetof(MsgSensor, min), FIELD_F32 },
    { "max", offsetof(MsgSensor, max), FIELD_F32 },
};

static const FieldDesc MSG_STATUS_FIELDS[] = {
    { "status_code", offsetof(MsgStatus, status_code), FIELD_U32 },
    { "flags", offsetof(MsgStatus, flags), FIELD_U32 },
};

static const FieldDesc MSG_POINT_CLOUD_FIELDS[] = {
    { "point_type", offsetof(MsgPointCloud, point_type), FIELD_U8 },
    { "point_count", offsetof(MsgPointCloud, point_count), FIELD_U32 },
};

static const FieldDesc MSG_ROBOT_STATE_FIELDS[] = {
    { "base_x", offsetof(MsgRobotState, base_x), FIELD_F64 },
    { "base_y", offsetof(MsgRobotState, base_y), FIELD_F64 },
    { "base_yaw", offsetof(MsgRobotState, base_yaw), FIELD_F64 },
    { "base_vx", offsetof(MsgRobotState, base_vx), FIELD_F64 },
    { "base_vy", offsetof(MsgRobotState, base_vy), FIELD_F64 },
    { "base_omega", offsetof(MsgRobotState, base_omega), FIELD_F64 },
    { "sim_time", offsetof(MsgRobotState, sim_time), FIELD_F64 },
};

static const FieldDesc MSG_ACTUATOR_CMD_FIELDS[] = {
    { "steering[0]", offsetof(MsgActuatorCmd, steering[0]), FIELD_F64 },
    { "steering[1]", offsetof(MsgActuatorCmd, steering[1]), FIELD_F64 },
    { "steering[2]", offsetof(MsgActuatorCmd, steering[2]), FIELD_F64 },
    { "steering[3]", offsetof(MsgActuatorCmd, steering[3]), FIELD_F64 },
    { "wheels[0]", offsetof(MsgActuatorCmd, wheels[0]), FIELD_F64 },
    { "wheels[1]", offsetof(MsgActuatorCmd, wheels[1]), FIELD_F64 },
    { "wheels[2]", offsetof(MsgActuatorCmd, wheels[2]), FIELD_F64 },
    { "wheels[3]", offsetof(MsgActuatorCmd, wheels[3]), FIELD_F64 },
};

static const MsgDesc MSG_DESCS[] = {
    { MSG_TYPE_POSE2D, MSG_POSE2D_FIELDS, FIELD_COUNT(MSG_POSE2D_FIELDS) },
    { MSG_TYPE_JOINT, MSG_JOINT_FIELDS, FIELD_COUNT(MSG_JOINT_FIELDS) },
    { MSG_TYPE_TWIST2D, MSG_TWIST2D_FIELDS, FIELD_COUNT(MSG_TWIST2D_FIELDS) },
    { MSG_TYPE_ACCEL2D, MSG_ACCEL2D_FIELDS, FIELD_COUNT(MSG_ACCEL2D_FIELDS) },
    { MSG_TYPE_TRANSFORM, MSG_TRANSFORM_FIELDS, FIELD_COUNT(MSG_TRANSFORM_FIELDS) },
    { MSG_TYPE_SENSOR, MSG_SENSOR_FIELDS, FIELD_COUNT(MSG_SENSOR_FIELDS) },
    { MSG_TYPE_STATUS, MSG_STATUS_FIELDS, FIELD_COUNT(MSG_STATUS_FIELDS) },
    { MSG_TYPE_POINT_CLOUD, MSG_POINT_CLOUD_FIELDS, FIELD_COUNT(MSG_POINT_CLOUD_FIELDS) },
    { MSG_TYPE_ROBOT_STATE, MSG_ROBOT_STATE_FIELDS, FIELD_COUNT(MSG_ROBOT_STATE_FIELDS) },
    { MSG_TYPE_ACTUATOR_CMD, MSG_ACTUATOR_CMD_FIELDS, FIELD_COUNT(MSG_ACTUATOR_CMD_FIELDS) },
};

enum { MSG_DESC_COUNT = FIELD_COUNT(MSG_DESCS) };

static inline const MsgDesc *msg_desc_find(uint32_t msg_type)
{
    for (int i = 0; i < MSG_DESC_COUNT; ++i)
    {
        if (MSG_DESCS[i].msg_type == msg_type)
        {
            return &MSG_DESCS[i];
        }
    }
    return NULL;
}

#undef FIELD_COUNT

#endif // MSG_DESC_H_
