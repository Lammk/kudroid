package android.content;

import android.os.Parcel;
import android.os.Parcelable;
import java.util.ArrayList;
import java.util.List;

public class IntentFilter implements Parcelable {
    private final List<String> mActions = new ArrayList<String>();
    private final List<String> mCategories = new ArrayList<String>();

    public IntentFilter() {}
    public IntentFilter(String action) { addAction(action); }
    public void addAction(String action) { if (!mActions.contains(action)) mActions.add(action); }
    public boolean hasAction(String action) { return mActions.contains(action); }
    public int countActions() { return mActions.size(); }
    public String getAction(int index) { return mActions.get(index); }
    public void addCategory(String category) { if (!mCategories.contains(category)) mCategories.add(category); }
    public boolean hasCategory(String category) { return mCategories.contains(category); }
    public int countCategories() { return mCategories.size(); }
    public String getCategory(int index) { return mCategories.get(index); }
    public int describeContents() { return 0; }
    public void writeToParcel(Parcel dest, int flags) {}
}
