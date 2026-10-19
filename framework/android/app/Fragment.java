package android.app;

public class Fragment {
    Activity mActivity;
    FragmentManager mManager;
    int mState;
    boolean mAdded;
    private android.os.Bundle mArguments;
    String mTag;
    android.view.View mView;

    public Fragment() {}

    public Activity getActivity() {
        return mActivity;
    }

    public FragmentManager getFragmentManager() {
        return mManager;
    }

    public android.content.Context getContext() {
        return getActivity();
    }

    public void onAttach(android.content.Context context) {
        if (context instanceof Activity) onAttach((Activity) context);
    }

    public void onAttach(Activity activity) {}

    public void onActivityCreated(android.os.Bundle state) {}

    public void onDetach() {
        mActivity = null;
    }

    public boolean isAdded() {
        return mActivity != null && mAdded;
    }

    public boolean isDetached() {
        return false;
    }

    public boolean isVisible() {
        return isAdded() && mView != null && mView.getVisibility() == android.view.View.VISIBLE;
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
