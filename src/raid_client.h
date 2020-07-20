#ifndef RAID_CLIENT_H
#define RAID_CLIENT_H

#include "raid_error.h"
#include "raid_read.h"
#include "raid_socket.h"
#include "raid_write.h"

/**
 * A Raid request.
 */
typedef enum raid_state {
    RAID_STATE_WAIT_MESSAGE,
    RAID_STATE_PROCESSING_MESSAGE,
} raid_state_t;

/**
 * The type of the request callback function.
 */
typedef void(*raid_callback_t)(raid_client_t*, raid_error_t, void*);

/**
 * A Raid request.
 */
typedef struct raid_request {
    const char* etag;
    raid_callback_t callback;
    void* callback_user_data;
    struct raid_request* next;
    struct raid_request* prev;
} raid_request_t;

/**
 * The client state holding sockets, requests, etc...
 */
typedef struct raid_client {
    raid_socket_t socket;
    raid_writer_t out_writer;
    raid_reader_t in_reader;
    const char* in_ptr;
    const char* in_end;
    char* msg_buf;
    size_t msg_total_size;
    size_t msg_len;
    raid_state_t state;
    raid_request_t* reqs;
} raid_client_t;

/**
 * Connect to the given host and port.
 */
raid_error_t raid_connect(raid_client_t* cl, const char* host, const char* port);

/**
 * Send a request to the raid server.
 */
raid_error_t raid_request(raid_client_t* cl, raid_callback_t cb, void* user_data);

/**
 * Loop to read data from socket and dispatch responses, call this in another thread.
 */
raid_error_t raid_recv_loop(raid_client_t* cl);

/**
 * Close the connection, making subsequent calls invalid.
 */
raid_error_t raid_close(raid_client_t* cl);


#endif