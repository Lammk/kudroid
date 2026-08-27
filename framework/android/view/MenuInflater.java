package android.view;

import android.content.Context;

/**
 * android.view.MenuInflater — builds Menu from XML resource.
 *
 * KuDroid does not have a resource compiler yet, so inflate() does not add any items; app self
 * calling menu.add() is the path that works.
 */
public class MenuInflater {
    private final Context mContext;

    public MenuInflater(Context context) {
        mContext = context;
    }

    public Context getContext() {
        return mContext;
    }

    public void inflate(int menuRes, Menu menu) {
    }
}
