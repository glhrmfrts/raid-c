#include <msgpack.h>
#include <ctype.h>
#include "raid.h"
#include "raid_internal.h"

#define RAID_TIMEOUT_DEFAULT_SECS (10)

// 1GB
#define RAID_MAX_MSG_SIZE (1*1024*1024*1024)

typedef struct {
    pthread_cond_t cond_var;
    pthread_mutex_t mutex;
    bool done;
    raid_error_t err;
    raid_writer_t response_writer;
} request_sync_data_t;

#ifdef RAID_DEBUG_REQUESTS
static void debug_etags(raid_client_t* cl)
{
    raid_request_t* req = cl->reqs;
    while (req) {
        printf("ETAG: %s\n", req->etag);
        req = req->next;
    }
}

static int debug_count_requests(raid_client_t* cl, const char* etag)
{
    int i = 0;
    raid_request_t* req = cl->reqs;
    while (req) {
        if (!strncmp(req->etag, etag, strlen(req->etag))) {
            i++;
        }
        req = req->next;
    }
    return i;
}
#endif

static void call_before_send_callbacks(raid_client_t* cl, const char* data, size_t data_len)
{
    raid_callback_t* cb = cl->callbacks;
    while (cb) {
        if (cb->type == RAID_CALLBACK_BEFORE_SEND) {
            cb->callback.before_send(cl, data, data_len, cb->user_data);
        }
        cb = cb->next;
    }
}

static void call_after_recv_callbacks(raid_client_t* cl, const char* data, size_t data_len)
{
    raid_callback_t* cb = cl->callbacks;
    while (cb) {
        if (cb->type == RAID_CALLBACK_AFTER_RECV) {
            cb->callback.after_recv(cl, data, data_len, cb->user_data);
        }
        cb = cb->next;
    }
}

static void call_msg_recv_callbacks(raid_client_t* cl, raid_reader_t* r)
{
    raid_callback_t* cb = cl->callbacks;
    while (cb) {
        if (cb->type == RAID_CALLBACK_MSG_RECV) {
            cb->callback.msg_recv(cl, r, cb->user_data);
        }
        cb = cb->next;
    }
}

static void clear_callbacks(raid_client_t* cl)
{
    raid_callback_t* cb = cl->callbacks;
    while (cb) {
        raid_callback_t* swap = cb;
        cb = cb->next;
        free(swap);
    }
    cl->callbacks = NULL;
}

static void free_request(raid_request_t* req)
{
    const char* etag = req->etag;
    raid_dealloc(req, etag);
    free((void*)etag);
}

static raid_request_t* find_request(raid_client_t* cl, const char* etag)
{
    raid_request_t* req = cl->reqs;
    while (req) {
        if (!strncmp(req->etag, etag, strlen(req->etag))) {
            return req;
        }
        req = req->next;
    }
    return NULL;
}

static void reply_request(raid_client_t* cl, raid_reader_t* r)
{
    // Find the request to reply to.
    raid_request_t* req = NULL;

    pthread_mutex_lock(&cl->reqs_mutex);
    if (r->etag_obj) {
        req = find_request(cl, r->etag_obj->via.str.ptr);
    }
    pthread_mutex_unlock(&cl->reqs_mutex);

    if (!req) {
        call_msg_recv_callbacks(cl, r);
    }
    else {
        // Fire the request callback.
        req->callback(cl, r, RAID_SUCCESS, req->callback_user_data);

        pthread_mutex_lock(&cl->reqs_mutex);
        LIST_REMOVE(cl->reqs, req);
        free_request(req);
        pthread_mutex_unlock(&cl->reqs_mutex);
    }
}

static void parse_response(raid_client_t* cl)
{
    raid_reader_t r;
    raid_reader_init(&r);
    raid_reader_set_data(&r, cl->msg_buf, cl->msg_len, true);

    if (r.obj->type == MSGPACK_OBJECT_MAP) {
        reply_request(cl, &r);
    }

    raid_reader_destroy(&r);
}

