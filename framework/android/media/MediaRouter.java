package android.media;

import android.content.Context;
import android.view.Display;
import java.util.ArrayList;
import java.util.List;

/**
 * android.media.MediaRouter.
 *
 * KuDroid presents audio through one CoreAudio output with no route selection,
 * so this is a single built-in route that is always selected. Every method is a
 * no-op or returns that route — the point is to keep Unity's
 * {@code bitter.jnibridge} reflection ({@code getSystemService("media_router")}
 * followed by calls on the result) from hitting an auto-stubbed class and
 * throwing {@code NoClassDefFoundError} -> {@code NoSuchMethodError} out of
 * {@code Looper.loop}, which killed {@code ActivityThread.main} for ULTRAKILL.
 */
public class MediaRouter {
    public static final int ROUTE_TYPE_LIVE_AUDIO = 0x00000002;
    public static final int ROUTE_TYPE_LIVE_VIDEO = 0x00000001;
    public static final int ROUTE_TYPE_REMOTE_DISPLAY = 0x00000004;
    public static final int ROUTE_TYPE_USER = 0x00800000;

    public static final int AVAILABILITY_FLAG_IGNORE_DEFAULT_ROUTE = 0x00000001;
    public static final int AVAILABILITY_FLAG_REQUIRE_MATCH = 0x00000002;

    public static final int CALLBACK_FLAG_FORCE_DISCOVERY = 0x00000008;
    public static final int CALLBACK_FLAG_PASSIVE_DISCOVERY = 0x00000002;
    public static final int CALLBACK_FLAG_PERFORM_ACTIVE_SCAN_AFTER_PENDING_TIMEOUT = 0x00000001;
    public static final int CALLBACK_FLAG_REQUEST_DISCOVERY = 0x00000004;
    public static final int CALLBACK_FLAG_UNFILTERED_EVENTS = 0x00000001;

    public MediaRouter() {}

    public MediaRouter(Context context) {}

    /** One built-in route, lazily created so getters never return null. */
    private RouteInfo mDefaultRoute;
    private final List<RouteInfo> mRoutes = new ArrayList<RouteInfo>();
    private final List<RouteCategory> mCategories = new ArrayList<RouteCategory>();

    private synchronized RouteInfo defaultRoute() {
        if (mDefaultRoute == null) {
            mDefaultRoute = new RouteInfo(new RouteCategory(
                    "System", ROUTE_TYPE_LIVE_AUDIO | ROUTE_TYPE_LIVE_VIDEO, false));
            mRoutes.add(mDefaultRoute);
        }
        return mDefaultRoute;
    }

    public void addCallback(int types, Callback cb) {}

    public void addCallback(int types, Callback cb, int flags) {}

    public void removeCallback(Callback cb) {}

    public void addVolumeCallback(VolumeCallback cb) {}

    public void removeVolumeCallback(VolumeCallback cb) {}

    public RouteInfo getSelectedRoute(int type) {
        return defaultRoute();
    }

    /** Newer overload some apps call; same single route. */
    public RouteInfo getSelectedRoute() {
        return defaultRoute();
    }

    public RouteInfo getDefaultRoute() {
        return defaultRoute();
    }

    public int getRouteCount() {
        defaultRoute();
        return mRoutes.size();
    }

    public RouteInfo getRouteAt(int index) {
        defaultRoute();
        if (index < 0 || index >= mRoutes.size()) return null;
        return mRoutes.get(index);
    }

    public List<RouteInfo> getRoutes() {
        defaultRoute();
        return new ArrayList<RouteInfo>(mRoutes);
    }

    public RouteCategory getCategory(int types) {
        return new RouteCategory("", types, false);
    }

    public int getCategoryCount() {
        return 1;
    }

    public RouteCategory getCategoryAt(int index) {
        return new RouteCategory("", ROUTE_TYPE_LIVE_AUDIO, false);
    }

    public void selectRoute(int types, RouteInfo route) {}

    public boolean isRouteAvailable(int types, int flags) {
        return true;
    }

    public RouteCategory createRouteCategory(CharSequence name, boolean isGroupable) {
        return new RouteCategory(name != null ? name.toString() : "", 0, isGroupable);
    }

    public RouteCategory createRouteCategory(int nameResId, boolean isGroupable) {
        return new RouteCategory("", 0, isGroupable);
    }

    public UserRouteInfo createUserRoute(RouteCategory category) {
        return new UserRouteInfo(category != null ? category
                : new RouteCategory("", 0, false));
    }

    public void addUserRoute(UserRouteInfo info) {}

    public void removeUserRoute(UserRouteInfo info) {}

