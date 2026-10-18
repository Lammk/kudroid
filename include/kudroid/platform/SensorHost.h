#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KuDroidSensorHost {
    // A positive period enables a stream; zero disables it.
    void (*configure)(int32_t type, int32_t periodUs);
} KuDroidSensorHost;

void kudroid_set_sensor_host(const KuDroidSensorHost* host, uint32_t availableTypes);
void kudroid_inject_sensor_sample(int32_t type, const float* values, int32_t count);
void kudroid_sensor_reset(void);
int32_t kudroid_sensor_available(int32_t type);
void kudroid_sensor_java_configure(int32_t type, int32_t periodUs);
int64_t kudroid_sensor_read(int32_t type, float* values, int32_t count);

#ifdef __cplusplus
}
#endif
