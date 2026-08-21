package android.content;

import android.os.IBinder;

/**
 * android.content.ServiceConnection — callback bindService.
 */
public interface ServiceConnection {
    void onServiceConnected(ComponentName name, IBinder service);

    void onServiceDisconnected(ComponentName name);
}
