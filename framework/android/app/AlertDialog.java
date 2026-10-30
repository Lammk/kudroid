package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.Message;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListAdapter;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.Space;
import android.widget.TextView;

/**
 * Android AlertDialog implementation.
 */
public class AlertDialog extends Dialog implements DialogInterface {
    public static final int THEME_TRADITIONAL = 1;
    public static final int THEME_HOLO_DARK = 2;
    public static final int THEME_HOLO_LIGHT = 3;
    public static final int THEME_DEVICE_DEFAULT_DARK = 4;
    public static final int THEME_DEVICE_DEFAULT_LIGHT = 5;

    private CharSequence mTitle;
    private CharSequence mMessage;
    private Drawable mIcon;
    private View mView;
    private View mCustomTitleView;
    private int mViewSpacingLeft;
    private int mViewSpacingTop;
    private int mViewSpacingRight;
    private int mViewSpacingBottom;

    private final CharSequence[] mButtonText = new CharSequence[3];
    private final OnClickListener[] mButtonListener = new OnClickListener[3];
    private final Button[] mButtons = new Button[3];

    private ListAdapter mAdapter;
    private ListView mListView;
    private int mCheckedItem = -1;
    private boolean[] mCheckedItems;
    private OnClickListener mItemListener;
    private OnMultiChoiceClickListener mMultiListener;

    private TextView mTitleView;
    private TextView mMessageView;
    private FrameLayout mCustomContainer;
    private boolean mBuilt;

    public AlertDialog(Context context) {
        super(context);
    }

    public AlertDialog(Context context, int themeResId) {
        super(context, themeResId);
    }

    protected AlertDialog(Context context, boolean cancelable, OnCancelListener cancelListener) {
        super(context, cancelable, cancelListener);
    }

    private static int whichToIndex(int which) {
        if (which == BUTTON_POSITIVE) return 0;
        if (which == BUTTON_NEGATIVE) return 1;
        if (which == BUTTON_NEUTRAL) return 2;
        return -1;
    }

