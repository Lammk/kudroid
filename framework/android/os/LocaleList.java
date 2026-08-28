package android.os;

import java.util.Locale;
import java.io.Serializable;

public final class LocaleList implements Parcelable, Serializable {
    private static final long serialVersionUID = 1L;
    private static final LocaleList sEmptyList = new LocaleList();
    private final Locale[] mList;

    public LocaleList(Locale... list) {
        mList = (list != null) ? list.clone() : new Locale[0];
    }
    public Locale get(int index) {
        return (index >= 0 && index < mList.length) ? mList[index] : null;
    }
    public boolean isEmpty() { return mList.length == 0; }
    public int size() { return mList.length; }
    public static LocaleList getEmptyLocaleList() { return sEmptyList; }
    public static LocaleList getDefault() { return new LocaleList(Locale.getDefault()); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
