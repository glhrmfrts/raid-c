#ifndef RAID_INTERNAL_H
#define RAID_INTERNAL_H

#include "raid.h"


#define LIST_APPEND(head, item) \
    if (head) { \
        head->prev = item; \
    } \
    item->next = head; \
    item->prev = NULL; \
    head = item

#define LIST_REMOVE(head, item) \
    if (item == head) { \
        head = item->next; \
    } \
    if (item->prev) { \
        item->prev->next = item->next; \
    } \
    if (item->next) { \
        item->next->prev = item->prev; \
    }

#define LIST_NODE_MEMBERS(type) \
    type* next; \
    type* prev; \

#define LIST_FOREACH(type, item, list) for (type* item = type##_list_begin(list); item; item = type##_list_next(item))

#define LIST_DEFINE_BEGIN(type) \
static type* type##_list_begin(type* head) \
{ \
    while (head->next) { \
        head = head->next; \
    } \
    return head; \
}

#define LIST_DEFINE_NEXT(type) static type* type##_list_next(type* head) { return head->prev; }


#define ATOMIC_READ(value) (__sync_fetch_and_add((volatile unsigned int*)&value, 0))

#define ATOMIC_ADD(value, add_value) (__sync_fetch_and_add((volatile unsigned int*)&value, add_value))


void raid_reader_set_data(raid_reader_t* r, const char* data, size_t data_len, bool is_response);


raid_error_t raid_write_key_value_int(raid_writer_t* cl, const char* key, size_t key_len, int64_t n);

raid_error_t raid_write_key_value_float(raid_writer_t* cl, const char* key, size_t key_len, double n);

raid_error_t raid_write_key_value_string(raid_writer_t* cl, const char* key, size_t key_len, const char* str, size_t len);


raid_error_t raid_socket_connect(raid_socket_t* s, const char* host, const char* port);

bool raid_socket_connected(raid_socket_t* s);

raid_error_t raid_socket_send(raid_socket_t* s, const char* data, size_t data_len);

raid_error_t raid_socket_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len);

raid_error_t raid_socket_close(raid_socket_t* s);


#endif
