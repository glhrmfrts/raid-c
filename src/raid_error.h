#ifndef RAID_ERROR_H
#define RAID_ERROR_H

typedef enum {
    RAID_SUCCESS,
    RAID_INVALID_ARGUMENT,
    RAID_INVALID_ADDRESS,
    RAID_SOCKET_ERROR,
    RAID_CONNECT_ERROR,
    RAID_RECV_TIMEOUT,
    RAID_NOT_CONNECTED,
    RAID_SHUTDOWN_ERROR,
    RAID_CLOSE_ERROR,
    RAID_UNKNOWN,
} raid_error_t;

const char* raid_error_to_string(raid_error_t err);

#endif