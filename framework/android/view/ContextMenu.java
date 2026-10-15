package android.view;

public interface ContextMenu extends Menu {
    ContextMenu setHeaderTitle(int titleRes);
    ContextMenu setHeaderTitle(CharSequence title);
    ContextMenu setHeaderIcon(int iconRes);
    void clearHeader();
}
