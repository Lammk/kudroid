package java.util;

/**
 * java.util.ResourceBundle — minimal shape.
 *
 * Only the object-lookup path is needed: Harmony's regex uses a ListResourceBundle
 * subclass as a lazily-built table of predefined character classes (\p{Alpha} and
 * friends), never for localisation.
 */
public abstract class ResourceBundle {
    protected ResourceBundle parent;

    public ResourceBundle() {
    }

    public final Object getObject(String key) {
        Object obj = handleGetObject(key);
        if (obj == null) {
            if (parent != null) {
                obj = parent.getObject(key);
            }
            if (obj == null) {
                throw new MissingResourceException(
                        "Can't find resource for key " + key,
                        getClass().getName(), key);
            }
        }
        return obj;
    }

    public final String getString(String key) {
        return (String) getObject(key);
    }

    public final String[] getStringArray(String key) {
        return (String[]) getObject(key);
    }

    protected abstract Object handleGetObject(String key);

    public abstract Enumeration<String> getKeys();

    public Locale getLocale() {
        return Locale.getDefault();
    }

    protected void setParent(ResourceBundle parent) {
        this.parent = parent;
    }

    public boolean containsKey(String key) {
        return handleGetObject(key) != null;
    }
}
