package android.view;

/**
 * android.view.Menu — tập hợp MenuItem.
 */
public interface Menu {
    public static final int NONE = 0;
    public static final int FIRST = 1;
    public static final int CATEGORY_CONTAINER = 0x00010000;
    public static final int CATEGORY_SYSTEM = 0x00020000;
    public static final int CATEGORY_SECONDARY = 0x00030000;
    public static final int CATEGORY_ALTERNATIVE = 0x00040000;

    MenuItem add(CharSequence title);

    MenuItem add(int titleRes);

    MenuItem add(int groupId, int itemId, int order, CharSequence title);

    MenuItem add(int groupId, int itemId, int order, int titleRes);

    SubMenu addSubMenu(CharSequence title);

    SubMenu addSubMenu(int groupId, int itemId, int order, CharSequence title);

    void removeItem(int id);

    void removeGroup(int groupId);

    void clear();

    void setGroupVisible(int group, boolean visible);

    void setGroupEnabled(int group, boolean enabled);

    boolean hasVisibleItems();

    MenuItem findItem(int id);

    int size();

    MenuItem getItem(int index);

    void close();
}
