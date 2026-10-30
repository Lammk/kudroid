package android.view.accessibility;

import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Accessibility node data holder. No accessibility service consumes it here, so the node
 * only records what callers set; compat helpers can build and inspect nodes freely.
 */
public class AccessibilityNodeInfo {
    public static final int ACTION_FOCUS = 0x00000001;
    public static final int ACTION_CLEAR_FOCUS = 0x00000002;
    public static final int ACTION_SELECT = 0x00000004;
    public static final int ACTION_CLEAR_SELECTION = 0x00000008;
    public static final int ACTION_CLICK = 0x00000010;
    public static final int ACTION_LONG_CLICK = 0x00000020;
    public static final int ACTION_ACCESSIBILITY_FOCUS = 0x00000040;
    public static final int ACTION_CLEAR_ACCESSIBILITY_FOCUS = 0x00000080;
    public static final int ACTION_NEXT_AT_MOVEMENT_GRANULARITY = 0x00000100;
    public static final int ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY = 0x00000200;
    public static final int ACTION_NEXT_HTML_ELEMENT = 0x00000400;
    public static final int ACTION_PREVIOUS_HTML_ELEMENT = 0x00000800;
    public static final int ACTION_SCROLL_FORWARD = 0x00001000;
    public static final int ACTION_SCROLL_BACKWARD = 0x00002000;
    public static final int ACTION_COPY = 0x00004000;
    public static final int ACTION_PASTE = 0x00008000;
    public static final int ACTION_CUT = 0x00010000;
    public static final int ACTION_SET_SELECTION = 0x00020000;
    public static final int ACTION_EXPAND = 0x00040000;
    public static final int ACTION_COLLAPSE = 0x00080000;
    public static final int ACTION_DISMISS = 0x00100000;
    public static final int ACTION_SET_TEXT = 0x00200000;

    public static final String ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT =
            "ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT";
    public static final String ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN =
            "ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN";
    public static final String ACTION_ARGUMENT_HTML_ELEMENT_STRING =
            "ACTION_ARGUMENT_HTML_ELEMENT_STRING";
    public static final String ACTION_ARGUMENT_SELECTION_START_INT =
            "ACTION_ARGUMENT_SELECTION_START_INT";
    public static final String ACTION_ARGUMENT_SELECTION_END_INT =
            "ACTION_ARGUMENT_SELECTION_END_INT";
    public static final String ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE =
            "ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE";
    public static final String ACTION_ARGUMENT_ROW_INT = "android.view.accessibility.action.ARGUMENT_ROW_INT";
    public static final String ACTION_ARGUMENT_COLUMN_INT = "android.view.accessibility.action.ARGUMENT_COLUMN_INT";
    public static final String ACTION_ARGUMENT_PROGRESS_VALUE = "android.view.accessibility.action.ARGUMENT_PROGRESS_VALUE";
    public static final String ACTION_ARGUMENT_MOVE_WINDOW_X = "ACTION_ARGUMENT_MOVE_WINDOW_X";
    public static final String ACTION_ARGUMENT_MOVE_WINDOW_Y = "ACTION_ARGUMENT_MOVE_WINDOW_Y";

    public static final int FOCUS_INPUT = 1;
    public static final int FOCUS_ACCESSIBILITY = 2;

    public static final int MOVEMENT_GRANULARITY_CHARACTER = 0x00000001;
    public static final int MOVEMENT_GRANULARITY_WORD = 0x00000002;
    public static final int MOVEMENT_GRANULARITY_LINE = 0x00000004;
    public static final int MOVEMENT_GRANULARITY_PARAGRAPH = 0x00000008;
    public static final int MOVEMENT_GRANULARITY_PAGE = 0x00000010;

    public static final int UNDEFINED_SELECTION_INDEX = -1;
    public static final int UNDEFINED_ITEM_ID = Integer.MAX_VALUE;

    public static final String EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY =
            "android.view.accessibility.extra.DATA_TEXT_CHARACTER_LOCATION_KEY";
    public static final String EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX =
            "android.view.accessibility.extra.DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX";
    public static final String EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH =
            "android.view.accessibility.extra.DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH";
    public static final int EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_MAX_LENGTH = 20000;