static int read_message(raid_client_t* cl)
{
    switch (cl->state) {
    case RAID_STATE_WAIT_MESSAGE: {
        if ((cl->in_end - cl->in_ptr) < 4) return -1;

        uint32_t len = ((uint8_t)cl->in_ptr[0] << 24) | ((uint8_t)cl->in_ptr[1] << 16) | ((uint8_t)cl->in_ptr[2] << 8) | ((uint8_t)cl->in_ptr[3]);
        if (len <= RAID_MAX_MSG_SIZE) {
            cl->state = RAID_STATE_PROCESSING_MESSAGE;
            cl->msg_total_size = len;
            cl->msg_len = 0;
            cl->msg_buf = raid_alloc(cl->msg_total_size*sizeof(char), "msg_buf");
            return 4;
        }
        else {
            return -1;
        }
    }

    case RAID_STATE_PROCESSING_MESSAGE: {
        size_t buf_left = (size_t)(cl->in_end - cl->in_ptr);
        size_t copy_len = (cl->msg_total_size - cl->msg_len);
        if (buf_left < copy_len) {
            copy_len = buf_left;
        }

        memcpy(cl->msg_buf + cl->msg_len, cl->in_ptr, copy_len);
        cl->msg_len += copy_len;

        if (cl->msg_len >= cl->msg_total_size) {
            call_after_recv_callbacks(cl, cl->msg_buf, cl->msg_len);
            parse_response(cl);
            raid_dealloc(cl->msg_buf, "msg_buf");
            cl->msg_buf = NULL;
            cl->state = RAID_STATE_WAIT_MESSAGE;
        }
        return copy_len;
    }

    default:
        return -1;
    }
}

static void process_data(raid_client_t* cl, const char* buf, size_t buf_len)
{
    cl->in_ptr = buf;
    cl->in_end = buf + buf_len;
    while (cl->in_ptr < cl->in_end) {
        int i = read_message(cl);
        if (i > 0) {
            cl->in_ptr += i;
        }
        else {
            break;
        }
    }
}

static void sync_request_callback(raid_client_t* cl, raid_reader_t* r, raid_error_t err, void* user_data)
{
    (void)cl;

    request_sync_data_t* data = (request_sync_data_t*)user_data;
    if (err == RAID_SUCCESS) {
        // To be safe, copy the data from the reader to our writer
        // (just swapping the pointers from another thread gives scary results)
        raid_write_object(&data->response_writer, r->obj);
    }

    data->err = err;
    pthread_mutex_lock(&data->mutex);
    data->done = true;
    pthread_cond_signal(&data->cond_var);
    pthread_mutex_unlock(&data->mutex);
}

static raid_error_t request_sync_init(request_sync_data_t* data, raid_client_t* cl)
{
    memset(data, 0, sizeof(request_sync_data_t));
    raid_writer_init(&data->response_writer, cl);

    int err = pthread_mutex_init(&data->mutex, NULL);
    if (err != 0) {
        return RAID_UNKNOWN;
    }

    err = pthread_cond_init(&data->cond_var, NULL);
    if (err != 0) {
        return RAID_UNKNOWN;
    }

    return RAID_SUCCESS;
}

static void request_sync_destroy(request_sync_data_t* data)
{
    pthread_mutex_destroy(&data->mutex);
    pthread_cond_destroy(&data->cond_var);
    raid_writer_destroy(&data->response_writer);
}

static void clear_requests_locked(raid_client_t* cl)
{
    pthread_mutex_lock(&cl->reqs_mutex);
    raid_request_t* req = cl->reqs;
    while (req) {
        req->callback(cl, NULL, RAID_NOT_CONNECTED, req->callback_user_data);

        raid_request_t* swap = req;
        req = req->next;
        free_request(swap);
    }
    cl->reqs = NULL;
    pthread_mutex_unlock(&cl->reqs_mutex);
}

static void check_requests_for_timeout_locked(raid_client_t* cl, raid_error_t recv_err)
{
    pthread_mutex_lock(&cl->reqs_mutex);

    int64_t now_time = (int64_t)time(NULL);
    raid_request_t* req = cl->reqs;
    while (req) {
        raid_request_t* next_req = req->next;
        bool should_remove = (recv_err == RAID_NOT_CONNECTED) || ((now_time - req->created_at) > req->timeout_secs);
        if (should_remove) {
            req->callback(cl, NULL, recv_err, req->callback_user_data);

            LIST_REMOVE(cl->reqs, req);
            free_request(req);
        }

        req = next_req;
    }

    pthread_mutex_unlock(&cl->reqs_mutex);
}

