package android.util;

import android.graphics.Path;

/**
 * SVG path-data parsing for vector drawables and path animations.
 *
 * Path data is kept as command letters plus their float arguments (a "node" list), which is
 * all that morphing needs: two paths morph when their commands match one to one.
 */
public class PathParser {
    private static final String LOGTAG = "PathParser";

    /** Parses SVG path data into a Path; returns null when the string is malformed. */
    public static Path createPathFromPathData(String pathString) {
        if (pathString == null) {
            throw new IllegalArgumentException("pathString cannot be null");
        }
        PathData data = parse(pathString);
        if (data == null) {
            Log.e(LOGTAG, "Error in parsing " + pathString);
            return null;
        }
        Path path = new Path();
        nodesToPath(data, path);
        return path;
    }

    /** Replaces outPath's contents with the geometry of data. */
    public static void createPathFromPathData(Path outPath, PathData data) {
        outPath.rewind();
        nodesToPath(data, outPath);
    }

    /** True when both paths have identical command sequences and can be interpolated. */
    public static boolean canMorph(PathData from, PathData to) {
        if (from == null || to == null || from.mCommands.length != to.mCommands.length) {
            return false;
        }
        for (int i = 0; i < from.mCommands.length; i++) {
            if (from.mCommands[i] != to.mCommands[i]
                    || from.mArgs[i].length != to.mArgs[i].length) {
                return false;
            }
        }
        return true;
    }

    /** Writes the interpolation of from and to at fraction into outData. */
    public static boolean interpolatePathData(PathData outData, PathData fromData, PathData toData,
            float fraction) {
        if (!canMorph(fromData, toData)) {
            throw new IllegalArgumentException("Can't interpolate between two incompatible pathData");
        }
        if (outData.mCommands.length != fromData.mCommands.length
                || !canMorph(outData, fromData)) {
            outData.setPathData(fromData);
        }
        for (int i = 0; i < fromData.mCommands.length; i++) {
            for (int j = 0; j < fromData.mArgs[i].length; j++) {
                final float a = fromData.mArgs[i][j];
                outData.mArgs[i][j] = a + (toData.mArgs[i][j] - a) * fraction;
            }
        }
        return true;
    }

    /** Parsed path data: one command letter and argument list per node. */
    public static class PathData {
        char[] mCommands = new char[0];
        float[][] mArgs = new float[0][];

        public PathData() {
        }

        public PathData(PathData data) {
            setPathData(data);
        }

        public PathData(String pathString) {
            PathData parsed = parse(pathString);
            if (parsed == null) {
                throw new IllegalArgumentException("Invalid pathData: " + pathString);
            }
            setPathData(parsed);
        }

        public void setPathData(PathData source) {
            mCommands = source.mCommands.clone();
            mArgs = new float[source.mArgs.length][];
            for (int i = 0; i < mArgs.length; i++) {
                mArgs[i] = source.mArgs[i].clone();
            }
        }
    }

    private static int argCount(char cmd) {
        switch (Character.toLowerCase(cmd)) {
            case 'a': return 7;
            case 'c': return 6;
            case 'q': case 's': return 4;
            case 'l': case 'm': case 't': return 2;
            case 'h': case 'v': return 1;
            case 'z': return 0;
            default: return -1;
        }
    }

    private static boolean isCommand(char c) {
        return argCount(c) >= 0 && c != 'e' && c != 'E';
    }

    /** Returns null when the text is not valid path data. */
    static PathData parse(String s) {
        final java.util.ArrayList<Character> cmds = new java.util.ArrayList<Character>();
        final java.util.ArrayList<float[]> args = new java.util.ArrayList<float[]>();
        final int n = s.length();
        final int[] pos = {0};
        char cmd = 0;
        boolean first = true;
        while (true) {
            skipSeparators(s, pos);
            if (pos[0] >= n) {
                break;
            }
            final char c = s.charAt(pos[0]);
            if (isCommand(c)) {
                cmd = c;
                pos[0]++;
                if (first && cmd != 'M' && cmd != 'm') {
                    return null;
                }
                first = false;
                if (argCount(cmd) == 0) {
                    cmds.add(cmd);
                    args.add(new float[0]);
                    continue;
                }
            } else if (cmd == 0 || argCount(cmd) == 0) {
                return null;
            } else if (cmd == 'M' || cmd == 'm') {
                // Extra coordinate pairs after a moveto are implicit linetos.
                cmd = cmd == 'M' ? 'L' : 'l';
            }
            final int count = argCount(cmd);
            final float[] values = new float[count];
            for (int i = 0; i < count; i++) {
                skipSeparators(s, pos);
                final boolean flag = Character.toLowerCase(cmd) == 'a' && (i == 3 || i == 4);
                if (!readNumber(s, pos, flag, values, i)) {
                    return null;
                }
            }
            cmds.add(cmd);
            args.add(values);
        }
        final PathData data = new PathData();
        data.mCommands = new char[cmds.size()];
        data.mArgs = new float[cmds.size()][];
        for (int i = 0; i < data.mCommands.length; i++) {
            data.mCommands[i] = cmds.get(i);
            data.mArgs[i] = args.get(i);
        }
        return data;
    }

