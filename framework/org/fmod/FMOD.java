package org.fmod;

import android.content.Context;
import android.content.res.AssetManager;
import android.media.AudioDeviceInfo;
import android.media.AudioTrack;

public class FMOD {
    private static Context sContext;
    private static boolean sUseOpenSL = false;

    public static void setUseOpenSL(boolean useOpenSL) {
        sUseOpenSL = useOpenSL;
    }

    public static boolean isUsingOpenSL() {
        return sUseOpenSL;
    }

    public static void init(Context context) {
        sContext = context;
        String prop = System.getProperty("kudroid.fmod.opensl", "false");
        if ("true".equalsIgnoreCase(prop) || "1".equals(prop)) {
            sUseOpenSL = true;
        }
    }

    public static void close() {
        sContext = null;
    }

    public static boolean checkInit() {
        return true;
    }

    public static int getOutputSampleRate() {
        return AudioTrack.getNativeOutputSampleRate(android.media.AudioManager.STREAM_MUSIC);
    }

    public static int getOutputBlockSize() {
        return 1024;
    }

    public static boolean supportsLowLatency() {
        return sUseOpenSL;
    }

    public static boolean supportsAAudio() {
        return false;
    }

    public static boolean lowLatencyFlag() {
        return sUseOpenSL;
    }

    public static boolean proAudioFlag() {
        return false;
    }

    public static boolean isBluetoothOn() {
        return false;
    }

    public static int fileDescriptorFromUri(String uri) {
        return -1;
    }

    public static AssetManager getAssetManager() {
        if (sContext != null) {
            return sContext.getAssets();
        }
        return null;
    }

    public static AudioDeviceInfo[] getAudioDevices(int flags) {
        return new AudioDeviceInfo[0];
    }
}