    private void build() {
        if (mBuilt) return;
        mBuilt = true;

        Context context = getContext();
        float density = 1.0f;
        if (context.getResources() != null && context.getResources().getDisplayMetrics() != null) {
            density = context.getResources().getDisplayMetrics().density;
        }
        int pad8 = (int) (8 * density);
        int pad16 = (int) (16 * density);
        int pad24 = (int) (24 * density);

        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(0, pad24, 0, pad8);

        // Title
        int alertTitleId = 0x01020027;
        if (mCustomTitleView != null) {
            root.addView(mCustomTitleView);
        } else {
            mTitleView = new TextView(context);
            mTitleView.setId(alertTitleId);
            if (mTitle != null && mTitle.length() > 0) {
                mTitleView.setText(mTitle);
                mTitleView.setTextSize(20.0f);
                mTitleView.setPadding(pad24, 0, pad24, pad16);
                root.addView(mTitleView);
            } else {
                mTitleView.setVisibility(View.GONE);
                root.addView(mTitleView);
            }
        }

        // Message
        ScrollView sv = new ScrollView(context);
        mMessageView = new TextView(context);
        mMessageView.setId(android.R.id.message);
        mMessageView.setPadding(pad24, 0, pad24, pad16);
        mMessageView.setTextSize(16.0f);
        if (mMessage != null && mMessage.length() > 0) {
            mMessageView.setText(mMessage);
        } else {
            mMessageView.setText("");
            sv.setVisibility(View.GONE);
        }
        sv.addView(mMessageView);
        root.addView(sv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // List
        if (mAdapter != null) {
            mListView = new ListView(context);
            mListView.setId(android.R.id.list);
            mListView.setAdapter(mAdapter);
            mListView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
                @Override
                public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                    if (mItemListener != null) {
                        mItemListener.onClick(AlertDialog.this, position);
                    }
                    dismiss();
                }
            });
            root.addView(mListView, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        // Custom view
        if (mView != null) {
            mCustomContainer = new FrameLayout(context);
            mCustomContainer.setId(android.R.id.custom);
            if (mView.getParent() instanceof ViewGroup) {
                ((ViewGroup) mView.getParent()).removeView(mView);
            }
            if (mViewSpacingLeft != 0 || mViewSpacingTop != 0 || mViewSpacingRight != 0 || mViewSpacingBottom != 0) {
                mView.setPadding(mViewSpacingLeft, mViewSpacingTop, mViewSpacingRight, mViewSpacingBottom);
            }
            mCustomContainer.addView(mView, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            root.addView(mCustomContainer, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        // Buttons
        LinearLayout buttonBar = new LinearLayout(context);
        buttonBar.setOrientation(LinearLayout.HORIZONTAL);
        buttonBar.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        buttonBar.setPadding(pad16, pad8, pad16, pad8);

        int[] buttonIds = { android.R.id.button1, android.R.id.button2, android.R.id.button3 };
        int[] whichArray = { BUTTON_POSITIVE, BUTTON_NEGATIVE, BUTTON_NEUTRAL };

        for (int k = 0; k < 3; k++) {
            mButtons[k] = new Button(context);
            mButtons[k].setId(buttonIds[k]);
            if (mButtonText[k] != null && mButtonText[k].length() > 0) {
                mButtons[k].setText(mButtonText[k]);
                mButtons[k].setVisibility(View.VISIBLE);
            } else {
                mButtons[k].setVisibility(View.GONE);
            }
            final int which = whichArray[k];
            final int idx = k;
            mButtons[k].setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (mButtonListener[idx] != null) {
                        mButtonListener[idx].onClick(AlertDialog.this, which);
                    }
                    dismiss();
                }
            });
        }

        LinearLayout.LayoutParams lpNeutral = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        buttonBar.addView(mButtons[2], lpNeutral);

        Space spacer = new Space(context);
        buttonBar.addView(spacer, new LinearLayout.LayoutParams(0, 1, 1.0f));

        LinearLayout.LayoutParams lpNeg = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lpNeg.leftMargin = pad8;
        buttonBar.addView(mButtons[1], lpNeg);

        LinearLayout.LayoutParams lpPos = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lpPos.leftMargin = pad8;
        buttonBar.addView(mButtons[0], lpPos);

        root.addView(buttonBar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        setContentView(root);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        build();
    }

    @Override
    public <T extends View> T findViewById(int id) {
        build();
        if (id == android.R.id.message && mMessageView != null) {
            return (T) mMessageView;
        }
        if (id == android.R.id.button1 && mButtons[0] != null) {
            return (T) mButtons[0];
        }
        if (id == android.R.id.button2 && mButtons[1] != null) {
            return (T) mButtons[1];
        }
        if (id == android.R.id.button3 && mButtons[2] != null) {
            return (T) mButtons[2];
        }
        if ((id == android.R.id.title || id == 0x01020027) && mTitleView != null) {
            return (T) mTitleView;
        }
        if (id == android.R.id.list && mListView != null) {
            return (T) mListView;
        }
        if (id == android.R.id.custom && mCustomContainer != null) {
            return (T) mCustomContainer;
        }
        return super.findViewById(id);
    }

    @Override
    public void setTitle(CharSequence title) {
        super.setTitle(title);
        mTitle = title;
        if (mTitleView != null) {
            mTitleView.setText(title);
            mTitleView.setVisibility(title != null && title.length() > 0 ? View.VISIBLE : View.GONE);
        }
    }

    public void setMessage(CharSequence message) {
        mMessage = message;
        if (mMessageView != null) {
            mMessageView.setText(message != null ? message : "");
            if (mMessageView.getParent() instanceof View) {
                ((View) mMessageView.getParent()).setVisibility(
                        message != null && message.length() > 0 ? View.VISIBLE : View.GONE);
            }
        }
    }

    public void setCustomTitle(View customTitleView) {
        mCustomTitleView = customTitleView;
    }

    public void setView(View view) {
        mView = view;
        if (mCustomContainer != null) {
            mCustomContainer.removeAllViews();
            if (view != null) {
                mCustomContainer.addView(view);
            }
        }
    }

    public void setView(View view, int viewSpacingLeft, int viewSpacingTop, int viewSpacingRight, int viewSpacingBottom) {
        mView = view;
        mViewSpacingLeft = viewSpacingLeft;
        mViewSpacingTop = viewSpacingTop;
        mViewSpacingRight = viewSpacingRight;
        mViewSpacingBottom = viewSpacingBottom;
        if (view != null && (viewSpacingLeft != 0 || viewSpacingTop != 0 || viewSpacingRight != 0 || viewSpacingBottom != 0)) {
            view.setPadding(viewSpacingLeft, viewSpacingTop, viewSpacingRight, viewSpacingBottom);
        }
        if (mCustomContainer != null) {
            mCustomContainer.removeAllViews();
            if (view != null) {
                mCustomContainer.addView(view);
            }
        }
    }

    public View getView() {
        return mView;
    }

    public void setIcon(int resId) {
        if (resId != 0 && getContext() != null) {
            mIcon = getContext().getDrawable(resId);
        }
    }

    public void setIcon(Drawable icon) {
        mIcon = icon;
    }

    public void setIconAttribute(int attrId) {}

    public Button getButton(int whichButton) {
        build();
        int idx = whichToIndex(whichButton);
        return (idx >= 0) ? mButtons[idx] : null;
    }

    public ListView getListView() {
        build();
        return mListView;
    }

    public void setButton(int whichButton, CharSequence text, Message msg) {
        setButton(whichButton, text, (OnClickListener) null);
    }

    public void setButton(int whichButton, CharSequence text, OnClickListener listener) {
        int idx = whichToIndex(whichButton);
        if (idx >= 0) {
            mButtonText[idx] = text;
            mButtonListener[idx] = listener;
            if (mButtons[idx] != null) {
                mButtons[idx].setText(text != null ? text : "");
                mButtons[idx].setVisibility(text != null && text.length() > 0 ? View.VISIBLE : View.GONE);
            }
        }
    }

    public void setButton(CharSequence text, OnClickListener listener) {
        setButton(BUTTON_POSITIVE, text, listener);
    }

    public void setButton2(CharSequence text, OnClickListener listener) {
        setButton(BUTTON_NEGATIVE, text, listener);
    }

    public void setButton3(CharSequence text, OnClickListener listener) {
        setButton(BUTTON_NEUTRAL, text, listener);
    }

    /**
     * Builder class for creating AlertDialog instances.
     */
    public static class Builder {
        private final Context mContext;
        private final AlertDialog mDialog;

        public Builder(Context context) {
            this(context, 0);
        }

        public Builder(Context context, int themeResId) {
            mContext = context;
            mDialog = new AlertDialog(context, themeResId);
        }

        public Context getContext() {
            return mContext;
        }

        public Builder setTitle(int titleId) {
            mDialog.setTitle(titleId);
            return this;
        }

        public Builder setTitle(CharSequence title) {
            mDialog.setTitle(title);
            return this;
        }

        public Builder setCustomTitle(View customTitleView) {
            mDialog.setCustomTitle(customTitleView);
            return this;
        }

        public Builder setMessage(int messageId) {
            mDialog.setMessage(mContext.getText(messageId));
            return this;
        }

        public Builder setMessage(CharSequence message) {
            mDialog.setMessage(message);
            return this;
        }

        public Builder setIcon(int iconId) {
            mDialog.setIcon(iconId);
            return this;
        }

        public Builder setIcon(Drawable icon) {
            mDialog.setIcon(icon);
            return this;
        }

        public Builder setIconAttribute(int attrId) {
            mDialog.setIconAttribute(attrId);
            return this;
        }

        public Builder setPositiveButton(int textId, OnClickListener listener) {
            mDialog.setButton(BUTTON_POSITIVE, mContext.getText(textId), listener);
            return this;
        }

        public Builder setPositiveButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_POSITIVE, text, listener);
            return this;
        }