    private static final int FLAG_CHECKABLE = 1 << 0;
    private static final int FLAG_CHECKED = 1 << 1;
    private static final int FLAG_FOCUSABLE = 1 << 2;
    private static final int FLAG_FOCUSED = 1 << 3;
    private static final int FLAG_SELECTED = 1 << 4;
    private static final int FLAG_CLICKABLE = 1 << 5;
    private static final int FLAG_LONG_CLICKABLE = 1 << 6;
    private static final int FLAG_ENABLED = 1 << 7;
    private static final int FLAG_PASSWORD = 1 << 8;
    private static final int FLAG_SCROLLABLE = 1 << 9;
    private static final int FLAG_ACCESSIBILITY_FOCUSED = 1 << 10;
    private static final int FLAG_VISIBLE_TO_USER = 1 << 11;
    private static final int FLAG_EDITABLE = 1 << 12;
    private static final int FLAG_MULTI_LINE = 1 << 13;
    private static final int FLAG_DISMISSABLE = 1 << 14;
    private static final int FLAG_CONTENT_INVALID = 1 << 15;
    private static final int FLAG_CONTEXT_CLICKABLE = 1 << 16;
    private static final int FLAG_IMPORTANT = 1 << 17;
    private static final int FLAG_SHOWING_HINT = 1 << 18;
    private static final int FLAG_HEADING = 1 << 19;
    private static final int FLAG_SCREEN_READER_FOCUSABLE = 1 << 20;
    private static final int FLAG_TEXT_ENTRY_KEY = 1 << 21;

    private int mFlags = FLAG_ENABLED | FLAG_VISIBLE_TO_USER | FLAG_IMPORTANT;
    private int mActions;
    private final ArrayList<AccessibilityAction> mActionList = new ArrayList<>();
    private final ArrayList<AccessibilityNodeInfo> mChildren = new ArrayList<>();
    private final Rect mBoundsInParent = new Rect();
    private final Rect mBoundsInScreen = new Rect();
    private CharSequence mPackageName;
    private CharSequence mClassName;
    private CharSequence mText;
    private CharSequence mHintText;
    private CharSequence mError;
    private CharSequence mContentDescription;
    private CharSequence mPaneTitle;
    private CharSequence mTooltipText;
    private CharSequence mStateDescription;
    private String mViewIdResourceName;
    private int mMovementGranularities;
    private int mTextSelectionStart = UNDEFINED_SELECTION_INDEX;
    private int mTextSelectionEnd = UNDEFINED_SELECTION_INDEX;
    private int mMaxTextLength = -1;
    private int mLiveRegion;
    private int mDrawingOrder;
    private View mSource;
    private View mParent;
    private Bundle mExtras;

    public AccessibilityNodeInfo() {}

    public static AccessibilityNodeInfo obtain() {
        return new AccessibilityNodeInfo();
    }

    public static AccessibilityNodeInfo obtain(View source) {
        AccessibilityNodeInfo info = new AccessibilityNodeInfo();
        info.setSource(source);
        return info;
    }

    public static AccessibilityNodeInfo obtain(View root, int virtualDescendantId) {
        return obtain(root);
    }

    public static AccessibilityNodeInfo obtain(AccessibilityNodeInfo info) {
        AccessibilityNodeInfo copy = new AccessibilityNodeInfo();
        copy.init(info);
        return copy;
    }

    private void init(AccessibilityNodeInfo o) {
        mFlags = o.mFlags;
        mActions = o.mActions;
        mActionList.clear();
        mActionList.addAll(o.mActionList);
        mChildren.clear();
        mChildren.addAll(o.mChildren);
        mBoundsInParent.set(o.mBoundsInParent);
        mBoundsInScreen.set(o.mBoundsInScreen);
        mPackageName = o.mPackageName;
        mClassName = o.mClassName;
        mText = o.mText;
        mHintText = o.mHintText;
        mError = o.mError;
        mContentDescription = o.mContentDescription;
        mPaneTitle = o.mPaneTitle;
        mTooltipText = o.mTooltipText;
        mStateDescription = o.mStateDescription;
        mViewIdResourceName = o.mViewIdResourceName;
        mMovementGranularities = o.mMovementGranularities;
        mTextSelectionStart = o.mTextSelectionStart;
        mTextSelectionEnd = o.mTextSelectionEnd;
        mMaxTextLength = o.mMaxTextLength;
        mLiveRegion = o.mLiveRegion;
        mDrawingOrder = o.mDrawingOrder;
        mSource = o.mSource;
        mParent = o.mParent;
        mExtras = o.mExtras != null ? new Bundle(o.mExtras) : null;
    }

