package com.google.android.play.core.assetpacks;

public abstract class AssetPackState {
    public abstract String name();
    public abstract int status();
    public abstract int errorCode();
    public abstract long bytesDownloaded();
    public abstract long totalBytesToDownload();
    public abstract int transferProgressPercentage();

    public static AssetPackState create(final String name, final int status, final int errorCode,
                                        final long bytesDownloaded, final long totalBytesToDownload,
                                        final int transferProgressPercentage) {
        return new AssetPackState() {
            public String name() { return name; }
            public int status() { return status; }
            public int errorCode() { return errorCode; }
            public long bytesDownloaded() { return bytesDownloaded; }
            public long totalBytesToDownload() { return totalBytesToDownload; }
            public int transferProgressPercentage() { return transferProgressPercentage; }
        };
    }
}