static void* raid_recv_loop(void* arg)
{
    raid_client_t* cl = (raid_client_t*)arg;
    char buf[4096];
    int buf_len = 0;

    while (raid_socket_connected(&cl->socket)) {
        raid_error_t err = raid_socket_recv(&cl->socket, buf, sizeof(buf), &buf_len);
        if (err && err != RAID_RECV_TIMEOUT) {
            fprintf(stderr, "[raid] recv error: %s\n", raid_error_to_string(err));
        }

        if (buf_len > 0) {
            process_data(cl, buf, buf_len);
        }
        else if (err == RAID_RECV_TIMEOUT) {
            check_requests_for_timeout_locked(cl, err);
            if (!cl->reqs && cl->state == RAID_STATE_PROCESSING_MESSAGE) {
                cl->state = RAID_STATE_WAIT_MESSAGE;
                raid_dealloc(cl->msg_buf, "msg_buf (timeout)");
                cl->msg_buf = NULL;
            }
        }
        buf_len = 0;

        // TODO: discover remaining unknown errors in socket_recv
        if (err == RAID_SUCCESS) {
            err = RAID_RECV_TIMEOUT;
        }
        check_requests_for_timeout_locked(cl, err);
    }

    clear_requests_locked(cl);
    return NULL;
}

static void join_recv_thread(raid_client_t* cl)
{
    if (cl->recv_thread_active) {
        cl->recv_thread_active = false;
        pthread_join(cl->recv_thread, NULL);
    }
}

static void detach_recv_thread(raid_client_t* cl)
{
    if (cl->recv_thread_active) {
        cl->recv_thread_active = false;
        pthread_detach(cl->recv_thread);
    }
}

raid_error_t raid_init(raid_client_t* cl, const char* host, const char* port)
{
    if (!host || !port)
        return RAID_INVALID_ARGUMENT;

    memset(cl, 0, sizeof(raid_client_t));
    cl->state = RAID_STATE_WAIT_MESSAGE;
    cl->host = strdup(host);
    cl->port = strdup(port);
    cl->socket.handle = -1;
    cl->request_timeout_secs = RAID_TIMEOUT_DEFAULT_SECS;

    int err = pthread_mutex_init(&cl->reqs_mutex, NULL);
    if (err != 0) {
        fprintf(stderr, "Cannot create mutex: %s\n", strerror(err));
        return RAID_UNKNOWN;
    }
    return RAID_SUCCESS;
}

raid_error_t raid_connect(raid_client_t* cl)
{
    raid_error_t result = RAID_UNKNOWN;
    pthread_mutex_lock(&cl->reqs_mutex);

    if (raid_socket_connected(&cl->socket)) {
        result = RAID_NOT_CONNECTED;
    }
    else {
        result = raid_socket_connect(&cl->socket, cl->host, cl->port);
        cl->recv_thread_active = false;
        if (result == RAID_SUCCESS) {
            // Increment connection id
            __sync_fetch_and_add((volatile unsigned int*)&cl->connection_id, 1);

            // Create receiver thread
            int err = pthread_create(&cl->recv_thread, NULL, &raid_recv_loop, (void*)cl);
            if (err != 0) {
                fprintf(stderr, "Cannot create thread: %s\n", strerror(err));
                result = RAID_UNKNOWN;
            }
            else {
                cl->recv_thread_active = true;
            }
        }
    }

    pthread_mutex_unlock(&cl->reqs_mutex);
    return result;
}

bool raid_connected(raid_client_t* cl)
{
    return raid_socket_connected(&cl->socket);
}

unsigned int raid_connection_id(raid_client_t* cl)
{
    return __sync_fetch_and_add((volatile unsigned int*)&cl->connection_id, 0);
}

void raid_add_before_send_callback(raid_client_t* cl, raid_before_send_callback_t cb, void* user_data)
{
    raid_callback_t* data = malloc(sizeof(raid_callback_t));
    data->type = RAID_CALLBACK_BEFORE_SEND;
    data->callback.before_send = cb;
    data->user_data = user_data;
    LIST_APPEND(cl->callbacks, data);
}

