package android.view;

/**
 * android.view.MenuItem — một mục trong Menu.
 */
public interface MenuItem {
    /** Không hiện trên action bar. */
    public static final int SHOW_AS_ACTION_NEVER = 0;
    public static final int SHOW_AS_ACTION_IF_ROOM = 1;
    public static final int SHOW_AS_ACTION_ALWAYS = 2;
    public static final int SHOW_AS_ACTION_WITH_TEXT = 4;
    public static final int SHOW_AS_ACTION_COLLAPSE_ACTION_VIEW = 8;

    /**
     * Callback khi mục được chọn. Trả true nếu đã xử lý.
     */
    public interface OnMenuItemClickListener {
        boolean onMenuItemClick(MenuItem item);
    }

    /**
     * Callback khi action view mở/đóng.
     */
    public interface OnActionExpandListener {
        boolean onMenuItemActionExpand(MenuItem item);

        boolean onMenuItemActionCollapse(MenuItem item);
    }

    int getItemId();

    int getGroupId();

    int getOrder();

    MenuItem setTitle(CharSequence title);

    MenuItem setTitle(int title);

    CharSequence getTitle();

    MenuItem setVisible(boolean visible);

    boolean isVisible();

    MenuItem setEnabled(boolean enabled);

    boolean isEnabled();

    MenuItem setCheckable(boolean checkable);

    boolean isCheckable();

    MenuItem setChecked(boolean checked);

    boolean isChecked();

    MenuItem setIcon(android.graphics.drawable.Drawable icon);

    MenuItem setIcon(int iconRes);

    android.graphics.drawable.Drawable getIcon();

    MenuItem setShowAsActionFlags(int actionEnum);

    void setShowAsAction(int actionEnum);

    MenuItem setActionView(View view);

    View getActionView();

    MenuItem setOnMenuItemClickListener(OnMenuItemClickListener listener);

    boolean hasSubMenu();

    SubMenu getSubMenu();
}