    public void recycle() {}

    private boolean flag(int mask) { return (mFlags & mask) != 0; }

    private void setFlag(int mask, boolean value) {
        if (value) mFlags |= mask;
        else mFlags &= ~mask;
    }

    public void setSource(View source) { mSource = source; }
    public void setSource(View root, int virtualDescendantId) { mSource = root; }
    public AccessibilityNodeInfo findFocus(int focus) { return null; }
    public AccessibilityNodeInfo focusSearch(int direction) { return null; }
    public int getWindowId() { return -1; }
    public int getChildCount() { return mChildren.size(); }
    public AccessibilityNodeInfo getChild(int index) { return mChildren.get(index); }
    public void addChild(View child) {}
    public void addChild(View root, int virtualDescendantId) {}
    public boolean removeChild(View child) { return false; }
    public boolean removeChild(View root, int virtualDescendantId) { return false; }

    public void addAction(int action) { mActions |= action; }

    public void addAction(AccessibilityAction action) {
        if (action != null && !mActionList.contains(action)) mActionList.add(action);
    }

    public void removeAction(int action) { mActions &= ~action; }

    public boolean removeAction(AccessibilityAction action) {
        return action != null && mActionList.remove(action);
    }

    public int getActions() { return mActions; }

    public List<AccessibilityAction> getActionList() {
        return Collections.unmodifiableList(mActionList);
    }

    public void setMovementGranularities(int granularities) { mMovementGranularities = granularities; }
    public int getMovementGranularities() { return mMovementGranularities; }

    public boolean performAction(int action) { return false; }
    public boolean performAction(int action, Bundle arguments) { return false; }

    public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(String text) {
        return Collections.emptyList();
    }

    public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByViewId(String viewId) {
        return Collections.emptyList();
    }

    public AccessibilityNodeInfo getParent() { return null; }
    public void setParent(View parent) { mParent = parent; }
    public void setParent(View root, int virtualDescendantId) { mParent = root; }

    public void getBoundsInParent(Rect outBounds) { outBounds.set(mBoundsInParent); }
    public void setBoundsInParent(Rect bounds) { mBoundsInParent.set(bounds); }
    public void getBoundsInScreen(Rect outBounds) { outBounds.set(mBoundsInScreen); }
    public void setBoundsInScreen(Rect bounds) { mBoundsInScreen.set(bounds); }

