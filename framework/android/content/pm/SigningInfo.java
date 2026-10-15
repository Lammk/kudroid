package android.content.pm;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * Information pertaining to the signing certificates used to sign a package.
 */
public final class SigningInfo implements Parcelable {
    private Signature[] mSignatures;

    public SigningInfo() {
        mSignatures = new Signature[0];
    }

    public SigningInfo(Signature[] signatures) {
        mSignatures = signatures != null ? signatures : new Signature[0];
    }

    public boolean hasMultipleSigners() {
        return mSignatures.length > 1;
    }

    public boolean hasPastSigningCertificates() {
        return false;
    }

    public Signature[] getSigningCertificateHistory() {
        return mSignatures;
    }

    public Signature[] getApkContentsSigners() {
        return mSignatures;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {}
}
