#include <stdarg.h>
#include <msgpack.h>
#include <ctype.h>
#include <time.h>
#include "raid.h"
#include "raid_internal.h"

#define RAID_KEY_HEADER "header"
#define RAID_KEY_ACTION "action"
#define RAID_KEY_ETAG "etag"
#define RAID_KEY_BODY "body"

static void msgpack_pack_str_with_body(msgpack_packer* pk, const char* str, size_t len)
{
    msgpack_pack_str(pk, len);
    msgpack_pack_str_body(pk, str, len);
}

static const char* gen_etag()
{
    static char buf[16];
    time_t t;
    time(&t);
    sprintf(buf, "%ld", t);
    return buf;
}

void raid_writer_init(raid_writer_t* w)
{
    memset(w, 0, sizeof(raid_writer_t));
    msgpack_sbuffer_init(&w->sbuf);
}

void raid_writer_destroy(raid_writer_t* w)
{
    msgpack_sbuffer_destroy(&w->sbuf);
}

raid_error_t raid_write_message(raid_writer_t* w, const char* action)
{
    msgpack_sbuffer_clear(&w->sbuf);

    /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
    msgpack_packer* pk = &w->pk;
    msgpack_packer_init(pk, &w->sbuf, msgpack_sbuffer_write);
    msgpack_pack_map(pk, 2);

    msgpack_pack_str_with_body(pk, RAID_KEY_HEADER, sizeof(RAID_KEY_HEADER) - 1);
    {
        msgpack_pack_map(pk, 2);

        const char* etag = gen_etag();
        msgpack_pack_str_with_body(pk, RAID_KEY_ACTION, sizeof(RAID_KEY_ACTION) - 1);
        msgpack_pack_str_with_body(pk, action, strlen(action));
        msgpack_pack_str_with_body(pk, RAID_KEY_ETAG, sizeof(RAID_KEY_ETAG) - 1);
        msgpack_pack_str_with_body(pk, etag, strlen(etag));
        w->etag = etag;
    }
    msgpack_pack_str_with_body(pk, RAID_KEY_BODY, sizeof(RAID_KEY_BODY) - 1);
    return RAID_SUCCESS;
}

raid_error_t raid_write_int(raid_writer_t* w, int n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_int32(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_float(raid_writer_t* w, float n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_float(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_string(raid_writer_t* w, const char* str, size_t len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, str, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_array(raid_writer_t* w, size_t len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_array(pk, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_map(raid_writer_t* w, size_t keys_len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_map(pk, keys_len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_int(raid_writer_t* w, const char* key, size_t key_len, int n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, key, key_len);
    msgpack_pack_int(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_float(raid_writer_t* w, const char* key, size_t key_len, float n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, key, key_len);
    msgpack_pack_float(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_string(raid_writer_t* w, const char* key, size_t key_len, const char* str, size_t len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, key, key_len);
    msgpack_pack_str_with_body(pk, str, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_arrayf(raid_writer_t* w, int n, const char* format, ...)
{
    raid_write_array(w, n);

    int result = RAID_SUCCESS;
    const char* fc = format;

    va_list args;
    va_start(args, format);
    for (int i = 0; i < n; i++) {
        while (isspace(*fc)) { fc++; }

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
        case 'd': {
            int int_arg = va_arg(args, int);
            raid_write_int(w, int_arg);
            break;
        }
        case 'f': {
            double float_arg = va_arg(args, double);
            raid_write_float(w, float_arg);
            break;
        }
        case 's': {
            const char* str_arg = va_arg(args, char*);
            raid_write_string(w, str_arg, strlen(str_arg));
            break;
        }
        }
    }
    va_end(args);

    return result;
}

raid_error_t raid_write_mapf(raid_writer_t* w, int n, const char* format, ...)
{
    raid_write_map(w, n);

    int result = RAID_SUCCESS;
    const char* fc = format;

    va_list args;
    va_start(args, format);
    for (int i = 0; i < n; i++) {
        while (isspace(*fc)) { fc++; }

        char delim = *fc++;
        if (delim != '\'' && delim != '"') {
            result = RAID_INVALID_ARGUMENT;
            break;
        }

        const char* key = NULL;
        size_t key_len = 0;
        for (int si = 0; si < 1024; si++) {
            char c = *fc++;
            if (c != delim) {
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

        while (isspace(*fc)) { fc++; }

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
        case 'd': {
            int int_arg = va_arg(args, int);
            raid_write_key_value_int(w, key, key_len, int_arg);
            break;
        }
        case 'f': {
            double float_arg = va_arg(args, double);
            raid_write_key_value_float(w, key, key_len, float_arg);
            break;
        }
        case 's': {
            const char* str_arg = va_arg(args, char*);
            raid_write_key_value_string(w, key, key_len, str_arg, strlen(str_arg));
            break;
        }
        }
    }
    va_end(args);

    return result;
}
