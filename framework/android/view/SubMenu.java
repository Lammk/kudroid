package android.view;

/**
 * android.view.SubMenu — Menu lồng dưới một MenuItem.
 */
public interface SubMenu extends Menu {
    SubMenu setHeaderTitle(CharSequence title);

    SubMenu setHeaderTitle(int titleRes);

    SubMenu setIcon(android.graphics.drawable.Drawable icon);

    SubMenu setIcon(int iconRes);

    void clearHeader();

    MenuItem getItem();
}
