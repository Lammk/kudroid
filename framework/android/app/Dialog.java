package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.SearchEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityEvent;

/**
 * Base class for dialogs.
 *
 * Owns a Window whose callback is the dialog itself, which is what AppCompat and Material
 * dialogs require. KuDroid does not give a dialog its own surface: the window and its
 * decor view exist so content can be built and inspected, but nothing is attached to the
 * screen.
 */
public class Dialog implements DialogInterface, Window.Callback, KeyEvent.Callback {
    private Activity mOwnerActivity;
    private final Context mContext;
    private final Window mWindow;
    private final Handler mListenersHandler;
    private View mDecor;
    private boolean mCreated;
    private boolean mShowing;
    private boolean mCanceled;
    private boolean mCancelable = true;
    private boolean mCanceledOnTouchOutside;
    private boolean mCanceledOnTouchOutsideSet;
    private OnDismissListener mDismissListener;
    private OnCancelListener mCancelListener;
    private OnShowListener mShowListener;
    private OnKeyListener mOnKeyListener;
    private CharSequence mTitle;

    public Dialog(Context context) {
        this(context, 0);
    }

    public Dialog(Context context, int themeResId) {
        this(context, themeResId, true);
    }

    Dialog(Context context, int themeResId, boolean createContextThemeWrapper) {
        if (createContextThemeWrapper) {
            if (themeResId == 0) themeResId = resolveDialogTheme(context);
            mContext = themeResId != 0 ? new android.view.ContextThemeWrapper(context, themeResId) : context;
        } else {
            mContext = context;
        }
        mWindow = new Window(mContext);
        mWindow.setCallback(this);
        final Looper looper = Looper.getMainLooper();
        mListenersHandler = looper != null ? new Handler(looper) : null;
    }

    protected Dialog(Context context, boolean cancelable, OnCancelListener cancelListener) {
        this(context);
        mCancelable = cancelable;
        mCancelListener = cancelListener;
    }

    private static int resolveDialogTheme(Context context) {
        try {
            final android.util.TypedValue outValue = new android.util.TypedValue();
            if (context.getTheme() != null && context.getTheme().resolveAttribute(
                    com.android.internal.R.attr.dialogTheme, outValue, true)) {
                return outValue.resourceId;
            }
        } catch (RuntimeException ignored) {
        }
        return 0;
    }

    /** The context in which this dialog was created, carrying the dialog theme. */
    public final Context getContext() {
        return mContext;
    }

    public ActionBar getActionBar() {
        return null;
    }

    public final void setOwnerActivity(Activity activity) {
        mOwnerActivity = activity;
    }

    public final Activity getOwnerActivity() {
        return mOwnerActivity;
    }

    public boolean isShowing() {
        return mShowing;
    }

    /** Builds the window content without showing it. */
    public void create() {
        if (!mCreated) {
            dispatchOnCreate(null);
        }
    }

    public void show() {
        if (mShowing) {
            if (mDecor != null) mDecor.setVisibility(View.VISIBLE);
            return;
        }
        mCanceled = false;
        if (!mCreated) {
            dispatchOnCreate(null);
        }
        onStart();
        mDecor = mWindow.getDecorView();
        mShowing = true;
        sendShowMessage();
    }

    public void hide() {
        if (mDecor != null) mDecor.setVisibility(View.GONE);
    }

    public void dismiss() {
        if (!mShowing) return;
        mShowing = false;
        try {
            onStop();
        } finally {
            if (mDecor != null) mDecor.setVisibility(View.VISIBLE);
            sendDismissMessage();
        }
    }

    private void sendDismissMessage() {
        final OnDismissListener listener = mDismissListener;
        if (listener == null) return;
        post(new Runnable() {
            @Override public void run() {
                listener.onDismiss(Dialog.this);
            }
        });
    }

    private void sendShowMessage() {
        final OnShowListener listener = mShowListener;
        if (listener == null) return;
        post(new Runnable() {
            @Override public void run() {
                listener.onShow(Dialog.this);
            }
        });
    }

    private void post(Runnable runnable) {
        if (mListenersHandler != null) {
            mListenersHandler.post(runnable);
        } else {
            runnable.run();
        }
    }

    // Called by the dispatchers, never by subclasses.
    void dispatchOnCreate(Bundle savedInstanceState) {
        if (!mCreated) {
            onCreate(savedInstanceState);
            mCreated = true;
        }
    }

    protected void onCreate(Bundle savedInstanceState) {
    }

    protected void onStart() {
    }

    protected void onStop() {
    }

    private static final String DIALOG_SHOWING_TAG = "android:dialogShowing";

    public Bundle onSaveInstanceState() {
        final Bundle bundle = new Bundle();
        bundle.putBoolean(DIALOG_SHOWING_TAG, mShowing);
        return bundle;
    }

    public void onRestoreInstanceState(Bundle savedInstanceState) {
        create();
    }

    public Window getWindow() {
        return mWindow;
    }

    public WindowManager getWindowManager() {
        return mWindow != null ? mWindow.getWindowManager() : null;
    }

    public View getCurrentFocus() {
        return mDecor != null ? mDecor.findFocus() : null;
    }

    public <T extends View> T findViewById(int id) {
        return mWindow.findViewById(id) == null ? null : (T) mWindow.findViewById(id);
    }

    public final <T extends View> T requireViewById(int id) {
        final T view = findViewById(id);
        if (view == null) {
            throw new IllegalArgumentException("ID does not reference a View inside this Dialog");
        }
        return view;
    }

    public void setContentView(int layoutResID) {
        mWindow.setContentView(layoutResID);
    }

