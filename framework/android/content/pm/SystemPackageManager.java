package android.content.pm;

import android.content.ComponentName;
import android.content.Intent;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * KuDroid's package database.
 *
 * On Android a PackageManager is a thin client of the system's package service,
 * which holds what AndroidManifest.xml declared. KuDroid has no system service, so
 * this class IS the database: the native side parses the manifest at launch and
 * registers the results here through {@link #registerComponentMetaData}, and every
 * PackageManager query is answered from that.
 *
 * The manifest data has to arrive before any component runs, because a component can
 * read it during its own construction. AGDK's GameActivity is the case that made this
 * necessary — its onCreate does
 *
 *   getPackageManager().getActivityInfo(getIntent().getComponent(), GET_META_DATA)
 *       .metaData.getString("android.app.lib_name")
 *
 * to learn which .so holds its renderer. Answering that with an empty ActivityInfo
 * leaves the activity with no native library, so the surface it creates stays blank —
 * a failure that looks like a graphics bug and is not one.
 */
public class SystemPackageManager extends PackageManager {

    /**
     * Manifest meta-data, keyed by fully-qualified component name.
     *
     * Static because there is one manifest per process while SystemPackageManager is
     * constructed per Context — an instance field would mean whichever Context an app
     * happened to ask through decided whether the data existed.
     */
    private static final Map<String, android.os.Bundle> sComponentMetaData =
            new HashMap<String, android.os.Bundle>();

    /** Meta-data declared directly under &lt;application&gt;. */
    private static android.os.Bundle sApplicationMetaData = new android.os.Bundle();

    /** The running package, so a query naming it can be answered. */
    private static String sPackageName = "";

    /** Every activity the manifest declared, in launch order. */
    private static final List<String> sActivities = new ArrayList<String>();

    /**
     * Register one component's meta-data. Called from the native launcher, once per
     * component, before the app starts.
     *
     * `componentName` empty means the &lt;application&gt; element. Values arrive as
     * strings because that is what AXML stores; a caller wanting an int uses
     * Bundle.getInt, which parses on the way out.
     */
    public static synchronized void registerComponentMetaData(String componentName,
                                                             String[] keys,
                                                             String[] values) {
        if (keys == null || values == null) return;
        android.os.Bundle bundle = new android.os.Bundle();
        final int n = Math.min(keys.length, values.length);
        for (int i = 0; i < n; ++i) {
            if (keys[i] == null || keys[i].isEmpty()) continue;
            String v = values[i] != null ? values[i] : "";
            if ("true".equalsIgnoreCase(v)) {
                bundle.putBoolean(keys[i], true);
            } else if ("false".equalsIgnoreCase(v)) {
                bundle.putBoolean(keys[i], false);
            } else if (v.startsWith("@0x") || (v.startsWith("@") && v.length() > 1 && Character.isDigit(v.charAt(1)))) {
                try {
                    String num = v.startsWith("@0x") ? v.substring(3) : v.substring(1);
                    int resId = (int) Long.parseLong(num, v.startsWith("@0x") ? 16 : 10);
                    bundle.putInt(keys[i], resId);
                } catch (Exception ignored) {
                    bundle.putString(keys[i], v);
                }
            } else {
                boolean isNumber = false;
                boolean isFloat = false;
                if (!v.isEmpty()) {
                    int dots = 0;
                    int start = (v.charAt(0) == '-' || v.charAt(0) == '+') ? 1 : 0;
                    if (start < v.length()) {
                        isNumber = true;
                        for (int k = start; k < v.length(); k++) {
                            char c = v.charAt(k);
                            if (c == '.') {
                                dots++;
                                if (dots > 1) { isNumber = false; break; }
                            } else if (c < '0' || c > '9') {
                                isNumber = false;
                                break;
                            }
                        }
                        if (isNumber && dots == 1 && v.length() > start + 1) {
                            isFloat = true;
                        } else if (dots > 0) {
                            isNumber = false;
                        }
                    }
                }
                if (isNumber) {
                    try {
                        if (isFloat) {
                            bundle.putFloat(keys[i], Float.parseFloat(v));
                        } else {
                            bundle.putInt(keys[i], Integer.parseInt(v));
                        }
                    } catch (NumberFormatException notNumber) {
                        bundle.putString(keys[i], v);
                    }
                } else {
                    bundle.putString(keys[i], v);
                }
            }
        }
        storeMetaData(componentName, bundle);
    }

    private static void storeMetaData(String componentName, android.os.Bundle bundle) {
        if (componentName == null || componentName.isEmpty()) {
            sApplicationMetaData = bundle;
        } else {
            sComponentMetaData.put(componentName, bundle);
        }
    }

    private static int resourceId(String reference, android.content.res.Resources resources) {
        String value = reference.startsWith("@") ? reference.substring(1) : reference;
        if (value.startsWith("0x")) return (int) Long.parseLong(value.substring(2), 16);
        if (!value.isEmpty() && Character.isDigit(value.charAt(0))) return (int) Long.parseLong(value);
        return resources.getIdentifier(value, null, sPackageName);
    }

    public static synchronized void registerComponentMetaDataTyped(String componentName,
            String[] keys, String[] values, int[] types) {
        if (keys == null || values == null || types == null) throw new NullPointerException();
        if (keys.length != values.length || keys.length != types.length)
            throw new IllegalArgumentException("Metadata arrays have different lengths");
        android.os.Bundle bundle = new android.os.Bundle();
        android.content.res.Resources resources = android.content.res.Resources.getSystem();
        for (int i = 0; i < keys.length; i++) {
            String key = keys[i];
            if (key == null || key.isEmpty()) continue;
            String value = values[i] == null ? "" : values[i];
            int type = types[i] & 0xff;
            if ((types[i] & 0x100) != 0) {
                bundle.putInt(key, resourceId(value, resources));
            } else if (type == android.util.TypedValue.TYPE_REFERENCE) {
                android.util.TypedValue resolved = new android.util.TypedValue();
                resources.getValue(resourceId(value, resources), resolved, true);
                if (resolved.type == android.util.TypedValue.TYPE_STRING) {
                    bundle.putString(key, resolved.string == null ? null : resolved.string.toString());
                } else if (resolved.type == android.util.TypedValue.TYPE_INT_BOOLEAN) {
                    bundle.putBoolean(key, resolved.data != 0);
                } else if (resolved.type >= android.util.TypedValue.TYPE_FIRST_INT
                        && resolved.type <= android.util.TypedValue.TYPE_LAST_INT) {
                    bundle.putInt(key, resolved.data);
                } else if (resolved.type == android.util.TypedValue.TYPE_FLOAT) {
                    bundle.putFloat(key, resolved.getFloat());
                }
            } else if (type == android.util.TypedValue.TYPE_INT_BOOLEAN) {
                bundle.putBoolean(key, "true".equalsIgnoreCase(value));
            } else if (type >= android.util.TypedValue.TYPE_FIRST_INT
                    && type <= android.util.TypedValue.TYPE_LAST_INT) {
                bundle.putInt(key, (int) Long.parseLong(value));
            } else if (type == android.util.TypedValue.TYPE_FLOAT) {
                bundle.putFloat(key, Float.parseFloat(value));
            } else if (type == android.util.TypedValue.TYPE_STRING || type == 0) {
                bundle.putString(key, value);
            }
        }
        storeMetaData(componentName, bundle);
    }

    private static int sApplicationTheme;
    private static final java.util.HashMap<String, Integer> sActivityThemes = new java.util.HashMap<>();

    public static synchronized void registerTheme(String component, int theme) {
        if (component == null || component.isEmpty()) sApplicationTheme = theme;
        else sActivityThemes.put(component, theme);
    }

    public static synchronized int getRegisteredApplicationTheme() { return sApplicationTheme; }

    private static synchronized int activityTheme(String component) {
        Integer theme = sActivityThemes.get(component);
        return theme == null ? 0 : theme;
    }

    /** Register the package name and activity list the manifest declared. */
    public static synchronized void registerPackage(String packageName, String[] activities) {
        if (packageName != null) sPackageName = packageName;
        sActivities.clear();
        sApplicationTheme = 0;
        sActivityThemes.clear();
        if (activities != null) {
            for (String a : activities) {
                if (a != null && !a.isEmpty()) sActivities.add(a);
            }
        }
    }

    private static int sVersionCode = 1;
    private static String sVersionName = "1.0.0";
    private static long sFirstInstallTime = 0;
    private static long sLastUpdateTime = 0;

    /** Manifest-declared version + install times; 1.0.0/1/0 stays the fallback. */
    public static synchronized void registerPackageInfo(String packageName, int versionCode,
                                                        String versionName, long firstInstallTime,
                                                        long lastUpdateTime) {
        if (packageName != null) sPackageName = packageName;
        sVersionCode = versionCode;
        if (versionName != null) sVersionName = versionName;
        sFirstInstallTime = firstInstallTime;
        sLastUpdateTime = lastUpdateTime;
    }

    public static synchronized String getRegisteredPackageName() {
        return sPackageName;
    }

    /**
     * The meta-data of `componentName`, falling back to the application's.
     *
     * The fallback matches how apps are written rather than how Android stores it: a
     * library documenting a manifest key rarely says which element it belongs under,
     * so across real APKs the same key turns up in either place. Returning the
     * application bundle for an unknown component is more useful than an empty one,
     * and no less correct — Android would have merged nothing, but it would also have
     * had the key in whichever place the app put it.
     */
    private static synchronized android.os.Bundle metaDataFor(String componentName) {
        if (componentName != null) {
            android.os.Bundle b = sComponentMetaData.get(componentName);
            if (b != null) return b;
        }
        return sApplicationMetaData;
    }

    @Override
    public ProviderInfo resolveContentProvider(String authority, int flags) { return null; }

    @Override
    public String getNameForUid(int uid) {
        synchronized (SystemPackageManager.class) {
            return uid == android.os.Process.myUid() && !sPackageName.isEmpty() ? sPackageName : null;
        }
    }

    @Override
    public ActivityInfo getReceiverInfo(ComponentName component, int flags)
            throws NameNotFoundException {
        // The package database has no registered manifest receivers.
        throw new NameNotFoundException(component == null ? "null component" : component.toString());
    }

    @Override
    public android.content.res.Resources getResourcesForApplication(ApplicationInfo info)
            throws NameNotFoundException {
        if (info == null) throw new NullPointerException("info == null");
        synchronized (SystemPackageManager.class) {
            if (!sPackageName.equals(info.packageName) && !"android".equals(info.packageName))
                throw new NameNotFoundException(info.packageName);
        }
        return android.content.res.Resources.getSystem();
    }

    @Override
    public List<PackageInfo> getInstalledPackages(int flags) {
        synchronized (SystemPackageManager.class) {
            List<PackageInfo> packages = new ArrayList<PackageInfo>();
            if (!sPackageName.isEmpty()) {
                try {
                    packages.add(getPackageInfo(sPackageName, flags));
                } catch (NameNotFoundException ignored) {
                    // Only packages with available metadata are listed.
                }
            }
            return packages;
        }
    }

    @Override
    public PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException {
        PackageInfo pi = new PackageInfo();
        pi.packageName = packageName;
        pi.applicationInfo = getApplicationInfo(packageName, flags);
        // Manifest values when plumbed, honest fallback otherwise (Unity splits
        // versionName on "."; null becomes an NPE far from the cause).
        pi.versionName = sVersionName;
        pi.versionCode = sVersionCode;
        pi.firstInstallTime = sFirstInstallTime;
        pi.lastUpdateTime = sLastUpdateTime;
        Signature sig = new Signature(new byte[] { 0x30, (byte) 0x82, 0x01 });
        pi.signatures = new Signature[] { sig };
        pi.signingInfo = new SigningInfo(pi.signatures);
        return pi;
    }

    @Override
    public ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException {
        ApplicationInfo ai = new ApplicationInfo();
        ai.packageName = packageName;
        ai.processName = packageName;
        ai.dataDir = "/data/data/" + packageName;
        ai.sourceDir = "/data/app/" + packageName + "/base.apk";
        ai.publicSourceDir = "/data/app/" + packageName + "/base.apk";
        ai.nativeLibraryDir = "/data/app/" + packageName + "/lib/" + android.os.Build.CPU_ABI;
        // Android only fills metaData when GET_META_DATA is asked for, but filling it
        // always is harmless and saves an app that forgot the flag — a common mistake
        // whose symptom (metaData present but empty) is indistinguishable from the
        // manifest not declaring anything.
        ai.metaData = metaDataFor(null);
        ai.theme = sPackageName.equals(packageName) ? getRegisteredApplicationTheme() : 0;
        return ai;
    }

    /**
     * The ActivityInfo of a declared component.
     *
     * Was absent entirely, so the call was auto-stubbed to null and the caller's
     * `ai.metaData` became a NullPointerException — GameActivity.onCreate died exactly
     * there. Throwing NameNotFoundException for an unknown component is what Android
     * does and what callers are written to expect; returning an empty ActivityInfo
     * instead would hide a wrong component name until something later read a field
     * off it.
     */
    @Override
    public ActivityInfo getActivityInfo(ComponentName component, int flags)
            throws NameNotFoundException {
        if (component == null) throw new NameNotFoundException("null component");
        final String className = component.getClassName();
        if (className == null || className.isEmpty()) {
            throw new NameNotFoundException("component has no class name");
        }

        ActivityInfo ai = new ActivityInfo();
        ai.name = className;
        ai.packageName = component.getPackageName() != null
                ? component.getPackageName() : sPackageName;
        ai.metaData = metaDataFor(className);
        ai.theme = activityTheme(className);
        ai.exported = true;
        ai.enabled = true;
        try {
            ai.applicationInfo = getApplicationInfo(ai.packageName, flags);
        } catch (NameNotFoundException ignored) {
            // The activity is still describable without it.
        }
        return ai;
    }

    @Override
    public ServiceInfo getServiceInfo(ComponentName component, int flags)
            throws NameNotFoundException {
        if (component == null) throw new NameNotFoundException("null component");
        ServiceInfo si = new ServiceInfo();
        si.name = component.getClassName();
        si.packageName = component.getPackageName() != null
                ? component.getPackageName() : sPackageName;
        si.metaData = metaDataFor(si.name);
        return si;
    }

    @Override
    public int checkPermission(String permName, String pkgName) {
        return RuntimePermissions.check(pkgName, permName);
    }

    /**
     * Whether the device has a named feature.
     *
     * "true for everything" is wrong for the features an app acts on rather than merely
     * reports. FEATURE_GAMEPAD is the clearest: a game told a gamepad is present hides its
     * touch controls and then receives no input, because nothing here bridges iOS's
     * GameController.framework.
     *
     * Everything not named below still returns true. That default is deliberate: the
     * overwhelming majority of feature queries gate on graphics or platform capabilities
     * that KuDroid does provide, and a false answer there turns a working app into one that
     * refuses to start.
     */
    @Override
    public boolean hasSystemFeature(String name) {
        if (name == null) {
            return false;
        }
        // The host exposes touch input rather than a television interface.
        if (name.equals("android.software.leanback") ||
                name.equals("android.software.leanback_only") ||
                name.equals("android.hardware.type.television")) {
            return false;
        }
        // No input hardware is reachable; see android.hardware.input.InputManager, which
        // reports the same thing through getInputDeviceIds().
        if (name.equals(FEATURE_GAMEPAD)) {
            return false;
        }
        // Camera and microphone are real, but only the original camera API is bridged:
        // no flash control, and none of the Camera2 capability levels.
        if (name.equals("android.hardware.camera.flash") ||
                name.startsWith("android.hardware.camera.level.") ||
                name.startsWith("android.hardware.camera.capability.")) {
            return false;
        }
        // Low-latency and pro audio describe guarantees KuDroid cannot make: audio goes
        // through a CoreAudio queue whose period is not under the guest's control. An app
        // that believes it has low-latency output sizes its mixer for a deadline it will
        // miss, which is audible as underruns rather than reported as an error.
        if (name.equals(FEATURE_AUDIO_LOW_LATENCY) || name.equals(FEATURE_AUDIO_PRO)) {
            return false;
        }
        return true;
    }

    @Override
    public boolean hasSystemFeature(String name, int version) {
        // The versioned form asks whether the feature is available AT LEAST at `version`.
        // Deferring to the unversioned answer is right for everything KuDroid reports,
        // because none of the features above are version-graded here.
        return hasSystemFeature(name);
    }

    /**
     * Resolve an Intent to the component that handles it.
     *
     * Returned a blank ResolveInfo before, whose activityInfo was null — an app that
     * resolved an Intent and read the result got a NullPointerException instead of
     * either an answer or a null it could check.
     */
    @Override
    public ResolveInfo resolveActivity(Intent intent, int flags) {
        if (intent == null) return null;
        final ComponentName component = intent.getComponent();
        if (component == null) {
            // An implicit Intent needs intent-filter matching, which KuDroid does not
            // parse. The only implicit intents answered are URL ones, which the host opens.
            if (android.app.ActivityThread.externalUrl(intent) != null) {
                ResolveInfo ri = new ResolveInfo();
                ri.activityInfo = new ActivityInfo();
                ri.activityInfo.name = "com.android.browser.BrowserActivity";
                ri.activityInfo.packageName = "com.android.browser";
                return ri;
            }
            return null;
        }
        try {
            ResolveInfo ri = new ResolveInfo();
            ri.activityInfo = getActivityInfo(component, flags);
            return ri;
        } catch (NameNotFoundException e) {
            return null;
        }
    }

    @Override
    public List<ResolveInfo> queryIntentActivities(Intent intent, int flags) {
        ResolveInfo one = resolveActivity(intent, flags);
        if (one == null) return Collections.emptyList();
        List<ResolveInfo> out = new ArrayList<ResolveInfo>(1);
        out.add(one);
        return out;
    }
}
