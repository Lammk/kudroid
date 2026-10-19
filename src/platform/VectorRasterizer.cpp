#include "kudroid/platform/VectorRasterizer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>

namespace {
constexpr double pi = 3.14159265358979323846;
struct Point { double x = 0, y = 0; };

// Parse SVG path numbers without consuming adjacent command letters.
class PathReader {
public:
    explicit PathReader(const std::string& text) : cursor(text.c_str()) {}
    bool read(CGMutablePathRef path) {
        char command = 0, previous = 0;
        Point current, start, control;
        while (*skip()) {
            if (letter(*cursor)) command = *cursor++;
            else if (command == 0) return false;
            const char op = command >= 'a' && command <= 'z' ? command - 32 : command;
            const bool relative = command >= 'a' && command <= 'z';
            if (!started && op != 'M') return false;
            if (op == 'Z') {
                CGPathCloseSubpath(path); current = start; control = current;
                previous = op; command = 0; continue;
            }
            int count = op == 'H' || op == 'V' ? 1 : op == 'M' || op == 'L' || op == 'T' ? 2 :
                    op == 'S' || op == 'Q' ? 4 : op == 'C' ? 6 : op == 'A' ? 7 : 0;
            if (!count) return false;
            double n[7]{};
            for (int i = 0; i < count; ++i) {
                if (op == 'A' && (i == 3 || i == 4)) {
                    skip(); if (*cursor != '0' && *cursor != '1') return false;
                    n[i] = *cursor++ - '0';
                } else if (!number(n[i])) return false;
            }
            Point base = relative ? current : Point{};
            auto point = [&](int i) { return Point{base.x + n[i], base.y + n[i + 1]}; };
            Point next = current;
            if (op == 'M' || op == 'L') {
                next = point(0);
                if (op == 'M') { CGPathMoveToPoint(path, nullptr, next.x, next.y); start = next; started = true;
                    command = relative ? 'l' : 'L'; }
                else CGPathAddLineToPoint(path, nullptr, next.x, next.y);
            } else if (op == 'H' || op == 'V') {
                if (op == 'H') next.x = base.x + n[0]; else next.y = base.y + n[0];
                CGPathAddLineToPoint(path, nullptr, next.x, next.y);
            } else if (op == 'C' || op == 'S') {
                Point first = op == 'C' ? point(0) :
                        previous == 'C' || previous == 'S' ? Point{2 * current.x - control.x, 2 * current.y - control.y} : current;
                Point second = point(op == 'C' ? 2 : 0); next = point(op == 'C' ? 4 : 2);
                CGPathAddCurveToPoint(path, nullptr, first.x, first.y, second.x, second.y, next.x, next.y);
                control = second;
            } else if (op == 'Q' || op == 'T') {
                Point first = op == 'Q' ? point(0) :
                        previous == 'Q' || previous == 'T' ? Point{2 * current.x - control.x, 2 * current.y - control.y} : current;
                next = point(op == 'Q' ? 2 : 0);
                CGPathAddQuadCurveToPoint(path, nullptr, first.x, first.y, next.x, next.y); control = first;
            } else if (op == 'A') {
                next = point(5);
                if (!arc(path, current, next, n[0], n[1], n[2], n[3] != 0, n[4] != 0)) return false;
            }
            if (!std::isfinite(next.x) || !std::isfinite(next.y)) return false;
            current = next; previous = op;
        }
        return started;
    }
private:
    const char* cursor;
    bool started = false;
    static bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
    const char* skip() { while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') ++cursor; return cursor; }
    bool number(double& out) {
        skip(); const char* begin = cursor;
        if (*cursor == '+' || *cursor == '-') ++cursor;
        bool digits = false;
        while (*cursor >= '0' && *cursor <= '9') { ++cursor; digits = true; }
        if (*cursor == '.') { ++cursor; while (*cursor >= '0' && *cursor <= '9') { ++cursor; digits = true; } }
        if (!digits) return false;
        if (*cursor == 'e' || *cursor == 'E') {
            ++cursor; if (*cursor == '+' || *cursor == '-') ++cursor;
            const char* exponent = cursor;
            while (*cursor >= '0' && *cursor <= '9') ++cursor;
            if (exponent == cursor) return false;
        }
        // XML path numbers always use a dot, independent of the process locale.
        const char* p = begin;
        const bool negative = *p == '-';
        if (*p == '-' || *p == '+') ++p;
        long double magnitude = 0;
        int fractional = 0;
        bool afterDot = false;
        while (p < cursor && *p != 'e' && *p != 'E') {
            if (*p == '.') afterDot = true;
            else { magnitude = magnitude * 10 + (*p - '0'); if (afterDot) ++fractional; }
            ++p;
        }
        int exponent = 0;
        if (p < cursor) {
            ++p; const bool minus = *p == '-';
            if (*p == '-' || *p == '+') ++p;
            while (p < cursor) { exponent = std::min(10000, exponent * 10 + (*p - '0')); ++p; }
            if (minus) exponent = -exponent;
        }
        out = static_cast<double>(magnitude * std::pow(10.0L, exponent - fractional));
        if (negative) out = -out;
        return std::isfinite(out) && std::abs(out) < 1e12;
    }
    static bool arc(CGMutablePathRef path, Point from, Point to, double rx, double ry,
                    double rotation, bool large, bool sweep) {
        if (from.x == to.x && from.y == to.y) return true;
        rx = std::abs(rx); ry = std::abs(ry);
        if (rx == 0 || ry == 0) { CGPathAddLineToPoint(path, nullptr, to.x, to.y); return true; }
        const double phi = std::fmod(rotation, 360) * pi / 180, c = std::cos(phi), s = std::sin(phi);
        const double dx = (from.x - to.x) / 2, dy = (from.y - to.y) / 2;
        const double xp = c * dx + s * dy, yp = -s * dx + c * dy;
        const double scale = xp * xp / (rx * rx) + yp * yp / (ry * ry);
        if (scale > 1) { rx *= std::sqrt(scale); ry *= std::sqrt(scale); }
        const double den = rx * rx * yp * yp + ry * ry * xp * xp;
        if (!std::isfinite(den) || den <= 0) return false;
        double factor = std::sqrt(std::max(0.0, (rx * rx * ry * ry - den) / den));
        if (large == sweep) factor = -factor;
        const double cxp = factor * rx * yp / ry, cyp = -factor * ry * xp / rx;
        const double cx = c * cxp - s * cyp + (from.x + to.x) / 2;
        const double cy = s * cxp + c * cyp + (from.y + to.y) / 2;
        const double ux = (xp - cxp) / rx, uy = (yp - cyp) / ry;
        const double vx = (-xp - cxp) / rx, vy = (-yp - cyp) / ry;
        double angle = std::atan2(uy, ux), delta = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        if (!sweep && delta > 0) delta -= 2 * pi;
        if (sweep && delta < 0) delta += 2 * pi;
        const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (pi / 4))));
        auto at = [&](double t) { return Point{cx + rx * c * std::cos(t) - ry * s * std::sin(t),
                                              cy + rx * s * std::cos(t) + ry * c * std::sin(t)}; };
        auto derivative = [&](double t) { return Point{-rx * c * std::sin(t) - ry * s * std::cos(t),
                                                       -rx * s * std::sin(t) + ry * c * std::cos(t)}; };
        for (int i = 0; i < segments; ++i) {
            const double end = angle + delta / segments, k = 4.0 / 3 * std::tan((end - angle) / 4);
            Point a = at(angle), b = at(end), da = derivative(angle), db = derivative(end);
            CGPathAddCurveToPoint(path, nullptr, a.x + k * da.x, a.y + k * da.y,
                                 b.x - k * db.x, b.y - k * db.y, b.x, b.y); angle = end;
        }
        return true;
    }
};
struct FlatPath {
    std::vector<Point> points;
    Point current, start;
    bool firstContourDone = false;
    void append(Point p) { points.push_back(p); current = p; }
    static void apply(void* context, const CGPathElement* element) {
        auto& f = *static_cast<FlatPath*>(context);
        if (f.firstContourDone) return;
        auto p = [&](int i) { return Point{element->points[i].x, element->points[i].y}; };
        switch (element->type) {
        case kCGPathElementMoveToPoint:
            if (!f.points.empty()) { f.firstContourDone = true; return; }
            f.start = p(0); f.append(f.start); break;
        case kCGPathElementAddLineToPoint: f.append(p(0)); break;
        case kCGPathElementCloseSubpath: f.append(f.start); break;
        case kCGPathElementAddQuadCurveToPoint:
        case kCGPathElementAddCurveToPoint: {
            Point from = f.current, a = p(0), b = p(1);
            const bool cubic = element->type == kCGPathElementAddCurveToPoint;
            Point c = cubic ? p(2) : b;
            // Bounded subdivision for length-based trim operations.
            for (int i = 1; i <= 64; ++i) {
                const double t = i / 64.0, u = 1 - t;
                if (cubic) f.append({u*u*u*from.x + 3*u*u*t*a.x + 3*u*t*t*b.x + t*t*t*c.x,
                                    u*u*u*from.y + 3*u*u*t*a.y + 3*u*t*t*b.y + t*t*t*c.y});
                else f.append({u*u*from.x + 2*u*t*a.x + t*t*b.x, u*u*from.y + 2*u*t*a.y + t*t*b.y});
            }
            break;
        }
        }
    }
};
CGPathRef trimmed(CGPathRef path, double start, double end, double offset) {
    FlatPath flat; CGPathApply(path, &flat, FlatPath::apply);
    CGMutablePathRef result = CGPathCreateMutable();
    double length = 0;
    for (size_t i = 1; i < flat.points.size(); ++i)
        length += std::hypot(flat.points[i].x - flat.points[i-1].x, flat.points[i].y - flat.points[i-1].y);
    auto normalized = [](double x) { return std::fmod(x, 1.0); };
    start = normalized(start + offset) * length; end = normalized(end + offset) * length;
    auto segment = [&](double low, double high) {
        double distance = 0; bool first = true;
        low = std::max(0.0, low); high = std::max(0.0, high);
        for (size_t i = 1; i < flat.points.size() && distance < high; ++i) {
            Point a = flat.points[i-1], b = flat.points[i];
            const double len = std::hypot(b.x - a.x, b.y - a.y), next = distance + len;
            if (len > 0 && next > low && distance < high) {
                const double l = std::max(0.0, (low - distance) / len), h = std::min(1.0, (high - distance) / len);
                if (first) { CGPathMoveToPoint(result, nullptr, a.x + l*(b.x-a.x), a.y + l*(b.y-a.y)); first = false; }
                CGPathAddLineToPoint(result, nullptr, a.x + h*(b.x-a.x), a.y + h*(b.y-a.y));
            }
            distance = next;
        }
    };
    if (start > end) { segment(start, length); segment(0, end); } else segment(start, end);
    return result;
}
void setColor(CGContextRef context, uint32_t argb, double alpha, bool stroke) {
    const double a = ((argb >> 24) & 255) / 255.0 * std::clamp(alpha, 0.0, 1.0);
    if (stroke) CGContextSetRGBStrokeColor(context, ((argb >> 16) & 255) / 255.0,
            ((argb >> 8) & 255) / 255.0, (argb & 255) / 255.0, a);
    else CGContextSetRGBFillColor(context, ((argb >> 16) & 255) / 255.0,
            ((argb >> 8) & 255) / 255.0, (argb & 255) / 255.0, a);
}
}
#endif

