#ifndef RAID_WRITE_H
#define RAID_WRITE_H

#include <msgpack.h>
#include "raid_error.h"

struct raid_client_t;

typedef struct raid_writer {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    const char* etag;
} raid_writer_t;

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

#endif RAID_WRITE_H