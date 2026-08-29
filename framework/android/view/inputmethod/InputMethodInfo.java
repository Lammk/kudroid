package android.view.inputmethod;

/**
 * Description of one installed input method.
 *
 * KuDroid installs none, so getInputMethodList() returns an empty list and nothing
 * constructs this. It exists so that the list's element type resolves — an app that
 * iterates the (empty) list still references the class in its bytecode.
 */
public final class InputMethodInfo {
    private final String mId;
    private final String mPackageName;
    private final String mServiceName;

    public InputMethodInfo(String id, String packageName, String serviceName) {
        mId = id;
        mPackageName = packageName;
        mServiceName = serviceName;
    }

    public String getId() { return mId; }
    public String getPackageName() { return mPackageName; }
    public String getServiceName() { return mServiceName; }
    public int getSubtypeCount() { return 0; }
    public boolean isDefault(android.content.Context context) { return false; }

    @Override
    public String toString() { return "InputMethodInfo{" + mId + "}"; }
}
