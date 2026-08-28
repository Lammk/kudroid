package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;
import java.io.Serializable;

public class Signature implements Parcelable, Serializable {
    private static final long serialVersionUID = 1L;
    private final byte[] mSignature;

    public Signature(byte[] signature) { mSignature = signature.clone(); }
    public Signature(String text) { mSignature = text.getBytes(); }
    public byte[] toByteArray() { return mSignature.clone(); }
    public String toCharsString() { return new String(mSignature); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
