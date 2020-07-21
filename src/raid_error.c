#include "raid.h"

const char* raid_error_to_string(raid_error_t err)
{
    switch (err) {
    case RAID_SUCCESS:
        return "success";
    case RAID_INVALID_ARGUMENT:
        return "invalid argument";
    case RAID_INVALID_ADDRESS:
        return "invalid address";
    case RAID_CONNECT_ERROR:
        return "connect error";
    case RAID_SOCKET_ERROR:
        return "socket error";
    case RAID_RECV_TIMEOUT:
        return "recv timeout";
    case RAID_NOT_CONNECTED:
        return "not connected";
    case RAID_SHUTDOWN_ERROR:
        return "shutdown error";
    case RAID_CLOSE_ERROR:
        return "close error";
    case RAID_UNKNOWN:
        return "unknown";
    default:
        return "TODO";
    }
}
