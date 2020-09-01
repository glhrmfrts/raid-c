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

static int current_index(raid_reader_t* r)
{
    if (r->nested_top <= 0) return 0;

    return r->indices[r->nested_top - 1];
}

static msgpack_object* parent(raid_reader_t* r)
{
    if (r->nested_top <= 0) return NULL;

    return r->parents[r->nested_top - 1];
}

static void begin_collection(raid_reader_t* r)
{
    r->indices[r->nested_top] = 0;
    r->parents[r->nested_top] = r->nested;
    r->nested_top++;
}

static void end_collection(raid_reader_t* r)
{
    r->nested_top--;
    r->nested = r->parents[r->nested_top];
}

void raid_reader_init(raid_reader_t* r)
{
    memset(r, 0, sizeof(raid_reader_t));

    r->mempool = malloc(sizeof(msgpack_zone));
    r->obj = malloc(sizeof(msgpack_object));
    msgpack_zone_init(r->mempool, 4096);
}

raid_reader_t* raid_reader_new()
{
    raid_reader_t* r = malloc(sizeof(raid_reader_t));
    raid_reader_init(r);
    return r;
}

void raid_reader_delete(raid_reader_t* r)
{
    if (r == NULL) return;

    raid_reader_destroy(r);
    free(r);
}

void raid_reader_init_with_data(raid_reader_t* r, const char* data, size_t data_len)
{
    raid_reader_init(r);
    raid_reader_set_data(r, data, data_len, false);
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
    if (r->src_data) {
        free(r->src_data);
    }
}

void raid_reader_swap(raid_reader_t* from, raid_reader_t* to)
{
    raid_reader_t tmp = *from;
    *from = *to;
    *to = tmp;
}

void raid_reader_set_data(raid_reader_t* r, const char* data, size_t data_len, bool is_response)
{
    // Copy the data because msgpack likes to hold pointers to our memory!!!!1
    r->src_data = malloc(sizeof(char)*data_len);
    r->src_data_len = data_len;
    memcpy(r->src_data, data, data_len);

    msgpack_zone_clear(r->mempool);
    msgpack_unpack(r->src_data, r->src_data_len, NULL, r->mempool, r->obj);
    if (is_response) {
        r->body = r->nested = find_obj(r->obj, "body");
        r->header = find_obj(r->obj, "header");
        if (!r->header) return;

        r->etag_obj = find_obj(r->header, "etag");
    }
    else {
        r->body = r->nested = r->obj;
    }
}

bool raid_is_nil(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_NIL;
}

bool raid_is_bool(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_BOOL;
}

bool raid_is_int(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_INT;
}

bool raid_is_float(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_FLOAT;
}

bool raid_is_string(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_STRING;
}

bool raid_is_binary(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_BINARY;
}

bool raid_is_array(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_ARRAY;
}

bool raid_is_map(raid_reader_t* r)
{
    return raid_read_type(r) == RAID_MAP;
}

bool raid_is_code(raid_reader_t* r, const char* code)
{
    if (!r->header) return false;

    for (size_t i = 0; i < r->header->via.map.size; i++) {
        if (!strncmp("code", r->header->via.map.ptr[i].key.via.str.ptr, 4)) {
            return !strncmp(code, r->header->via.map.ptr[i].val.via.str.ptr, strlen(code));
        }
    }
    return false;
}

bool raid_read_code(raid_reader_t* r, char** res, size_t* len)
{
    if (!r->header) return false;

    for (size_t i = 0; i < r->header->via.map.size; i++) {
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

bool raid_read_code_cstring(raid_reader_t* r, char** res)
{
    if (!r->header) return false;

    for (size_t i = 0; i < r->header->via.map.size; i++) {
        const char* ptr = r->header->via.map.ptr[i].val.via.str.ptr;
        size_t size = r->header->via.map.ptr[i].val.via.str.size;
        if (!strncmp("code", r->header->via.map.ptr[i].key.via.str.ptr, 4)) {
            *res = malloc(size+1);
            memcpy(*res, ptr, size);
            (*res)[size] = '\0';
            return true;
        }
    }
    return false;
}

bool raid_read_etag_cstring(raid_reader_t* r, char** res)
{
    if (!r->etag_obj) return false;

    const char* ptr = r->etag_obj->via.str.ptr;
    size_t size = r->etag_obj->via.str.size;
    *res = malloc(size+1);
    memcpy(*res, ptr, size);
    (*res)[size] = '\0';
    return true;
}

raid_type_t raid_read_type(raid_reader_t* r)
{
    if (!r->nested) return RAID_INVALID;

    switch (r->nested->type) {
    case MSGPACK_OBJECT_NIL:
        return RAID_NIL;

    case MSGPACK_OBJECT_BOOLEAN:
        return RAID_BOOL;

    case MSGPACK_OBJECT_POSITIVE_INTEGER:
    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        return RAID_INT;

    case MSGPACK_OBJECT_FLOAT:
    case MSGPACK_OBJECT_FLOAT32:
        return RAID_FLOAT;

    case MSGPACK_OBJECT_STR:
    case MSGPACK_OBJECT_BIN:
        return RAID_STRING;

    case MSGPACK_OBJECT_ARRAY:
        return RAID_ARRAY;

    case MSGPACK_OBJECT_MAP:
        return RAID_MAP;

    default:
        return RAID_INVALID;
    }
}

bool raid_read_bool(raid_reader_t* r, bool* res)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_BOOLEAN)
        return false;

    *res = r->nested->via.boolean;
    return true;
}

