package android.os;

public class StatFs {
    private String mPath;
    public StatFs(String path) { mPath = path; }
    public void restat(String path) { mPath = path; }
    public int getBlockSize() { return 4096; }
    public long getBlockSizeLong() { return 4096L; }
    public int getBlockCount() { return 1000000; }
    public long getBlockCountLong() { return 1000000L; }
    public int getFreeBlocks() { return 500000; }
    public long getFreeBlocksLong() { return 500000L; }
    public long getFreeBytes() { return 500000L * 4096L; }
    public int getAvailableBlocks() { return 500000; }
    public long getAvailableBlocksLong() { return 500000L; }
    public long getAvailableBytes() { return 500000L * 4096L; }
    public long getTotalBytes() { return 1000000L * 4096L; }
}
