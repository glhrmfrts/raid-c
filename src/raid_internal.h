#ifndef RAID_INTERNAL_H
#define RAID_INTERNAL_H

#include "raid.h"


void raid_reader_set_data(raid_reader_t* r, const char* data, size_t data_len, bool is_response);

void raid_reader_swap(raid_reader_t* from, raid_reader_t* to);


raid_error_t raid_write_key_value_int(raid_writer_t* cl, const char* key, size_t key_len, int n);

raid_error_t raid_write_key_value_float(raid_writer_t* cl, const char* key, size_t key_len, float n);

raid_error_t raid_write_key_value_string(raid_writer_t* cl, const char* key, size_t key_len, const char* str, size_t len);


raid_error_t raid_socket_connect(raid_socket_t* s, const char* host, const char* port);

bool raid_socket_connected(raid_socket_t* s);

raid_error_t raid_socket_send(raid_socket_t* s, const char* data, size_t data_len);

raid_error_t raid_socket_recv(raid_socket_t* s, char* buf, size_t buf_len, int* out_len);

raid_error_t raid_socket_close(raid_socket_t* s);


#endif
