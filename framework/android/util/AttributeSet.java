package android.util;

/**
 * Minimal android.util.AttributeSet implementation.
 *
 * A collection of XML attributes. For KuDroid's minimal framework, this is a
 * stub that returns defaults.
 */
public interface AttributeSet {
    /**
     * Return the number of attributes.
     */
    int getAttributeCount();

    /**
     * Return an attribute name.
     */
    String getAttributeName(int index);

    /**
     * Return an attribute value.
     */
    String getAttributeValue(int index);

    /**
     * Return an attribute value by name.
     */
    String getAttributeValue(String namespace, String name);

    /**
     * Return an int attribute.
     */
    int getAttributeIntValue(String namespace, String name, int defaultValue);

    /**
     * Return a boolean attribute.
     */
    boolean getAttributeBooleanValue(String namespace, String name, boolean defaultValue);

    /**
     * Return a float attribute.
     */
    float getAttributeFloatValue(String namespace, String name, float defaultValue);
}
