package androidx.activity.result;

import androidx.activity.result.contract.ActivityResultContract;

public abstract class ActivityResultRegistry {
    public <I, O> ActivityResultLauncher<I> register(
            final String key,
            final ActivityResultContract<I, O> contract,
            final ActivityResultCallback<O> callback) {
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
