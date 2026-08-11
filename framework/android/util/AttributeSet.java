package android.util;

/**
 * triển khai android.util.attributeset tối thiểu.
 *
 * một bộ sưu tập các thuộc tính xml. đối với khuôn khổ tối thiểu của kudroid, đây là một
 * mô phỏng trả về các giá trị mặc định.
 */
public interface AttributeSet {
    /**
     * trả về số lượng các thuộc tính.
     */
    int getAttributeCount();

    /**
     * trả về tên thuộc tính.
     */
    String getAttributeName(int index);

    /**
     * trả về giá trị thuộc tính.
     */
    String getAttributeValue(int index);

    /**
     * trả về giá trị thuộc tính theo tên.
     */
    String getAttributeValue(String namespace, String name);

    /**
     * trả về một thuộc tính số nguyên (int).
     */
    int getAttributeIntValue(String namespace, String name, int defaultValue);

    /**
     * trả về một thuộc tính boolean.
     */
    boolean getAttributeBooleanValue(String namespace, String name, boolean defaultValue);

    /**
     * trả về một thuộc tính float.
     */
    float getAttributeFloatValue(String namespace, String name, float defaultValue);
}
