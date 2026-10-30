package android.view;

import android.content.Context;

public abstract class ActionProvider {
    private final Context mContext;

    public interface VisibilityListener {
        void onActionProviderVisibilityChanged(boolean isVisible);
    }

    public ActionProvider(Context context) {
        mContext = context;
    }

    public Context getContext() {
        return mContext;
    }

    public abstract View onCreateActionView();

    public View onCreateActionView(MenuItem forItem) {
        return onCreateActionView();
    }

    public boolean onPerformDefaultAction() {
        return false;
    }

    public boolean hasSubMenu() {
        return false;
    }

    public void onPrepareSubMenu(SubMenu subMenu) {
    }

    public boolean overridesItemVisibility() {
        return false;
    }

    public boolean isVisible() {
        return true;
    }

    public void refreshVisibility() {
    }

    public void setVisibilityListener(VisibilityListener listener) {
    }
}
