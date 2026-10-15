package android.content;

import android.os.Parcel;
import android.os.Parcelable;
import java.io.Serializable;

public final class ComponentName implements Parcelable, Cloneable, Comparable<ComponentName>, Serializable {
    private static final long serialVersionUID = 1L;
    private final String mPackage;
    private final String mClass;

    public ComponentName(String pkg, String cls) {
        if (pkg == null) throw new NullPointerException("package name is null");
        if (cls == null) throw new NullPointerException("class name is null");
        mPackage = pkg;
        mClass = cls;
    }
    public ComponentName(Context pkg, String cls) {
        this(pkg.getPackageName(), cls);
    }
    public ComponentName(Context pkg, Class<?> cls) {
        this(pkg.getPackageName(), cls.getName());
    }
    public String getPackageName() { return mPackage; }
    public String getClassName() { return mClass; }
    public String getShortClassName() {
        if (mClass.startsWith(mPackage)) {
            int PN = mPackage.length();
            int CN = mClass.length();
            if (CN > PN && mClass.charAt(PN) == '.') {
                return mClass.substring(PN);
            }
        }
        return mClass;
    }
    public String flattenToString() { return mPackage + "/" + mClass; }
    public static ComponentName unflattenFromString(String str) {
        int sep = str.indexOf('/');
        if (sep < 0 || (sep + 1) >= str.length()) return null;
        return new ComponentName(str.substring(0, sep), str.substring(sep + 1));
    }
    public String toString() { return "ComponentInfo{" + mPackage + "/" + mClass + "}"; }
    public int compareTo(ComponentName that) {
        int v = this.mPackage.compareTo(that.mPackage);
        if (v != 0) return v;
        return this.mClass.compareTo(that.mClass);
    }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel out, int flags) {
        out.writeString(mPackage);
        out.writeString(mClass);
    }
}
