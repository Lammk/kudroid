#pragma once

#include <cstdint>

// ── android.hardware.Camera, for KuART ───────────────────────────────────────
//
// The host (iOS) owns the capture session; this layer carries commands from the guest
// to it and frames and photos back. Frames are stored as NV21, latest only, and are
// announced to the guest's main Looper rather than delivered from the capture thread.
extern "C" {

// Capture session control, implemented by the host. Every function may be called from
// any thread. `open`, `start` and `take_picture` return 0 on success.
typedef struct KuDroidCameraHost {
    int (*count)(void);
    // facing: 0 back, 1 front. orientation: clockwise degrees the raw frame needs to be
    // upright with the device in its natural orientation. Returns 0 on success.
    int (*info)(int cameraId, int* facing, int* orientation);
    int (*open)(int cameraId);
    void (*close)(void);
    int (*start)(int width, int height);
    void (*stop)(void);
    int (*take_picture)(void);
    // Show or hide the live preview over the guest's surface. `displayRotation` is the
    // clockwise rotation the app asked for through setDisplayOrientation.
    void (*set_preview)(int visible, int displayRotation);
} KuDroidCameraHost;

void kudroid_set_camera_host(const KuDroidCameraHost* host);

// Host to guest.

// One video frame as bi-planar 4:2:0 (a Y plane and an interleaved Cb/Cr plane).
// `rotate180` rotates the stored frame, for sensors mounted upside down relative to
// the orientation reported for the camera.
void kudroid_camera_push_frame(const uint8_t* yPlane, int yStride, const uint8_t* cbcrPlane,
                               int cbcrStride, int width, int height, int rotate180);
// An encoded JPEG for the last takePicture. `length` <= 0 reports a failed capture.
void kudroid_camera_push_photo(const uint8_t* jpeg, int length);
// A fatal capture error; 1 is CAMERA_ERROR_UNKNOWN.
void kudroid_camera_report_error(int error);

// True while frames, photos or errors wait for the guest.
int kudroid_camera_has_pending(void);

// Close the session and drop queued data at the end of a guest run.
void kudroid_camera_reset(void);

// Guest to host, behind android.hardware.Camera's native methods.
int32_t bionic_kudroid_camera_count(void);
int32_t bionic_kudroid_camera_info(int32_t cameraId);  // facing | orientation << 8, or -1
int32_t bionic_kudroid_camera_open(int32_t cameraId);
void bionic_kudroid_camera_close(void);
int32_t bionic_kudroid_camera_start(int32_t width, int32_t height);
void bionic_kudroid_camera_stop(void);
int32_t bionic_kudroid_camera_take_picture(void);
void bionic_kudroid_camera_set_preview(int32_t visible, int32_t displayRotation);
int32_t bionic_kudroid_camera_events(void);  // bit 0 frame, 1 photo, 2 error; clears them
int32_t bionic_kudroid_camera_frame_size(void);  // width << 16 | height
int32_t bionic_kudroid_camera_copy_frame(uint8_t* dst, int32_t capacity);
int32_t bionic_kudroid_camera_photo_size(void);
int32_t bionic_kudroid_camera_copy_photo(uint8_t* dst, int32_t capacity);
int32_t bionic_kudroid_camera_take_error(void);

}  // extern "C"
