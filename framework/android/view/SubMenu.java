package android.view;

public interface SubMenu extends Menu {
    SubMenu setHeaderTitle(int titleRes);
    SubMenu setHeaderTitle(CharSequence title);
    SubMenu setHeaderIcon(int iconRes);
    void clearHeader();
    MenuItem getItem();
}
