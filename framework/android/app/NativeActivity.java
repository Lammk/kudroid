package android.app;

import android.content.Context;
import android.os.Bundle;
import android.view.InputQueue;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;

public class NativeActivity extends Activity implements SurfaceHolder.Callback2, InputQueue.Callback {
    public static final String META_DATA_LIB_NAME = "android.app.lib_name";
    public static final String META_DATA_FUNC_NAME = "android.app.func_name";

    private SurfaceHolder mCurSurfaceHolder;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onStart() {
        super.onStart();
    }

    @Override
    protected void onStop() {
        super.onStop();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        mCurSurfaceHolder = holder;
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        mCurSurfaceHolder = holder;
    }

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {
        mCurSurfaceHolder = holder;
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        mCurSurfaceHolder = null;
    }

    @Override
    public void onInputQueueCreated(InputQueue queue) {
    }

    @Override
    public void onInputQueueDestroyed(InputQueue queue) {
    }

    public void onGlobalLayout() {
    }
}
