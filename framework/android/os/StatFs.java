package android.os;

public class StatFs {
    private final String path;

    public StatFs(String path) {
        this.path = path;
    }
    public void restat(String path) {}
    public int getBlockSize() { return 4096; }
    public long getBlockSizeLong() { return 4096L; }
    public int getBlockCount() { return 16777216; }
    public long getBlockCountLong() { return 16777216L; }
    public int getFreeBlocks() { return 8388608; }
    public long getFreeBlocksLong() { return 8388608L; }
    public int getAvailableBlocks() { return 8388608; }
    public long getAvailableBlocksLong() { return 8388608L; }
    public long getFreeBytes() { return getFreeBlocksLong() * getBlockSizeLong(); }
    public long getAvailableBytes() { return getAvailableBlocksLong() * getBlockSizeLong(); }
    public long getTotalBytes() { return getBlockCountLong() * getBlockSizeLong(); }
}
