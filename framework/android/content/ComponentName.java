package android.content;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * android.content.ComponentName — (package, class) pair identifies a component.
 */
public class ComponentName implements Parcelable, Cloneable {
    private final String mPackage;
    private final String mClass;

    public ComponentName(String pkg, String cls) {
        if (pkg == null) throw new NullPointerException("package name is null");
        if (cls == null) throw new NullPointerException("class name is null");
        mPackage = pkg;
        mClass = cls;
    }

    public ComponentName(Context pkg, String cls) {
        if (cls == null) throw new NullPointerException("class name is null");
        mPackage = pkg.getPackageName();
        mClass = cls;
    }

    public ComponentName(Context pkg, Class<?> cls) {
        mPackage = pkg.getPackageName();
        mClass = cls.getName();
    }

    public String getPackageName() {
        return mPackage;
    }

    public String getClassName() {
        return mClass;
    }

    /** Shortened class name: remove the package prefix if the class is in that package. */
    public String getShortClassName() {
        if (mClass.startsWith(mPackage)) {
            int pn = mPackage.length();
            if (mClass.length() > pn && mClass.charAt(pn) == '.') {
                return mClass.substring(pn);
            }
        }
        return mClass;
    }

    public String flattenToString() {
        return mPackage + "/" + mClass;
    }

    public String flattenToShortString() {
        return mPackage + "/" + getShortClassName();
    }

    public static ComponentName unflattenFromString(String str) {
        if (str == null) return null;
        int sep = str.indexOf('/');
        if (sep < 0 || sep + 1 >= str.length()) return null;
        String pkg = str.substring(0, sep);
        String cls = str.substring(sep + 1);
        if (cls.length() > 0 && cls.charAt(0) == '.') {
            cls = pkg + cls;
        }
        return new ComponentName(pkg, cls);
    }

    @Override
    public String toString() {
        return "ComponentInfo{" + mPackage + "/" + mClass + "}";
    }

    @Override
    public boolean equals(Object obj) {
        if (!(obj instanceof ComponentName)) return false;
        ComponentName other = (ComponentName) obj;
        return mPackage.equals(other.mPackage) && mClass.equals(other.mClass);
    }

    @Override
    public int hashCode() {
        return mPackage.hashCode() + mClass.hashCode();
    }

    @Override
    public ComponentName clone() {
        return new ComponentName(mPackage, mClass);
    }

    public int describeContents() {
        return 0;
    }

    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(mPackage);
        dest.writeString(mClass);
    }

    public static final Parcelable.Creator<ComponentName> CREATOR
            = new Parcelable.Creator<ComponentName>() {
        public ComponentName createFromParcel(Parcel source) {
            return new ComponentName(source.readString(), source.readString());
        }

        public ComponentName[] newArray(int size) {
            return new ComponentName[size];
        }
    };
}
