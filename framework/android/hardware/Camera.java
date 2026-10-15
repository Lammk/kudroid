package android.hardware;

import android.graphics.ImageFormat;
import android.graphics.SurfaceTexture;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceHolder;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;

/**
 * The original camera API, backed by the host's capture session.
 *
 * One camera can be open at a time. Callbacks run on the main Looper. Preview frames
 * are NV21; the heap has no collector, so without callback buffers one frame array is
 * reused for every frame instead of allocating a new one per frame.
 */
public class Camera {
    public static final int CAMERA_ERROR_UNKNOWN = 1;
    public static final int CAMERA_ERROR_EVICTED = 2;
    public static final int CAMERA_ERROR_SERVER_DIED = 100;

    private static final int EVENT_FRAME = 1;
    private static final int EVENT_PHOTO = 2;
    private static final int EVENT_ERROR = 4;

    private static Camera sOpen;

    private static final int[][] SIZES = {
        { 1920, 1080 }, { 1280, 720 }, { 640, 480 }, { 352, 288 }
    };

    private static native int nativeCount();
    private static native int nativeInfo(int cameraId);
    private static native int nativeOpen(int cameraId);
    private static native void nativeClose();
    private static native int nativeStart(int width, int height);
    private static native void nativeStop();
    private static native int nativeTakePicture();
    private static native void nativeSetPreviewDisplay(boolean visible, int displayRotation);
    private static native int nativeEvents();
    private static native int nativeFrameSize();
    private static native int nativeCopyFrame(byte[] dst);
    private static native int nativePhotoSize();
    private static native int nativeCopyPhoto(byte[] dst);
    private static native int nativeTakeError();

    public interface PreviewCallback { void onPreviewFrame(byte[] data, Camera camera); }
    public interface ShutterCallback { void onShutter(); }
    public interface PictureCallback { void onPictureTaken(byte[] data, Camera camera); }
    public interface AutoFocusCallback { void onAutoFocus(boolean success, Camera camera); }
    public interface ErrorCallback { void onError(int error, Camera camera); }

    public static class CameraInfo {
        public static final int CAMERA_FACING_BACK = 0;
        public static final int CAMERA_FACING_FRONT = 1;
        public int facing;
        public int orientation;
        public boolean canDisableShutterSound;
    }

    public class Size {
        public int width;
        public int height;

        public Size(int w, int h) {
            width = w;
            height = h;
        }

        @Override public boolean equals(Object obj) {
            if (!(obj instanceof Size)) return false;
            Size s = (Size) obj;
            return width == s.width && height == s.height;
        }

        @Override public int hashCode() { return width * 32713 + height; }
    }

    private final int mCameraId;
    private Parameters mParameters;
    private boolean mPreviewing;
    private boolean mReleased;
    private int mDisplayOrientation = 0;
    private boolean mHasDisplay;

    private PreviewCallback mPreviewCallback;
    private boolean mOneShot;
    private boolean mUseBuffers;
    private final ArrayList<byte[]> mBuffers = new ArrayList<byte[]>();
    private byte[] mFrame;

    private ShutterCallback mShutterCallback;
    private PictureCallback mRawCallback;
    private PictureCallback mJpegCallback;
    private boolean mPicturePending;
    private ErrorCallback mErrorCallback;

    private Camera(int cameraId) {
        mCameraId = cameraId;
        mParameters = new Parameters();
    }

    public static int getNumberOfCameras() {
        return nativeCount();
    }

    public static void getCameraInfo(int cameraId, CameraInfo cameraInfo) {
        final int packed = nativeInfo(cameraId);
        if (packed < 0) throw new RuntimeException("Fail to get camera info");
        cameraInfo.facing = packed & 0xFF;
        cameraInfo.orientation = (packed >> 8) & 0x1FF;
        cameraInfo.canDisableShutterSound = false;
    }

    /** Opens the first back-facing camera. */
    public static Camera open() {
        final int count = getNumberOfCameras();
        final CameraInfo info = new CameraInfo();
        for (int i = 0; i < count; i++) {
            getCameraInfo(i, info);
            if (info.facing == CameraInfo.CAMERA_FACING_BACK) return open(i);
        }
        return null;
    }

