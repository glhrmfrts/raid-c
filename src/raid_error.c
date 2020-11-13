#include "raid.h"

const char* raid_error_to_string(raid_error_t err)
{
    switch (err) {
    case RAID_SUCCESS:
        return "RAID_SUCCESS: success";
    case RAID_INVALID_ARGUMENT:
        return "RAID_INVALID_ARGUMENT: invalid argument";
    case RAID_INVALID_ADDRESS:
        return "RAID_INVALID_ADDRESS: invalid host address";
    case RAID_CONNECT_ERROR:
        return "RAID_CONNECT_ERROR: error connecting to host";
    case RAID_SOCKET_ERROR:
        return "RAID_SOCKET_ERROR: error opening socket file descriptor";
    case RAID_RECV_TIMEOUT:
        return "RAID_RECV_TIMEOUT: timed out waiting data from server";
    case RAID_NOT_CONNECTED:
        return "RAID_NOT_CONNECTED: not connected to host";
    case RAID_SHUTDOWN_ERROR:
        return "RAID_SHUTDOWN_ERROR: invalid socket state";
    case RAID_CLOSE_ERROR:
        return "RAID_CLOSE_ERROR: invalid socket file descriptor";
    case RAID_UNKNOWN:
        return "RAID_UNKNOWN: unknown error";
    default:
        return "unmapped error";
    }
}
