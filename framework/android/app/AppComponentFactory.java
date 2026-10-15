package android.app;

import android.content.BroadcastReceiver;
import android.content.ContentProvider;
import android.content.Intent;

/**
 * Factory an app can declare with android:appComponentFactory to intercept every
 * component instantiation the framework performs.
 *
 * This used to be an empty generated stub, which meant KuDroid never instantiated
 * the declared factory at all. That is not a cosmetic gap: the factory is the FIRST
 * guest class Android touches, so its {@code <clinit>} runs before any Application
 * or Activity code. Build tooling depends on that ordering — resource shrinkers,
 * dependency-injection frameworks and string-pool obfuscators put initialisation
 * there — and an app whose factory never ran can find its own static state empty
 * and fail far away from the real cause.
 *
 * The instantiate* methods mirror AOSP: reflect on the name, no-arg constructor,
 * cast to the component type. Subclasses (androidx.core.app.CoreComponentFactory
 * and friends) override them and call back through super.
 */
public class AppComponentFactory {

    public AppComponentFactory() {
    }

    public ClassLoader instantiateClassLoader(ClassLoader cl,
            android.content.pm.ApplicationInfo aInfo) {
        return cl;
    }

    public Application instantiateApplication(ClassLoader cl, String className)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        return (Application) newInstance(cl, className);
    }

    public Activity instantiateActivity(ClassLoader cl, String className, Intent intent)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        return (Activity) newInstance(cl, className);
    }

    public BroadcastReceiver instantiateReceiver(ClassLoader cl, String className, Intent intent)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        return (BroadcastReceiver) newInstance(cl, className);
    }

    public Service instantiateService(ClassLoader cl, String className, Intent intent)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        return (Service) newInstance(cl, className);
    }

    public ContentProvider instantiateProvider(ClassLoader cl, String className)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        return (ContentProvider) newInstance(cl, className);
    }

    /**
     * Load and construct by name.
     *
     * The class loader is honoured when one is supplied so an app that installs its
     * own loader keeps working; KuDroid's ClassLoader.loadClass delegates to
     * Class.forName, which is what actually resolves against the loaded DEX files.
     */
    private static Object newInstance(ClassLoader cl, String className)
            throws ClassNotFoundException, IllegalAccessException, InstantiationException {
        if (className == null || className.isEmpty()) {
            throw new ClassNotFoundException("empty class name");
        }
        Class<?> clazz = (cl != null) ? cl.loadClass(className) : Class.forName(className);
        if (clazz == null) throw new ClassNotFoundException(className);
        return clazz.newInstance();
    }
}
