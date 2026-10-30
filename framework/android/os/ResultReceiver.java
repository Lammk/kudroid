package android.os;

/**
 * Callback delivered on a Handler (or inline when none is given). Everything runs in one
 * process, so send() calls onReceiveResult directly instead of going through a binder.
 */
public class ResultReceiver implements Parcelable {
    final boolean mLocal = true;
    final Handler mHandler;

    public ResultReceiver() {
        mHandler = null;
    }

    public ResultReceiver(Handler handler) {
        mHandler = handler;
    }

    public void send(final int resultCode, final Bundle resultData) {
        if (mHandler != null) {
            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    onReceiveResult(resultCode, resultData);
                }
            });
        } else {
            onReceiveResult(resultCode, resultData);
        }
    }

    protected void onReceiveResult(int resultCode, Bundle resultData) {}

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {}

    public static final Parcelable.Creator<ResultReceiver> CREATOR =
            new Parcelable.Creator<ResultReceiver>() {
                @Override
                public ResultReceiver createFromParcel(Parcel in) {
                    return null;
                }

                @Override
                public ResultReceiver[] newArray(int size) {
                    return new ResultReceiver[size];
                }
            };
}
