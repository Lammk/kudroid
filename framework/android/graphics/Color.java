package android.graphics;

/**
 * triển khai android.graphics.color tối thiểu.
 *
 * cung cấp các hằng số màu và trình trợ giúp chuyển đổi. đối với khuôn khổ
 * tối thiểu của kudroid, triển khai các trình trợ giúp argb phổ biến.
 */
public class Color {
    public static final int BLACK = 0xFF000000;
    public static final int WHITE = 0xFFFFFFFF;
    public static final int RED = 0xFFFF0000;
    public static final int GREEN = 0xFF00FF00;
    public static final int BLUE = 0xFF0000FF;
    public static final int YELLOW = 0xFFFFFF00;
    public static final int CYAN = 0xFF00FFFF;
    public static final int MAGENTA = 0xFFFF00FF;
    public static final int GRAY = 0xFF808080;
    public static final int LTGRAY = 0xFFC0C0C0;
    public static final int DKGRAY = 0xFF404040;
    public static final int TRANSPARENT = 0x00000000;

    private Color() {
    }

    /**
     * trả về thành phần alpha của một màu.
     */
    public static int alpha(int color) {
        return color >>> 24;
    }

    /**
     * trả về thành phần màu đỏ của một màu.
     */
    public static int red(int color) {
        return (color >> 16) & 0xFF;
    }

    /**
     * trả về thành phần màu xanh lá cây của một màu.
     */
    public static int green(int color) {
        return (color >> 8) & 0xFF;
    }

    /**
     * trả về thành phần màu xanh dương của một màu.
     */
    public static int blue(int color) {
        return color & 0xFF;
    }

    /**
     * trả về một màu từ các thành phần argb.
     */
    public static int argb(int alpha, int red, int green, int blue) {
        return (alpha << 24) | (red << 16) | (green << 8) | blue;
    }

    /**
     * trả về một màu từ các thành phần rgb (đục).
     */
    public static int rgb(int red, int green, int blue) {
        return (0xFF << 24) | (red << 16) | (green << 8) | blue;
    }

    /**
     * phân tích cú pháp một màu từ một chuỗi (ví dụ: "#rrggbb" hoặc "#aarrggbb").
     */
    public static int parseColor(String colorString) {
        if (colorString == null) return BLACK;
        String s = colorString.trim();
        if (s.startsWith("#")) {
            s = s.substring(1);
            if (s.length() == 6) {
                s = "FF" + s;
            }
            return (int) Long.parseLong(s, 16);
        }
        return BLACK;
    }
}
