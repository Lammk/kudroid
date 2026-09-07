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
        public AssetPackLocation getPackLocation(String packName) {
            String root = AssetManager.getAssetsDir();
            if (root == null) root = "";
            File dir = new File(root, packName);
            String path = dir.exists() ? dir.getAbsolutePath() : (root.isEmpty() ? packName : root);
            return AssetPackLocation.create(path, path);
        }

        public Map<String, AssetPackLocation> getPackLocations() {
            return Collections.emptyMap();
        }

        public Task<AssetPackStates> getPackStates(List<String> packNames) {
            Map<String, AssetPackState> map = new HashMap<String, AssetPackState>();
            if (packNames != null) {
                for (String name : packNames) {
                    map.put(name, AssetPackState.create(name, AssetPackStatus.COMPLETED, AssetPackErrorCode.NO_ERROR, 0, 0, 100));
                }
            }
            return Tasks.forResult(AssetPackStates.create(0, map));
        }

        public Task<AssetPackStates> fetch(List<String> packNames) {
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
