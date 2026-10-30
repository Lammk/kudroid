package com.android.internal.view.menu;

import android.content.Context;
import android.view.MenuItem;
import android.view.SubMenu;

public class SubMenuBuilder extends MenuBuilder implements SubMenu {
    private final MenuBuilder mParentMenu;
    private final MenuItemImpl mItem;
    private CharSequence mHeaderTitle;

    public SubMenuBuilder(Context context, MenuBuilder parentMenu, MenuItemImpl item) {
        super(context);
        mParentMenu = parentMenu;
        mItem = item;
    }

    @Override
    public SubMenu setHeaderTitle(int titleRes) {
        if (titleRes != 0) {
            mHeaderTitle = getContext().getString(titleRes);
        }
        return this;
    }

    @Override
    public SubMenu setHeaderTitle(CharSequence title) {
        mHeaderTitle = title;
        return this;
    }

    @Override
    public SubMenu setHeaderIcon(int iconRes) {
        return this;
    }

    @Override
    public void clearHeader() {
        mHeaderTitle = null;
    }

    @Override
    public MenuItem getItem() {
        return mItem;
    }

    public MenuBuilder getParentMenu() {
        return mParentMenu;
    }
}
