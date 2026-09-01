package androidx.activity;

import android.app.Activity;
import androidx.activity.result.ActivityResultCaller;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.ActivityResultCallback;
import androidx.activity.result.ActivityResultRegistry;
import androidx.activity.result.contract.ActivityResultContract;

public class ComponentActivity extends Activity implements ActivityResultCaller {
    private final ActivityResultRegistry mActivityResultRegistry = new ActivityResultRegistry() {};

    public ActivityResultRegistry getActivityResultRegistry() {
        return mActivityResultRegistry;
    }

    @Override
    public <I, O> ActivityResultLauncher<I> registerForActivityResult(
            ActivityResultContract<I, O> contract,
            ActivityResultCallback<O> callback) {
        return registerForActivityResult(contract, mActivityResultRegistry, callback);
    }

    @Override
    public <I, O> ActivityResultLauncher<I> registerForActivityResult(
            ActivityResultContract<I, O> contract,
            ActivityResultRegistry registry,
            ActivityResultCallback<O> callback) {
        if (registry != null) {
            return registry.register("activity_rq#" + System.identityHashCode(callback), contract, callback);
        }
        return new ActivityResultLauncher<I>() {
            @Override
            public void launch(I input, androidx.core.app.ActivityOptionsCompat options) {}
            @Override
            public void unregister() {}
            @Override
            public ActivityResultContract<I, ?> getContract() { return contract; }
        };
    }
}
