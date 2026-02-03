#pragma once

#include <cstdint>

namespace wowee {
namespace game {

// World of Warcraft 3.3.5a opcodes
// Values derived from community reverse-engineering efforts
// Reference: https://wowdev.wiki/World_Packet
enum class Opcode : uint16_t {
    // Client to Server
    CMSG_PING = 0x1DC,
    CMSG_AUTH_SESSION = 0x1ED,
    CMSG_CHAR_ENUM = 0x037,
    CMSG_PLAYER_LOGIN = 0x03D,

    // Movement
    CMSG_MOVE_START_FORWARD = 0x0B5,
    CMSG_MOVE_START_BACKWARD = 0x0B6,
    CMSG_MOVE_STOP = 0x0B7,
    CMSG_MOVE_START_STRAFE_LEFT = 0x0B8,
    CMSG_MOVE_START_STRAFE_RIGHT = 0x0B9,
    CMSG_MOVE_STOP_STRAFE = 0x0BA,
    CMSG_MOVE_JUMP = 0x0BB,
    CMSG_MOVE_START_TURN_LEFT = 0x0BC,
    CMSG_MOVE_START_TURN_RIGHT = 0x0BD,
    CMSG_MOVE_STOP_TURN = 0x0BE,
    CMSG_MOVE_SET_FACING = 0x0DA,
    CMSG_MOVE_FALL_LAND = 0x0C9,
    CMSG_MOVE_START_SWIM = 0x0CA,
    CMSG_MOVE_STOP_SWIM = 0x0CB,
    CMSG_MOVE_HEARTBEAT = 0x0EE,

    // Server to Client
    SMSG_AUTH_CHALLENGE = 0x1EC,
    SMSG_AUTH_RESPONSE = 0x1EE,
    SMSG_CHAR_ENUM = 0x03B,
    SMSG_PONG = 0x1DD,
    SMSG_LOGIN_VERIFY_WORLD = 0x236,
    SMSG_ACCOUNT_DATA_TIMES = 0x209,
    SMSG_FEATURE_SYSTEM_STATUS = 0x3ED,
    SMSG_MOTD = 0x33D,

    // Entity/Object updates
    SMSG_UPDATE_OBJECT = 0x0A9,
    SMSG_DESTROY_OBJECT = 0x0AA,

    // Chat
    CMSG_MESSAGECHAT = 0x095,
    SMSG_MESSAGECHAT = 0x096,

    // TODO: Add more opcodes as needed
};

} // namespace game
} // namespace wowee
