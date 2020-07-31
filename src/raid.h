/** @file raid.h
 *
 *  @brief Raid client functions.
 *
 *  Example usage:
 *
 *  @code
 *  raid_client_t client;
 *
 *  raid_error_t err = raid_connect(&client, "host", "port");
 *
 *  raid_writer_t w;
 *  raid_writer_init(&w, &hcs->raid);
 *  raid_write_message(&w, "api.action");
 *  raid_write_mapf(&w, 2, "'somenumber' %d 'somestr' %s", 42, "mystring");
 *  raid_request_async(&client, &w, response_callback, NULL); // Async request
 *
 *  raid_reader_t r;
 *  raid_reader_init(&r);
 *  raid_request(&client, &w, &r); // Sync request
 *
 *  int64_t number;
 *  raid_read_int(&r, &number); // In this example, the server returns a number
 *
 *  raid_writer_destroy(&w); // When writer is not needed anymore
 *  raid_reader_destroy(&r); // When reader is not needed anymore
 *  raid_close(&client); // Close connection
 *  @endcode
 */

#ifndef RAID_H
#define RAID_H

#include <pthread.h>
#include <msgpack.h>

struct raid_client;

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

typedef enum {
    RAID_INVALID,
    RAID_NIL,
    RAID_INT,
    RAID_FLOAT,
    RAID_STRING,
    RAID_BINARY,
    RAID_ARRAY,
    RAID_MAP
} raid_type_t;

typedef enum { 
    RAID_CALLBACK_BEFORE_SEND,
    RAID_CALLBACK_AFTER_RECV,
    RAID_CALLBACK_MSG_RECV,
} raid_callback_type_t;

typedef struct raid_socket {
    int handle;
} raid_socket_t;

typedef struct raid_reader {
    char* src_data; // owns
    size_t src_data_len;
    msgpack_zone* mempool; // owns
    msgpack_object* obj; // owns
    msgpack_object* etag_obj;
    msgpack_object* header;
    msgpack_object* body;
    msgpack_object* nested;
    msgpack_object* parents[64];
    int indices[64];
    int nested_top;
} raid_reader_t;

typedef struct raid_writer {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    char* etag;
    struct raid_client* cl;
} raid_writer_t;

typedef enum raid_state {
    RAID_STATE_WAIT_MESSAGE,
    RAID_STATE_PROCESSING_MESSAGE,
} raid_state_t;

/**
 * The type of the request callback function.
 */
typedef void(*raid_response_callback_t)(struct raid_client*, raid_reader_t*, raid_error_t, void*);

/**
 * Callback called before sending data to the server.
 */
typedef void(*raid_before_send_callback_t)(struct raid_client*, const char*, size_t, void*);

/**
 * Callback called after receiving data from the server.
 */
typedef void(*raid_after_recv_callback_t)(struct raid_client*, const char*, size_t, void*);

/**
 * Callback called after receiving a valid message from the server
 * which is not a response to a previously made request.
 */
typedef void(*raid_msg_recv_callback_t)(struct raid_client*, raid_reader_t*, void*);

/**
 * A Raid request.
 */
typedef struct raid_request {
    int64_t created_at;
    int64_t timeout_secs;
    char* etag;
    raid_response_callback_t callback;
    void* callback_user_data;
    struct raid_request* next;
    struct raid_request* prev;
} raid_request_t;

typedef struct raid_callback {
    raid_callback_type_t type;
    void* user_data;
    union {
        raid_before_send_callback_t before_send;
        raid_after_recv_callback_t after_recv;
        raid_msg_recv_callback_t msg_recv;
    } callback;
    struct raid_callback* next;
    struct raid_callback* prev;
} raid_callback_t;

/**
 * The client state holding sockets, requests, etc...
 */
typedef struct raid_client {
    unsigned int connection_id;
    raid_socket_t socket;
    char* host;
    char* port;
    const char* in_ptr;
    const char* in_end;
    char* msg_buf;
    size_t msg_total_size;
    size_t msg_len;
    size_t etag_gen_cnt;
    raid_state_t state;
    raid_request_t* reqs;
    raid_callback_t* callbacks;
    pthread_mutex_t reqs_mutex;
    pthread_t recv_thread;
} raid_client_t;

/**
 * @brief Configure the client's host and port.
 * 
 * @param cl Raid client instance.
 * @param host Hostname to connect.
 * @param port Port to connect in the host.
 * @return Any errors that might occur.
 */
raid_error_t raid_init(raid_client_t* cl, const char* host, const char* port);

/**
 * @brief Connect to the client's host and port.
 *
 * @param cl Raid client instance.
 * @return Any errors that might occur.
 */
raid_error_t raid_connect(raid_client_t* cl);

/**
 * @brief Returns whether the client is connected or not.
 * 
 * @param cl Raid client instance.
 * @return Whether the client is connected or not.
 */
bool raid_connected(raid_client_t* cl);

/**
 * @brief Returns the current connection id, changes every time we open a connection.
 * 
 * @param cl Raid client instance.
 * @return The current connection id.
 */
unsigned int raid_connection_id(raid_client_t* cl);

