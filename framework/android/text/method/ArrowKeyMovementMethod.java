package android.text.method;

import android.text.Spannable;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.TextView;

public class ArrowKeyMovementMethod implements MovementMethod {
    private static ArrowKeyMovementMethod sInstance;

    public static MovementMethod getInstance() {
        if (sInstance == null) {
            sInstance = new ArrowKeyMovementMethod();
        }
        return sInstance;
    }

    public ArrowKeyMovementMethod() {}

    public void initialize(TextView widget, Spannable text) {}
    public boolean onKeyDown(TextView widget, Spannable text, int keyCode, KeyEvent event) { return false; }
    public boolean onKeyUp(TextView widget, Spannable text, int keyCode, KeyEvent event) { return false; }
    public boolean onKeyOther(TextView view, Spannable text, KeyEvent event) { return false; }
    public void onTakeFocus(TextView widget, Spannable text, int direction) {}
    public boolean onTrackballEvent(TextView widget, Spannable text, MotionEvent event) { return false; }
    public boolean onTouchEvent(TextView widget, Spannable text, MotionEvent event) { return false; }
    public boolean onGenericMotionEvent(TextView widget, Spannable text, MotionEvent event) { return false; }
    public boolean canSelectArbitrarily() { return true; }
}