    private static void skipSeparators(String s, int[] pos) {
        while (pos[0] < s.length()) {
            final char c = s.charAt(pos[0]);
            if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r') {
                pos[0]++;
            } else {
                break;
            }
        }
    }

    /** Reads one number; arc flags are a single '0' or '1' and may be packed against the next. */
    private static boolean readNumber(String s, int[] pos, boolean flag, float[] out, int index) {
        final int n = s.length();
        int p = pos[0];
        if (p >= n) {
            return false;
        }
        if (flag) {
            final char c = s.charAt(p);
            if (c != '0' && c != '1') {
                return false;
            }
            out[index] = c - '0';
            pos[0] = p + 1;
            return true;
        }
        final int start = p;
        if (s.charAt(p) == '+' || s.charAt(p) == '-') {
            p++;
        }
        boolean digits = false;
        while (p < n && Character.isDigit(s.charAt(p))) {
            p++;
            digits = true;
        }
        if (p < n && s.charAt(p) == '.') {
            p++;
            while (p < n && Character.isDigit(s.charAt(p))) {
                p++;
                digits = true;
            }
        }
        if (!digits) {
            return false;
        }
        if (p < n && (s.charAt(p) == 'e' || s.charAt(p) == 'E')) {
            int q = p + 1;
            if (q < n && (s.charAt(q) == '+' || s.charAt(q) == '-')) {
                q++;
            }
            final int expStart = q;
            while (q < n && Character.isDigit(s.charAt(q))) {
                q++;
            }
            if (q > expStart) {
                p = q;
            }
        }
        try {
            out[index] = Float.parseFloat(s.substring(start, p));
        } catch (NumberFormatException e) {
            return false;
        }
        pos[0] = p;
        return true;
    }

    private static void nodesToPath(PathData data, Path path) {
        float curX = 0, curY = 0;
        float startX = 0, startY = 0;
        float ctrlX = 0, ctrlY = 0;
        char prev = 'm';
        for (int i = 0; i < data.mCommands.length; i++) {
            final char cmd = data.mCommands[i];
            final float[] a = data.mArgs[i];
            final boolean rel = Character.isLowerCase(cmd);
            final float ox = rel ? curX : 0;
            final float oy = rel ? curY : 0;
            switch (Character.toLowerCase(cmd)) {
                case 'z':
                    path.close();
                    curX = startX;
                    curY = startY;
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                case 'm':
                    curX = ox + a[0];
                    curY = oy + a[1];
                    startX = curX;
                    startY = curY;
                    path.moveTo(curX, curY);
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                case 'l':
                    curX = ox + a[0];
                    curY = oy + a[1];
                    path.lineTo(curX, curY);
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                case 'h':
                    curX = ox + a[0];
                    path.lineTo(curX, curY);
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                case 'v':
                    curY = oy + a[0];
                    path.lineTo(curX, curY);
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                case 'c': {
                    final float x1 = ox + a[0], y1 = oy + a[1];
                    final float x2 = ox + a[2], y2 = oy + a[3];
                    final float x = ox + a[4], y = oy + a[5];
                    path.cubicTo(x1, y1, x2, y2, x, y);
                    ctrlX = x2;
                    ctrlY = y2;
                    curX = x;
                    curY = y;
                    break;
                }
                case 's': {
                    final boolean smooth = "cCsS".indexOf(prev) >= 0;
                    final float x1 = smooth ? 2 * curX - ctrlX : curX;
                    final float y1 = smooth ? 2 * curY - ctrlY : curY;
                    final float x2 = ox + a[0], y2 = oy + a[1];
                    final float x = ox + a[2], y = oy + a[3];
                    path.cubicTo(x1, y1, x2, y2, x, y);
                    ctrlX = x2;
                    ctrlY = y2;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'q': {
                    final float x1 = ox + a[0], y1 = oy + a[1];
                    final float x = ox + a[2], y = oy + a[3];
                    path.quadTo(x1, y1, x, y);
                    ctrlX = x1;
                    ctrlY = y1;
                    curX = x;
                    curY = y;
                    break;
                }
                case 't': {
                    final boolean smooth = "qQtT".indexOf(prev) >= 0;
                    final float x1 = smooth ? 2 * curX - ctrlX : curX;
                    final float y1 = smooth ? 2 * curY - ctrlY : curY;
                    final float x = ox + a[0], y = oy + a[1];
                    path.quadTo(x1, y1, x, y);
                    ctrlX = x1;
                    ctrlY = y1;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'a':
                    arcTo(path, curX, curY, ox + a[5], oy + a[6], a[0], a[1], a[2],
                            a[3] != 0, a[4] != 0);
                    curX = ox + a[5];
                    curY = oy + a[6];
                    ctrlX = curX;
                    ctrlY = curY;
                    break;
                default:
                    break;
            }
            prev = cmd;
        }
    }

    /** Appends an SVG elliptical arc as cubic segments (endpoint to centre parameterisation). */
    private static void arcTo(Path path, double x0, double y0, double x1, double y1,
            double rx, double ry, double angle, boolean largeArc, boolean sweep) {
        if (x0 == x1 && y0 == y1) {
            return;
        }
        rx = Math.abs(rx);
        ry = Math.abs(ry);
        if (rx == 0 || ry == 0) {
            path.lineTo((float) x1, (float) y1);
            return;
        }
        final double phi = Math.toRadians(angle % 360.0);
        final double cosPhi = Math.cos(phi);
        final double sinPhi = Math.sin(phi);
        final double dx = (x0 - x1) / 2.0;
        final double dy = (y0 - y1) / 2.0;
        final double x1p = cosPhi * dx + sinPhi * dy;
        final double y1p = -sinPhi * dx + cosPhi * dy;
        final double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1) {
            final double scale = Math.sqrt(lambda);
            rx *= scale;
            ry *= scale;
        }
        final double rx2 = rx * rx;
        final double ry2 = ry * ry;
        double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        final double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
        double coef = den == 0 ? 0 : Math.sqrt(Math.max(0, num / den));
        if (largeArc == sweep) {
            coef = -coef;
        }
        final double cxp = coef * (rx * y1p / ry);
        final double cyp = coef * -(ry * x1p / rx);
        final double cx = cosPhi * cxp - sinPhi * cyp + (x0 + x1) / 2.0;
        final double cy = sinPhi * cxp + cosPhi * cyp + (y0 + y1) / 2.0;
        final double theta1 = Math.atan2((y1p - cyp) / ry, (x1p - cxp) / rx);
        double delta = Math.atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx) - theta1;
        if (sweep && delta < 0) {
            delta += 2 * Math.PI;
        } else if (!sweep && delta > 0) {
            delta -= 2 * Math.PI;
        }
        final int segments = (int) Math.ceil(Math.abs(delta) / (Math.PI / 2) - 1e-9);
        final double step = delta / Math.max(segments, 1);
        final double t = 4.0 / 3.0 * Math.tan(step / 4.0);
        double angleStart = theta1;
        for (int i = 0; i < segments; i++) {
            final double angleEnd = angleStart + step;
            final double c1 = Math.cos(angleStart);
            final double s1 = Math.sin(angleStart);
            final double c2 = Math.cos(angleEnd);
            final double s2 = Math.sin(angleEnd);
            final double e1x = c1 - t * s1;
            final double e1y = s1 + t * c1;
            final double e2x = c2 + t * s2;
            final double e2y = s2 - t * c2;
            path.cubicTo(
                    (float) (cx + rx * cosPhi * e1x - ry * sinPhi * e1y),
                    (float) (cy + rx * sinPhi * e1x + ry * cosPhi * e1y),
                    (float) (cx + rx * cosPhi * e2x - ry * sinPhi * e2y),
                    (float) (cy + rx * sinPhi * e2x + ry * cosPhi * e2y),
                    (float) (cx + rx * cosPhi * c2 - ry * sinPhi * s2),
                    (float) (cy + rx * sinPhi * c2 + ry * cosPhi * s2));
            angleStart = angleEnd;
        }
    }
}
