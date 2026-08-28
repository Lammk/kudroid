package android.app;

import android.os.Parcel;
import android.os.Parcelable;

public final class NotificationChannel implements Parcelable {
    private final String mId;
    private CharSequence mName;
    private int mImportance;

    public NotificationChannel(String id, CharSequence name, int importance) {
        mId = id;
        mName = name;
        mImportance = importance;
    }
    public String getId() { return mId; }
    public CharSequence getName() { return mName; }
    public void setName(CharSequence name) { mName = name; }
    public int getImportance() { return mImportance; }
    public void setImportance(int importance) { mImportance = importance; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