namespace kudroid {
bool rasterize_vector(const std::vector<std::string>& paths, const float* commands,
                      const uint32_t* colors, int width, int height,
                      float viewport_width, float viewport_height, uint32_t* pixels) {
#if defined(__APPLE__)
    if (!pixels || !commands || !colors || width <= 0 || height <= 0 ||
        !std::isfinite(viewport_width) || !std::isfinite(viewport_height) || viewport_width <= 0 || viewport_height <= 0)
        return false;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(pixels, width, height, 8, static_cast<size_t>(width) * 4,
            space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!context) return false;
    CGContextTranslateCTM(context, 0, height); CGContextScaleCTM(context, 1, -1);
    CGContextScaleCTM(context, width / viewport_width, height / viewport_height);
    CGContextSetShouldAntialias(context, true);
    bool valid = true; int depth = 0;
    for (size_t i = 0; i < paths.size() && valid; ++i) {
        const float* c = commands + i * 16;
        for (int n = 0; n < 16; ++n) if (!std::isfinite(c[n])) { valid = false; break; }
        if (!valid) break;
        if (c[0] == 2) {
            if (++depth > 256) { valid = false; break; }
            CGContextSaveGState(context);
            CGContextConcatCTM(context, CGAffineTransformMake(c[1], c[2], c[3], c[4], c[5], c[6]));
        } else if (c[0] == 3) {
            if (depth <= 0) { valid = false; break; }
            --depth; CGContextRestoreGState(context);
        } else if (c[0] == 0 || c[0] == 1) {
            CGMutablePathRef path = CGPathCreateMutable();
            valid = PathReader(paths[i]).read(path);
            CGPathRef rendered = path;
            if (valid && c[0] == 0 && (c[8] != 0 || c[9] != 1)) rendered = trimmed(path, c[8], c[9], c[10]);
            if (valid) {
                CGContextAddPath(context, rendered);
                if (c[0] == 1) {
                    if (c[7] == 1) CGContextEOClip(context); else CGContextClip(context);
                } else {
                    setColor(context, colors[i*2], c[1], false);
                    if (c[7] == 1) CGContextEOFillPath(context); else CGContextFillPath(context);
                    if (c[3] > 0 && (colors[i*2+1] >> 24) != 0) {
                        CGContextAddPath(context, rendered); setColor(context, colors[i*2+1], c[2], true);
                        CGContextSetLineWidth(context, c[3]); CGContextSetMiterLimit(context, std::max(1.0f, c[4]));
                        CGContextSetLineCap(context, c[5] == 1 ? kCGLineCapRound : c[5] == 2 ? kCGLineCapSquare : kCGLineCapButt);
                        CGContextSetLineJoin(context, c[6] == 1 ? kCGLineJoinRound : c[6] == 2 ? kCGLineJoinBevel : kCGLineJoinMiter);
                        CGContextStrokePath(context);
                    }
                }
            }
            if (rendered != path) CGPathRelease(rendered);
            CGPathRelease(path);
        } else valid = false;
    }
    valid = valid && depth == 0;
    CGContextRelease(context);
    // Convert premultiplied RGBA bytes to straight Android ARGB values.
    if (valid) for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        const uint8_t* rgba = reinterpret_cast<const uint8_t*>(pixels + i);
        const uint32_t a = rgba[3];
        const uint32_t r = a ? std::min(255u, (rgba[0] * 255u + a / 2) / a) : 0;
        const uint32_t g = a ? std::min(255u, (rgba[1] * 255u + a / 2) / a) : 0;
        const uint32_t b = a ? std::min(255u, (rgba[2] * 255u + a / 2) / a) : 0;
        pixels[i] = a << 24 | r << 16 | g << 8 | b;
    }
    return valid;
#else
    (void)paths; (void)commands; (void)colors; (void)width; (void)height;
    (void)viewport_width; (void)viewport_height; (void)pixels;
    return false;
#endif
}
}
