package android.provider;

import android.net.Uri;

public final class MediaStore {
    public static final String AUTHORITY = "media";
    public static final Uri AUTHORITY_URI = Uri.parse("content://" + AUTHORITY);

    private MediaStore() {}

    public static final class Audio {
        private Audio() {}

        public static final class Media {
            public static final Uri INTERNAL_CONTENT_URI = getContentUri("internal");
            public static final Uri EXTERNAL_CONTENT_URI = getContentUri("external");
            public static final String CONTENT_TYPE = "vnd.android.cursor.dir/audio";
            public static final String ENTRY_CONTENT_TYPE = "vnd.android.cursor.item/audio";
            public static final String DEFAULT_SORT_ORDER = "title_key";

            private Media() {}

            public static Uri getContentUri(String volumeName) {
                return AUTHORITY_URI.buildUpon().appendPath(volumeName)
                        .appendPath("audio").appendPath("media").build();
            }
        }
    }
}
