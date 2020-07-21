#ifndef RAID_H
#define RAID_H

#include <pthread.h>
#include <msgpack.h>

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

typedef struct raid_socket {
    int handle;
    char* host;
    char* port;
} raid_socket_t;

typedef struct raid_reader {
    msgpack_zone mempool;
    msgpack_object obj;
    msgpack_object* header;
    msgpack_object* body;
    msgpack_object* nested;
    msgpack_object* parent;
    msgpack_object* parents[10];
    int indices[10];
    int nested_top;
} raid_reader_t;

typedef struct raid_writer {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    const char* etag;
} raid_writer_t;

/**
 * A Raid request.
 */
typedef enum raid_state {
    RAID_STATE_WAIT_MESSAGE,
    RAID_STATE_PROCESSING_MESSAGE,
} raid_state_t;

struct raid_client;

/**
 * The type of the request callback function.
 */
typedef void(*raid_callback_t)(struct raid_client*, raid_error_t, void*);

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
    pthread_mutex_t reqs_mutex;
    pthread_t recv_thread;
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
void* raid_recv_loop(void* cl);

/**
 * Close the connection, making subsequent calls invalid.
 */
raid_error_t raid_close(raid_client_t* cl);


raid_error_t raid_socket_connect(raid_socket_t* s, const char* host, const char* port);

bool raid_socket_connected(raid_socket_t* s);

raid_error_t raid_socket_send(raid_socket_t* s, const char* data, size_t data_len);

raid_error_t raid_socket_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len);

raid_error_t raid_socket_close(raid_socket_t* s);


void raid_read_init(raid_client_t* cl, msgpack_object* header, msgpack_object* body);

bool raid_is_code(raid_client_t* cl, const char* code);

bool raid_read_code(raid_client_t* cl, char** res, size_t* len);

bool raid_read_int(raid_client_t* cl, int64_t* res);

bool raid_read_float(raid_client_t* cl, double* res);

bool raid_read_string(raid_client_t* cl, char** res, size_t* len);

bool raid_read_map_key(raid_client_t* cl, char** key, size_t* len);


bool raid_read_begin_array(raid_client_t* cl, size_t* len);

void raid_read_end_array(raid_client_t* cl);


bool raid_read_begin_map(raid_client_t* cl, size_t* len);

void raid_read_end_map(raid_client_t* cl);


bool raid_read_next(raid_client_t* cl);


raid_error_t raid_write_message(raid_client_t* cl, const char* action);

raid_error_t raid_write_int(raid_client_t* cl, int n);

raid_error_t raid_write_float(raid_client_t* cl, float n);

raid_error_t raid_write_string(raid_client_t* cl, const char* str, size_t len);

raid_error_t raid_write_array(raid_client_t* cl, size_t len);

raid_error_t raid_write_map(raid_client_t* cl, size_t keys_len);

raid_error_t raid_write_key_value_int(raid_client_t* cl, const char* key, size_t key_len, int n);

raid_error_t raid_write_key_value_float(raid_client_t* cl, const char* key, size_t key_len, float n);

raid_error_t raid_write_key_value_string(raid_client_t* cl, const char* key, size_t key_len, const char* str, size_t len);

raid_error_t raid_write_arrayf(raid_client_t* cl, int n, const char* format, ...);

raid_error_t raid_write_mapf(raid_client_t* cl, int n, const char* format, ...);


const char* raid_error_to_string(raid_error_t err);


#endif
