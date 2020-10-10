#include "raid.h"
#include "raid_internal.h"

void raid_request_group_init(raid_request_group_t* g, raid_client_t* raid)
{
    memset(g, 0, sizeof(raid_request_group_t));
    g->raid = raid;
}

void raid_request_group_destroy(raid_request_group_t* g)
{
    raid_request_group_entry_t* entry = g->entries;
    while (entry) {
        raid_writer_destroy(&entry->writer);
        raid_reader_destroy(&entry->reader);
        raid_request_group_entry_t* next = entry->next;
        free(entry);
        entry = next;
    }
    g->entries = NULL;
    g->num_entries = 0;
    g->num_entries_done = 0;
}

raid_request_group_t* raid_request_group_new(raid_client_t* raid)
{
    raid_request_group_t* g = malloc(sizeof(raid_request_group_t));
    raid_request_group_init(g, raid);
    return g;
}

void raid_request_group_delete(raid_request_group_t* g)
{
    raid_request_group_destroy(g);
    free(g);
}

raid_request_group_entry_t* raid_request_group_add(raid_request_group_t* g)
{
    raid_request_group_entry_t* entry = malloc(sizeof(raid_request_group_entry_t));
    memset(entry, 0, sizeof(raid_request_group_entry_t));
    entry->group = g;
    raid_writer_init(&entry->writer, g->raid);
    raid_reader_init(&entry->reader);
    LIST_APPEND(g->entries, entry);
    return entry;
}

static void request_group_response_callback(raid_client_t* cl, raid_reader_t* r, raid_error_t err, void* ud)
{
    raid_request_group_entry_t* entry = ud;
    entry->error = err;
    if (r) {
        raid_reader_swap(r, &entry->reader);
    }
    if (entry->response_callback) {
        entry->response_callback(cl, &entry->reader, err, entry->user_data);
    }

    // Notify this request entry is done.
    pthread_mutex_lock(&entry->group->entries_mutex);
    pthread_cond_signal(&entry->group->entries_cond);
    pthread_mutex_unlock(&entry->group->entries_mutex);
}

raid_error_t raid_request_group_send(raid_request_group_t* g)
{
    LIST_FOREACH(raid_request_group_entry_t, entry, g->entries) {
        raid_error_t err = raid_request_async(g->raid, &entry->writer, request_group_response_callback, (void*)entry);
        if (err != RAID_SUCCESS) {
            return err;
        }
    }
    return RAID_SUCCESS;
}

void raid_request_group_wait(raid_request_group_t* g)
{
    while (g->num_entries_done < g->num_entries) {
        pthread_mutex_lock(&g->entries_mutex);
        pthread_cond_wait(&g->entries_cond, &g->entries_mutex);
        g->num_entries_done += 1;
        pthread_mutex_unlock(&g->entries_mutex);
    }
}

raid_error_t raid_request_group_send_and_wait(raid_request_group_t* g)
{
    raid_error_t err = raid_request_group_send(g);
    if (err == RAID_SUCCESS) {
        raid_request_group_wait(g);
    }
    return err;
}

void raid_request_group_read_to_array(raid_request_group_t* g, raid_reader_t* out_reader, raid_error_t** out_errs)
{
    raid_writer_t aw;
    raid_writer_init(&aw, g->raid);
    raid_write_array(&aw, g->num_entries);

    if (out_errs) {
        *out_errs = malloc(sizeof(raid_error_t) * g->num_entries);
    }

    size_t i = 0;
    LIST_FOREACH(raid_request_group_entry_t, entry, g->entries) {
        if (entry->reader.body) {
            raid_write_object(&aw, entry->reader.body);
        }
        else {
            raid_write_nil(&aw);
        }
        if (out_errs) {
            out_errs[i] = entry->error;
        }
        i++;
    }

    raid_reader_set_data(out_reader, raid_writer_data(&aw), raid_writer_size(&aw), false);
    raid_writer_destroy(&aw);
}