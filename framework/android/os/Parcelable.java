package android.os;

/**
 * android.os.Parcelable — hợp đồng tuần tự hoá.
 *
 * Interface này KHÔNG có phần thân trong Android thật; khai báo chính là bản
 * đầy đủ. App implement writeToParcel + CREATOR, framework chỉ cần biết chữ ký.
 */
public interface Parcelable {
    /** Cờ writeToParcel: object là giá trị trả về của một hàm. */
    public static final int PARCELABLE_WRITE_RETURN_VALUE = 0x0001;

    /** describeContents: chứa file descriptor. */
    public static final int CONTENTS_FILE_DESCRIPTOR = 0x0001;

    int describeContents();

    void writeToParcel(Parcel dest, int flags);

    /**
     * Factory tái tạo object từ Parcel. App khai báo static field CREATOR.
     */
    public interface Creator<T> {
        T createFromParcel(Parcel source);

        T[] newArray(int size);
    }

    /**
     * Creator cần ClassLoader (dùng khi object chứa Parcelable lồng nhau).
     */
    public interface ClassLoaderCreator<T> extends Creator<T> {
        T createFromParcel(Parcel source, ClassLoader loader);
    }
}
