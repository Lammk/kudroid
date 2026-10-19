package android.app;

import java.util.ArrayList;

public class FragmentTransaction {
    private final FragmentManager manager;
    private final ArrayList<Fragment> fragments = new ArrayList<>();
    private final ArrayList<String> tags = new ArrayList<>();
    private final ArrayList<Boolean> removals = new ArrayList<>();
    private boolean committed;

    FragmentTransaction(FragmentManager manager) { this.manager = manager; }

    public FragmentTransaction add(Fragment fragment, String tag) {
        return operation(fragment, tag, false);
    }

    public FragmentTransaction remove(Fragment fragment) {
        return operation(fragment, null, true);
    }

    private FragmentTransaction operation(Fragment fragment, String tag, boolean remove) {
        if (committed) throw new IllegalStateException("Transaction already committed");
        if (fragment == null) throw new NullPointerException("fragment == null");
        fragments.add(fragment);
        tags.add(tag);
        removals.add(remove);
        return this;
    }

    public int commit() { commit(false, false); return -1; }

    public int commitAllowingStateLoss() { commit(true, false); return -1; }

    public void commitNow() { commit(false, true); }

    public void commitNowAllowingStateLoss() { commit(true, true); }

    private void commit(boolean allowStateLoss, boolean immediate) {
        if (committed) throw new IllegalStateException("Transaction already committed");
        committed = true;
        manager.enqueue(this, allowStateLoss, immediate);
    }

    void execute() {
        for (int i = 0; i < fragments.size(); i++) {
            if (removals.get(i)) manager.remove(fragments.get(i));
            else manager.add(fragments.get(i), tags.get(i));
        }
    }
}
