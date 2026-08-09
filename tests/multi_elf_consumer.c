#include <pthread.h>
#include <stdlib.h>

extern int kudroid_dependency_value(void);
extern int __android_log_print(int priority, const char* tag,
                               const char* format, ...);

int kudroid_multi_elf_test(void) {
    pthread_mutex_t mutex;
    void* memory = malloc(16);
    if (!memory) return -1;
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        free(memory);
        return -2;
    }
    if (pthread_mutex_lock(&mutex) != 0) {
        pthread_mutex_destroy(&mutex);
        free(memory);
        return -3;
    }

    const int value = kudroid_dependency_value() + 7;
    __android_log_print(4, "KuDroidMultiELF",
                        "provider value=%d, consumer adjustment=7", value - 7);

    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    free(memory);
    return value;
}
