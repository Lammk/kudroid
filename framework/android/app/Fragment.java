package android.app;

/**
 * android.app.Fragment — the platform Fragment, deprecated on Android but still referenced.
 *
 * Was an empty generated stub. All five real APKs in the corpus call
 * {@code Fragment.getActivity()Landroid/app/Activity;} — usually from a support library
 * shim that bridges platform and AndroidX fragments — so the stub failed at the first use.
 *
 * KuDroid runs no fragment manager: nothing creates fragments, nothing attaches them, and
 * the lifecycle methods below are never called. The class is here so code that mentions
 * Fragment can load and so getActivity() answers rather than throwing. It answers with the
 * Activity that is actually running, which for a single-activity app is the right one.
 */
public class Fragment {
    private Activity mActivity;
    private android.os.Bundle mArguments;
    private String mTag;
    private android.view.View mView;

    public Fragment() {}

    /**
     * The Activity this fragment is attached to.
     *
     * Falls back to the running Activity when nothing attached it, which is the case for
     * every fragment KuDroid sees. Returning null would be defensible and is worse in
     * practice: callers dereference it immediately — {@code getActivity().getWindow()} — so
     * null turns a working path into an NPE somewhere unrelated.
     */
    public Activity getActivity() {
        if (mActivity != null) return mActivity;
        return ActivityThread.currentActivity();
    }

    public android.content.Context getContext() {
        return getActivity();
    }

    /** Called by a fragment manager. Nothing in KuDroid calls it; kept for apps that do. */
    public void onAttach(android.content.Context context) {
        if (context instanceof Activity) mActivity = (Activity) context;
    }

    public void onDetach() {
        mActivity = null;
    }

    public boolean isAdded() {
        return mActivity != null;
    }

    public boolean isDetached() {
        return mActivity == null;
    }

    public boolean isVisible() {
        return mView != null && mView.getVisibility() == android.view.View.VISIBLE;
    }

    public void setArguments(android.os.Bundle args) {
        mArguments = args;
    }

    public android.os.Bundle getArguments() {
        return mArguments;
    }

    public String getTag() {
        return mTag;
    }

    public android.view.View getView() {
        return mView;
    }

    public void onCreate(android.os.Bundle savedInstanceState) {
    }

    public android.view.View onCreateView(android.view.LayoutInflater inflater,
                                          android.view.ViewGroup container,
                                          android.os.Bundle savedInstanceState) {
        return null;
    }

    public void onViewCreated(android.view.View view, android.os.Bundle savedInstanceState) {
    }

    public void onStart() {
    }

    public void onResume() {
    }

    public void onPause() {
    }

    public void onStop() {
    }

    public void onDestroyView() {
        mView = null;
    }

    public void onDestroy() {
    }

    public void onSaveInstanceState(android.os.Bundle outState) {
    }
}
