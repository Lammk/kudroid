package android.app;

import android.os.Parcel;
import android.os.Parcelable;

public final class NotificationChannel implements Parcelable {
    private final String mId;
    private CharSequence mName;
    private int mImportance;
    private String mDescription;
    private int mLockscreenVisibility = -1000;
    private boolean mAllowBubbles = true;

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
    public String getDescription() { return mDescription; }
    public void setDescription(String description) { mDescription = description; }
    public int getLockscreenVisibility() { return mLockscreenVisibility; }
    public void setLockscreenVisibility(int visibility) { mLockscreenVisibility = visibility; }
    public boolean canBubble() { return mAllowBubbles; }
    public void setAllowBubbles(boolean allow) { mAllowBubbles = allow; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
