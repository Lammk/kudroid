package androidx.activity.result;

import androidx.activity.result.contract.ActivityResultContract;

public abstract class ActivityResultLauncher<I> {
    public void launch(I input) { launch(input, null); }
    public abstract void launch(I input, androidx.core.app.ActivityOptionsCompat options);
    public abstract void unregister();
    public abstract ActivityResultContract<I, ?> getContract();
}