    public boolean isCheckable() { return flag(FLAG_CHECKABLE); }
    public void setCheckable(boolean v) { setFlag(FLAG_CHECKABLE, v); }
    public boolean isChecked() { return flag(FLAG_CHECKED); }
    public void setChecked(boolean v) { setFlag(FLAG_CHECKED, v); }
    public boolean isFocusable() { return flag(FLAG_FOCUSABLE); }
    public void setFocusable(boolean v) { setFlag(FLAG_FOCUSABLE, v); }
    public boolean isFocused() { return flag(FLAG_FOCUSED); }
    public void setFocused(boolean v) { setFlag(FLAG_FOCUSED, v); }
    public boolean isVisibleToUser() { return flag(FLAG_VISIBLE_TO_USER); }
    public void setVisibleToUser(boolean v) { setFlag(FLAG_VISIBLE_TO_USER, v); }
    public boolean isAccessibilityFocused() { return flag(FLAG_ACCESSIBILITY_FOCUSED); }
    public void setAccessibilityFocused(boolean v) { setFlag(FLAG_ACCESSIBILITY_FOCUSED, v); }
    public boolean isSelected() { return flag(FLAG_SELECTED); }
    public void setSelected(boolean v) { setFlag(FLAG_SELECTED, v); }
    public boolean isClickable() { return flag(FLAG_CLICKABLE); }
    public void setClickable(boolean v) { setFlag(FLAG_CLICKABLE, v); }
    public boolean isLongClickable() { return flag(FLAG_LONG_CLICKABLE); }
    public void setLongClickable(boolean v) { setFlag(FLAG_LONG_CLICKABLE, v); }
    public boolean isContextClickable() { return flag(FLAG_CONTEXT_CLICKABLE); }
    public void setContextClickable(boolean v) { setFlag(FLAG_CONTEXT_CLICKABLE, v); }
    public boolean isEnabled() { return flag(FLAG_ENABLED); }
    public void setEnabled(boolean v) { setFlag(FLAG_ENABLED, v); }
    public boolean isPassword() { return flag(FLAG_PASSWORD); }
    public void setPassword(boolean v) { setFlag(FLAG_PASSWORD, v); }
    public boolean isScrollable() { return flag(FLAG_SCROLLABLE); }
    public void setScrollable(boolean v) { setFlag(FLAG_SCROLLABLE, v); }
    public boolean isEditable() { return flag(FLAG_EDITABLE); }
    public void setEditable(boolean v) { setFlag(FLAG_EDITABLE, v); }
    public boolean isMultiLine() { return flag(FLAG_MULTI_LINE); }
    public void setMultiLine(boolean v) { setFlag(FLAG_MULTI_LINE, v); }
    public boolean isDismissable() { return flag(FLAG_DISMISSABLE); }
    public void setDismissable(boolean v) { setFlag(FLAG_DISMISSABLE, v); }
    public boolean isContentInvalid() { return flag(FLAG_CONTENT_INVALID); }
    public void setContentInvalid(boolean v) { setFlag(FLAG_CONTENT_INVALID, v); }
    public boolean isImportantForAccessibility() { return flag(FLAG_IMPORTANT); }
    public void setImportantForAccessibility(boolean v) { setFlag(FLAG_IMPORTANT, v); }
    public boolean isShowingHintText() { return flag(FLAG_SHOWING_HINT); }
    public void setShowingHintText(boolean v) { setFlag(FLAG_SHOWING_HINT, v); }
    public boolean isHeading() { return flag(FLAG_HEADING); }
    public void setHeading(boolean v) { setFlag(FLAG_HEADING, v); }
    public boolean isScreenReaderFocusable() { return flag(FLAG_SCREEN_READER_FOCUSABLE); }
    public void setScreenReaderFocusable(boolean v) { setFlag(FLAG_SCREEN_READER_FOCUSABLE, v); }
    public boolean isTextEntryKey() { return flag(FLAG_TEXT_ENTRY_KEY); }
    public void setTextEntryKey(boolean v) { setFlag(FLAG_TEXT_ENTRY_KEY, v); }

    public CharSequence getPackageName() { return mPackageName; }
    public void setPackageName(CharSequence packageName) { mPackageName = packageName; }
    public CharSequence getClassName() { return mClassName; }
    public void setClassName(CharSequence className) { mClassName = className; }
    public CharSequence getText() { return mText; }
    public void setText(CharSequence text) { mText = text; }
    public CharSequence getHintText() { return mHintText; }
    public void setHintText(CharSequence hint) { mHintText = hint; }
    public void setError(CharSequence error) { mError = error; }
    public CharSequence getError() { return mError; }
    public CharSequence getContentDescription() { return mContentDescription; }
    public void setContentDescription(CharSequence d) { mContentDescription = d; }
    public CharSequence getPaneTitle() { return mPaneTitle; }
    public void setPaneTitle(CharSequence title) { mPaneTitle = title; }
    public CharSequence getTooltipText() { return mTooltipText; }
    public void setTooltipText(CharSequence text) { mTooltipText = text; }
    public CharSequence getStateDescription() { return mStateDescription; }
    public void setStateDescription(CharSequence text) { mStateDescription = text; }
    public String getViewIdResourceName() { return mViewIdResourceName; }
    public void setViewIdResourceName(String name) { mViewIdResourceName = name; }
    public int getMaxTextLength() { return mMaxTextLength; }
    public void setMaxTextLength(int max) { mMaxTextLength = max; }
    public int getLiveRegion() { return mLiveRegion; }
    public void setLiveRegion(int mode) { mLiveRegion = mode; }
    public int getDrawingOrder() { return mDrawingOrder; }
    public void setDrawingOrder(int order) { mDrawingOrder = order; }
    public int getTextSelectionStart() { return mTextSelectionStart; }
    public int getTextSelectionEnd() { return mTextSelectionEnd; }

