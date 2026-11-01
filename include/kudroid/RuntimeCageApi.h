#pragma once
#include <stdint.h>

typedef struct KudroidRuntimeCageRequirements {
    uint64_t version;
    uint64_t length;
    uint64_t alignment;
} KudroidRuntimeCageRequirements;

typedef struct KudroidRuntimeCageApi {
    uint64_t version;
    uint64_t page_size;
    void* context;
    void* original_init;
    void* (*reserve)(void*, uint64_t, uint64_t, void*);
    int (*release)(void*, void*, uint64_t);
    int (*protect)(void*, void*, uint64_t, int);
    int (*discard)(void*, void*, uint64_t);
} KudroidRuntimeCageApi;
