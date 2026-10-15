package android.text.method;

import android.text.Editable;
import android.view.KeyEvent;
import android.view.View;

public abstract class BaseKeyListener implements KeyListener {
    public int getInputType() { return 0; }
    public boolean onKeyDown(View view, Editable text, int keyCode, KeyEvent event) { return false; }
    public boolean onKeyUp(View view, Editable text, int keyCode, KeyEvent event) { return false; }
    public boolean onKeyOther(View view, Editable text, KeyEvent event) { return false; }
    public void clearMetaKeyState(View view, Editable text, int states) {}
}
