package android.webkit;

public class JsResult {
    private boolean mHandled = false;
    private boolean mConfirmed = false;

    public final void confirm() {
        mHandled = true;
        mConfirmed = true;
    }

    public final void cancel() {
        mHandled = true;
        mConfirmed = false;
    }

    final boolean getHandled() { return mHandled; }
    final boolean getConfirmed() { return mConfirmed; }
}
