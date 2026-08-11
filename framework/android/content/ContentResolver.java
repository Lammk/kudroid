package android.content;

/**
 * triển khai android.content.contentresolver tối thiểu.
 *
 * cung cấp quyền truy cập vào các nhà cung cấp nội dung. đối với khuôn khổ tối thiểu của kudroid,
 * đây là một mô phỏng trả về null/mặc định.
 */
public class ContentResolver {
    private final Context mContext;

    public ContentResolver(Context context) {
        mContext = context;
    }

    /**
     * trả về bối cảnh mà trình phân giải này được tạo ra.
     */
    public Context getContext() {
        return mContext;
    }

    /**
     * truy vấn một uri nội dung. hiện tại trả về null.
     */
    public android.database.Cursor query(android.net.Uri uri, String[] projection,
                                         String selection, String[] selectionArgs,
                                         String sortOrder) {
        return null;
    }

    /**
     * chèn một hàng. hiện tại trả về null.
     */
    public android.net.Uri insert(android.net.Uri url, android.content.ContentValues values) {
        return null;
    }

    /**
     * xóa các hàng. hiện tại trả về 0.
     */
    public int delete(android.net.Uri url, String where, String[] selectionArgs) {
        return 0;
    }

    /**
     * cập nhật các hàng. hiện tại trả về 0.
     */
    public int update(android.net.Uri uri, android.content.ContentValues values,
                      String where, String[] selectionArgs) {
        return 0;
    }
}
