package com.google.android.play.core.assetpacks;

import android.app.Activity;
import com.google.android.play.core.tasks.Task;
import java.util.List;
import java.util.Map;

public interface AssetPackManager {
    AssetPackLocation getPackLocation(String packName);
    Map<String, AssetPackLocation> getPackLocations();
    Task<AssetPackStates> getPackStates(List<String> packNames);
    Task<AssetPackStates> fetch(List<String> packNames);
    Task<Void> cancel(List<String> packNames);
    void clearListeners();
    void registerListener(AssetPackStateUpdateListener listener);
    void unregisterListener(AssetPackStateUpdateListener listener);
    Task<Integer> showCellularDataConfirmation(Activity activity);
    Task<Void> removePack(String packName);
}
