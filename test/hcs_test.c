#include <stdio.h>
#include <unistd.h>
#include <raid.h>

static void operator_announce_cb(raid_client_t* cl, raid_reader_t* r, raid_error_t err, void* user_data)
{
    if (raid_is_code(r, "HCS-O0000")) {
        int64_t* ttl = (int64_t*)user_data;
        if (!raid_read_int(r, ttl)) {
            fprintf(stderr, "Error reading ttl!\n");
        }
    }
    else {
        char* code;
        size_t len;
        raid_read_code(r, &code, &len);

        fprintf(stderr, "Unknown code: %s\n", code);
    }
}

int main(int argc, char** argv)
{
    int64_t ttl = 0;
    raid_client_t cl;
    raid_error_t err = raid_connect(&cl, "hcs.vikingmakt.tech", "31110");
    if (err != RAID_SUCCESS) {
        fprintf(stderr, "Error connecting: %s\n", raid_error_to_string(err));
        return 1;
    }

    raid_writer_t w;
    raid_writer_init(&w, &hcs->raid);

    {
        // Test async request
        raid_write_message(&w, "hcs.operator.announce");
        raid_write_mapf(&w, 2, "'_' %s 'name' %s", "helloworld", "helloworld");

        err = raid_request_async(&cl, &w, operator_announce_cb, (void*)&ttl);
        if (err) {
            fprintf(stderr, "Error sending request: %s\n", raid_error_to_string(err));
            return 1;
        }

        sleep(2);

        if (!ttl) {
            fprintf(stderr, "Test failed!\n");
            return 1;
        }
        printf("Test passed: ttl = %ld\n", ttl);
    }

    {
        // Test sync request
        raid_write_message(&w, "hcs.operator.announce");
        raid_write_mapf(&w, 2, "'_' %s 'name' %s", "helloworld", "helloworld");

        raid_reader_t r;
        raid_reader_init(&r);
        err = raid_request(&cl, &w, &r);
        if (err) {
            fprintf(stderr, "Error sending request: %s\n", raid_error_to_string(err));
            return 1;
        }

        ttl = 0;
        if (raid_is_code(&r, "HCS-O0000")) {
            if (!raid_read_int(&r, &ttl)) {
                fprintf(stderr, "Error reading ttl\n");
                return 1;
            }
        }

        if (ttl) {
            printf("Sync Test passed: ttl = %ld\n", ttl);
        }

        raid_reader_destroy(&r);
    }

    raid_writer_destroy(&w);
    raid_close(&cl);
    return 0;
}
