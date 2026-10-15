package android.widget;

/**
 * android.widget.SectionIndexer — for ListView to quickly jump by section (A-Z).
 */
public interface SectionIndexer {
    Object[] getSections();

    int getPositionForSection(int sectionIndex);

    int getSectionForPosition(int position);
}