        public Builder setNegativeButton(int textId, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEGATIVE, mContext.getText(textId), listener);
            return this;
        }

        public Builder setNegativeButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEGATIVE, text, listener);
            return this;
        }

        public Builder setNeutralButton(int textId, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEUTRAL, mContext.getText(textId), listener);
            return this;
        }

        public Builder setNeutralButton(CharSequence text, OnClickListener listener) {
            mDialog.setButton(BUTTON_NEUTRAL, text, listener);
            return this;
        }

        public Builder setCancelable(boolean cancelable) {
            mDialog.setCancelable(cancelable);
            return this;
        }

        public Builder setOnCancelListener(OnCancelListener onCancelListener) {
            mDialog.setOnCancelListener(onCancelListener);
            return this;
        }

        public Builder setOnDismissListener(OnDismissListener onDismissListener) {
            mDialog.setOnDismissListener(onDismissListener);
            return this;
        }

        public Builder setOnKeyListener(OnKeyListener onKeyListener) {
            mDialog.setOnKeyListener(onKeyListener);
            return this;
        }

        public Builder setItems(int itemsId, OnClickListener listener) {
            if (mContext != null && itemsId != 0) {
                return setItems(mContext.getResources().getTextArray(itemsId), listener);
            }
            return this;
        }

        public Builder setItems(CharSequence[] items, OnClickListener listener) {
            mDialog.mAdapter = new android.widget.ArrayAdapter<CharSequence>(
                    mContext, android.R.layout.simple_list_item_1, items);
            mDialog.mItemListener = listener;
            return this;
        }

        public Builder setAdapter(ListAdapter adapter, OnClickListener listener) {
            mDialog.mAdapter = adapter;
            mDialog.mItemListener = listener;
            return this;
        }

        public Builder setSingleChoiceItems(int itemsId, int checkedItem, OnClickListener listener) {
            if (mContext != null && itemsId != 0) {
                return setSingleChoiceItems(mContext.getResources().getTextArray(itemsId), checkedItem, listener);
            }
            return this;
        }

        public Builder setSingleChoiceItems(CharSequence[] items, int checkedItem, OnClickListener listener) {
            mDialog.mCheckedItem = checkedItem;
            mDialog.mItemListener = listener;
            mDialog.mAdapter = new android.widget.ArrayAdapter<CharSequence>(
                    mContext, android.R.layout.simple_list_item_single_choice, items);
            return this;
        }

        public Builder setSingleChoiceItems(ListAdapter adapter, int checkedItem, OnClickListener listener) {
            mDialog.mCheckedItem = checkedItem;
            mDialog.mAdapter = adapter;
            mDialog.mItemListener = listener;
            return this;
        }

        public Builder setMultiChoiceItems(int itemsId, boolean[] checkedItems, DialogInterface.OnMultiChoiceClickListener listener) {
            if (mContext != null && itemsId != 0) {
                return setMultiChoiceItems(mContext.getResources().getTextArray(itemsId), checkedItems, listener);
            }
            return this;
        }

        public Builder setMultiChoiceItems(CharSequence[] items, boolean[] checkedItems, DialogInterface.OnMultiChoiceClickListener listener) {
            mDialog.mCheckedItems = checkedItems;
            mDialog.mMultiListener = listener;
            mDialog.mAdapter = new android.widget.ArrayAdapter<CharSequence>(
                    mContext, android.R.layout.simple_list_item_multiple_choice, items);
            return this;
        }

        public Builder setView(int layoutResId) {
            return this;
        }

        public Builder setView(View view) {
            mDialog.setView(view);
            return this;
        }

        public Builder setView(View view, int viewSpacingLeft, int viewSpacingTop, int viewSpacingRight, int viewSpacingBottom) {
            mDialog.setView(view, viewSpacingLeft, viewSpacingTop, viewSpacingRight, viewSpacingBottom);
            return this;
        }

        public AlertDialog create() {
            return mDialog;
        }

        public AlertDialog show() {
            mDialog.show();
            return mDialog;
        }
    }
}
