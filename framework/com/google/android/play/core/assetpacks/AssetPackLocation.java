package com.google.android.play.core.assetpacks;

public abstract class AssetPackLocation {
    public abstract String assetsPath();
    public abstract String packPath();

    public static AssetPackLocation create(final String packPath, final String assetsPath) {
        return new AssetPackLocation() {
            public String assetsPath() { return assetsPath; }
            public String packPath() { return packPath; }
        };
    }
}