    public static synchronized Camera open(int cameraId) {
        if (!android.content.pm.RuntimePermissions.isGranted(
                android.app.ActivityThread.getPackageName(),
                android.content.pm.RuntimePermissions.CAMERA)) {
            throw new RuntimeException("Fail to connect to camera service");
        }
        if (nativeInfo(cameraId) < 0) {
            throw new RuntimeException("Fail to connect to camera service");
        }
        if (sOpen != null) sOpen.release();
        if (nativeOpen(cameraId) != 0) {
            throw new RuntimeException("Fail to connect to camera service");
        }
        sOpen = new Camera(cameraId);
        return sOpen;
    }

    public final void release() {
        synchronized (Camera.class) {
            if (mReleased) return;
            mReleased = true;
            mPreviewing = false;
            if (sOpen == this) sOpen = null;
        }
        nativeSetPreviewDisplay(false, 0);
        nativeClose();
    }

    public final void lock() {}
    public final void unlock() {}
    public final void reconnect() throws IOException {}

    public final void setPreviewDisplay(SurfaceHolder holder) throws IOException {
        mHasDisplay = holder != null;
        if (!mReleased) nativeSetPreviewDisplay(mHasDisplay, mDisplayOrientation);
    }

    /** Accepted for apps that only want frames; the texture itself is never filled. */
    public final void setPreviewTexture(SurfaceTexture surfaceTexture) throws IOException {
        mHasDisplay = false;
        if (!mReleased) nativeSetPreviewDisplay(false, mDisplayOrientation);
    }

