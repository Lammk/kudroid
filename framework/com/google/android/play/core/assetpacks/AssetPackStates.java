package com.google.android.play.core.assetpacks;

import java.util.Map;

public abstract class AssetPackStates {
    public abstract long totalBytes();
    public abstract Map<String, AssetPackState> packStates();

    public static AssetPackStates create(final long totalBytes, final Map<String, AssetPackState> packStates) {
        return new AssetPackStates() {
            public long totalBytes() { return totalBytes; }
            public Map<String, AssetPackState> packStates() { return packStates; }
        };
    }
}
