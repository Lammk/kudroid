/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.view;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.util.AttributeSet;

import java.lang.ref.WeakReference;

/**
 * A ViewStub is an invisible, zero-sized View that can be used to lazily inflate layout
 * resources at runtime. When inflated, it replaces itself in its parent with the inflated
 * layout's root, keeping its position and LayoutParams.
 */
public final class ViewStub extends View {
    private int mLayoutResource = 0;
    private int mInflatedId;
    private WeakReference<View> mInflatedViewRef;
    private LayoutInflater mInflater;
    private OnInflateListener mInflateListener;

    public ViewStub(Context context) {
        this(context, 0);
    }

    public ViewStub(Context context, int layoutResource) {
        this(context, null);
        mLayoutResource = layoutResource;
    }

    public ViewStub(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public ViewStub(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context, attrs, defStyleAttr, 0);
    }

    public ViewStub(Context context, AttributeSet attrs, int defStyleAttr, int defStyleRes) {
        super(context);
        final TypedArray a = context.obtainStyledAttributes(attrs,
                com.android.internal.R.styleable.ViewStub, defStyleAttr, defStyleRes);
        try {
            mInflatedId = a.getResourceId(com.android.internal.R.styleable.ViewStub_inflatedId, NO_ID);
            mLayoutResource = a.getResourceId(com.android.internal.R.styleable.ViewStub_layout, 0);
            setId(a.getResourceId(com.android.internal.R.styleable.ViewStub_id, NO_ID));
        } finally {
            a.recycle();
        }
        setVisibility(GONE);
    }

    public int getInflatedId() {
        return mInflatedId;
    }

    public void setInflatedId(int inflatedId) {
        mInflatedId = inflatedId;
    }

    public int getLayoutResource() {
        return mLayoutResource;
    }

    public void setLayoutResource(int layoutResource) {
        mLayoutResource = layoutResource;
    }

    public void setLayoutInflater(LayoutInflater inflater) {
        mInflater = inflater;
    }

    public LayoutInflater getLayoutInflater() {
        return mInflater;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(0, 0);
    }

    @Override
    public void draw(Canvas canvas) {
    }

    @Override
    public void setVisibility(int visibility) {
        if (mInflatedViewRef != null) {
            View view = mInflatedViewRef.get();
            if (view != null) {
                view.setVisibility(visibility);
            } else {
                throw new IllegalStateException("setVisibility called on un-referenced view");
            }
        } else {
            super.setVisibility(visibility);
            if (visibility == VISIBLE || visibility == INVISIBLE) {
                inflate();
            }
        }
    }

    /** Inflate the layout resource, replace this stub in its parent, and return the new view. */
    public View inflate() {
        final ViewParent viewParent = getParent();
        if (viewParent != null && viewParent instanceof ViewGroup) {
            if (mLayoutResource != 0) {
                final ViewGroup parent = (ViewGroup) viewParent;
                final View view = inflateViewNoAdd(parent);
                replaceSelfWithView(view, parent);
                mInflatedViewRef = new WeakReference<View>(view);
                if (mInflateListener != null) {
                    mInflateListener.onInflate(this, view);
                }
                return view;
            } else {
                throw new IllegalArgumentException("ViewStub must have a valid layoutResource");
            }
        } else {
            throw new IllegalStateException("ViewStub must have a non-null ViewGroup viewParent");
        }
    }

    private View inflateViewNoAdd(ViewGroup parent) {
        final LayoutInflater factory = mInflater != null ? mInflater : LayoutInflater.from(getContext());
        final View view = factory.inflate(mLayoutResource, parent, false);
        if (mInflatedId != NO_ID) {
            view.setId(mInflatedId);
        }
        return view;
    }

    private void replaceSelfWithView(View view, ViewGroup parent) {
        final int index = parent.indexOfChild(this);
        parent.removeViewInLayout(this);
        final ViewGroup.LayoutParams layoutParams = getLayoutParams();
        if (layoutParams != null) {
            parent.addView(view, index, layoutParams);
        } else {
            parent.addView(view, index);
        }
    }

    public void setOnInflateListener(OnInflateListener inflateListener) {
        mInflateListener = inflateListener;
    }

    public interface OnInflateListener {
        void onInflate(ViewStub stub, View inflated);
    }
}
