package android.text.method;

import android.graphics.Rect;
import android.view.View;

public class PasswordTransformationMethod extends SingleLineTransformationMethod {
    private static PasswordTransformationMethod sInstance;

    public static PasswordTransformationMethod getInstance() {
        if (sInstance != null) return sInstance;
        sInstance = new PasswordTransformationMethod();
        return sInstance;
    }

    public PasswordTransformationMethod() {}

    @Override
    public CharSequence getTransformation(CharSequence source, View view) {
        return source;
    }

    @Override
    public void onFocusChanged(View view, CharSequence sourceText, boolean focused, int direction, Rect previouslyFocusedRect) {}
}
