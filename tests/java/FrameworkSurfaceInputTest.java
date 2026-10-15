import android.view.*;
import android.graphics.*;

public class FrameworkSurfaceInputTest {
    static int failures;
    static void check(boolean ok, String message) {
        System.out.println((ok ? "PASS " : "FAIL ") + message);
        if (!ok) failures++;
    }
    static class Receiver extends View {
        final boolean accepts;
        int calls, action;
        float x, y, secondX;
        Receiver(boolean accepts) { super(null); this.accepts = accepts; }
        @Override public boolean dispatchTouchEvent(MotionEvent e) {
            calls++; action = e.getActionMasked(); x = e.getX(); y = e.getY();
            if (e.getPointerCount() > 1) secondX = e.getX(1);
            return accepts;
        }
    }
    public static void main(String[] args) {
        ViewGroup root = new ViewGroup(null);
        Receiver bottom = new Receiver(true), overlay = new Receiver(false);
        root.addView(bottom); root.addView(overlay);
        root.layout(0, 0, 500, 500);
        bottom.layout(30, 40, 300, 400); overlay.layout(0, 0, 500, 500);
        MotionEvent down = MotionEvent.obtainWithPointers(0, 0, 0,
                new int[]{0, 1}, new float[]{80, 100}, new float[]{90, 120}, 2);
        check(root.dispatchTouchEvent(down), "non-consuming overlay lets DOWN reach lower view");
        check(bottom.calls == 1 && bottom.x == 50 && bottom.y == 50 && bottom.secondX == 70,
              "child receives all pointers in local coordinates");
        check(down.getX() == 80 && down.getY() == 90, "parent event coordinates are preserved");
        root.dispatchTouchEvent(MotionEvent.obtain(2, 490, 490, 1));
        check(bottom.calls == 2, "MOVE stays with the child that accepted DOWN");
        root.dispatchTouchEvent(MotionEvent.obtain(3, 490, 490, 2));
        check(bottom.calls == 3 && bottom.action == 3, "CANCEL reaches captured child before reset");
        Bitmap bitmap = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888);
        new Canvas(bitmap).drawColor(0xff123456);
        check(bitmap.getPixel(1, 1) == 0xff123456, "bitmap Canvas draws into its own pixels");
        java.nio.ByteBuffer pixels = java.nio.ByteBuffer.allocate(17);
        pixels.position(1);
        bitmap.copyPixelsToBuffer(pixels);
        check(pixels.position() == 17 && (pixels.get(1) & 255) == 0x12 &&
              (pixels.get(2) & 255) == 0x34 && (pixels.get(3) & 255) == 0x56 &&
              (pixels.get(4) & 255) == 255, "pixel buffer exports RGBA bytes and advances position");
        bitmap.setPixel(0, 0, 0x80c86432);
        pixels.clear();
        bitmap.copyPixelsToBuffer(pixels);
        check((pixels.get(0) & 255) == 100 && (pixels.get(1) & 255) == 50 &&
              (pixels.get(2) & 255) == 25 && (pixels.get(3) & 255) == 128,
              "pixel buffer exports premultiplied alpha");
        java.nio.ByteBuffer shortBuffer = java.nio.ByteBuffer.allocate(1);
        boolean rejected = false;
        try { bitmap.copyPixelsToBuffer(shortBuffer); } catch (RuntimeException expected) { rejected = true; }
        check(rejected && shortBuffer.position() == 0, "short destination is rejected before writing");
        Bitmap alpha = Bitmap.createBitmap(3, 1, Bitmap.Config.ALPHA_8);
        alpha.eraseColor(0x80000000);
        java.nio.IntBuffer integers = java.nio.IntBuffer.wrap(new int[]{0x12345678});
        alpha.copyPixelsToBuffer(integers);
        check(integers.get(0) == 0x12808080 && integers.position() == 0,
              "partial buffer element preserves trailing bytes and whole-element position");
        Paint paint = new Paint();
        paint.setARGB(128, 10, 20, 30);
        check(paint.getColor() == 0x800a141e, "Paint.setARGB stores all channels");
        check(paint.setTypeface(Typeface.DEFAULT) == null &&
              paint.setTypeface(null) == Typeface.DEFAULT, "Paint.setTypeface returns previous typeface");
        paint.setTextSize(20);
        check(paint.measureText("abcd", 1, 3) == 20, "Paint range measurement uses selected text");
        MotionEvent copied = MotionEvent.obtain(down);
        copied.offsetLocation(-30, -40);
        check(copied.getRawX(1) == 100 && copied.getX(1) == 70,
              "offset event preserves raw coordinates for every pointer");
        if (failures != 0) throw new AssertionError(failures + " failures");
    }
}
