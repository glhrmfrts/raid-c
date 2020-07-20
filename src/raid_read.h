#ifndef RAID_READ_H
#define RAID_READ_H

#include <msgpack.h>
#include "raid_error.h"

struct raid_client_t;

typedef struct raid_reader {
    msgpack_zone mempool;
    msgpack_object obj;
    msgpack_object* header;
    msgpack_object* body;
    msgpack_object* nested;
    msgpack_object* parent;
    msgpack_object* parents[10];
    int indices[10];
    int nested_top;
} raid_reader_t;


bool raid_is_code(raid_client_t* cl, const char* code);

bool raid_read_code(raid_client_t* cl, char** res);

bool raid_read_int(raid_client_t* cl, int* res);

bool raid_read_float(raid_client_t* cl, float* res);

bool raid_read_string(raid_client_t* cl, char** res, size_t* len);

bool raid_read_map_key(raid_client_t* cl, char** key, size_t* len);


bool raid_read_begin_array(raid_client_t* cl, size_t* len);

void raid_read_end_array(raid_client_t* cl);


bool raid_read_begin_map(raid_client_t* cl, size_t* len);

void raid_read_end_map(raid_client_t* cl);


bool raid_read_next(raid_client_t* cl);


#endif