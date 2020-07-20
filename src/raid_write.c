#include <varargs.h>
#include <msgpack.h>
#include <ctype.h>
#include <time.h>
#include "raid_client.h"

#define RAID_KEY_HEADER "header"
#define RAID_KEY_ACTION "action"
#define RAID_KEY_ETAG "etag"
#define RAID_KEY_BODY "body"

static const char* gen_etag()
{
    static char buf[16];
    time_t t;
    time(&t);
    sprintf(buf, "%ld", t);
    return buf;
}

raid_error_t raid_write_message(raid_client_t* cl, const char* action)
{
    /* msgpack::sbuffer is a simple buffer implementation. */
    msgpack_sbuffer_init(&cl->out_writer.sbuf);

    /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_packer_init(pk, &cl->out_writer.sbuf, msgpack_sbuffer_write);
    msgpack_pack_map(pk, 2);

    msgpack_pack_map(pk, 2);
    msgpack_pack_str_body(pk, RAID_KEY_HEADER, sizeof(RAID_KEY_HEADER) - 1);
    {
        const char* etag = gen_etag();
        msgpack_pack_str_body(pk, RAID_KEY_ACTION, sizeof(RAID_KEY_ACTION) - 1);
        msgpack_pack_str_body(pk, action, strlen(action));
        msgpack_pack_str_body(pk, RAID_KEY_ETAG, sizeof(RAID_KEY_ETAG) - 1);
        msgpack_pack_str_body(pk, etag, strlen(etag));
        cl->out_writer.etag = etag;
    }
    msgpack_pack_str_body(pk, RAID_KEY_BODY, sizeof(RAID_KEY_BODY) - 1);
    return RAID_SUCCESS;
}

raid_error_t raid_write_int(raid_client_t* cl, int n)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_int32(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_float(raid_client_t* cl, float n)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_float(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_string(raid_client_t* cl, const char* str, size_t len)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_str_body(pk, str, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_array(raid_client_t* cl, size_t len)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_array(pk, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_map(raid_client_t* cl, size_t keys_len)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_map(pk, keys_len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_int(raid_client_t* cl, const char* key, size_t key_len, int n)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_str_body(pk, key, key_len);
    msgpack_pack_int(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_float(raid_client_t* cl, const char* key, size_t key_len, float n)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_str_body(pk, key, key_len);
    msgpack_pack_float(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_string(raid_client_t* cl, const char* key, size_t key_len, const char* str, size_t len)
{
    msgpack_packer* pk = &cl->out_writer.pk;
    msgpack_pack_str_body(pk, key, key_len);
    msgpack_pack_str_body(pk, str, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_arrayf(raid_client_t* cl, int n, const char* format, ...)
{
    raid_write_array(cl, n);

    int result = RAID_SUCCESS;
    const char* fc = format;

    va_list args;
    va_start(args, format);
    for (int i = 0; i < n; i++) {
        if ((*fc++) != '%') {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        char c = *fc++;
        if (!c) {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        switch (c) {
        case 'd':
            int int_arg = va_arg(args, int);
            raid_write_int(cl, int_arg);
            break;
        case 'f':
            float float_arg = va_arg(args, float);
            raid_write_float(cl, float_arg);
            break;
        case 's':
            const char* str_arg = va_arg(args, char*);
            raid_write_string(cl, str_arg, strlen(str_arg));
            break;
        }

        if (i < n-1) {
            char c = *fc++;
            if (!(c && c == ' ')) {
                result = RAID_INVALID_ARGUMENT;
                break;
            }
        }
    }
    va_end(args);

    return result;
}

raid_error_t raid_write_mapf(raid_client_t* cl, int n, const char* format, ...)
{
    raid_write_map(cl, n);

    int result = RAID_SUCCESS;
    const char* fc = format;

    va_list args;
    va_start(args, format);
    for (int i = 0; i < n; i++) {
        if ((*fc++) != '"') {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        const char* key = NULL;
        size_t key_len = 0;
        for (int si = 0; si < 1024; si++) {
            char c = *fc++;
            if (isalnum(c)) {
                if (!key) {
                    key = fc - 1;
                    key_len = 1;
                }
                else {
                    key_len++;
                }
            }
            else {
                break;
            }
        }
        if (!key) {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        if ((*fc++) != '%') {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        char c = *fc++;
        if (!c) {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        switch (c) {
        case 'd':
            int int_arg = va_arg(args, int);
            raid_write_key_value_int(cl, key, key_len, int_arg);
            break;
        case 'f':
            float float_arg = va_arg(args, float);
            raid_write_key_value_float(cl, key, key_len, float_arg);
            break;
        case 's':
            const char* str_arg = va_arg(args, char*);
            raid_write_key_value_str(cl, key, key_len, str_arg, strlen(str_arg));
            break;
        }

        if (i < n-1) {
            char c = *fc++;
            if (!(c && c == ' ')) {
                result = RAID_INVALID_ARGUMENT;
                break;
            }
        }
    }
    va_end(args);

    return result;
}