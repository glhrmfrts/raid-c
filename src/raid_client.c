#include <msgpack.h>
#include <ctype.h>
#include "raid.h"
#include "raid_internal.h"

static void* raid_recv_loop(void* arg);

static void debug_etags(raid_client_t* cl)
{
    raid_request_t* req = cl->reqs;
    while (req) {
        printf("etag: %s\n", req->etag);
        req = req->next;
    }
}

static msgpack_object* find_obj(msgpack_object* obj, const char* key)
{
    for (size_t i = 0; i < obj->via.map.size; i++) {
        if (!strncmp(key, obj->via.map.ptr[i].key.via.str.ptr, strlen(key))) {
            return &obj->via.map.ptr[i].val;
        }
    }
    return NULL;
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

static void reply_request(raid_client_t* cl)
{
    msgpack_object* body = find_obj(&cl->in_reader.obj, "body");
    msgpack_object* header = find_obj(&cl->in_reader.obj, "header");
    if (!header) return;

    msgpack_object* etag = find_obj(header, "etag");
    if (!etag) return;

    // Find the request to reply to.
    raid_request_t* req = find_request(cl, etag->via.str.ptr);
    if (!req) return;

    // Initialize reader state.
    raid_read_init(cl, header, body);

    // Fire the request callback.
    req->callback(cl, RAID_SUCCESS, req->callback_user_data);

    pthread_mutex_lock(&cl->reqs_mutex);

    //debug_etags(cl);

    // Remove from list.
    if (req->prev) {
        req->prev->next = req->next;
    }
    if (req->next) {
        req->next->prev = req->prev;
    }
    if (req == cl->reqs) {
        cl->reqs = NULL;
    }
    free(req->etag);
    free(req);

    pthread_mutex_unlock(&cl->reqs_mutex);
}

static void parse_response(raid_client_t* cl)
{
    msgpack_zone_init(&cl->in_reader.mempool, 2048);

    msgpack_unpack(cl->msg_buf, cl->msg_len, NULL, &cl->in_reader.mempool, &cl->in_reader.obj);

    if (cl->in_reader.obj.type == MSGPACK_OBJECT_MAP) {
        reply_request(cl);
    }

    msgpack_zone_destroy(&cl->in_reader.mempool);
}

static int read_message(raid_client_t* cl)
{
    switch (cl->state) {
    case RAID_STATE_WAIT_MESSAGE: {
        if ((cl->in_end - cl->in_ptr) < 4) return -1;

        uint32_t len = (cl->in_ptr[0] << 24) | (cl->in_ptr[1] << 16) | (cl->in_ptr[2] << 8) | (cl->in_ptr[3]);
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
            parse_response(cl);
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
        if (i >= 0) {
            cl->in_ptr += i;
        }
        else {
            break;
        }
    }
}

raid_error_t raid_connect(raid_client_t* cl, const char* host, const char* port)
{
    cl->reqs = NULL;
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

raid_error_t raid_request(raid_client_t* cl, raid_callback_t cb, void* user_data)
{
    pthread_mutex_lock(&cl->reqs_mutex);

    // Append a request to the list
    raid_request_t* req = malloc(sizeof(raid_request_t));
    memset(req, 0, sizeof(raid_request_t));
    req->etag = strdup(cl->out_writer.etag);
    req->callback = cb;
    req->callback_user_data = user_data;
    if (cl->reqs) {
        cl->reqs->prev = req;
    }
    req->next = cl->reqs;
    req->prev = NULL;
    cl->reqs = req;

    pthread_mutex_unlock(&cl->reqs_mutex);

    //printf("%s\n", cl->out_writer.sbuf.data);

    // Send data to socket
    int32_t size = cl->out_writer.sbuf.size;
    static char data_size[4];
    data_size[0] = (size >> 24) & 0xFF;
    data_size[1] = (size >> 16) & 0xFF;
    data_size[2] = (size >> 8) & 0xFF;
    data_size[3] = size & 0xFF;

    raid_error_t result = raid_socket_send(&cl->socket, data_size, sizeof(data_size));
    if (!result) {
        result = raid_socket_send(&cl->socket, cl->out_writer.sbuf.data, size);
    }

    // Destroy writer buffer
    msgpack_sbuffer_destroy(&cl->out_writer.sbuf);

    cl->out_writer.etag = NULL;
    return result;
}

const char* raid_request_etag(raid_client_t* cl)
{
    return cl->out_writer.etag;
}

const char* raid_response_etag(raid_client_t* cl)
{
    if (cl->in_reader.header) {
        return cl->in_reader.etag;
    }
    return NULL;
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
                    req->callback(cl, err, req->callback_user_data);

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
