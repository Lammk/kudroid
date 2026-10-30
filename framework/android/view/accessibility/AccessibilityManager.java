package android.view.accessibility;

import android.content.Context;

public final class AccessibilityManager {
    public AccessibilityManager() {}
    public static AccessibilityManager getInstance(Context context) { return new AccessibilityManager(); }
    public boolean isEnabled() { return false; }
    public boolean isTouchExplorationEnabled() { return false; }
    public java.util.List<android.accessibilityservice.AccessibilityServiceInfo> getEnabledAccessibilityServiceList(int feedbackTypeFlags) {
        return java.util.Collections.emptyList();
    }
}
