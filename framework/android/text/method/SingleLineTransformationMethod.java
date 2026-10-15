package android.text.method;

import android.graphics.Rect;
import android.view.View;

public class SingleLineTransformationMethod implements TransformationMethod {
    private static SingleLineTransformationMethod sInstance;

    public static SingleLineTransformationMethod getInstance() {
        if (sInstance != null) return sInstance;
        sInstance = new SingleLineTransformationMethod();
        return sInstance;
    }

    public SingleLineTransformationMethod() {}

    @Override
    public CharSequence getTransformation(CharSequence source, View view) {
        return source != null ? source.toString().replace('\n', ' ') : null;
    }

    @Override
    public void onFocusChanged(View view, CharSequence sourceText, boolean focused, int direction, Rect previouslyFocusedRect) {}
}
