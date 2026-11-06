package android.app;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.SystemPackageManager;
import android.os.Binder;
import android.os.IBinder;

public final class ServiceBindingTests {
    private static int checks;
    private static final String PACKAGE = "test.services";
    private static final String ACTION = "test.services.BIND";
    private static final String NAME = TestService.class.getName();
    private static void check(boolean ok, String message) {
        checks++;
        if (!ok) throw new AssertionError(message);
    }

    public static final class TestService extends Service {
        static int creates, binds, unbinds, destroys, starts;
        static Intent received;
        public void onCreate() { creates++; }
        public IBinder onBind(Intent intent) { binds++; received = intent; return new Binder(); }
        public boolean onUnbind(Intent intent) { unbinds++; return false; }
        public void onDestroy() { destroys++; }
        public int onStartCommand(Intent intent, int flags, int id) { starts++; return START_NOT_STICKY; }
    }
    public static final class NullService extends Service {
        public IBinder onBind(Intent intent) { return null; }
    }
    private static final class Connection implements ServiceConnection {
        int connected, nullBindings;
        ComponentName component;
        IBinder binder;
        public void onServiceConnected(ComponentName name, IBinder service) {
            connected++; component = name; binder = service;
        }
        public void onServiceDisconnected(ComponentName name) {}
        public void onNullBinding(ComponentName name) { nullBindings++; component = name; }
    }

    public static int run() throws Exception {
        SystemPackageManager.registerPackage(PACKAGE, new String[0]);
        SystemPackageManager.registerService(NAME, true, false, "test.permission");
        SystemPackageManager.registerServiceIntentFilter(NAME, new String[]{ACTION},
                new String[]{"test.category", "android.intent.category.DEFAULT"}, 3, false);
        ApplicationContext context = new ApplicationContext(PACKAGE);
        PackageManager pm = context.getPackageManager();
        Intent request = new Intent(ACTION).setPackage(PACKAGE).putExtra("value", 7);
        ResolveInfo resolved = pm.resolveService(request, 0);
        check(resolved != null && NAME.equals(resolved.serviceInfo.name)
                && "test.permission".equals(resolved.serviceInfo.permission), "resolve action and package");
        check(pm.resolveService(new Intent(ACTION).setPackage("missing.package"), 0) == null,
                "package restriction prevents a false match");
        check(pm.resolveService(new Intent("missing.action").setPackage(PACKAGE), 0) == null,
                "unknown actions remain unavailable");
        check(pm.resolveService(new Intent(request).addCategory("missing.category"), 0) == null,
                "all requested categories must match");
        check(pm.resolveService(new Intent(request).addCategory("test.category"),
                PackageManager.MATCH_DEFAULT_ONLY) != null, "declared categories resolve");
        check(pm.resolveService(new Intent(request).setData(android.net.Uri.parse("https://example.com")), 0) == null,
                "an action-only filter does not match a data intent");
        SystemPackageManager.registerService("test.services.Priority", true, true, "");
        SystemPackageManager.registerServiceIntentFilter("test.services.Priority", new String[]{ACTION},
                new String[0], 4, false);
        check("test.services.Priority".equals(pm.resolveService(request, 0).serviceInfo.name),
                "higher-priority service resolves first");
        ComponentName priority = new ComponentName(PACKAGE, "test.services.Priority");
        pm.setComponentEnabledSetting(priority, PackageManager.COMPONENT_ENABLED_STATE_DISABLED, 0);
        check(NAME.equals(pm.resolveService(request, 0).serviceInfo.name), "disabled components do not resolve");
        check(NAME.equals(new ApplicationContext(PACKAGE).getPackageManager()
                .resolveService(request, 0).serviceInfo.name), "enabled settings are shared across contexts");
        ComponentName component = new ComponentName(PACKAGE, NAME);
        check(pm.resolveService(new Intent("different").setComponent(component), 0) != null,
                "explicit components do not require a filter match");
        try {
            pm.getServiceInfo(new ComponentName(PACKAGE, "missing.Service"), 0);
            check(false, "unknown service metadata was fabricated");
        } catch (PackageManager.NameNotFoundException expected) { check(true, "unknown service metadata fails"); }
        ActivityThread thread = new ActivityThread();
        Connection first = new Connection();
        check(thread.bindServiceFromApp(request, first, Context.BIND_AUTO_CREATE, context), "resolved binding accepted");
        check(first.connected == 1 && NAME.equals(first.component.getClassName())
                && PACKAGE.equals(first.component.getPackageName()) && first.binder != null,
                "connection receives resolved component and real binder");
        check(TestService.creates == 1 && TestService.binds == 1
                && TestService.received.getIntExtra("value", 0) == 7 && request.getComponent() == null,
                "binding preserves request extras without changing caller intent");
        Connection second = new Connection();
        check(thread.bindServiceFromApp(new Intent(request).setComponent(component), second,
                Context.BIND_AUTO_CREATE, context) && second.binder == first.binder && TestService.binds == 1,
                "explicit and action bindings share one service");
        thread.unbindServiceFromApp(first);
        check(TestService.destroys == 0, "remaining connections keep the service alive");
        thread.unbindServiceFromApp(second);
        check(TestService.unbinds == 1 && TestService.destroys == 1, "last unbind destroys an idle service");
        check(thread.startServiceFromApp(request, context) != null && TestService.starts == 1,
                "startService resolves the same action intent");
        check(thread.stopServiceFromApp(request) && TestService.destroys == 2,
                "stopService resolves and destroys the started service");
        Connection absent = new Connection();
        check(!thread.bindServiceFromApp(new Intent("missing.action").setPackage("missing.package"),
                absent, Context.BIND_AUTO_CREATE, context) && absent.connected == 0,
                "unavailable services fail binding synchronously");
        try { thread.unbindServiceFromApp(absent); check(false, "failed binding retained a connection"); }
        catch (IllegalArgumentException expected) { check(true, "failed binding has no connection record"); }
        String nullName = NullService.class.getName();
        SystemPackageManager.registerService(nullName, true, false, "");
        Connection nullConnection = new Connection();
        check(thread.bindServiceFromApp(new Intent().setComponent(new ComponentName(PACKAGE, nullName)),
                nullConnection, Context.BIND_AUTO_CREATE, context) && nullConnection.nullBindings == 1,
                "a declared service returning null completes with onNullBinding");
        thread.unbindServiceFromApp(nullConnection);
        SystemPackageManager.registerPackage(PACKAGE, new String[0], false);
        SystemPackageManager.registerService(NAME, true, false, "");
        pm.setComponentEnabledSetting(component, PackageManager.COMPONENT_ENABLED_STATE_ENABLED, 0);
        check(pm.resolveService(new Intent().setComponent(component), 0) == null,
                "component overrides cannot enable a disabled application");
        SystemPackageManager.registerPackage(PACKAGE, new String[0]);
        check(pm.resolveService(request, 0) == null, "package replacement clears stale services");
        return checks;
    }
}