/**
 * @brief Adds a "before_send" callback, which gets called before sending data to the server.
 * 
 * @param cl Raid client instance.
 * @param cb Callback to be called.
 * @param user_data Callback user data.
 * @return Any errors that might occur.
 */
void raid_add_before_send_callback(raid_client_t* cl, raid_before_send_callback_t cb, void* user_data);

/**
 * @brief Adds a "after_recv" callback, which gets called after receiving data to the server.
 * 
 * @param cl Raid client instance.
 * @param cb Callback to be called.
 * @param user_data Callback user data.
 * @return Any errors that might occur.
 */
void raid_add_after_recv_callback(raid_client_t* cl, raid_after_recv_callback_t cb, void* user_data);

/**
 * @brief Adds a "msg_recv" callback, which gets called after receiving a
 * valid message from the server which is not a response to a previously made request.
 * 
 * @param cl Raid client instance.
 * @param cb Callback to be called.
 * @param user_data Callback user data.
 * @return Any errors that might occur.
 */
void raid_add_msg_recv_callback(raid_client_t* cl, raid_msg_recv_callback_t cb, void* user_data);

/**
 * @brief Send a request to the raid server.
 * 
 * @param cl Raid client instance.
 * @param w Request writer.
 * @param cb Response callback.
 * @param user_data Callback user data.
 * @return Any errors that might occur.
 */
raid_error_t raid_request_async(raid_client_t* cl, const raid_writer_t* w, raid_response_callback_t cb, void* user_data);

/**
 * @brief Send a request to the raid server and block until response is received.
 * 
 * @param cl Raid client instance.
 * @param w Request writer.
 * @param r Reader to receive response.
 * @return Any errors that might occur.
 */
raid_error_t raid_request(raid_client_t* cl, const raid_writer_t* w, raid_reader_t* r);

/**
 * @brief Close the connection, making subsequent calls invalid.
 * 
 * @param cl Raid client instance.
 * @return Any errors that might occur.
 */
raid_error_t raid_disconnect(raid_client_t* cl);

/**
 * @brief Destroy the client instance and it's resources.
 *
 * @param cl Raid client instance.
 */
void raid_destroy(raid_client_t* cl);

/**
 * @brief Generates an etag (random string), the caller owns the string.
 * 
 * @return Any errors that might occur.
 */
char* raid_gen_etag(raid_client_t* cl);

/**
 * @brief Initialize the reader state.
 * 
 * @param r Reader instance.
 */
void raid_reader_init(raid_reader_t* r);

/**
 * @brief Initialize the reader state with data.
 * 
 * @param r Reader instance.
 * @param w Writer instance.
 */
void raid_reader_init_with_data(raid_reader_t* r, const char* data, size_t data_len);

/**
 * @brief Destroy the reader state.
 * 
 * @param r Reader instance.
 */
void raid_reader_destroy(raid_reader_t* r);

/**
 * @brief Swap the contents of two readers.
 * 
 * @param from First reader.
 * @param to Second reader.
 */
void raid_reader_swap(raid_reader_t* from, raid_reader_t* to);

/**
 * @brief Returns true if the response message code is equal to this one.
 * 
 * @param r Raid client instance.
 * @param code String with the code to compare.
 * @return Whether response message code is equal or not.
 */
bool raid_is_code(raid_reader_t* r, const char* code);

/**
 * @brief Reads the code from the response message.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive the code string.
 * @param len Pointer to receive the length of the string.
 * @return Whether the code could be read or not.
 */
bool raid_read_code(raid_reader_t* r, char** res, size_t* len);

/**
 * @brief Reads the null-terminated code from the response message.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive the code string.
 * @return Whether the code could be read or not.
 */
bool raid_read_code_cstring(raid_reader_t* r, char** res);

bool raid_read_etag_cstring(raid_reader_t* r, char** res);

/**
 * @brief Returns the type of the current value in the response message body.
 * 
 * @param r Raid client instance.
 * @return The type of the value.
 */
raid_type_t raid_read_type(raid_reader_t* r);

/**
 * @brief Reads an integer from the response message body.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive integer value.
 * @return Whether the value could be read or not.
 */
bool raid_read_int(raid_reader_t* r, int64_t* res);

/**
 * @brief Reads a float from the response message body.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive float value.
 * @return Whether the value could be read or not.
 */
bool raid_read_float(raid_reader_t* r, double* res);

/**
 * @brief Reads a binary array from the response message body.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive data.
 * @param len Pointer to receive the length of the array.
 * @return Whether the value could be read or not.
 */
bool raid_read_binary(raid_reader_t* r, char** res, size_t* len);

/**
 * @brief Reads a string from the response message body.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive string value.
 * @param len Pointer to receive the length of the string.
 * @return Whether the value could be read or not.
 */
bool raid_read_string(raid_reader_t* r, char** res, size_t* len);

/**
 * @brief Reads a string from the response message body and null-terminates it.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive string value.
 * @return Whether the value could be read or not.
 */
bool raid_read_cstring(raid_reader_t* r, char** res);

