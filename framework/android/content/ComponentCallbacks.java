package android.content;

public interface ComponentCallbacks {
    void onConfigurationChanged(android.content.res.Configuration newConfig);

    void onLowMemory();
}
