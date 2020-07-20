#ifndef RAID_SOCKET_H
#define RAID_SOCKET_H

#include <stdbool.h>
#include "raid_error.h"

typedef struct raid_socket {
    void* handle;
    char* host;
    char* port;
} raid_socket_t;

raid_error_t raid_socket_connect(raid_socket_t* s, const char* host, const char* port);

bool raid_socket_connected(raid_socket_t* s);

raid_error_t raid_socket_send(raid_socket_t* s, const char* data, size_t data_len);

raid_error_t raid_socket_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len);

raid_error_t raid_socket_close(raid_socket_t* s);

#endif