    public void clearUserRoutes() {}

    public static class RouteCategory {
        private final CharSequence mName;
        private final int mSupportedTypes;
        private final boolean mGroupable;

        public RouteCategory(CharSequence name, int supportedTypes, boolean groupable) {
            mName = name;
            mSupportedTypes = supportedTypes;
            mGroupable = groupable;
        }

        public CharSequence getName() {
            return mName;
        }

        public CharSequence getName(Context context) {
            return mName;
        }

        public int getSupportedTypes() {
            return mSupportedTypes;
        }

        public boolean isGroupable() {
            return mGroupable;
        }
    }

    public static class RouteInfo {
        public static final int PLAYBACK_TYPE_LOCAL = 0;
        public static final int PLAYBACK_TYPE_REMOTE = 1;
        public static final int PLAYBACK_VOLUME_FIXED = 0;
        public static final int PLAYBACK_VOLUME_VARIABLE = 1;
        public static final int DEVICE_TYPE_UNKNOWN = 0;
        public static final int DEVICE_TYPE_TV = 1;
        public static final int DEVICE_TYPE_SPEAKER = 2;
        public static final int DEVICE_TYPE_BLUETOOTH = 3;

        private final RouteCategory mCategory;
        private CharSequence mName = "System";
        private CharSequence mDescription;
        private Object mTag;

        public RouteInfo(RouteCategory category) {
            mCategory = category;
        }

        public CharSequence getName() {
            return mName;
        }

        public CharSequence getName(Context context) {
            return mName;
        }

        public CharSequence getDescription() {
            return mDescription;
        }

        public RouteCategory getCategory() {
            return mCategory;
        }

        public int getSupportedTypes() {
            return mCategory != null ? mCategory.getSupportedTypes() : 0;
        }

        public int getPlaybackType() {
            return PLAYBACK_TYPE_LOCAL;
        }

        public int getPlaybackStream() {
            return AudioManager.STREAM_MUSIC;
        }

        public int getVolume() {
            return 10;
        }

        public int getVolumeMax() {
            return 15;
        }

        public int getVolumeHandling() {
            return PLAYBACK_VOLUME_VARIABLE;
        }

        public int getDeviceType() {
            return DEVICE_TYPE_SPEAKER;
        }

        public boolean isEnabled() {
            return true;
        }

        public boolean isConnecting() {
            return false;
        }

        public boolean isSelected() {
            return true;
        }

        public boolean matchesTypes(int types) {
            return (getSupportedTypes() & types) != 0;
        }

        public Display getPresentationDisplay() {
            return null;
        }

        public String getId() {
            return "ROUTE_SYSTEM";
        }

        public void select() {}

        public void setTag(Object tag) {
            mTag = tag;
        }

        public Object getTag() {
            return mTag;
        }

        public void requestSetVolume(int volume) {}

        public void requestUpdateVolume(int direction) {}
    }

    public static class UserRouteInfo extends RouteInfo {
        public UserRouteInfo(RouteCategory category) {
            super(category);
        }

        public void setName(CharSequence name) {}

        public void setName(int resId) {}

        public void setDescription(CharSequence description) {}

        public void setSupportedTypes(int types) {}

        public void setPlaybackType(int type) {}

        public void setPlaybackStream(int stream) {}

        public void setVolume(int volume) {}

        public void setVolumeMax(int volumeMax) {}

        public void setVolumeHandling(int volumeHandling) {}

        public void setPresentationDisplay(Display display) {}

        public void setRemoteControlClient(Object rcc) {}
    }

    public static abstract class Callback {
        public void onRouteSelected(MediaRouter router, int type, RouteInfo info) {}

        public void onRouteUnselected(MediaRouter router, int type, RouteInfo info) {}

        public void onRouteAdded(MediaRouter router, RouteInfo info) {}

        public void onRouteRemoved(MediaRouter router, RouteInfo info) {}

        public void onRouteChanged(MediaRouter router, RouteInfo info) {}

        public void onRouteGrouped(MediaRouter router, RouteInfo info,
                RouteInfo group, int index) {}

        public void onRouteUngrouped(MediaRouter router, RouteInfo info, RouteInfo group) {}

        public void onRouteVolumeChanged(MediaRouter router, RouteInfo info) {}

        public void onRoutePresentationDisplayChanged(MediaRouter router, RouteInfo info) {}
    }

    public static abstract class SimpleCallback extends Callback {}

    public static abstract class VolumeCallback {
        public void onVolumeSetRequest(RouteInfo info, int volume) {}

        public void onVolumeUpdateRequest(RouteInfo info, int direction) {}
    }
}
