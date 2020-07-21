#include "raid.h"
#include "raid_internal.h"


static msgpack_object* find_obj(msgpack_object* obj, const char* key)
{
    for (size_t i = 0; i < obj->via.map.size; i++) {
        if (!strncmp(key, obj->via.map.ptr[i].key.via.str.ptr, strlen(key))) {
            return &obj->via.map.ptr[i].val;
        }
    }
    return NULL;
}

void raid_reader_init(raid_reader_t* r)
{
    memset(r, 0, sizeof(raid_reader_t));

    r->mempool = malloc(sizeof(msgpack_zone));
    r->obj = malloc(sizeof(msgpack_object));
    msgpack_zone_init(r->mempool, 2048);
}

void raid_reader_destroy(raid_reader_t* r)
{
    if (r->mempool != NULL) {
        msgpack_zone_destroy(r->mempool);
        free(r->mempool);
    }
    if (r->obj != NULL) {
        free(r->obj);
    }
}

void raid_reader_move(raid_reader_t* from, raid_reader_t* to)
{
    memcpy(to, from, sizeof(raid_reader_t));
    from->mempool = NULL;
    from->obj = NULL;
    from->header = NULL;
}

void raid_reader_set_data(raid_reader_t* r, const char* data, size_t data_len)
{
    msgpack_unpack(data, data_len, NULL, r->mempool, r->obj);
    r->body = r->nested = find_obj(r->obj, "body");
    r->header = find_obj(r->obj, "header");
    if (!r->header) return;

    r->etag_obj = find_obj(r->header, "etag");
}

bool raid_is_code(raid_reader_t* r, const char* code)
{
    if (!r->header) return false;

    for (int i = 0; i < r->header->via.map.size; i++) {
        if (!strncmp("code", r->header->via.map.ptr[i].key.via.str.ptr, 4)) {
            return !strncmp(code, r->header->via.map.ptr[i].val.via.str.ptr, strlen(code));
        }
    }
    return false;
}

bool raid_read_code(raid_reader_t* r, char** res, size_t* len)
{
    if (!r->header) return false;

    for (int i = 0; i < r->header->via.map.size; i++) {
        const char* ptr = r->header->via.map.ptr[i].val.via.str.ptr;
        size_t size = r->header->via.map.ptr[i].val.via.str.size;
        if (!strncmp("code", r->header->via.map.ptr[i].key.via.str.ptr, 4)) {
            *res = malloc(size);
            *len = size;
            memcpy(*res, ptr, size);
            return true;
        }
    }
    return false;
}

bool raid_read_int(raid_reader_t* r, int64_t* res)
{
    if (!r->header) return false;

    if (r->nested->type != MSGPACK_OBJECT_POSITIVE_INTEGER && r->nested->type != MSGPACK_OBJECT_NEGATIVE_INTEGER)
        return false;

    *res = r->nested->via.i64;
    return true;
}

bool raid_read_float(raid_reader_t* r, double* res)
{
    if (!r->header) return false;

    if (r->nested->type != MSGPACK_OBJECT_FLOAT)
        return false;

    *res = r->nested->via.f64;
    return true;
}

bool raid_read_string(raid_reader_t* r, char** res, size_t* len)
{
    if (!r->header) return false;

    if (r->nested->type != MSGPACK_OBJECT_STR && r->nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = r->nested->via.str.ptr;
    *len = r->nested->via.str.size;
    *res = malloc(*len);
    memcpy(*res, ptr, *len);
    return true;
}

bool raid_read_map_key(raid_reader_t* r, char** key, size_t* len)
{
    if (!r->header) return false;

    if (!r->parent || r->parent->type != MSGPACK_OBJECT_MAP)
        return false;

    msgpack_object* obj = &r->parent->via.map.ptr[r->indices[r->nested_top]].key;
    const char* ptr = obj->via.str.ptr;
    *len = obj->via.str.size;
    *key = malloc(*len);
    memcpy(*key, ptr, *len);
    return true;
}

bool raid_read_begin_array(raid_reader_t* r, size_t* len)
{
    if (!r->header) return false;

    if (r->nested->type != MSGPACK_OBJECT_ARRAY)
        return false;

    *len = r->nested->via.array.size;

    r->indices[r->nested_top] = 0;
    r->parents[r->nested_top] = r->parent = r->nested;
    r->nested = r->nested->via.array.ptr;
    r->nested_top++;
    return true;
}

void raid_read_end_array(raid_reader_t* r)
{
    if (!r->header) return;

    r->nested_top--;
    r->nested = r->parents[r->nested_top];
}

bool raid_read_begin_map(raid_reader_t* r, size_t* len)
{
    if (!r->header) return false;

    if (r->nested->type != MSGPACK_OBJECT_MAP)
        return false;

    *len = r->nested->via.map.size;

    r->indices[r->nested_top] = 0;
    r->parents[r->nested_top] = r->parent = r->nested;
    r->nested = &r->nested->via.map.ptr->val;
    r->nested_top++;
    return true;
}

void raid_read_end_map(raid_reader_t* r)
{
    if (!r->header) return;

    r->nested_top--;
    r->nested = r->parents[r->nested_top];
}

bool raid_read_next(raid_reader_t* r)
{
    if (!r->header) return false;

    if (r->parent->type == MSGPACK_OBJECT_ARRAY) {
        int* idx = &r->indices[r->nested_top];
        *idx += 1;
        r->nested = &r->parent->via.array.ptr[*idx];
        return true;
    }
    else if (r->parent->type == MSGPACK_OBJECT_MAP) {
        int* idx = &r->indices[r->nested_top];
        *idx += 1;
        r->nested = &r->parent->via.map.ptr[*idx].val;
        return true;
    }
    return false;
}
