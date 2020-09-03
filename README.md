# raid-c

RAID protocol TCP/IP client.

## What is this?

RAID is a simple message passing protocol used by UTOS.

See more here: https://gopher.commons.host/gopher://umgeher.org/0/utos/man/hcs-api-raid.1

## How to use

```c
#include <raid.h>

int main(int argc, char** argv)
{
  raid_client_t client;
  raid_writer_t writer;
  raid_reader_t reader;
  raid_error_t err;
  
  // Initialize the client resources
  err = raid_init(&client, "HOST", "PORT");
  if (err) { printf("Error initializing the client: %s\b", raid_error_to_string(err)); return 1; }
  
  // Connect to the server
  err = raid_connect(&client);
  if (err) { printf("Error connecting to server: %s\b", raid_error_to_string(err)); return 1; }
  
  // Write a message
  const char* body = "Hello World";
  raid_writer_init(&writer);
  raid_write_message(&writer, "api.action");
  raid_write_string(&writer, body, strlen(body));
  
  // Send the message and wait for the response
  raid_reader_init(&reader);
  err = raid_request(&client, &writer, &reader);
  if (err) { printf("Error sending the message: %s\b", raid_error_to_string(err)); return 1; }
  
  // Read the response (in this case we'll suppose an echo response)
  char* res_body = NULL;
  if (!raid_read_cstring(&reader, &res_body)) {
    printf("Response is not a string\n");
    return 1;
  }
  
  printf("Response: %s\n", res_body);
  
  raid_reader_destroy(&reader);
  raid_writer_destroy(&writer);
  raid_destroy(&client);
  return 0;
}
```

Alternatively, we could send an asynchronous request, providing a callback to be called when the client receives a response:

```c
  // Send the message and provide the callback
  void* user_data = NULL;
  err = raid_request_async(&client, &writer, handle_response, user_data);
  if (err) { printf("Error sending the message: %s\b", raid_error_to_string(err)); return 1; }
```

An example callback:

```c
void handle_response(raid_client_t* cl, raid_reader_t* r, raid_error_t err, void* user_data)
{
  if (err) { printf("Error waiting/receiving the message: %s\b", raid_error_to_string(err)); return; }
  
  // Read the response (in this case we'll suppose an echo response)
  char* res_body = NULL;
  if (!raid_read_cstring(r, &res_body)) {
    printf("Response is not a string\n");
    return 1;
  }
  
  printf("Response: %s\n", res_body);
}
```

## License

ISC

## Authors

- Guilherme Nemeth
