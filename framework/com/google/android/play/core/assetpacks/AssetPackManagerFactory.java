package com.google.android.play.core.assetpacks;

import android.app.Activity;
import android.content.Context;
import android.content.res.AssetManager;
import com.google.android.play.core.assetpacks.model.AssetPackStatus;
import com.google.android.play.core.assetpacks.model.AssetPackErrorCode;
import com.google.android.play.core.tasks.Task;
import com.google.android.play.core.tasks.Tasks;

import java.io.File;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class AssetPackManagerFactory {
    private static AssetPackManager sInstance;

    private AssetPackManagerFactory() {}

    public static synchronized AssetPackManager getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new StubAssetPackManager();
        }
        return sInstance;
    }

    private static final class StubAssetPackManager implements AssetPackManager {
        // Single source of truth: an install-time pack exists iff its dir is
        // under the assets root. Everything below derives from that so states
        // and locations can never contradict each other (COMPLETED + empty
        // locations sent callers down ENOENT cascades).
        private static String packDir(String packName) {
            String root = AssetManager.getAssetsDir();
            if (root == null) root = "";
            File dir = new File(root, packName);
            return dir.exists() ? dir.getAbsolutePath() : null;
        }

        public AssetPackLocation getPackLocation(String packName) {
            String path = packDir(packName);
            if (path == null) return null;
            return AssetPackLocation.create(path, path);
        }

        public Map<String, AssetPackLocation> getPackLocations() {
            return Collections.emptyMap();
        }

        public Task<AssetPackStates> getPackStates(List<String> packNames) {
            Map<String, AssetPackState> map = new HashMap<String, AssetPackState>();
            if (packNames != null) {
                for (String name : packNames) {
                    if (packDir(name) != null) {
                        map.put(name, AssetPackState.create(name, AssetPackStatus.COMPLETED, AssetPackErrorCode.NO_ERROR, 0, 0, 100));
                    } else {
                        map.put(name, AssetPackState.create(name, AssetPackStatus.NOT_INSTALLED, AssetPackErrorCode.PACK_UNAVAILABLE, 0, 0, 0));
                    }
                }
            }
            return Tasks.forResult(AssetPackStates.create(0, map));
        }

        public Task<AssetPackStates> fetch(List<String> packNames) {
            // No delivery backend: report current truth, do not lie success.
            return getPackStates(packNames);
        }

        public Task<Void> cancel(List<String> packNames) {
            return Tasks.forResult(null);
        }

        public void clearListeners() {}
        public void registerListener(AssetPackStateUpdateListener listener) {}
        public void unregisterListener(AssetPackStateUpdateListener listener) {}

        public Task<Integer> showCellularDataConfirmation(Activity activity) {
            return Tasks.forResult(Activity.RESULT_OK);
        }

        public Task<Void> removePack(String packName) {
            return Tasks.forResult(null);
        }
    }
}
