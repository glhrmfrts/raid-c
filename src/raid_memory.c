#include <stdlib.h>
#include <stdio.h>
#include "raid.h"

void* raid_alloc(size_t size, const char* name)
{
    void* data = malloc(size);
#ifdef RAID_DEBUG_MEM
    fprintf(stderr, "[raid] %p alloc(%zu): %s\n", data, size, name);
#endif
    return data;
}

void raid_dealloc(void* ptr, const char* name)
{
#ifdef RAID_DEBUG_MEM
    fprintf(stderr, "[raid] %p dealloc: %s\n", ptr, name);
#endif
    if (ptr != NULL) {
        free(ptr);
    }
}