bool raid_read_int(raid_reader_t* r, int64_t* res)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_POSITIVE_INTEGER && r->nested->type != MSGPACK_OBJECT_NEGATIVE_INTEGER)
        return false;

    *res = r->nested->via.i64;
    return true;
}

bool raid_read_float(raid_reader_t* r, double* res)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_FLOAT && r->nested->type != MSGPACK_OBJECT_FLOAT32)
        return false;

    *res = r->nested->via.f64;
    return true;
}

bool raid_read_binary(raid_reader_t* r, char** res, size_t* len)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = r->nested->via.bin.ptr;
    *len = r->nested->via.bin.size;
    *res = malloc(*len);
    memcpy(*res, ptr, *len);
    return true;
}

bool raid_read_string(raid_reader_t* r, char** res, size_t* len)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_STR && r->nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = r->nested->via.str.ptr;
    *len = r->nested->via.str.size;
    *res = malloc(*len);
    memcpy(*res, ptr, *len);
    return true;
}

bool raid_read_cstring(raid_reader_t* r, char** res)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_STR && r->nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = r->nested->via.str.ptr;
    size_t len = r->nested->via.str.size;
    *res = malloc(len + 1);
    memcpy(*res, ptr, len);
    (*res)[len] = '\0';
    return true;
}

bool raid_copy_cstring(raid_reader_t* r, char* buf, size_t buf_len)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_STR && r->nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = r->nested->via.str.ptr;
    size_t len = r->nested->via.str.size;
    if (len > buf_len - 1)
        return false;

    memcpy(buf, ptr, len);
    buf[len] = '\0';
    return true;
}

bool raid_read_map_key(raid_reader_t* r, char** key, size_t* len)
{
    if (!r->nested) return false;

    if (!parent(r) || parent(r)->type != MSGPACK_OBJECT_MAP)
        return false;

    msgpack_object* obj = &parent(r)->via.map.ptr[current_index(r)].key;
    const char* ptr = obj->via.str.ptr;
    *len = obj->via.str.size;
    *key = malloc(*len);
    memcpy(*key, ptr, *len);
    return true;
}

bool raid_read_map_key_cstring(raid_reader_t* r, char** key)
{
    if (!r->nested) return false;

    if (!parent(r) || parent(r)->type != MSGPACK_OBJECT_MAP)
        return false;

    msgpack_object* obj = &parent(r)->via.map.ptr[current_index(r)].key;
    const char* ptr = obj->via.str.ptr;
    size_t size = obj->via.str.size;

    *key = malloc(size+1);
    memcpy(*key, ptr, size);
    (*key)[size] = '\0';
    return true;
}

bool raid_is_map_key(raid_reader_t* r, const char* key)
{
    if (!r->nested || !parent(r)) return false;

    if (!parent(r) || parent(r)->type != MSGPACK_OBJECT_MAP)
        return false;

    msgpack_object* obj = &parent(r)->via.map.ptr[current_index(r)].key;
    const char* ptr = obj->via.str.ptr;
    return !strncmp(ptr, key, obj->via.str.size);
}

bool raid_read_begin_array(raid_reader_t* r, size_t* len)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_ARRAY)
        return false;

    *len = r->nested->via.array.size;

    begin_collection(r);
    r->nested = r->nested->via.array.ptr;
    
    return true;
}

void raid_read_end_array(raid_reader_t* r)
{
    if (!r->nested) return;

    end_collection(r);
}

bool raid_read_begin_map(raid_reader_t* r, size_t* len)
{
    if (!r->nested) return false;

    if (r->nested->type != MSGPACK_OBJECT_MAP)
        return false;

    *len = r->nested->via.map.size;

    begin_collection(r);
    r->nested = &r->nested->via.map.ptr->val;
    return true;
}

void raid_read_end_map(raid_reader_t* r)
{
    if (!r->nested) return;

    end_collection(r);
}

bool raid_read_next(raid_reader_t* r)
{
    if (!r->nested || !parent(r)) return false;

    if (parent(r)->type == MSGPACK_OBJECT_ARRAY) {
        int* idx = &r->indices[r->nested_top - 1];
        *idx += 1;
        r->nested = &parent(r)->via.array.ptr[*idx];
        return true;
    }
    else if (parent(r)->type == MSGPACK_OBJECT_MAP) {
        int* idx = &r->indices[r->nested_top - 1];
        *idx += 1;
        r->nested = &parent(r)->via.map.ptr[*idx].val;
        return true;
    }
    return false;
}