    public void setTextSelection(int start, int end) {
        mTextSelectionStart = start;
        mTextSelectionEnd = end;
    }

    public AccessibilityNodeInfo getLabelFor() { return null; }
    public void setLabelFor(View labeled) {}
    public void setLabelFor(View root, int virtualDescendantId) {}
    public AccessibilityNodeInfo getLabeledBy() { return null; }
    public void setLabeledBy(View label) {}
    public void setLabeledBy(View root, int virtualDescendantId) {}
    public AccessibilityNodeInfo getTraversalBefore() { return null; }
    public void setTraversalBefore(View view) {}
    public void setTraversalBefore(View root, int virtualDescendantId) {}
    public AccessibilityNodeInfo getTraversalAfter() { return null; }
    public void setTraversalAfter(View view) {}
    public void setTraversalAfter(View root, int virtualDescendantId) {}
    public AccessibilityWindowInfo getWindow() { return null; }
    public void setCollectionInfo(CollectionInfo collectionInfo) {}
    public void setCollectionItemInfo(CollectionItemInfo collectionItemInfo) {}
    public void setRangeInfo(RangeInfo rangeInfo) {}
    public void setCanOpenPopup(boolean canOpenPopup) {}
    public void setInputType(int inputType) {}
    public void setAvailableExtraData(List<String> extraDataKeys) {}
    public void refreshWithExtraData(String extraDataKey, Bundle args) {}
    public boolean refresh() { return true; }
    public int describeContents() { return 0; }

    public Bundle getExtras() {
        if (mExtras == null) mExtras = new Bundle();
        return mExtras;
    }

    public boolean hasExtras() {
        return mExtras != null;
    }

