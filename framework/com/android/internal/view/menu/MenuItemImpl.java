package com.android.internal.view.menu;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.ActionProvider;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.SubMenu;
import android.view.View;

public class MenuItemImpl implements MenuItem {
    private final Context mContext;
    private final MenuBuilder mMenu;
    private final int mId;
    private final int mGroup;
    private final int mOrder;
    private CharSequence mTitle;
    private CharSequence mTitleCondensed;
    private Drawable mIcon;
    private int mIconRes;
    private boolean mVisible = true;
    private boolean mEnabled = true;
    private boolean mCheckable = false;
    private boolean mChecked = false;
    private int mShowAsAction = SHOW_AS_ACTION_NEVER;
    private View mActionView;
    private ActionProvider mActionProvider;
    private OnMenuItemClickListener mClickListener;
    private OnActionExpandListener mExpandListener;
    private SubMenu mSubMenu;
    private boolean mIsActionViewExpanded = false;

    public MenuItemImpl(MenuBuilder menu, int group, int id, int order, CharSequence title) {
        mMenu = menu;
        mContext = menu != null ? menu.getContext() : null;
        mGroup = group;
        mId = id;
        mOrder = order;
        mTitle = title;
    }

    @Override
    public int getItemId() {
        return mId;
    }

    @Override
    public int getGroupId() {
        return mGroup;
    }

    @Override
    public int getOrder() {
        return mOrder;
    }

    @Override
    public MenuItem setTitle(CharSequence title) {
        mTitle = title;
        return this;
    }

    @Override
    public MenuItem setTitle(int title) {
        if (mContext != null && title != 0) {
            mTitle = mContext.getString(title);
        }
        return this;
    }

    @Override
    public CharSequence getTitle() {
        return mTitle;
    }

    @Override
    public MenuItem setTitleCondensed(CharSequence title) {
        mTitleCondensed = title;
        return this;
    }

    @Override
    public CharSequence getTitleCondensed() {
        return mTitleCondensed != null ? mTitleCondensed : mTitle;
    }

    @Override
    public MenuItem setVisible(boolean visible) {
        mVisible = visible;
        return this;
    }

    @Override
    public boolean isVisible() {
        return mVisible;
    }

    @Override
    public MenuItem setEnabled(boolean enabled) {
        mEnabled = enabled;
        return this;
    }

    @Override
    public boolean isEnabled() {
        return mEnabled;
    }

    @Override
    public MenuItem setCheckable(boolean checkable) {
        mCheckable = checkable;
        return this;
    }

    @Override
    public boolean isCheckable() {
        return mCheckable;
    }

    @Override
    public MenuItem setChecked(boolean checked) {
        mChecked = checked;
        return this;
    }

    @Override
    public boolean isChecked() {
        return mChecked;
    }

    @Override
    public MenuItem setIcon(Drawable icon) {
        mIcon = icon;
        mIconRes = 0;
        return this;
    }

    @Override
    public MenuItem setIcon(int iconRes) {
        mIconRes = iconRes;
        mIcon = null;
        return this;
    }

    @Override
    public Drawable getIcon() {
        if (mIcon != null) return mIcon;
        if (mIconRes != 0 && mContext != null) {
            return mContext.getDrawable(mIconRes);
        }
        return null;
    }

    @Override
    public MenuItem setShowAsActionFlags(int actionEnum) {
        setShowAsAction(actionEnum);
        return this;
    }

    @Override
    public void setShowAsAction(int actionEnum) {
        mShowAsAction = actionEnum;
    }

    @Override
    public MenuItem setActionView(View view) {
        mActionView = view;
        return this;
    }

    @Override
    public MenuItem setActionView(int resId) {
        if (mContext != null && resId != 0) {
            mActionView = LayoutInflater.from(mContext).inflate(resId, null);
        }
        return this;
    }

    @Override
    public View getActionView() {
        if (mActionView != null) return mActionView;
        if (mActionProvider != null) {
            mActionView = mActionProvider.onCreateActionView(this);
        }
        return mActionView;
    }

    @Override
    public MenuItem setActionProvider(ActionProvider actionProvider) {
        mActionProvider = actionProvider;
        return this;
    }

    @Override
    public ActionProvider getActionProvider() {
        return mActionProvider;
    }

    @Override
    public MenuItem setOnMenuItemClickListener(OnMenuItemClickListener listener) {
        mClickListener = listener;
        return this;
    }

    @Override
    public MenuItem setOnActionExpandListener(OnActionExpandListener listener) {
        mExpandListener = listener;
        return this;
    }

    @Override
    public boolean expandActionView() {
        mIsActionViewExpanded = true;
        if (mExpandListener != null) {
            return mExpandListener.onMenuItemActionExpand(this);
        }
        return true;
    }

    @Override
    public boolean collapseActionView() {
        mIsActionViewExpanded = false;
        if (mExpandListener != null) {
            return mExpandListener.onMenuItemActionCollapse(this);
        }
        return true;
    }

    @Override
    public boolean isActionViewExpanded() {
        return mIsActionViewExpanded;
    }

    @Override
    public boolean hasSubMenu() {
        return mSubMenu != null;
    }

    @Override
    public SubMenu getSubMenu() {
        return mSubMenu;
    }

    public void setSubMenu(SubMenu subMenu) {
        mSubMenu = subMenu;
    }

    public boolean invoke() {
        if (mClickListener != null && mClickListener.onMenuItemClick(this)) {
            return true;
        }
        if (mMenu != null && mMenu.dispatchMenuItemSelected(this)) {
            return true;
        }
        return false;
    }
}
