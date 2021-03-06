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

#define RAID_ETAG_SIZE 8

static void msgpack_pack_str_with_body(msgpack_packer* pk, const char* str, size_t len)
{
    msgpack_pack_str(pk, len);
    msgpack_pack_str_body(pk, str, len);
}

static raid_error_t raid_write_message_ex(raid_writer_t* w, const char* action, bool write_body)
{
    msgpack_sbuffer_clear(&w->sbuf);

    /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
    msgpack_packer* pk = &w->pk;
    msgpack_pack_map(pk, write_body ? 2 : 1);

    msgpack_pack_str_with_body(pk, RAID_KEY_HEADER, sizeof(RAID_KEY_HEADER) - 1);
    {
        msgpack_pack_map(pk, 2);

        if (w->etag) {
            free(w->etag);
        }

        pthread_mutex_lock(&w->cl->reqs_mutex);
        w->etag = raid_gen_etag(w->cl);
        pthread_mutex_unlock(&w->cl->reqs_mutex);

        msgpack_pack_str_with_body(pk, RAID_KEY_ACTION, sizeof(RAID_KEY_ACTION) - 1);
        msgpack_pack_str_with_body(pk, action, strlen(action));
        msgpack_pack_str_with_body(pk, RAID_KEY_ETAG, sizeof(RAID_KEY_ETAG) - 1);
        msgpack_pack_str_with_body(pk, w->etag, strlen(w->etag));
    }

    if (write_body) {
        msgpack_pack_str_with_body(pk, RAID_KEY_BODY, sizeof(RAID_KEY_BODY) - 1);
    }

    return RAID_SUCCESS;
}

char* raid_gen_etag(raid_client_t* cl)
{
    static const char ucase[] = "qwertyuiopasdfghjklzxcvbnmMNBVCXZLKJHGFDSAPOIUYTREWQ1234567890";
    srand(time(NULL)+(cl->etag_gen_cnt++)*3);

    char* buf = malloc(sizeof(char)*(RAID_ETAG_SIZE + 1));
    const size_t ucase_count = sizeof(ucase) - 1;
    for (int i = 0; i < RAID_ETAG_SIZE; i++) {
        char random_char;
        int random_index = (double)rand() / RAND_MAX * ucase_count;
        random_char = ucase[random_index];
        buf[i] = random_char;
    }
    buf[RAID_ETAG_SIZE] = '\0';

    return buf;
}

void raid_writer_init(raid_writer_t* w, raid_client_t* cl)
{
    memset(w, 0, sizeof(raid_writer_t));
    w->cl = cl;
    msgpack_sbuffer_init(&w->sbuf);
    msgpack_packer_init(&w->pk, &w->sbuf, msgpack_sbuffer_write);
}

void raid_writer_destroy(raid_writer_t* w)
{
    if (w->etag) {
        free(w->etag);
    }
    msgpack_sbuffer_destroy(&w->sbuf);
}

raid_writer_t* raid_writer_new(raid_client_t* cl)
{
    raid_writer_t* w = malloc(sizeof(raid_writer_t));
    raid_writer_init(w, cl);
    return w;
}

void raid_writer_delete(raid_writer_t* w)
{
    if (w == NULL) return;

    raid_writer_destroy(w);
    free(w);
}

const char* raid_writer_etag(const raid_writer_t* w)
{
    return w->etag;
}

const char* raid_writer_data(const raid_writer_t* w)
{
    return w->sbuf.data;
}

size_t raid_writer_size(const raid_writer_t* w)
{
    return w->sbuf.size;
}

raid_error_t raid_write_message(raid_writer_t* w, const char* action)
{
    return raid_write_message_ex(w, action, true);
}

raid_error_t raid_write_message_without_body(raid_writer_t* w, const char* action)
{
    return raid_write_message_ex(w, action, false);
}

raid_error_t raid_write_raw(raid_writer_t* w, const char* data, size_t data_len)
{
    msgpack_sbuffer_write(&w->sbuf, data, data_len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_nil(raid_writer_t* w)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_nil(pk);
    return RAID_SUCCESS;
}

raid_error_t raid_write_bool(raid_writer_t* w, bool b)
{
    msgpack_packer* pk = &w->pk;
    if (b) {
        msgpack_pack_true(pk);
    }
    else {
        msgpack_pack_false(pk);
    }
    return RAID_SUCCESS;
}

raid_error_t raid_write_int(raid_writer_t* w, int64_t n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_int64(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_float(raid_writer_t* w, double n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_float(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_binary(raid_writer_t* w, const char* data, size_t len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_bin(pk, len);
    msgpack_pack_bin_body(pk, data, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_string(raid_writer_t* w, const char* str, size_t len)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, str, len);
    return RAID_SUCCESS;
}

raid_error_t raid_write_cstring(raid_writer_t* w, const char* str)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, str, strlen(str));
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

raid_error_t raid_write_object(raid_writer_t* w, const msgpack_object* obj)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_object(pk, *obj);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_int(raid_writer_t* w, const char* key, size_t key_len, int64_t n)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, key, key_len);
    msgpack_pack_int64(pk, n);
    return RAID_SUCCESS;
}

raid_error_t raid_write_key_value_float(raid_writer_t* w, const char* key, size_t key_len, double n)
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

raid_error_t raid_write_key_value_object(raid_writer_t* w, const char* key, size_t key_len, const msgpack_object* obj)
{
    msgpack_packer* pk = &w->pk;
    msgpack_pack_str_with_body(pk, key, key_len);
    msgpack_pack_object(pk, *obj);
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
            int64_t int_arg = va_arg(args, int64_t);
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
        case 'o': {
            const msgpack_object* obj_arg = va_arg(args, msgpack_object*);
            raid_write_object(w, obj_arg);
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
            int64_t int_arg = va_arg(args, int64_t);
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
        case 'o': {
            const msgpack_object* obj_arg = va_arg(args, msgpack_object*);
            raid_write_key_value_object(w, key, key_len, obj_arg);
            break;
        }
        }
    }
    va_end(args);

    return result;
}
