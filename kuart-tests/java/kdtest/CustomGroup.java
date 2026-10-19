package kdtest;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.FrameLayout;

public class CustomGroup extends FrameLayout {
    public boolean finished;

    public CustomGroup(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    protected void onFinishInflate() {
        finished = true;
    }
}
