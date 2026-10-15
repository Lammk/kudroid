package android.os;

/**
 * android.os.Parcelable — serialization contract.
 *
 * This interface does NOT have a body in real Android; The declaration is the version
 * full. App implements writeToParcel + CREATOR, the framework only needs to know the signature.
 */
public interface Parcelable {
    /** WriteToParcel flag: object is the return value of a function. */
    public static final int PARCELABLE_WRITE_RETURN_VALUE = 0x0001;

    /** describeContents: contains file descriptor. */
    public static final int CONTENTS_FILE_DESCRIPTOR = 0x0001;

    int describeContents();

    void writeToParcel(Parcel dest, int flags);

    /**
     * Factory recreates objects from Parcel. App declares static field CREATOR.
     */
    public interface Creator<T> {
        T createFromParcel(Parcel source);

        T[] newArray(int size);
    }

    /**
     * Creator needs ClassLoader (used when object contains nested Parcelable).
     */
    public interface ClassLoaderCreator<T> extends Creator<T> {
        T createFromParcel(Parcel source, ClassLoader loader);
    }
}
