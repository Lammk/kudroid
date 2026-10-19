package android.view;

public interface ContextMenu extends Menu {
    ContextMenu setHeaderTitle(int titleRes);
    ContextMenu setHeaderTitle(CharSequence title);
    ContextMenu setHeaderIcon(int iconRes);
    void clearHeader();
    ContextMenu setHeaderView(View view);

    /** Extra information about the item for which the context menu is shown. */
    interface ContextMenuInfo {
    }
}
