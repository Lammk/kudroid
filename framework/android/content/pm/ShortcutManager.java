package android.content.pm;

import java.util.ArrayList;
import java.util.List;

public class ShortcutManager {
    public ShortcutManager() {}

    public List<ShortcutInfo> getDynamicShortcuts() {
        return new ArrayList<ShortcutInfo>();
    }

    public List<ShortcutInfo> getManifestShortcuts() {
        return new ArrayList<ShortcutInfo>();
    }

    public List<ShortcutInfo> getPinnedShortcuts() {
        return new ArrayList<ShortcutInfo>();
    }

    public boolean setDynamicShortcuts(List<ShortcutInfo> shortcutInfoList) {
        return true;
    }

    public boolean addDynamicShortcuts(List<ShortcutInfo> shortcutInfoList) {
        return true;
    }

    public void removeDynamicShortcuts(List<String> shortcutIds) {}

    public void removeAllDynamicShortcuts() {}

    public boolean isRequestPinShortcutSupported() {
        return false;
    }

    public int getMaxShortcutCountPerActivity() {
        return 15;
    }
}
