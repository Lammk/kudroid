package android.graphics;

public class ImageFormat {
    public static final int UNKNOWN = 0;
    public static final int RGB_565 = 4;
    public static final int NV16 = 0x10;
    public static final int NV21 = 0x11;
    public static final int YUY2 = 0x14;
    public static final int JPEG = 0x100;
    public static final int YUV_420_888 = 0x23;
    public static final int YV12 = 0x32315659;

    public static int getBitsPerPixel(int format) {
        switch (format) {
            case RGB_565: return 16;
            case NV16: return 16;
            case YUY2: return 16;
            case YV12: return 12;
            case NV21: return 12;
            case YUV_420_888: return 12;
            default: return -1;
        }
    }
}
