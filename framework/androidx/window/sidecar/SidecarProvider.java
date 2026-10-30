package androidx.window.sidecar;

import android.content.Context;

public final class SidecarProvider {
    private SidecarProvider() {}

    public static String getApiVersion() {
        return "";
    }

    public static SidecarInterface getSidecarImpl(Context context) {
        return null;
    }
}
