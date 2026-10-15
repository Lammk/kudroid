package android.os;

public final class CancellationSignal {
    private boolean mIsCanceled;
    private OnCancelListener mOnCancelListener;

    public interface OnCancelListener {
        void onCancel();
    }

    public CancellationSignal() {}

    public boolean isCanceled() {
        synchronized (this) {
            return mIsCanceled;
        }
    }

    public void throwIfCanceled() {
        if (isCanceled()) {
            throw new OperationCanceledException();
        }
    }

    public void cancel() {
        OnCancelListener listener = null;
        synchronized (this) {
            if (mIsCanceled) return;
            mIsCanceled = true;
            listener = mOnCancelListener;
            mOnCancelListener = null;
        }
        if (listener != null) {
            listener.onCancel();
        }
    }

    public void setOnCancelListener(OnCancelListener listener) {
        synchronized (this) {
            if (mIsCanceled) {
                if (listener != null) listener.onCancel();
                return;
            }
            mOnCancelListener = listener;
        }
    }
}
