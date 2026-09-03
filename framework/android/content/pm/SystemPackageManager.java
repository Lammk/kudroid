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
            bundle.putString(keys[i], values[i] != null ? values[i] : "");
        }
        if (componentName == null || componentName.isEmpty()) {
            sApplicationMetaData = bundle;
        } else {
            sComponentMetaData.put(componentName, bundle);
        }
    }

    /** Register the package name and activity list the manifest declared. */
    public static synchronized void registerPackage(String packageName, String[] activities) {
        if (packageName != null) sPackageName = packageName;
        sActivities.clear();
        if (activities != null) {
            for (String a : activities) {
                if (a != null && !a.isEmpty()) sActivities.add(a);
            }
        }
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
    public PackageInfo getPackageInfo(String packageName, int flags) throws NameNotFoundException {
        PackageInfo pi = new PackageInfo();
        pi.packageName = packageName;
        pi.applicationInfo = getApplicationInfo(packageName, flags);
        return pi;
    }

    @Override
    public ApplicationInfo getApplicationInfo(String packageName, int flags) throws NameNotFoundException {
        ApplicationInfo ai = new ApplicationInfo();
        ai.packageName = packageName;
        ai.dataDir = "/data/data/" + packageName;
        ai.sourceDir = "/data/app/" + packageName + "/base.apk";
        ai.publicSourceDir = "/data/app/" + packageName + "/base.apk";
        ai.nativeLibraryDir = "/data/app/" + packageName + "/lib/" + android.os.Build.CPU_ABI;
        // Android only fills metaData when GET_META_DATA is asked for, but filling it
        // always is harmless and saves an app that forgot the flag — a common mistake
        // whose symptom (metaData present but empty) is indistinguishable from the
        // manifest not declaring anything.
        ai.metaData = metaDataFor(null);
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
    public int checkPermission(String permName, String pkgName) { return PERMISSION_GRANTED; }

    /**
     * Whether the device has a named feature.
     *
     * "true for everything" is wrong for the features an app acts on rather than merely
     * reports. FEATURE_GAMEPAD is the clearest: a game told a gamepad is present hides its
     * touch controls and then receives no input, because nothing here bridges iOS's
     * GameController.framework. The same reasoning covers the camera and the microphone —
     * both are absent from KuDroid, and claiming them makes an app open a capture session
     * that fails much later.
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
        // No input hardware is reachable; see android.hardware.input.InputManager, which
        // reports the same thing through getInputDeviceIds().
        if (name.equals(FEATURE_GAMEPAD)) {
            return false;
        }
        // No capture path exists for either.
        if (name.equals(FEATURE_CAMERA) || name.equals(FEATURE_MICROPHONE)) {
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
            // parse. Null is the honest answer and is what apps check for.
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
