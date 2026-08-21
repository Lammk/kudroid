package android.content;

import java.util.Map;

/**
 * triển khai android.content.sharedpreferences tối thiểu.
 *
 * cung cấp một kho lưu trữ khóa-giá trị đơn giản trong bộ nhớ. đối với khuôn khổ tối thiểu của kudroid,
 * dữ liệu không được lưu vào đĩa (trả về giá trị mặc định khi khởi động lại).
 */
public interface SharedPreferences {

    /**
     * lấy một giá trị chuỗi.
     */
    String getString(String key, String defValue);

    /**
     * lấy một giá trị số nguyên.
     */
    int getInt(String key, int defValue);

    /**
     * lấy một giá trị long.
     */
    long getLong(String key, long defValue);

    /**
     * lấy một giá trị float.
     */
    float getFloat(String key, float defValue);

    /**
     * lấy một giá trị boolean.
     */
    boolean getBoolean(String key, boolean defValue);

    /**
     * kiểm tra xem các tùy chọn có chứa một khóa hay không.
     */
    boolean contains(String key);

    /**
     * trả về một trình chỉnh sửa để sửa đổi các tùy chọn.
     */
    Editor edit();

    /**
     * giao diện để sửa đổi các giá trị trong đối tượng sharedpreferences.
     */
    interface Editor {
        Editor putString(String key, String value);
        Editor putInt(String key, int value);
        Editor putLong(String key, long value);
        Editor putFloat(String key, float value);
        Editor putBoolean(String key, boolean value);
        Editor remove(String key);
        Editor clear();
        boolean commit();
        void apply();
    }

    /**
     * Callback khi một key bị thay đổi.
     */
    interface OnSharedPreferenceChangeListener {
        void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key);
    }
}
