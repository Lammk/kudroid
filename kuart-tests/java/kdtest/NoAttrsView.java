package kdtest;

import android.content.Context;
import android.view.View;

/** No (Context, AttributeSet) constructor: inflating it must fail with InflateException. */
public class NoAttrsView extends View {
    public NoAttrsView(Context context) {
        super(context);
    }
}