/**
 * @brief Copy a string from the response message body to a pre-allocated buffer.
 * 
 * @param r Raid client instance.
 * @param buf A pointer to an allocated buffer to receive string value.
 * @param buf_len Size of the buffer.
 * @return Returns false if the buffer size is not enough to hold the complete string.
 */
bool raid_copy_cstring(raid_reader_t* r, char* buf, size_t buf_len);

/**
 * @brief Reads the current map key.
 * 
 * @param r Raid client instance.
 * @param key Pointer to receive string value.
 * @param len Pointer to receive the length of the string.
 * @return Whether the value could be read or not.
 */
bool raid_read_map_key(raid_reader_t* r, char** key, size_t* len);

/**
 * @brief Reads the current map key null-terminated version.
 * 
 * @param r Raid client instance.
 * @param key Pointer to receive string value.
 * @param len Pointer to receive the length of the string.
 * @return Whether the value could be read or not.
 */
bool raid_read_map_key_cstring(raid_reader_t* r, char** key);

/**
 * @brief Compares the current map key.
 * 
 * @param r Raid client instance.
 * @param key String to compare to.
 * @return Whether the current map key is equal to the argument or not.
 */
bool raid_is_map_key(raid_reader_t* r, const char* key);

/**
 * @brief Begins reading an array from the response message body.
 *
 * @param r Raid client instance.
 * @param len Pointer to receive the length of the array.
 * @return Whether the value could be read or not.
 */
bool raid_read_begin_array(raid_reader_t* r, size_t* len);

/**
 * @brief Stops reading an array from the response message body.
 *
 * @param r Raid client instance.
 */
void raid_read_end_array(raid_reader_t* r);

/**
 * @brief Begins reading a map from the response message body.
 *
 * @param r Raid client instance.
 * @param len Pointer to receive the length of the map.
 * @return Whether the value could be read or not.
 */
bool raid_read_begin_map(raid_reader_t* r, size_t* len);

/**
 * @brief Stops reading a map from the response message body.
 *
 * @param r Raid client instance.
 */
void raid_read_end_map(raid_reader_t* r);

/**
 * @brief Advance to the next item in the current array or map.
 *
 * @param r Raid client instance.
 * @return Whether the value could be read or not.
 */
bool raid_read_next(raid_reader_t* r);

/**
 * @brief Initialize the writer state.
 * 
 * @param w Writer instance.
 */
void raid_writer_init(raid_writer_t* w, raid_client_t* cl);

/**
 * @brief Destroy the writer state.
 * 
 * @param w Writer instance.
 */
void raid_writer_destroy(raid_writer_t* w);

/**
 * @brief Begin writing a request message to send to the server.
 * 
 * @param w Raid client instance.
 * @param action Action of the message.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_message(raid_writer_t* w, const char* action);

/**
 * @brief Begin writing a request message that does not have a body, only header.
 * 
 * @param w Raid client instance.
 * @param action Action of the message.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_message_without_body(raid_writer_t* w, const char* action);

raid_error_t raid_write_object(raid_writer_t* w, const msgpack_object* obj);

/**
 * @brief Write nil in the request body.
 * 
 * @param w Raid client instance.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_nil(raid_writer_t* w);

/**
 * @brief Write an integer in the request body.
 * 
 * @param w Raid client instance.
 * @param n Integer number.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_int(raid_writer_t* w, int64_t n);

/**
 * @brief Write a float in the request body.
 * 
 * @param w Raid client instance.
 * @param n Float number.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_float(raid_writer_t* w, double n);

/**
 * @brief Write a binary array in the request body.
 * 
 * @param w Raid client instance.
 * @param str Array data.
 * @param len Array size.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_binary(raid_writer_t* w, const char* data, size_t len);

/**
 * @brief Write a string in the request body.
 * 
 * @param w Raid client instance.
 * @param str String data.
 * @param len String size.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_string(raid_writer_t* w, const char* str, size_t len);

/**
 * @brief Write an array in the request body.
 * 
 * @param w Raid client instance.
 * @param len Array size.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_array(raid_writer_t* w, size_t len);

/**
 * @brief Write a map in the request body.
 * 
 * @param w Raid client instance.
 * @param keys_len Number of keys in the map.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_map(raid_writer_t* w, size_t keys_len);

/**
 * @brief Variadic function to write an array in the request body according to the given format
 * and arguments passed, e.g.: @c raid_write_arrayf(cl, 2, "%d %s", 10, "string")
 * 
 * @param w Raid client instance.
 * @param n Number of arguments.
 * @param format Format string.
 * @param ... Arguments to put in the array.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_arrayf(raid_writer_t* w, int n, const char* format, ...);

/**
 * @brief Variadic function to write a map in the request body according to the given format
 * and arguments passed, e.g.: @c raid_write_mapf(cl, 2, "'number' %d 'name' %s", 10, "string")
 * 
 * @param w Raid client instance.
 * @param n Number of arguments.
 * @param format Format string.
 * @param ... Arguments to put in the map.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_mapf(raid_writer_t* w, int n, const char* format, ...);

/**
 * @brief Get human-readable description for errors.
 * 
 * @param err The error code,
 * @return Error description.
 */
const char* raid_error_to_string(raid_error_t err);


#endif
