#pragma once

#ifdef __cplusplus
extern "C" {
#endif
void kudroid_set_thermal_status(int status);
int kudroid_get_thermal_status(void);
void kudroid_thermal_reset(void);

typedef void (*kudroid_thermal_callback)(void* data, int status);
void* bionic_AThermal_acquireManager(void);
void bionic_AThermal_releaseManager(void* manager);
int bionic_AThermal_getCurrentThermalStatus(void* manager);
float bionic_AThermal_getThermalHeadroom(void* manager, int forecastSeconds);
int bionic_AThermal_registerThermalStatusListener(void* manager, kudroid_thermal_callback callback, void* data);
int bionic_AThermal_unregisterThermalStatusListener(void* manager, kudroid_thermal_callback callback, void* data);
#ifdef __cplusplus
}
#endif