    public static class AccessibilityAction {
        public static final AccessibilityAction ACTION_FOCUS = new AccessibilityAction(AccessibilityNodeInfo.ACTION_FOCUS, null);
        public static final AccessibilityAction ACTION_CLEAR_FOCUS = new AccessibilityAction(AccessibilityNodeInfo.ACTION_CLEAR_FOCUS, null);
        public static final AccessibilityAction ACTION_SELECT = new AccessibilityAction(AccessibilityNodeInfo.ACTION_SELECT, null);
        public static final AccessibilityAction ACTION_CLEAR_SELECTION = new AccessibilityAction(AccessibilityNodeInfo.ACTION_CLEAR_SELECTION, null);
        public static final AccessibilityAction ACTION_CLICK = new AccessibilityAction(AccessibilityNodeInfo.ACTION_CLICK, null);
        public static final AccessibilityAction ACTION_LONG_CLICK = new AccessibilityAction(AccessibilityNodeInfo.ACTION_LONG_CLICK, null);
        public static final AccessibilityAction ACTION_ACCESSIBILITY_FOCUS = new AccessibilityAction(AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS, null);
        public static final AccessibilityAction ACTION_CLEAR_ACCESSIBILITY_FOCUS = new AccessibilityAction(AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS, null);
        public static final AccessibilityAction ACTION_NEXT_AT_MOVEMENT_GRANULARITY = new AccessibilityAction(AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, null);
        public static final AccessibilityAction ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY = new AccessibilityAction(AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, null);
        public static final AccessibilityAction ACTION_NEXT_HTML_ELEMENT = new AccessibilityAction(AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null);
        public static final AccessibilityAction ACTION_PREVIOUS_HTML_ELEMENT = new AccessibilityAction(AccessibilityNodeInfo.ACTION_PREVIOUS_HTML_ELEMENT, null);
        public static final AccessibilityAction ACTION_SCROLL_FORWARD = new AccessibilityAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, null);
        public static final AccessibilityAction ACTION_SCROLL_BACKWARD = new AccessibilityAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, null);
        public static final AccessibilityAction ACTION_COPY = new AccessibilityAction(AccessibilityNodeInfo.ACTION_COPY, null);
        public static final AccessibilityAction ACTION_PASTE = new AccessibilityAction(AccessibilityNodeInfo.ACTION_PASTE, null);
        public static final AccessibilityAction ACTION_CUT = new AccessibilityAction(AccessibilityNodeInfo.ACTION_CUT, null);
        public static final AccessibilityAction ACTION_SET_SELECTION = new AccessibilityAction(AccessibilityNodeInfo.ACTION_SET_SELECTION, null);
        public static final AccessibilityAction ACTION_EXPAND = new AccessibilityAction(AccessibilityNodeInfo.ACTION_EXPAND, null);
        public static final AccessibilityAction ACTION_COLLAPSE = new AccessibilityAction(AccessibilityNodeInfo.ACTION_COLLAPSE, null);
        public static final AccessibilityAction ACTION_DISMISS = new AccessibilityAction(AccessibilityNodeInfo.ACTION_DISMISS, null);
        public static final AccessibilityAction ACTION_SET_TEXT = new AccessibilityAction(AccessibilityNodeInfo.ACTION_SET_TEXT, null);
        // Ids of the actions that exist only as AccessibilityAction objects.
        public static final AccessibilityAction ACTION_SHOW_ON_SCREEN = new AccessibilityAction(0x01020036, null);
        public static final AccessibilityAction ACTION_SCROLL_TO_POSITION = new AccessibilityAction(0x01020037, null);
        public static final AccessibilityAction ACTION_SCROLL_UP = new AccessibilityAction(0x01020038, null);
        public static final AccessibilityAction ACTION_SCROLL_LEFT = new AccessibilityAction(0x01020039, null);
        public static final AccessibilityAction ACTION_SCROLL_DOWN = new AccessibilityAction(0x0102003a, null);
        public static final AccessibilityAction ACTION_SCROLL_RIGHT = new AccessibilityAction(0x0102003b, null);
        public static final AccessibilityAction ACTION_CONTEXT_CLICK = new AccessibilityAction(0x0102003c, null);
        public static final AccessibilityAction ACTION_SET_PROGRESS = new AccessibilityAction(0x0102003d, null);
        public static final AccessibilityAction ACTION_MOVE_WINDOW = new AccessibilityAction(0x0102003e, null);
        public static final AccessibilityAction ACTION_SHOW_TOOLTIP = new AccessibilityAction(0x0102003f, null);
        public static final AccessibilityAction ACTION_HIDE_TOOLTIP = new AccessibilityAction(0x01020040, null);
        public static final AccessibilityAction ACTION_PAGE_UP = new AccessibilityAction(0x01020041, null);
        public static final AccessibilityAction ACTION_PAGE_DOWN = new AccessibilityAction(0x01020042, null);
        public static final AccessibilityAction ACTION_PAGE_LEFT = new AccessibilityAction(0x01020043, null);
        public static final AccessibilityAction ACTION_PAGE_RIGHT = new AccessibilityAction(0x01020044, null);

        private final int mActionId;
        private final CharSequence mLabel;

        public AccessibilityAction(int actionId, CharSequence label) {
            mActionId = actionId;
            mLabel = label;
        }

        public int getId() { return mActionId; }
        public CharSequence getLabel() { return mLabel; }

        @Override
        public int hashCode() { return mActionId; }

        @Override
        public boolean equals(Object other) {
            return other instanceof AccessibilityAction
                    && ((AccessibilityAction) other).mActionId == mActionId;
        }
    }

    public static class ExtraRenderingInfo {
        public ExtraRenderingInfo() {}
    }

    public static class TouchDelegateInfo {
        public TouchDelegateInfo() {}
    }

    public static class CollectionInfo {
        public CollectionInfo() {}
    }

    public static class CollectionItemInfo {
        public CollectionItemInfo() {}
    }

    public static class RangeInfo {
        public RangeInfo() {}
    }
}