void raid_add_after_recv_callback(raid_client_t* cl, raid_after_recv_callback_t cb, void* user_data)
{
    raid_callback_t* data = malloc(sizeof(raid_callback_t));
    data->type = RAID_CALLBACK_AFTER_RECV;
    data->callback.after_recv = cb;
    data->user_data = user_data;
    LIST_APPEND(cl->callbacks, data);
}

void raid_add_msg_recv_callback(raid_client_t* cl, raid_msg_recv_callback_t cb, void* user_data)
{
    raid_callback_t* data = malloc(sizeof(raid_callback_t));
    data->type = RAID_CALLBACK_MSG_RECV;
    data->callback.msg_recv = cb;
    data->user_data = user_data;
    LIST_APPEND(cl->callbacks, data);
}

void raid_set_request_timeout(raid_client_t* cl, int64_t timeout_secs)
{
    cl->request_timeout_secs = timeout_secs;
}

raid_error_t raid_request_async(raid_client_t* cl, const raid_writer_t* w, raid_response_callback_t cb, void* user_data)
{
    raid_error_t result = RAID_SUCCESS;
    pthread_mutex_lock(&cl->reqs_mutex);

    if (raid_socket_connected(&cl->socket)) {
        // Send data to socket
        int32_t size = w->sbuf.size;
        char data_size[4];
        data_size[0] = (size >> 24) & 0xFF;
        data_size[1] = (size >> 16) & 0xFF;
        data_size[2] = (size >> 8) & 0xFF;
        data_size[3] = size & 0xFF;

        call_before_send_callbacks(cl, w->sbuf.data, size);

        result = raid_socket_send(&cl->socket, data_size, sizeof(data_size));
        if (result == RAID_SUCCESS) {
            result = raid_socket_send(&cl->socket, w->sbuf.data, size);
        }

        if (result == RAID_NOT_CONNECTED) {
            raid_socket_close(&cl->socket);
            detach_recv_thread(cl);
        }
        else if (result == RAID_SUCCESS) {
            // Append a request to the list
            raid_request_t* req = raid_alloc(sizeof(raid_request_t), w->etag);
            memset(req, 0, sizeof(raid_request_t));
            req->created_at = (int64_t)time(NULL);
            req->timeout_secs = cl->request_timeout_secs;
            req->etag = strdup(w->etag);
            req->callback = cb;
            req->callback_user_data = user_data;
            LIST_APPEND(cl->reqs, req);
        }
    }
    else {
        result = RAID_NOT_CONNECTED;
    }

    pthread_mutex_unlock(&cl->reqs_mutex);
    return result;
}

raid_error_t raid_request(raid_client_t* cl, const raid_writer_t* w, raid_reader_t* out)
{
    request_sync_data_t* data = malloc(sizeof(request_sync_data_t));
    raid_error_t res = request_sync_init(data, cl);
    if (res != RAID_SUCCESS) {
        request_sync_destroy(data);
        free(data);
        return res;
    }

    res = raid_request_async(cl, w, sync_request_callback, (void*)data);
    if (res != RAID_SUCCESS) {
        request_sync_destroy(data);
        free(data);
        return res;
    }

    pthread_mutex_lock(&data->mutex);
    while (!data->done) {
        pthread_cond_wait(&data->cond_var, &data->mutex);
    }
    pthread_mutex_unlock(&data->mutex);

    res = data->err;
    if (res == RAID_SUCCESS) {
        raid_reader_set_data(out, data->response_writer.sbuf.data, data->response_writer.sbuf.size, true);
    }

    request_sync_destroy(data);
    free(data);
    return res;
}

raid_error_t raid_disconnect(raid_client_t* cl)
{
    pthread_mutex_lock(&cl->reqs_mutex);
    raid_error_t err = raid_socket_close(&cl->socket);
    pthread_mutex_unlock(&cl->reqs_mutex);

    join_recv_thread(cl);
    return err;
}

void raid_destroy(raid_client_t* cl)
{
    if (raid_socket_connected(&cl->socket)) {
        pthread_mutex_lock(&cl->reqs_mutex);
        (void)raid_socket_close(&cl->socket);
        pthread_mutex_unlock(&cl->reqs_mutex);
    }

    join_recv_thread(cl);
    pthread_mutex_destroy(&cl->reqs_mutex);
    clear_callbacks(cl);
    if (cl->host) {
        free(cl->host);
    }
    if (cl->port) {
        free(cl->port);
    }
}
