package com.android.internal.view.menu;

import android.content.Context;
import android.view.Menu;
import android.view.MenuItem;
import android.view.SubMenu;
import java.util.ArrayList;

public class MenuBuilder implements Menu {
    private final Context mContext;
    private final ArrayList<MenuItemImpl> mItems = new ArrayList<MenuItemImpl>();
    private Callback mCallback;

    public interface Callback {
        boolean onMenuItemSelected(MenuBuilder menu, MenuItem item);
        void onMenuModeChange(MenuBuilder menu);
    }

    public MenuBuilder(Context context) {
        mContext = context;
    }

    public Context getContext() {
        return mContext;
    }

    public void setCallback(Callback callback) {
        mCallback = callback;
    }

    public MenuBuilder getRootMenu() {
        return this;
    }

    public boolean dispatchMenuItemSelected(MenuItem item) {
        if (mCallback != null && mCallback.onMenuItemSelected(this, item)) {
            return true;
        }
        return false;
    }

    @Override
    public MenuItem add(CharSequence title) {
        return add(0, 0, 0, title);
    }

    @Override
    public MenuItem add(int titleRes) {
        return add(0, 0, 0, titleRes != 0 && mContext != null ? mContext.getString(titleRes) : "");
    }

    @Override
    public MenuItem add(int groupId, int itemId, int order, CharSequence title) {
        MenuItemImpl item = new MenuItemImpl(this, groupId, itemId, order, title);
        int insertIndex = mItems.size();
        for (int i = 0; i < mItems.size(); i++) {
            if (mItems.get(i).getOrder() > order) {
                insertIndex = i;
                break;
            }
        }
        mItems.add(insertIndex, item);
        return item;
    }

    @Override
    public MenuItem add(int groupId, int itemId, int order, int titleRes) {
        return add(groupId, itemId, order, titleRes != 0 && mContext != null ? mContext.getString(titleRes) : "");
    }

    @Override
    public SubMenu addSubMenu(CharSequence title) {
        return addSubMenu(0, 0, 0, title);
    }

    @Override
    public SubMenu addSubMenu(int titleRes) {
        return addSubMenu(0, 0, 0, titleRes != 0 && mContext != null ? mContext.getString(titleRes) : "");
    }

    @Override
    public SubMenu addSubMenu(int groupId, int itemId, int order, CharSequence title) {
        MenuItemImpl item = (MenuItemImpl) add(groupId, itemId, order, title);
        SubMenuBuilder subMenu = new SubMenuBuilder(mContext, this, item);
        item.setSubMenu(subMenu);
        return subMenu;
    }

    @Override
    public SubMenu addSubMenu(int groupId, int itemId, int order, int titleRes) {
        return addSubMenu(groupId, itemId, order, titleRes != 0 && mContext != null ? mContext.getString(titleRes) : "");
    }

    @Override
    public void removeItem(int id) {
        for (int i = 0; i < mItems.size(); i++) {
            if (mItems.get(i).getItemId() == id) {
                mItems.remove(i);
                break;
            }
        }
    }

    @Override
    public void removeGroup(int groupId) {
        for (int i = mItems.size() - 1; i >= 0; i--) {
            if (mItems.get(i).getGroupId() == groupId) {
                mItems.remove(i);
            }
        }
    }

    @Override
    public void clear() {
        mItems.clear();
    }

    @Override
    public void setGroupVisible(int group, boolean visible) {
        for (MenuItemImpl item : mItems) {
            if (item.getGroupId() == group) {
                item.setVisible(visible);
            }
        }
    }

    @Override
    public void setGroupEnabled(int group, boolean enabled) {
        for (MenuItemImpl item : mItems) {
            if (item.getGroupId() == group) {
                item.setEnabled(enabled);
            }
        }
    }

    @Override
    public boolean hasVisibleItems() {
        for (MenuItemImpl item : mItems) {
            if (item.isVisible()) return true;
        }
        return false;
    }

    @Override
    public MenuItem findItem(int id) {
        for (MenuItemImpl item : mItems) {
            if (item.getItemId() == id) return item;
            if (item.hasSubMenu()) {
                MenuItem subItem = item.getSubMenu().findItem(id);
                if (subItem != null) return subItem;
            }
        }
        return null;
    }

    @Override
    public int size() {
        return mItems.size();
    }

    @Override
    public MenuItem getItem(int index) {
        if (index >= 0 && index < mItems.size()) {
            return mItems.get(index);
        }
        return null;
    }

    @Override
    public void close() {
    }
}
