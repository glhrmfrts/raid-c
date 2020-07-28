#include <msgpack.h>
#include <ctype.h>
#include "raid.h"
#include "raid_internal.h"

typedef struct {
    pthread_cond_t cond_var;
    pthread_mutex_t mutex;
    bool done;
    raid_error_t err;
    raid_writer_t response_writer;
} request_sync_data_t;

static void* raid_recv_loop(void* arg);

static void debug_etags(raid_client_t* cl)
{
    raid_request_t* req = cl->reqs;
    while (req) {
        printf("etag: %s\n", req->etag);
        req = req->next;
    }
}

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
    raid_request_t* req = find_request(cl, r->etag_obj->via.str.ptr);
    if (!req) {
        call_msg_recv_callbacks(cl, r);
        return;
    }

    // Fire the request callback.
    req->callback(cl, r, RAID_SUCCESS, req->callback_user_data);

    // Remove from list.
    pthread_mutex_lock(&cl->reqs_mutex);

    if (req == cl->reqs) {
        cl->reqs = req->next;
    }
    if (req->prev) {
        req->prev->next = req->next;
    }
    if (req->next) {
        req->next->prev = req->prev;
    }
    free(req->etag);
    free(req);

    pthread_mutex_unlock(&cl->reqs_mutex);
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
        cl->state = RAID_STATE_PROCESSING_MESSAGE;
        cl->msg_total_size = len;
        cl->msg_len = 0;
        cl->msg_buf = malloc(cl->msg_total_size*sizeof(char));
        return 4;
    }

    case RAID_STATE_PROCESSING_MESSAGE: {
        size_t copy_len = cl->in_end - cl->in_ptr;
        memcpy(cl->msg_buf + cl->msg_len, cl->in_ptr, copy_len);
        cl->msg_len += copy_len;

        if (cl->msg_len >= cl->msg_total_size) {
            call_after_recv_callbacks(cl, cl->msg_buf, cl->msg_len);
            parse_response(cl);
            free(cl->msg_buf);
            cl->msg_buf = NULL;
            cl->state = RAID_STATE_WAIT_MESSAGE;
        }
        return cl->in_end - cl->in_ptr;
    }
    }

    return -1;
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

raid_error_t raid_connect(raid_client_t* cl, const char* host, const char* port)
{
    memset(cl, 0, sizeof(raid_client_t));
    cl->state = RAID_STATE_WAIT_MESSAGE;
    raid_error_t err = raid_socket_connect(&cl->socket, host, port);
    if (err == RAID_SUCCESS) {
        int res = pthread_create(&cl->recv_thread, NULL, &raid_recv_loop, (void*)cl);
        if (res != 0) {
            fprintf(stderr, "Cannot create thread: %s\n", strerror(res));
        }

        res = pthread_mutex_init(&cl->reqs_mutex, NULL);
        if (res != 0) {
            fprintf(stderr, "Cannot create mutex: %s\n", strerror(res));
        }
    }
    return err;
}

bool raid_connected(raid_client_t* cl)
{
    return raid_socket_connected(&cl->socket);
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

raid_error_t raid_request_async(raid_client_t* cl, const raid_writer_t* w, raid_response_callback_t cb, void* user_data)
{
    pthread_mutex_lock(&cl->reqs_mutex);

    // Append a request to the list
    raid_request_t* req = malloc(sizeof(raid_request_t));
    memset(req, 0, sizeof(raid_request_t));
    req->etag = strdup(w->etag);
    req->callback = cb;
    req->callback_user_data = user_data;
    if (cl->reqs) {
        cl->reqs->prev = req;
    }
    req->next = cl->reqs;
    req->prev = NULL;
    cl->reqs = req;
    
    // Send data to socket
    int32_t size = w->sbuf.size;
    static char data_size[4];
    data_size[0] = (size >> 24) & 0xFF;
    data_size[1] = (size >> 16) & 0xFF;
    data_size[2] = (size >> 8) & 0xFF;
    data_size[3] = size & 0xFF;

    call_before_send_callbacks(cl, w->sbuf.data, size);

    raid_error_t result = raid_socket_send(&cl->socket, data_size, sizeof(data_size));
    if (!result) {
        result = raid_socket_send(&cl->socket, w->sbuf.data, size);
    }

    pthread_mutex_unlock(&cl->reqs_mutex);

    return result;
}

raid_error_t raid_request(raid_client_t* cl, const raid_writer_t* w, raid_reader_t* out)
{
    request_sync_data_t* data = malloc(sizeof(request_sync_data_t));
    memset(data, 0, sizeof(request_sync_data_t));
    raid_writer_init(&data->response_writer);

    int err = pthread_mutex_init(&data->mutex, NULL);
    if (err != 0) {
        return RAID_UNKNOWN;
    }

    err = pthread_cond_init(&data->cond_var, NULL);
    if (err != 0) {
        return RAID_UNKNOWN;
    }

    raid_error_t res = raid_request_async(cl, w, sync_request_callback, (void*)data);
    if (res != RAID_SUCCESS) {
        pthread_mutex_destroy(&data->mutex);
        pthread_cond_destroy(&data->cond_var);
        raid_writer_destroy(&data->response_writer);
        free(data);
        return res;
    }

    pthread_mutex_lock(&data->mutex);
    while (!data->done) {
        pthread_cond_wait(&data->cond_var, &data->mutex);
    }
    pthread_mutex_unlock(&data->mutex);

    pthread_mutex_destroy(&data->mutex);
    pthread_cond_destroy(&data->cond_var);

    res = data->err;
    if (res == RAID_SUCCESS) {
        raid_reader_set_data(out, data->response_writer.sbuf.data, data->response_writer.sbuf.size, true);
    }
    
    raid_writer_destroy(&data->response_writer);
    free(data);

    return res;
}

raid_error_t raid_close(raid_client_t* cl)
{
    raid_error_t err = raid_socket_close(&cl->socket);
    pthread_join(cl->recv_thread, NULL);
    pthread_mutex_destroy(&cl->reqs_mutex);
    return err;
}

static void* raid_recv_loop(void* arg)
{
    raid_client_t* cl = (raid_client_t*)arg;
    char buf[4096];
    int buf_len = 0;

    while (true) {
        if (!raid_socket_connected(&cl->socket)) {
            break;
        }

        raid_error_t err = raid_socket_recv(&cl->socket, buf, sizeof(buf), &buf_len);
        if (err) {
            fprintf(stderr, "[raid] recv error: %s\n", raid_error_to_string(err));
            if (err == RAID_NOT_CONNECTED || err == RAID_RECV_TIMEOUT) {
                raid_request_t* req = cl->reqs;
                while (req) {
                    req->callback(cl, NULL, err, req->callback_user_data);

                    raid_request_t* swap = req;
                    req = req->next;
                    free(swap->etag);
                    free(swap);
                }
                cl->reqs = NULL;
            }
        }
        else {
            process_data(cl, buf, buf_len);
        }
    }

    return NULL;
}
