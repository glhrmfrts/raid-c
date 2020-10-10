#include "test_config.h"
#include <raid.h>

#define TEST_ASSERT(cond, message) \
    if (!(cond)) { fprintf(stderr, "Assertion failed: \"%s\" - %s\n", #cond, message); return 1; }

#define TEST_ERR_CHECK(err) \
    if (err) { fprintf(stderr, "Error: %s!\n", raid_error_to_string(err)); return 1; }

#define TEST_CALL(err, func_call)                                  \
    err = func_call;                                                    \
    if (err) { fprintf(stderr, "Error (%s): %s!\n", #func_call, raid_error_to_string(err)); return 1; }

#define TEST_EXPECT_ERR(err, ex_err) \
    if (!err) { return 1; } \
    else if (err == ex_err) { \
        fprintf(stderr, "Expected error: %s!\n", raid_error_to_string(err)); \
        return 0; \
    } else { \
        fprintf(stderr, "Unexpected error: %s!\n", raid_error_to_string(err)); \
        return 1; \
    }

#define TEST_RUN(hcsptr, func) \
    printf("%s: %s\n\n", #func, func(hcsptr) ? "error" : "passed")

bool test_conn(raid_client_t* raid)
{
  raid_error_t err = raid_connect(raid);
  TEST_ERR_CHECK(err);
  return false;
}

bool test_write_msgpack(raid_client_t* raid)
{
  const char* text = "TEXT";
  
  raid_writer_t w;
  raid_writer_init(&w, raid);

  {
    raid_write_array(&w, 6);
    raid_write_nil(&w);
    raid_write_bool(&w, true);
    raid_write_int(&w, 42);
    raid_write_float(&w, 6.9);
    raid_write_string(&w, text, strlen(text));
    raid_write_mapf(&w, 2, "'number' %d 'string' %s", 1234, "Hello world");
  }

  {
    /* msgpack::sbuffer is a simple buffer implementation. */
    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);

    /* serialize values into the buffer using msgpack_sbuffer_write callback function. */
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&pk, 6);
    msgpack_pack_nil(&pk);
    msgpack_pack_true(&pk);
    msgpack_pack_int64(&pk, 42);
    msgpack_pack_float(&pk, 6.9);
    msgpack_pack_str(&pk, strlen(text)); msgpack_pack_str_body(&pk, text, strlen(text));
    
    msgpack_pack_map(&pk, 2);
    msgpack_pack_str(&pk, strlen("number")); msgpack_pack_str_body(&pk, "number", strlen("number"));
    msgpack_pack_int64(&pk, 1234);
    msgpack_pack_str(&pk, strlen("string")); msgpack_pack_str_body(&pk, "string", strlen("string"));
    msgpack_pack_str(&pk, strlen("Hello world")); msgpack_pack_str_body(&pk, "Hello world", strlen("Hello world"));

    if (memcmp(sbuf.data, w.sbuf.data, sbuf.size)) {
      fprintf(stderr, "Error writing msgpack!\n");

      fprintf(stderr, "msgpack pk:  ");
      for (size_t i = 0; i < sbuf.size; i++) {
	fprintf(stderr, "%x ", sbuf.data[i]);
      }
      fprintf(stderr, "\n");

      fprintf(stderr, "raid writer: ");
      for (size_t i = 0; i < w.sbuf.size; i++) {
	fprintf(stderr, "%x ", w.sbuf.data[i]);
      }
      fprintf(stderr, "\n");
      return true;
    }
  }
  
  raid_writer_destroy(&w);
  return false;
}

bool test_write_read(raid_client_t* raid)
{
  const char* text = "TEXT";
  
  raid_writer_t w;
  raid_writer_init(&w, raid);

  {
    raid_write_array(&w, 6);
    raid_write_nil(&w);
    raid_write_bool(&w, true);
    raid_write_int(&w, 42);
    raid_write_float(&w, 6.9);
    raid_write_string(&w, text, strlen(text));
    raid_write_mapf(&w, 2, "'number' %d 'string' %s", 1234, "Hello world");
  }

  {
    raid_reader_t r;
    raid_reader_init_with_data(&r, w.sbuf.data, w.sbuf.size);

    size_t size = 0;
    TEST_ASSERT(raid_is_array(&r), "Top-level object should be array");
    TEST_ASSERT(raid_read_begin_array(&r, &size), "should be able to read array size");

    TEST_ASSERT(size == 6, "Array size should be 6");
    TEST_ASSERT(raid_is_nil(&r), "Array[0] should be nil");
    raid_read_next(&r);

    bool b = false;
    TEST_ASSERT(raid_is_bool(&r), "Array[1] should be bool");
    TEST_ASSERT(raid_read_bool(&r, &b), "should be able to read bool");
    TEST_ASSERT(b, "Array[1] should be true");
    raid_read_next(&r);
    
    int64_t i = false;
    TEST_ASSERT(raid_is_int(&r), "Array[2] should be int");
    TEST_ASSERT(raid_read_int(&r, &i), "should be able to read int");
    TEST_ASSERT(i == 42, "Array[2] should be 42");
    raid_read_next(&r);
    
    double f = 0.0;
    TEST_ASSERT(raid_is_float(&r), "Array[3] should be float");
    TEST_ASSERT(raid_read_float(&r, &f), "should be able to read float");
    raid_read_next(&r);
    
    char* cstr = NULL;
    TEST_ASSERT(raid_is_string(&r), "Array[4] should be string");
    TEST_ASSERT(raid_read_cstring(&r, &cstr), "should be able to read string");
    TEST_ASSERT(cstr != NULL, "string should not be NULL");
    TEST_ASSERT(!strcmp(cstr, text), "Array[4] should be 'TEXT'");
    raid_read_next(&r);

    size_t map_size = 0;
    TEST_ASSERT(raid_is_map(&r), "Array[5] should be map");
    TEST_ASSERT(raid_read_begin_map(&r, &map_size), "should be able to read map size");
    TEST_ASSERT(map_size == 2, "map size should be 2");
    for (size_t i = 0; i < map_size; i++) {
      const char* key = NULL;
      TEST_ASSERT(raid_read_map_key_cstring(&r, &key), "should be able to read map key");
      if (!strcmp(key, "number")) {
	int64_t val = 0;
	TEST_ASSERT(raid_is_int(&r), "map key 'number' should be int");
	TEST_ASSERT(raid_read_int(&r, &val), "should be able to read map int value");
	TEST_ASSERT(val == 1234, "map int value should be 1234");
      }
      else if (!strcmp(key, "string")) {
	char* str = NULL;
	TEST_ASSERT(raid_is_string(&r), "map key 'string' should be string");
	TEST_ASSERT(raid_read_cstring(&r, &str), "should be able to read map string value");
	TEST_ASSERT(str != NULL, "map string value should not be NULL");
	TEST_ASSERT(!strcmp(str, "Hello world"), "map string value should be 'Hello world'");
      }
      else {
	return true;
      }
	
      raid_read_next(&r);
    }
    raid_read_end_map(&r);
    raid_read_next(&r);
      
    raid_read_end_array(&r);
    
    raid_reader_destroy(&r);
  }
  
  raid_writer_destroy(&w);
  return false;
}

bool test_read_garbage(raid_client_t* raid)
{
  raid_reader_t r;
  char random_data[512];
  raid_reader_init(&r);

  TEST_ASSERT(raid_read_type(&r) == RAID_INVALID, "type should be invalid without data");
  
  for (size_t i = 0; i < sizeof(random_data); i++) {
    random_data[i] = (char)(i++);
  }
  raid_reader_set_data(&r, random_data, sizeof(random_data), false);

  // msgpack thinks this is an int (which is correct according to the spec)
  TEST_ASSERT(raid_read_type(&r) == RAID_INT, "type should be an int");

  for (size_t i = 0; i < sizeof(random_data); i++) {
    random_data[i] = (char)0xff;
  }
  raid_reader_set_data(&r, random_data, sizeof(random_data), false);
  TEST_ASSERT(raid_read_type(&r) == RAID_INT, "type should be an int");
  
  raid_reader_destroy(&r);
  return false;
}

bool test_request_group(raid_client_t* raid)
{
  raid_request_group_t* group = raid_request_group_new(raid);
  for (int i = 0; i < 2; i++) {
    raid_request_group_entry_t* entry = raid_request_group_add(group);
    raid_write_message(&entry->writer, "hcs.exam.get");

    const char* uid = "";
    const char* key = NULL;
    if (i == 0) {
      key = "physician";
    }
    else {
      key = "patient";
    }
    raid_write_mapf(&entry->writer, "'_' %s 'k' %s", uid, key);
  }

  raid_error_t err;
  TEST_CALL(err, raid_request_group_send_and_wait(group));

  raid_reader_t* r = raid_reader_new();
  raid_request_group_read_to_array(group, r, NULL);
  
  raid_reader_delete(r);
  raid_request_group_delete(group);
}

int main(int argc, char** argv)
{
  raid_client_t raid;
  raid_error_t err;

  err = raid_init(&raid, RAID_HOST, RAID_PORT);
  if (err != RAID_SUCCESS) {
    fprintf(stderr, "Error: %s!\n", raid_error_to_string(err));
    return EXIT_FAILURE;
  }

#ifdef RAID_TEST_CONN
  TEST_RUN(&raid, test_conn);
#endif

  TEST_RUN(&raid, test_write_msgpack);
  TEST_RUN(&raid, test_write_read);
  TEST_RUN(&raid, test_read_garbage);

  raid_destroy(&raid);
  return EXIT_SUCCESS;
}
