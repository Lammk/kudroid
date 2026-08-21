package android.widget;

/**
 * android.widget.SectionIndexer — cho ListView nhảy nhanh theo section (A-Z).
 */
public interface SectionIndexer {
    Object[] getSections();

    int getPositionForSection(int sectionIndex);

    int getSectionForPosition(int position);
}
