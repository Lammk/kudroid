import android.view.View;

public final class ViewSizeChangedTest {
    private static final class SizedView extends View {
        int calls;
        int width, height, oldWidth, oldHeight;
        boolean sizeBeforeLayout;

        SizedView() {
            super(null);
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            check(getWidth() == w && getHeight() == h, "bounds updated before callback");
            width = w;
            height = h;
            oldWidth = oldw;
            oldHeight = oldh;
            calls++;
            sizeBeforeLayout = true;
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            check(calls > 0 && sizeBeforeLayout, "size callback precedes layout");
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        SizedView view = new SizedView();
        final int[] layoutCalls = {0};
        View.OnLayoutChangeListener listener = new View.OnLayoutChangeListener() {
            @Override
            public void onLayoutChange(View v, int l, int t, int r, int b,
                                       int oldl, int oldt, int oldr, int oldb) {
                check(v.getLeft() == l && v.getRight() == r, "listener sees current bounds");
                layoutCalls[0]++;
                v.removeOnLayoutChangeListener(this);
            }
        };
        view.addOnLayoutChangeListener(listener);
        view.addOnLayoutChangeListener(listener);
        view.layout(0, 0, 640, 480);
        check(layoutCalls[0] == 1, "duplicate registration and removal during callback are safe");
        check(view.calls == 1 && view.width == 640 && view.height == 480,
              "first layout reports dimensions");
        check(view.oldWidth == 0 && view.oldHeight == 0, "first layout reports old dimensions");
        view.layout(0, 0, 640, 480);
        view.layout(10, 20, 650, 500);
        check(view.calls == 1, "unchanged size and translation do not report a resize");
        view.layout(10, 20, 810, 620);
        check(view.calls == 2 && view.width == 800 && view.height == 600
              && view.oldWidth == 640 && view.oldHeight == 480, "resize preserves old dimensions");
        view.layout(0, 0, 0, 0);
        check(view.calls == 3 && view.width == 0 && view.height == 0,
              "transition to zero dimensions is reported");
        check(layoutCalls[0] == 1, "removed listener is not called again");
        System.out.println("View size callback PASSED");
    }
}
