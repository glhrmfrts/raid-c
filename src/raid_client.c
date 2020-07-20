#include <varargs.h>
#include <msgpack.h>
#include <ctype.h>
#include "raid_client.h"

static raid_request_t* find_request(raid_client_t* cl, const char* etag)
{
    raid_request_t* req = cl->reqs;
    while (req) {
        if (!strcmp(req->etag, etag)) {
            return req;
        }
        req = req->next;
    }
    return NULL;
}

static void reply_request(raid_client_t* cl, const char* etag)
{
    // Find the request to reply to.
    raid_request_t* req = find_request(cl, etag);
    if (!req) return;

    // Initialize reader state.
    cl->in_reader.header = find_header(cl);
    cl->in_reader.body = find_body(cl);
    cl->in_reader.nested = cl->in_reader.body;
    cl->in_reader.parent = NULL;
    cl->in_reader.nested_top = 0;

    // Fire the request callback.
    req->callback(cl, RAID_SUCCESS, req->callback_user_data);

    // Remove from list.
    if (req->prev) {
        req->prev->next = req->next;
    }
    if (req->next) {
        req->next->prev = req->prev;
    }
    free(req);
}

static void parse_response(raid_client_t* cl)
{
    msgpack_zone_init(&cl->in_reader.mempool, 2048);

    msgpack_object obj;
    msgpack_unpack(cl->msg_buf, cl->msg_len, NULL, &cl->in_reader.mempool, &obj);

    if (obj.type == MSGPACK_OBJECT_MAP) {
        const char* etag = find_etag(obj);
        if (etag) {
            reply_request(cl, etag);
        }
    }

    msgpack_zone_destroy(&cl->in_reader.mempool);
}

static int read_message(raid_client_t* cl)
{
    switch (cl->state) {
    case RAID_STATE_WAIT_MESSAGE:
        if ((cl->in_ptr - cl->in_end) < 4) return -1;

        uint32_t len = (cl->in_ptr[0] << 24) | (cl->in_ptr[1] << 16) | (cl->in_ptr[2] << 8) | (cl->in_ptr[3]);
        cl->state = RAID_STATE_PROCESSING_MESSAGE;
        cl->msg_total_size = len;
        cl->msg_len = 0;
        cl->msg_buf = malloc(cl->msg_total_size*sizeof(char));
        break;

    case RAID_STATE_PROCESSING_MESSAGE:
        size_t copy_len = cl->in_end - cl->in_ptr;
        memcpy(cl->msg_buf + cl->msg_len, cl->in_ptr, copy_len);
        cl->msg_len += copy_len;

        if (cl->msg_len >= cl->msg_total_size) {
            parse_response(cl);
            cl->state = RAID_STATE_WAIT_MESSAGE;
        }
        break;
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
    }
}

raid_error_t raid_connect(raid_client_t* cl, const char* host, const char* port)
{
    cl->reqs = NULL;
    return raid_socket_connect(&cl->socket, host, port);
}

raid_error_t raid_request(raid_client_t* cl, raid_callback_t cb, void* user_data)
{
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

    // Send data to socket
    raid_error_t result = raid_socket_send(&cl->socket, cl->out_writer.sbuf.data, cl->out_writer.sbuf.size);

    // Destroy writer buffer
    msgpack_sbuffer_destroy(&cl->out_writer.sbuf);

    return result;
}

raid_error_t raid_recv_loop(raid_client_t* cl)
{
    char buf[4096];
    int buf_len = 0;

    while (true) {
        if (!raid_socket_connected(cl->socket.handle)) {
            break;
        }

        raid_error_t err = raid_socket_recv(&cl->socket, buf, sizeof(buf), &buf_len);
        if (err) {            
            //cl->callbacks.recv_error(cl, err, cl->callbacks.user_data);
        }
        else {
            process_data(cl, buf, buf_len);
        }
    }
}