#include "raid.h"
#include "raid_internal.h"


void raid_read_init(raid_client_t* cl, msgpack_object* header, msgpack_object* body)
{
    cl->in_reader.header = header;
    cl->in_reader.body = body;
    cl->in_reader.nested = body;
    cl->in_reader.parent = NULL;
    cl->in_reader.nested_top = 0;
}

bool raid_is_code(raid_client_t* cl, const char* code)
{
    if (!cl->in_reader.header) return false;

    for (int i = 0; i < cl->in_reader.header->via.map.size; i++) {
        if (!strncmp("code", cl->in_reader.header->via.map.ptr[i].key.via.str.ptr, 4)) {
            return !strncmp(code, cl->in_reader.header->via.map.ptr[i].val.via.str.ptr, strlen(code));
        }
    }
    return false;
}

bool raid_read_code(raid_client_t* cl, char** res, size_t* len)
{
    if (!cl->in_reader.header) return false;

    for (int i = 0; i < cl->in_reader.header->via.map.size; i++) {
        const char* ptr = cl->in_reader.header->via.map.ptr[i].val.via.str.ptr;
        size_t size = cl->in_reader.header->via.map.ptr[i].val.via.str.size;
        if (!strncmp("code", cl->in_reader.header->via.map.ptr[i].key.via.str.ptr, 4)) {
            *res = malloc(size);
            *len = size;
            memcpy(*res, ptr, size);
            return true;
        }
    }
    return false;
}

bool raid_read_int(raid_client_t* cl, int64_t* res)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.nested->type != MSGPACK_OBJECT_POSITIVE_INTEGER && cl->in_reader.nested->type != MSGPACK_OBJECT_NEGATIVE_INTEGER)
        return false;

    *res = cl->in_reader.nested->via.i64;
    return true;
}

bool raid_read_float(raid_client_t* cl, double* res)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.nested->type != MSGPACK_OBJECT_FLOAT)
        return false;

    *res = cl->in_reader.nested->via.f64;
    return true;
}

bool raid_read_string(raid_client_t* cl, char** res, size_t* len)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.nested->type != MSGPACK_OBJECT_STR && cl->in_reader.nested->type != MSGPACK_OBJECT_BIN)
        return false;

    const char* ptr = cl->in_reader.nested->via.str.ptr;
    *len = cl->in_reader.nested->via.str.size;
    *res = malloc(*len);
    memcpy(*res, ptr, *len);
    return true;
}

bool raid_read_map_key(raid_client_t* cl, char** key, size_t* len)
{
    if (!cl->in_reader.header) return false;

    if (!cl->in_reader.parent || cl->in_reader.parent->type != MSGPACK_OBJECT_MAP)
        return false;

    msgpack_object* obj = &cl->in_reader.parent->via.map.ptr[cl->in_reader.indices[cl->in_reader.nested_top]].key;
    const char* ptr = obj->via.str.ptr;
    *len = obj->via.str.size;
    *key = malloc(*len);
    memcpy(*key, ptr, *len);
    return true;
}

bool raid_read_begin_array(raid_client_t* cl, size_t* len)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.nested->type != MSGPACK_OBJECT_ARRAY)
        return false;

    *len = cl->in_reader.nested->via.array.size;

    cl->in_reader.indices[cl->in_reader.nested_top] = 0;
    cl->in_reader.parents[cl->in_reader.nested_top] = cl->in_reader.parent = cl->in_reader.nested;
    cl->in_reader.nested = cl->in_reader.nested->via.array.ptr;
    cl->in_reader.nested_top++;
    return true;
}

void raid_read_end_array(raid_client_t* cl)
{
    if (!cl->in_reader.header) return;

    cl->in_reader.nested_top--;
    cl->in_reader.nested = cl->in_reader.parents[cl->in_reader.nested_top];
}

bool raid_read_begin_map(raid_client_t* cl, size_t* len)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.nested->type != MSGPACK_OBJECT_MAP)
        return false;

    *len = cl->in_reader.nested->via.map.size;

    cl->in_reader.indices[cl->in_reader.nested_top] = 0;
    cl->in_reader.parents[cl->in_reader.nested_top] = cl->in_reader.parent = cl->in_reader.nested;
    cl->in_reader.nested = &cl->in_reader.nested->via.map.ptr->val;
    cl->in_reader.nested_top++;
    return true;
}

void raid_read_end_map(raid_client_t* cl)
{
    if (!cl->in_reader.header) return;

    cl->in_reader.nested_top--;
    cl->in_reader.nested = cl->in_reader.parents[cl->in_reader.nested_top];
}

bool raid_read_next(raid_client_t* cl)
{
    if (!cl->in_reader.header) return false;

    if (cl->in_reader.parent->type == MSGPACK_OBJECT_ARRAY) {
        int* idx = &cl->in_reader.indices[cl->in_reader.nested_top];
        *idx += 1;
        cl->in_reader.nested = &cl->in_reader.parent->via.array.ptr[*idx];
        return true;
    }
    else if (cl->in_reader.parent->type == MSGPACK_OBJECT_MAP) {
        int* idx = &cl->in_reader.indices[cl->in_reader.nested_top];
        *idx += 1;
        cl->in_reader.nested = &cl->in_reader.parent->via.map.ptr[*idx].val;
        return true;
    }
    return false;
}
