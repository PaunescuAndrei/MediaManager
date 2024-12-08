#pragma once
#include <QString>
#include "MpcApi.h"

typedef enum PlayerCommandType {
    MPC_DIRECT_COMMAND,
    CHANGE_VIDEO,
    OSD_MESSAGE,
} PlayerCommandType;

struct PlayerCommand {
    explicit PlayerCommand(PlayerCommandType cmd_type) : command(cmd_type) {};
    virtual ~PlayerCommand() = default;
    PlayerCommandType command;
};

struct ChangeVideoCommand : PlayerCommand {
    QString video_path;
    int video_id;
    double position;
    bool change_in_progress = false;
    ChangeVideoCommand(QString path, int id, double pos) : PlayerCommand(CHANGE_VIDEO), video_path(path), video_id(id), position(pos) {};
};

struct MpcDirectCommand : PlayerCommand {
    MPCAPI_COMMAND mpc_command;
    QString data;
    MpcDirectCommand(MPCAPI_COMMAND mpc_cmd, QString data = "") : PlayerCommand(MPC_DIRECT_COMMAND), mpc_command(mpc_cmd), data(data) {};
};

struct OsdMessageCommand : PlayerCommand {
    QString message;
    int duration;
    OsdMessageCommand(QString msg, int dur) : PlayerCommand(OSD_MESSAGE), message(msg), duration(dur) {};
};
