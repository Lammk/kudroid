package android.app;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;
import java.util.List;

public class FragmentManager {
    private final Activity host;
    private final ArrayList<Fragment> fragments = new ArrayList<>();
    private final ArrayList<FragmentTransaction> pending = new ArrayList<>();
    private int state;
    private boolean executing;
    private boolean destroyed;
    private boolean stateSaved;

    public FragmentManager() { this(null); }

    FragmentManager(Activity host) { this.host = host; }

    public Fragment findFragmentByTag(String tag) {
        if (tag == null) return null;
        for (int i = fragments.size() - 1; i >= 0; i--) {
            Fragment fragment = fragments.get(i);
            if (tag.equals(fragment.mTag)) return fragment;
        }
        return null;
    }

    public List<Fragment> getFragments() { return new ArrayList<>(fragments); }

    public FragmentTransaction beginTransaction() { return new FragmentTransaction(this); }

    public boolean isDestroyed() { return destroyed; }

    public boolean isStateSaved() { return stateSaved; }

    void noteStateNotSaved() { stateSaved = false; }

    void markStateSaved() { stateSaved = true; }

    private void checkThread() {
        if (host == null) throw new IllegalStateException("FragmentManager has no host");
        Looper main = Looper.getMainLooper();
        if (main == null || Looper.myLooper() != main) {
            throw new IllegalStateException("Must be called from the main thread");
        }
    }

    void enqueue(FragmentTransaction transaction, boolean allowStateLoss, boolean immediate) {
        checkThread();
        if (!allowStateLoss && (stateSaved || destroyed)) {
            throw new IllegalStateException("Cannot commit after state has been saved or destroyed");
        }
        if (destroyed) return;
        if (immediate) {
            if (executing) throw new IllegalStateException("Already executing transactions");
            executing = true;
            try { transaction.execute(); } finally { executing = false; }
        } else {
            pending.add(transaction);
            if (pending.size() == 1) {
                new Handler(Looper.getMainLooper()).post(new Runnable() {
                    public void run() {
                        if (!destroyed) executePendingTransactions();
                    }
                });
            }
        }
    }

    public boolean executePendingTransactions() {
        checkThread();
        if (executing) throw new IllegalStateException("Already executing transactions");
        boolean changed = false;
        executing = true;
        try {
            while (!pending.isEmpty()) {
                FragmentTransaction transaction = pending.remove(0);
                transaction.execute();
                changed = true;
            }
        } finally {
            executing = false;
        }
        return changed;
    }

    void add(Fragment fragment, String tag) {
        if (fragment.mManager != null && fragment.mManager != this) {
            throw new IllegalStateException("Fragment belongs to another manager");
        }
        if (fragment.mAdded) throw new IllegalStateException("Fragment already added");
        if (tag != null && fragment.mTag != null && !tag.equals(fragment.mTag)) {
            throw new IllegalStateException("Cannot change fragment tag");
        }
        if (tag != null) fragment.mTag = tag;
        fragment.mManager = this;
        fragment.mAdded = true;
        fragments.add(fragment);
        moveToState(fragment, state, null);
    }

    void remove(Fragment fragment) {
        if (fragment.mManager != this || !fragment.mAdded) return;
        fragments.remove(fragment);
        fragment.mAdded = false;
        moveToState(fragment, 0, null);
        fragment.mManager = null;
    }

    void dispatchState(int target, Bundle savedState) {
        executePendingTransactions();
        state = target;
        Fragment[] snapshot = fragments.toArray(new Fragment[fragments.size()]);
        executing = true;
        try {
            for (Fragment fragment : snapshot) {
                if (fragment.mAdded) moveToState(fragment, target, savedState);
            }
        } finally {
            executing = false;
        }
    }

    private void moveToState(Fragment fragment, int target, Bundle savedState) {
        while (fragment.mState < target) {
            switch (++fragment.mState) {
                case 1:
                    fragment.mActivity = host;
                    fragment.onAttach((android.content.Context) host);
                    fragment.onCreate(savedState);
                    break;
                case 2:
                    fragment.mView = fragment.onCreateView(host.getLayoutInflater(), null, savedState);
                    if (fragment.mView != null) fragment.onViewCreated(fragment.mView, savedState);
                    fragment.onActivityCreated(savedState);
                    break;
                case 3: fragment.onStart(); break;
                case 4: fragment.onResume(); break;
            }
        }
        while (fragment.mState > target) {
            switch (fragment.mState--) {
                case 4: fragment.onPause(); break;
                case 3: fragment.onStop(); break;
                case 2:
                    fragment.onDestroyView();
                    fragment.mView = null;
                    break;
                case 1:
                    fragment.onDestroy();
                    fragment.onDetach();
                    fragment.mActivity = null;
                    break;
            }
        }
    }

    void destroy() {
        executePendingTransactions();
        destroyed = true;
        pending.clear();
        dispatchState(0, null);
        for (Fragment fragment : fragments) {
            fragment.mAdded = false;
            fragment.mManager = null;
        }
        fragments.clear();
        pending.clear();
    }
}
