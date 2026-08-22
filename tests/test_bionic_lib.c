#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif

extern int __android_log_print(int priority, const char* tag,
                               const char* format, ...);

int kudroid_add(int a, int b) {
    return a + b;
}

int kudroid_bionic_test(void) {
    pthread_mutex_t mutex;
    int result = 0;
    void* memory = malloc(32);

    if (!memory) {
        return -1;
    }

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        free(memory);
        return -2;
    }

    if (pthread_mutex_lock(&mutex) != 0) {
        pthread_mutex_destroy(&mutex);
        free(memory);
        return -3;
    }

    __android_log_print(ANDROID_LOG_INFO, "KuDroidTest",
                        "Bionic shim test: malloc + pthread mutex OK");

    if (pthread_mutex_unlock(&mutex) != 0) {
        result = -4;
    }

    pthread_mutex_destroy(&mutex);
    free(memory);
    return result;
}
