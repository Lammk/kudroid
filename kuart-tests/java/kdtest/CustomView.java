package kdtest;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/** A view with the (Context, AttributeSet) constructor an inflater looks up. */
public class CustomView extends View {
    public int constructed = 1;

    public CustomView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }
}
