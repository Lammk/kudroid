package androidx.activity.result.contract;

import android.content.Context;
import android.content.Intent;

public abstract class ActivityResultContract<I, O> {
    public abstract Intent createIntent(Context context, I input);
    public abstract O parseResult(int resultCode, Intent intent);
    public static final class SynchronousResult<T> {
        private final T mValue;
        public SynchronousResult(T value) { mValue = value; }
        public T getValue() { return mValue; }
    }
    public SynchronousResult<O> getSynchronousResult(Context context, I input) {
        return null;
    }
}
