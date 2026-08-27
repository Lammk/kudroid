package android.util;

/**
 * minimal android.util.attributeset implementation.
 *
 * a collection of xml attributes. for kudroid minimal framework, here is one
 * simulation returns default values.
 */
public interface AttributeSet {
    /**
     * returns the number of attributes.
     */
    int getAttributeCount();

    /**
     * returns the attribute name.
     */
    String getAttributeName(int index);

    /**
     * returns the attribute value.
     */
    String getAttributeValue(int index);

    /**
     * returns attribute value by name.
     */
    String getAttributeValue(String namespace, String name);

    /**
     * returns an integer property.
     */
    int getAttributeIntValue(String namespace, String name, int defaultValue);

    /**
     * returns a boolean property.
     */
    boolean getAttributeBooleanValue(String namespace, String name, boolean defaultValue);

    /**
     * returns a float property.
     */
    float getAttributeFloatValue(String namespace, String name, float defaultValue);
}
