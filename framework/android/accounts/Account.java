package android.accounts;

import android.os.Parcel;
import android.os.Parcelable;

public class Account implements Parcelable {
    public final String name;
    public final String type;

    public Account(String name, String type) {
        this.name = name;
        this.type = type;
    }
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Account)) return false;
        Account other = (Account) o;
        return name.equals(other.name) && type.equals(other.type);
    }
    public int hashCode() { return 31 * name.hashCode() + type.hashCode(); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
