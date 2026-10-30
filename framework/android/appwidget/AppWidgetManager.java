package android.appwidget;

import android.content.ComponentName;
import android.content.Context;
import android.widget.RemoteViews;

/** There is no launcher to host widgets, so no widget ids exist and updates go nowhere. */
public class AppWidgetManager {
    public static final String ACTION_APPWIDGET_UPDATE = "android.appwidget.action.APPWIDGET_UPDATE";
    public static final String ACTION_APPWIDGET_ENABLED = "android.appwidget.action.APPWIDGET_ENABLED";
    public static final String ACTION_APPWIDGET_DELETED = "android.appwidget.action.APPWIDGET_DELETED";
    public static final String ACTION_APPWIDGET_DISABLED = "android.appwidget.action.APPWIDGET_DISABLED";
    public static final String ACTION_APPWIDGET_OPTIONS_CHANGED = "android.appwidget.action.APPWIDGET_UPDATE_OPTIONS";
    public static final String ACTION_APPWIDGET_RESTORED = "android.appwidget.action.APPWIDGET_RESTORED";
    public static final String EXTRA_APPWIDGET_ID = "appWidgetId";
    public static final String EXTRA_APPWIDGET_IDS = "appWidgetIds";
    public static final String EXTRA_APPWIDGET_OPTIONS = "appWidgetOptions";
    public static final String EXTRA_APPWIDGET_OLD_IDS = "appWidgetOldIds";
    public static final String OPTION_APPWIDGET_MIN_WIDTH = "appWidgetMinWidth";
    public static final String OPTION_APPWIDGET_MIN_HEIGHT = "appWidgetMinHeight";
    public static final String OPTION_APPWIDGET_MAX_WIDTH = "appWidgetMaxWidth";
    public static final String OPTION_APPWIDGET_MAX_HEIGHT = "appWidgetMaxHeight";
    public static final int INVALID_APPWIDGET_ID = 0;

    private static AppWidgetManager sInstance;

    private AppWidgetManager() {}

    public static synchronized AppWidgetManager getInstance(Context context) {
        if (sInstance == null) sInstance = new AppWidgetManager();
        return sInstance;
    }

    public int[] getAppWidgetIds(ComponentName provider) { return new int[0]; }
    public void updateAppWidget(int[] appWidgetIds, RemoteViews views) {}
    public void updateAppWidget(int appWidgetId, RemoteViews views) {}
    public void updateAppWidget(ComponentName provider, RemoteViews views) {}
    public void partiallyUpdateAppWidget(int[] appWidgetIds, RemoteViews views) {}
    public void partiallyUpdateAppWidget(int appWidgetId, RemoteViews views) {}
    public void notifyAppWidgetViewDataChanged(int[] appWidgetIds, int viewId) {}
    public void notifyAppWidgetViewDataChanged(int appWidgetId, int viewId) {}
    public android.os.Bundle getAppWidgetOptions(int appWidgetId) { return new android.os.Bundle(); }
    public boolean isRequestPinAppWidgetSupported() { return false; }
}