    public void setDisplayOrientation(int degrees) {
        if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
            throw new RuntimeException("Invalid display orientation: " + degrees);
        }
        mDisplayOrientation = degrees;
        if (!mReleased && mHasDisplay) nativeSetPreviewDisplay(true, degrees);
    }

    public final void startPreview() {
        if (mReleased) throw new RuntimeException("startPreview failed");
        final Size size = mParameters.getPreviewSize();
        if (nativeStart(size.width, size.height) != 0) {
            throw new RuntimeException("startPreview failed");
        }
        mPreviewing = true;
    }

    public final void stopPreview() {
        if (mReleased) return;
        mPreviewing = false;
        nativeStop();
    }

    public final void setPreviewCallback(PreviewCallback cb) {
        mPreviewCallback = cb;
        mOneShot = false;
        mUseBuffers = false;
    }

    public final void setOneShotPreviewCallback(PreviewCallback cb) {
        mPreviewCallback = cb;
        mOneShot = true;
        mUseBuffers = false;
    }

    public final void setPreviewCallbackWithBuffer(PreviewCallback cb) {
        mPreviewCallback = cb;
        mOneShot = false;
        mUseBuffers = true;
    }

    public final void addCallbackBuffer(byte[] callbackBuffer) {
        if (callbackBuffer == null) return;
        synchronized (mBuffers) { mBuffers.add(callbackBuffer); }
    }

    public final void setErrorCallback(ErrorCallback cb) { mErrorCallback = cb; }

    public final void autoFocus(final AutoFocusCallback cb) {
        if (cb == null) return;
        // The capture session focuses continuously, so a focus request is already met.
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override public void run() { cb.onAutoFocus(true, Camera.this); }
        });
    }

    public final void cancelAutoFocus() {}

    public boolean enableShutterSound(boolean enabled) { return enabled; }

    public final void takePicture(ShutterCallback shutter, PictureCallback raw,
                                  PictureCallback jpeg) {
        if (mReleased) throw new RuntimeException("takePicture failed");
        mShutterCallback = shutter;
        mRawCallback = raw;
        mJpegCallback = jpeg;
        if (nativeTakePicture() != 0) throw new RuntimeException("takePicture failed");
        mPicturePending = true;
        // As on Android, frames stop until the app restarts the preview.
        mPreviewing = false;
    }

    public final void takePicture(ShutterCallback shutter, PictureCallback raw,
                                  PictureCallback postview, PictureCallback jpeg) {
        takePicture(shutter, raw, jpeg);
    }

    public Parameters getParameters() {
        final Parameters copy = new Parameters();
        copy.copyFrom(mParameters);
        return copy;
    }

    public void setParameters(Parameters params) {
        if (params == null) throw new IllegalArgumentException("params must not be null");
        mParameters.copyFrom(params);
    }

    /** Runs on the main Looper after the host posted frames, photos or errors. */
    public static void dispatchPending() {
        final int events = nativeEvents();
        final Camera camera = sOpen;
        if (camera == null || events == 0) return;
        if ((events & EVENT_FRAME) != 0) camera.deliverFrame();
        if ((events & EVENT_PHOTO) != 0) camera.deliverPhoto();
        if ((events & EVENT_ERROR) != 0) camera.deliverError();
    }

    private void deliverFrame() {
        final PreviewCallback cb = mPreviewCallback;
        if (!mPreviewing || cb == null) return;
        final int size = nativeFrameSize();
        final int width = size >>> 16;
        final int height = size & 0xFFFF;
        if (width == 0 || height == 0) return;
        final int bytes = width * height * 3 / 2;
        byte[] target = null;
        if (mUseBuffers) {
            synchronized (mBuffers) {
                // Frames are dropped while the app has no buffer to give.
                if (mBuffers.isEmpty()) return;
                target = mBuffers.remove(0);
            }
            if (target.length < bytes) return;
        } else {
            if (mFrame == null || mFrame.length != bytes) mFrame = new byte[bytes];
            target = mFrame;
        }
        if (nativeCopyFrame(target) <= 0) return;
        if (mOneShot) {
            mPreviewCallback = null;
            mOneShot = false;
        }
        cb.onPreviewFrame(target, this);
    }

    private void deliverPhoto() {
        if (!mPicturePending) return;
        mPicturePending = false;
        final int size = nativePhotoSize();
        final byte[] data = new byte[size];
        if (size > 0) nativeCopyPhoto(data);
        final ShutterCallback shutter = mShutterCallback;
        final PictureCallback raw = mRawCallback;
        final PictureCallback jpeg = mJpegCallback;
        mShutterCallback = null;
        mRawCallback = null;
        mJpegCallback = null;
        if (shutter != null) shutter.onShutter();
        if (raw != null) raw.onPictureTaken(null, this);
        if (jpeg != null) jpeg.onPictureTaken(size > 0 ? data : null, this);
    }

    private void deliverError() {
        final int error = nativeTakeError();
        final ErrorCallback cb = mErrorCallback;
        if (cb != null && error != 0) cb.onError(error, this);
    }

    public class Parameters {
        public static final String FOCUS_MODE_AUTO = "auto";
        public static final String FOCUS_MODE_FIXED = "fixed";
        public static final String FOCUS_MODE_CONTINUOUS_PICTURE = "continuous-picture";
        public static final String FOCUS_MODE_CONTINUOUS_VIDEO = "continuous-video";
        public static final String FLASH_MODE_OFF = "off";
        public static final String WHITE_BALANCE_AUTO = "auto";
        public static final String SCENE_MODE_AUTO = "auto";
        public static final String ANTIBANDING_AUTO = "auto";

        private final LinkedHashMap<String, String> mMap = new LinkedHashMap<String, String>();
        private int mPreviewWidth = 640;
        private int mPreviewHeight = 480;
        private int mPictureWidth = 640;
        private int mPictureHeight = 480;
        private int mPreviewFormat = ImageFormat.NV21;
        private int mJpegQuality = 90;
        private int mRotation = 0;
        private String mFocusMode = FOCUS_MODE_CONTINUOUS_PICTURE;

        Parameters() {}

        void copyFrom(Parameters other) {
            mMap.clear();
            mMap.putAll(other.mMap);
            mPreviewWidth = other.mPreviewWidth;
            mPreviewHeight = other.mPreviewHeight;
            mPictureWidth = other.mPictureWidth;
            mPictureHeight = other.mPictureHeight;
            mPreviewFormat = other.mPreviewFormat;
            mJpegQuality = other.mJpegQuality;
            mRotation = other.mRotation;
            mFocusMode = other.mFocusMode;
        }

        private List<Size> sizes() {
            final ArrayList<Size> list = new ArrayList<Size>();
            for (int[] s : SIZES) list.add(new Size(s[0], s[1]));
            return list;
        }

        private boolean supportedSize(int width, int height) {
            for (int[] s : SIZES) if (s[0] == width && s[1] == height) return true;
            return false;
        }

        public void set(String key, String value) { mMap.put(key, value); }
        public String get(String key) { return mMap.get(key); }

        public void setPreviewSize(int width, int height) {
            if (!supportedSize(width, height)) {
                throw new RuntimeException("setParameters failed: unsupported preview size "
                        + width + "x" + height);
            }
            mPreviewWidth = width;
            mPreviewHeight = height;
        }

        public Size getPreviewSize() { return new Size(mPreviewWidth, mPreviewHeight); }
        public List<Size> getSupportedPreviewSizes() { return sizes(); }
        public Size getPreferredPreviewSizeForVideo() { return null; }
        public List<Size> getSupportedVideoSizes() { return null; }

        public void setPreviewFormat(int pixelFormat) {
            if (pixelFormat != ImageFormat.NV21) {
                throw new RuntimeException("setParameters failed: unsupported preview format");
            }
            mPreviewFormat = pixelFormat;
        }

        public int getPreviewFormat() { return mPreviewFormat; }

        public List<Integer> getSupportedPreviewFormats() {
            final ArrayList<Integer> list = new ArrayList<Integer>();
            list.add(Integer.valueOf(ImageFormat.NV21));
            return list;
        }

        public void setPreviewFpsRange(int min, int max) {}
        public void getPreviewFpsRange(int[] range) {
            range[0] = 15000;
            range[1] = 30000;
        }

        public List<int[]> getSupportedPreviewFpsRange() {
            final ArrayList<int[]> list = new ArrayList<int[]>();
            list.add(new int[] { 15000, 30000 });
            return list;
        }

        public void setPreviewFrameRate(int fps) {}
        public int getPreviewFrameRate() { return 30; }

        public List<Integer> getSupportedPreviewFrameRates() {
            final ArrayList<Integer> list = new ArrayList<Integer>();
            list.add(Integer.valueOf(15));
            list.add(Integer.valueOf(30));
            return list;
        }

        /** Photos come out at the session's resolution; the size is recorded only. */
        public void setPictureSize(int width, int height) {
            mPictureWidth = width;
            mPictureHeight = height;
        }

        public Size getPictureSize() { return new Size(mPictureWidth, mPictureHeight); }
        public List<Size> getSupportedPictureSizes() { return sizes(); }

        public void setPictureFormat(int pixelFormat) {
            if (pixelFormat != ImageFormat.JPEG) {
                throw new RuntimeException("setParameters failed: unsupported picture format");
            }
        }

        public int getPictureFormat() { return ImageFormat.JPEG; }

        public List<Integer> getSupportedPictureFormats() {
            final ArrayList<Integer> list = new ArrayList<Integer>();
            list.add(Integer.valueOf(ImageFormat.JPEG));
            return list;
        }

        public void setJpegQuality(int quality) { mJpegQuality = quality; }
        public int getJpegQuality() { return mJpegQuality; }
        public void setRotation(int rotation) { mRotation = rotation; }
        public int getRotation() { return mRotation; }

        public void setFocusMode(String value) { mFocusMode = value; }
        public String getFocusMode() { return mFocusMode; }

        public List<String> getSupportedFocusModes() {
            final ArrayList<String> list = new ArrayList<String>();
            list.add(FOCUS_MODE_AUTO);
            list.add(FOCUS_MODE_CONTINUOUS_PICTURE);
            return list;
        }

        /** Flash control is not bridged, so none is reported. */
        public String getFlashMode() { return null; }
        public void setFlashMode(String value) {}
        public List<String> getSupportedFlashModes() { return null; }

        public boolean isZoomSupported() { return false; }
        public int getMaxZoom() { return 0; }
        public int getZoom() { return 0; }
        public void setZoom(int value) {}

        public int getMaxNumFocusAreas() { return 0; }
        public int getMaxNumMeteringAreas() { return 0; }
    }
}
