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
 *  raid_writer_init(&w);
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
    msgpack_zone* mempool; // owns
    msgpack_object* obj; // owns
    msgpack_object* etag_obj;
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
typedef void(*raid_callback_t)(struct raid_client*, raid_reader_t*, raid_error_t, void*);

/**
 * A Raid request.
 */
typedef struct raid_request {
    char* etag;
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
 * @brief Connect to the given host and port.
 * 
 * @param cl Raid client instance.
 * @param host Hostname to connect.
 * @param port Port to connect in the host.
 * @return Any errors that might occur.
 */
raid_error_t raid_connect(raid_client_t* cl, const char* host, const char* port);

bool raid_connected(raid_client_t* cl);

/**
 * @brief Send a request to the raid server.
 * 
 * @param cl Raid client instance.
 * @param w Request writer.
 * @param cb Response callback.
 * @param user_data Callback user data.
 * @return Any errors that might occur.
 */
raid_error_t raid_request_async(raid_client_t* cl, const raid_writer_t* w, raid_callback_t cb, void* user_data);

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
raid_error_t raid_close(raid_client_t* cl);

/**
 * @brief Initialize the reader state.
 * 
 * @param r Reader instance.
 */
void raid_reader_init(raid_reader_t* r);

/**
 * @brief Destroy the reader state.
 * 
 * @param r Reader instance.
 */
void raid_reader_destroy(raid_reader_t* r);

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
 * @brief Reads a string from the response message body.
 * 
 * @param r Raid client instance.
 * @param res Pointer to receive string value.
 * @param len Pointer to receive the length of the string.
 * @return Whether the value could be read or not.
 */
bool raid_read_string(raid_reader_t* r, char** res, size_t* len);

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
void raid_writer_init(raid_writer_t* w);

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
 * @brief Write an integer in the request body.
 * 
 * @param w Raid client instance.
 * @param n Integer number.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_int(raid_writer_t* w, int n);

/**
 * @brief Write a float in the request body.
 * 
 * @param w Raid client instance.
 * @param n Float number.
 * @return Any errors that might occur.
 */
raid_error_t raid_write_float(raid_writer_t* w, float n);

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
