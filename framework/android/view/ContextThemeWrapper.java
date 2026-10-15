package android.view;
 
import android.content.Context;
import android.content.res.Resources;

public class ContextThemeWrapper extends android.app.ContextThemeWrapper {
    public ContextThemeWrapper() {
        super();
    }

    public ContextThemeWrapper(Context base) {
        super(base);
    }

    public ContextThemeWrapper(Context base, int themeResId) {
        super(base, themeResId);
    }

    public ContextThemeWrapper(Context base, Resources.Theme theme) {
        super(base, theme);
    }
}
