package android.os;

import java.io.Serializable;

public final class UserHandle implements Parcelable, Serializable {
    private static final long serialVersionUID = 5431697484392471928L;
    private final int mHandle;

    public UserHandle(int h) { mHandle = h; }
    public int getIdentifier() { return mHandle; }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) { out.writeInt(mHandle); }
}
