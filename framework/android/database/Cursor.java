package android.database;

/**
 * triển khai android.database.cursor tối thiểu.
 *
 * cung cấp quyền truy cập vào kết quả truy vấn. đối với khuôn khổ tối thiểu của kudroid, đây là
 * một con trỏ trống (không có hàng).
 */
public interface Cursor {
    /** trả về số hàng. */
    int getCount();

    /** di chuyển đến hàng đầu tiên. */
    boolean moveToFirst();

    /** di chuyển đến hàng tiếp theo. */
    boolean moveToNext();

    /** trả về việc con trỏ đã đóng hay chưa. */
    boolean isClosed();

    /** đóng con trỏ. */
    void close();

    /** trả về một giá trị chuỗi cho cột đã cho. */
    String getString(int columnIndex);

    /** trả về một giá trị số nguyên cho cột đã cho. */
    int getInt(int columnIndex);

    /** trả về một giá trị long cho cột đã cho. */
    long getLong(int columnIndex);

    /** trả về một giá trị float cho cột đã cho. */
    float getFloat(int columnIndex);

    /** trả về một giá trị double cho cột đã cho. */
    double getDouble(int columnIndex);

    /** trả về việc giá trị tại cột đã cho có rỗng hay không. */
    boolean isNull(int columnIndex);
}
