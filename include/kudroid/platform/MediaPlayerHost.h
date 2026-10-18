#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum KuDroidMediaCommand {
    KUDROID_MEDIA_PREPARE = 1, KUDROID_MEDIA_START, KUDROID_MEDIA_PAUSE,
    KUDROID_MEDIA_STOP, KUDROID_MEDIA_RELEASE, KUDROID_MEDIA_POSITION,
    KUDROID_MEDIA_DURATION, KUDROID_MEDIA_WIDTH, KUDROID_MEDIA_HEIGHT,
    KUDROID_MEDIA_SEEK, KUDROID_MEDIA_VOLUME, KUDROID_MEDIA_LOOP,
    KUDROID_MEDIA_SURFACE, KUDROID_MEDIA_PLAYING, KUDROID_MEDIA_STATUS
};

typedef struct KuDroidMediaPlayerHost {
    int64_t (*create)(void);
    int32_t (*source)(int64_t handle, const char* path, int32_t fd,
                      int64_t offset, int64_t length);
    int64_t (*command)(int64_t handle, int32_t command, double a, double b);
    void (*reset_all)(void);
} KuDroidMediaPlayerHost;

void kudroid_set_media_player_host(const KuDroidMediaPlayerHost* host);
// Events: prepared = 1, completion = 2, seek complete = 4, error = 100.
void kudroid_media_player_post(int64_t handle, int32_t event, int32_t extra);
int kudroid_media_player_take(int64_t* handle, int32_t* event, int32_t* extra);
int64_t kudroid_media_player_create(void);
int32_t kudroid_media_player_source(int64_t handle, const char* path, int32_t fd,
                                   int64_t offset, int64_t length);
int64_t kudroid_media_player_command(int64_t handle, int32_t command, double a, double b);
void kudroid_media_player_reset(void);

#ifdef __cplusplus
}
#endif
