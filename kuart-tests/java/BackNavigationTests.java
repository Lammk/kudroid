import android.app.Activity;
import android.app.ActivityThread;
import android.app.ApplicationContext;
import android.content.Intent;
import android.view.KeyEvent;
import android.view.View;

public final class BackNavigationTests {
    public static class Page extends Activity {
        int backs, downs, ups, restarts, destroys;
        @Override public void onBackPressed() { backs++; }
        @Override public boolean onKeyDown(int code, KeyEvent event) {
            downs++;
            return super.onKeyDown(code, event);
        }
        @Override public boolean onKeyUp(int code, KeyEvent event) {
            ups++;
            return super.onKeyUp(code, event);
        }
        @Override protected void onRestart() { restarts++; }
        @Override protected void onDestroy() { destroys++; super.onDestroy(); }
        @Override public void requestRender() {}
        @Override public void renderViewHierarchy() {}
    }
    public static class SecondPage extends Page {}
    public static class FailingPage extends Page {
        static FailingPage instance;
        @Override protected void onCreate(android.os.Bundle state) {
            instance = this;
            throw new IllegalStateException("creation failure");
        }
    }
    static class PreImeView extends View {
        int keys;
        PreImeView(android.content.Context context) { super(context); setFocusable(true); }
        @Override public boolean dispatchKeyEventPreIme(KeyEvent event) { keys++; return true; }
    }

    public static String run() {
        TestSupport.report.setLength(0);
        try { navigation(); } catch (Throwable t) { TestSupport.fail("back navigation", t); }
        return TestSupport.report.toString();
    }

    static void navigation() throws Exception {
        ActivityThread thread = new ActivityThread();
        Class<?> type = thread.getClass();
        java.lang.reflect.Field current = type.getDeclaredField("sCurrentActivityThread");
        java.lang.reflect.Field target = type.getDeclaredField("mInitialActivity");
        java.lang.reflect.Field stack = type.getDeclaredField("mBackStack");
        current.setAccessible(true); target.setAccessible(true); stack.setAccessible(true);
        Object saved = current.get(null);
        try {
            java.lang.reflect.Method attach = type.getDeclaredMethod("attach");
            attach.setAccessible(true); attach.invoke(thread);
            Page first = new Page();
            first.attach(new ApplicationContext()); first.performStart(); first.performResume();
            target.set(thread, first);
            ActivityThread.postBack();
            WindowTraversalTests.drainFrames();
            TestSupport.check(first.backs == 1 && first.downs == 1 && first.ups == 1,
                    "Back delivers one tracked key pair on the main queue");
            ActivityThread.postKeyEvent(1, 4, 100, 150);
            WindowTraversalTests.drainFrames();
            TestSupport.check(first.backs == 1, "an unhandled native UP alone cannot finish an activity");
            KeyEvent times = new KeyEvent(100L, 150L, 0, 4, 0);
            TestSupport.check(times.getDownTime() == 100 && times.getEventTime() == 150,
                    "key constructors retain separate original timestamps");
            KeyEvent.DispatcherState state = new KeyEvent.DispatcherState();
            state.startTracking(times, first);
            state.performedLongPress(times);
            KeyEvent canceled = new KeyEvent(100L, 150L, 1, 4, 0);
            state.handleUpEvent(canceled);
            first.onKeyUp(4, canceled);
            TestSupport.check(canceled.isTracking() && canceled.isCanceled() && first.backs == 1,
                    "handled long press cancels the tracked Back release");
            PreImeView focus = new PreImeView(first);
            first.setContentView(focus); focus.requestFocus();
            ActivityThread.postBack(); WindowTraversalTests.drainFrames();
            TestSupport.check(focus.keys == 2 && first.backs == 1,
                    "focused pre-IME handler consumes Back before activity dispatch");
            first.setContentView(new View(first));
            java.lang.reflect.Method start = type.getDeclaredMethod("handleStartActivity", Activity.class, Intent.class);
            start.setAccessible(true);
            Intent intent = new Intent(first, SecondPage.class);
            start.invoke(thread, first, intent); WindowTraversalTests.drainFrames();
            Page second = (Page) target.get(thread);
            TestSupport.check(second != first && second.getIntent() == intent && !first.isStarted()
                    && !first.isDestroyed(), "launch stops and retains the previous activity and launch intent");
            second.finish(); second.finish(); WindowTraversalTests.drainFrames();
            TestSupport.check(target.get(thread) == first && first.isResumed() && first.restarts == 1
                    && second.destroys == 1, "finish restores previous activity and destroys leaf once");
            start.invoke(thread, first, new Intent(first, FailingPage.class)); WindowTraversalTests.drainFrames();
            TestSupport.check(target.get(thread) == first && first.isResumed()
                    && ((java.util.ArrayList<?>) stack.get(thread)).isEmpty()
                    && FailingPage.instance.isDestroyed(), "failed creation rolls back without a phantom history entry");
            start.invoke(thread, first, new Intent(first, SecondPage.class)); WindowTraversalTests.drainFrames();
            second = (Page) target.get(thread);
            start.invoke(thread, first, new Intent(first, Page.class)); WindowTraversalTests.drainFrames();
            Page third = (Page) target.get(thread);
            TestSupport.check(!second.isStarted() && third.isResumed(),
                    "launch from a retained activity stops the actual foreground activity");
            first.finish(); WindowTraversalTests.drainFrames();
            third.finish(); WindowTraversalTests.drainFrames();
            TestSupport.check(target.get(thread) == second && second.isResumed() && first.isDestroyed(),
                    "finishing a retained activity removes it from history");
            second.getIntent().addFlags(Intent.FLAG_ACTIVITY_NO_HISTORY);
            start.invoke(thread, second, new Intent(second, Page.class)); WindowTraversalTests.drainFrames();
            TestSupport.check(second.isDestroyed() && ((java.util.ArrayList<?>) stack.get(thread)).isEmpty(),
                    "no-history activity is destroyed when replaced");
        } finally { current.set(null, saved); }
    }
}