    public void setContentView(View view) {
        mWindow.setContentView(view);
    }

    public void setContentView(View view, ViewGroup.LayoutParams params) {
        mWindow.setContentView(view, params);
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
        mWindow.addContentView(view, params);
    }

    public void setTitle(CharSequence title) {
        mTitle = title;
        mWindow.setTitle(title);
    }

    public void setTitle(int titleId) {
        setTitle(mContext.getText(titleId));
    }

    public CharSequence getTitle() {
        return mTitle;
    }

    public boolean requestWindowFeature(int featureId) {
        return getWindow().requestFeature(featureId);
    }

    public LayoutInflater getLayoutInflater() {
        return getWindow().getLayoutInflater();
    }

    public void takeKeyEvents(boolean get) {
    }

    public final void setVolumeControlStream(int streamType) {
    }

    public final int getVolumeControlStream() {
        return Integer.MIN_VALUE;
    }

    // ---- Cancel / dismiss state.

    public void setCancelable(boolean flag) {
        mCancelable = flag;
    }

    public boolean isCancelable() {
        return mCancelable;
    }

    public void setCanceledOnTouchOutside(boolean cancel) {
        if (cancel && !mCancelable) {
            mCancelable = true;
        }
        mCanceledOnTouchOutside = cancel;
        mCanceledOnTouchOutsideSet = true;
    }

    public void cancel() {
        if (!mCanceled) {
            mCanceled = true;
            final OnCancelListener listener = mCancelListener;
            if (listener != null) {
                post(new Runnable() {
                    @Override public void run() {
                        listener.onCancel(Dialog.this);
                    }
                });
            }
        }
        dismiss();
    }

    public void setOnCancelListener(OnCancelListener listener) {
        mCancelListener = listener;
    }

    public void setOnDismissListener(OnDismissListener listener) {
        mDismissListener = listener;
    }

    public void setOnShowListener(OnShowListener listener) {
        mShowListener = listener;
    }

    public void setCancelMessage(android.os.Message msg) {
    }

    public void setDismissMessage(android.os.Message msg) {
    }

    public void setOnKeyListener(OnKeyListener onKeyListener) {
        mOnKeyListener = onKeyListener;
    }

    // ---- Input.

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            event.startTracking();
            return true;
        }
        return false;
    }

    public boolean onKeyLongPress(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && event.isTracking() && !event.isCanceled()) {
            onBackPressed();
            return true;
        }
        return false;
    }

    public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {
        return false;
    }

    public void onBackPressed() {
        if (mCancelable) {
            cancel();
        }
    }

    public boolean onKeyShortcut(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onTouchEvent(MotionEvent event) {
        return false;
    }

    public boolean onTrackballEvent(MotionEvent event) {
        return false;
    }

    public boolean onGenericMotionEvent(MotionEvent event) {
        return false;
    }

    // ---- Window.Callback.

    public void onWindowAttributesChanged(WindowManager.LayoutParams params) {
    }

    public void onContentChanged() {
    }

    public void onWindowFocusChanged(boolean hasFocus) {
    }

    public void onAttachedToWindow() {
    }

    public void onDetachedFromWindow() {
    }

    public boolean dispatchKeyEvent(KeyEvent event) {
        if (mOnKeyListener != null && mOnKeyListener.onKey(this, event.getKeyCode(), event)) {
            return true;
        }
        if (mWindow.superDispatchKeyEvent(event)) {
            return true;
        }
        return event.dispatch(this, mDecor != null ? mDecor.getKeyDispatcherState() : null, this);
    }

    public boolean dispatchKeyShortcutEvent(KeyEvent event) {
        return mWindow.superDispatchKeyEvent(event) || onKeyShortcut(event.getKeyCode(), event);
    }

    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (mWindow.superDispatchTouchEvent(ev)) {
            return true;
        }
        return onTouchEvent(ev);
    }

    public boolean dispatchTrackballEvent(MotionEvent ev) {
        return onTrackballEvent(ev);
    }

    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        return onGenericMotionEvent(ev);
    }

    public boolean dispatchPopulateAccessibilityEvent(AccessibilityEvent event) {
        return false;
    }

    public View onCreatePanelView(int featureId) {
        return null;
    }

    public boolean onCreatePanelMenu(int featureId, Menu menu) {
        return featureId == Window.FEATURE_OPTIONS_PANEL && onCreateOptionsMenu(menu);
    }

    public boolean onPreparePanel(int featureId, View view, Menu menu) {
        return featureId != Window.FEATURE_OPTIONS_PANEL || !menu.hasVisibleItems()
                ? true : onPrepareOptionsMenu(menu);
    }

    public boolean onMenuOpened(int featureId, Menu menu) {
        return true;
    }

    public boolean onMenuItemSelected(int featureId, MenuItem item) {
        return false;
    }

    public void onPanelClosed(int featureId, Menu menu) {
    }

    public boolean onCreateOptionsMenu(Menu menu) {
        return true;
    }

    public boolean onPrepareOptionsMenu(Menu menu) {
        return true;
    }

    public boolean onSearchRequested(SearchEvent searchEvent) {
        return false;
    }

    public boolean onSearchRequested() {
        return false;
    }

    public android.view.ActionMode onWindowStartingActionMode(android.view.ActionMode.Callback callback) {
        return null;
    }

    public android.view.ActionMode onWindowStartingActionMode(android.view.ActionMode.Callback callback, int type) {
        return null;
    }

    public void onActionModeStarted(android.view.ActionMode mode) {
    }

    public void onActionModeFinished(android.view.ActionMode mode) {
    }

    public void onPointerCaptureChanged(boolean hasCapture) {
    }
}
