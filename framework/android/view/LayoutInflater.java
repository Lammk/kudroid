/*
 * Copyright (C) 2006 The Android Open Source Project
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
import android.content.res.XmlResourceParser;
import android.util.AttributeSet;
import android.util.Xml;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

import java.lang.reflect.Constructor;
import java.util.HashMap;

/**
 * Instantiates a layout XML file into its View objects. Semantics follow AOSP's
 * LayoutInflater: factory order, the "view", "include", "merge", "tag" and
 * "requestFocus" elements, and the attachToRoot rules.
 */
public class LayoutInflater {
    private static final String TAG_MERGE = "merge";
    private static final String TAG_INCLUDE = "include";
    private static final String TAG_REQUEST_FOCUS = "requestFocus";
    private static final String TAG_TAG = "tag";
    private static final String ATTR_LAYOUT = "layout";

    private static final int[] ATTRS_THEME = new int[] {com.android.internal.R.attr.theme};

    // Prefixes tried, in order, for a view tag written without a package.
    private static final String[] CLASS_PREFIX_LIST = {
        "android.widget.", "android.webkit.", "android.app.", "android.view."
    };

    private static final Class<?>[] CONSTRUCTOR_SIGNATURE = new Class<?>[] {
        Context.class, AttributeSet.class
    };

    private static final HashMap<String, Constructor<? extends View>> sConstructorMap =
            new HashMap<String, Constructor<? extends View>>();

    public interface Factory {
        View onCreateView(String name, Context context, AttributeSet attrs);
    }

    public interface Factory2 extends Factory {
        View onCreateView(View parent, String name, Context context, AttributeSet attrs);
    }

    private final Context mContext;
    private Factory mFactory;
    private Factory2 mFactory2;
    private Factory2 mPrivateFactory;
    private boolean mFactorySet;
    // [0] = the Context the next view is created with, [1] = its AttributeSet.
    private final Object[] mConstructorArgs = new Object[2];

    public LayoutInflater() { mContext = null; }

    public LayoutInflater(Context context) { mContext = context; }

    protected LayoutInflater(LayoutInflater original, Context newContext) {
        mContext = newContext;
        mFactory = original.mFactory;
        mFactory2 = original.mFactory2;
        mPrivateFactory = original.mPrivateFactory;
    }

    public static LayoutInflater from(Context context) {
        if (context == null) throw new NullPointerException();
        LayoutInflater inflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        if (inflater == null) throw new IllegalStateException("Layout inflater service unavailable");
        return inflater;
    }

    public LayoutInflater cloneInContext(Context newContext) {
        return new LayoutInflater(this, newContext);
    }

    public Context getContext() { return mContext; }

    public Factory getFactory() { return mFactory; }

    public Factory2 getFactory2() { return mFactory2; }

    public void setFactory(Factory factory) {
        if (mFactorySet) throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        if (factory == null) throw new NullPointerException("Given factory can not be null");
        mFactorySet = true;
        mFactory = mFactory == null ? factory : new FactoryMerger(factory, null, mFactory, mFactory2);
    }

    public void setFactory2(Factory2 factory) {
        if (mFactorySet) throw new IllegalStateException("A factory has already been set on this LayoutInflater");
        if (factory == null) throw new NullPointerException("Given factory can not be null");
        mFactorySet = true;
        mFactory = mFactory2 = mFactory == null ? factory
                : new FactoryMerger(factory, factory, mFactory, mFactory2);
    }

    /** The factory the Activity installs; consulted after any app factory. */
    public void setPrivateFactory(Factory2 factory) {
        mPrivateFactory = mPrivateFactory == null ? factory
                : new FactoryMerger(factory, factory, mPrivateFactory, mPrivateFactory);
    }

    private static final class FactoryMerger implements Factory2 {
        private final Factory mF1, mF2;
        private final Factory2 mF12, mF22;

        FactoryMerger(Factory f1, Factory2 f12, Factory f2, Factory2 f22) {
            mF1 = f1;
            mF2 = f2;
            mF12 = f12;
            mF22 = f22;
        }

        public View onCreateView(String name, Context context, AttributeSet attrs) {
            View v = mF1.onCreateView(name, context, attrs);
            if (v != null) return v;
            return mF2.onCreateView(name, context, attrs);
        }

        public View onCreateView(View parent, String name, Context context, AttributeSet attrs) {
            View v = mF12 != null ? mF12.onCreateView(parent, name, context, attrs)
                    : mF1.onCreateView(name, context, attrs);
            if (v != null) return v;
            return mF22 != null ? mF22.onCreateView(parent, name, context, attrs)
                    : mF2.onCreateView(name, context, attrs);
        }
    }

    public View inflate(int resource, ViewGroup root) {
        return inflate(resource, root, root != null);
    }

    public View inflate(XmlPullParser parser, ViewGroup root) {
        return inflate(parser, root, root != null);
    }

    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        final android.content.res.Resources res = getContext().getResources();
        final XmlResourceParser parser = res.getLayout(resource);
        try {
            return inflate(parser, root, attachToRoot);
        } finally {
            parser.close();
        }
    }

    public View inflate(XmlPullParser parser, ViewGroup root, boolean attachToRoot) {
        synchronized (mConstructorArgs) {
            final Context inflaterContext = mContext;
            final AttributeSet attrs = Xml.asAttributeSet(parser);
            Context lastContext = (Context) mConstructorArgs[0];
            mConstructorArgs[0] = inflaterContext;
            View result = root;
            try {
                int type;
                while ((type = parser.next()) != XmlPullParser.START_TAG
                        && type != XmlPullParser.END_DOCUMENT) {
                    // Empty: skip to the root element.
                }
                if (type != XmlPullParser.START_TAG) {
                    throw new InflateException(parser.getPositionDescription() + ": No start tag found!");
                }
                final String name = parser.getName();
                if (TAG_MERGE.equals(name)) {
                    if (root == null || !attachToRoot) {
                        throw new InflateException("<merge /> can be used only with a valid "
                                + "ViewGroup root and attachToRoot=true");
                    }
                    rInflate(parser, root, inflaterContext, attrs, false);
                } else {
                    final View temp = createViewFromTag(root, name, inflaterContext, attrs);
                    ViewGroup.LayoutParams params = null;
                    if (root != null) {
                        params = root.generateLayoutParams(attrs);
                        if (!attachToRoot) temp.setLayoutParams(params);
                    }
                    rInflateChildren(parser, temp, attrs, true);
                    if (root != null && attachToRoot) root.addView(temp, params);
                    if (root == null || !attachToRoot) result = temp;
                }
            } catch (XmlPullParserException e) {
                throw new InflateException(e.getMessage(), e);
            } catch (InflateException e) {
                throw e;
            } catch (Exception e) {
                throw new InflateException(parser.getPositionDescription() + ": " + e.getMessage(), e);
            } finally {
                mConstructorArgs[0] = lastContext;
                mConstructorArgs[1] = null;
            }
            return result;
        }
    }

    /** Create a view from its class name and attributes, with `prefix` for a short name. */
    public final View createView(String name, String prefix, AttributeSet attrs)
            throws ClassNotFoundException, InflateException {
        Context context = (Context) mConstructorArgs[0];
        if (context == null) context = mContext;
        return createView(context, name, prefix, attrs);
    }

    public final View createView(Context viewContext, String name, String prefix, AttributeSet attrs)
            throws ClassNotFoundException, InflateException {
        Constructor<? extends View> constructor;
        synchronized (sConstructorMap) {
            constructor = sConstructorMap.get(name);
        }
        Class<? extends View> clazz = null;
        try {
            if (constructor == null) {
                clazz = Class.forName(prefix != null ? (prefix + name) : name, false,
                        mContext.getClassLoader()).asSubclass(View.class);
                constructor = clazz.getConstructor(CONSTRUCTOR_SIGNATURE);
                constructor.setAccessible(true);
                synchronized (sConstructorMap) {
                    sConstructorMap.put(name, constructor);
                }
            }
            final Object lastContext = mConstructorArgs[0];
            mConstructorArgs[0] = viewContext;
            final Object[] args = new Object[] {viewContext, attrs};
            mConstructorArgs[1] = attrs;
            try {
                final View view = constructor.newInstance(args);
                if (view instanceof ViewStub) {
                    // Use the same context when inflating ViewStub later.
                    final ViewStub viewStub = (ViewStub) view;
                    viewStub.setLayoutInflater(cloneInContext((Context) args[0]));
                }
                return view;
            } finally {
                mConstructorArgs[0] = lastContext;
            }
        } catch (NoSuchMethodException e) {
            throw new InflateException(attrs.getPositionDescription() + ": Error inflating class "
                    + (prefix != null ? (prefix + name) : name), e);
        } catch (ClassCastException e) {
            throw new InflateException(attrs.getPositionDescription() + ": Class is not a View "
                    + (prefix != null ? (prefix + name) : name), e);
        } catch (ClassNotFoundException e) {
            throw e;
        } catch (Exception e) {
            throw new InflateException(attrs.getPositionDescription() + ": Error inflating class "
                    + (clazz == null ? "<unknown>" : clazz.getName()), e);
        }
    }

    /** Hook for subclasses: create a view for a tag with no package, trying the android.* ones. */
    protected View onCreateView(String name, AttributeSet attrs) throws ClassNotFoundException {
        for (int i = 0; i < CLASS_PREFIX_LIST.length; i++) {
            try {
                View view = createView(name, CLASS_PREFIX_LIST[i], attrs);
                if (view != null) return view;
            } catch (ClassNotFoundException e) {
                // Try the next prefix.
            }
        }
        throw new ClassNotFoundException(name);
    }

    protected View onCreateView(View parent, String name, AttributeSet attrs)
            throws ClassNotFoundException {
        return onCreateView(name, attrs);
    }

    private View tryCreateView(View parent, String name, Context context, AttributeSet attrs) {
        View view;
        if (mFactory2 != null) {
            view = mFactory2.onCreateView(parent, name, context, attrs);
        } else if (mFactory != null) {
            view = mFactory.onCreateView(name, context, attrs);
        } else {
            view = null;
        }
        if (view == null && mPrivateFactory != null) {
            view = mPrivateFactory.onCreateView(parent, name, context, attrs);
        }
        return view;
    }

    View createViewFromTag(View parent, String name, Context context, AttributeSet attrs) {
        return createViewFromTag(parent, name, context, attrs, false);
    }

    View createViewFromTag(View parent, String name, Context context, AttributeSet attrs,
            boolean ignoreThemeAttr) {
        if (name.equals("view")) {
            name = attrs.getAttributeValue(null, "class");
        }
        // Apply a theme wrapper, if allowed and one is specified.
        if (!ignoreThemeAttr) {
            final TypedArray ta = context.obtainStyledAttributes(attrs, ATTRS_THEME);
            final int themeResId = ta.getResourceId(0, 0);
            if (themeResId != 0) {
                context = new ContextThemeWrapper(context, themeResId);
            }
            ta.recycle();
        }
        try {
            View view = tryCreateView(parent, name, context, attrs);
            if (view == null) {
                final Object lastContext = mConstructorArgs[0];
                mConstructorArgs[0] = context;
                try {
                    if (-1 == name.indexOf('.')) {
                        view = onCreateView(parent, name, attrs);
                    } else {
                        view = createView(context, name, null, attrs);
                    }
                } finally {
                    mConstructorArgs[0] = lastContext;
                }
            }
            return view;
        } catch (InflateException e) {
            throw e;
        } catch (ClassNotFoundException e) {
            throw new InflateException(attrs.getPositionDescription() + ": Error inflating class "
                    + name, e);
        } catch (Exception e) {
            throw new InflateException(attrs.getPositionDescription() + ": Error inflating class "
                    + name, e);
        }
    }

    final void rInflateChildren(XmlPullParser parser, View parent, AttributeSet attrs,
            boolean finishInflate) throws XmlPullParserException, java.io.IOException {
        rInflate(parser, parent, parent.getContext(), attrs, finishInflate);
    }

    void rInflate(XmlPullParser parser, View parent, Context context, AttributeSet attrs,
            boolean finishInflate) throws XmlPullParserException, java.io.IOException {
        final int depth = parser.getDepth();
        int type;
        boolean pendingRequestFocus = false;
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth)
                && type != XmlPullParser.END_DOCUMENT) {
            if (type != XmlPullParser.START_TAG) continue;
            final String name = parser.getName();
            if (TAG_REQUEST_FOCUS.equals(name)) {
                pendingRequestFocus = true;
                consumeChildElements(parser);
            } else if (TAG_TAG.equals(name)) {
                parseViewTag(parser, parent, attrs);
            } else if (TAG_INCLUDE.equals(name)) {
                if (parser.getDepth() == 0) {
                    throw new InflateException("<include /> cannot be the root element");
                }
                parseInclude(parser, context, parent, attrs);
            } else if (TAG_MERGE.equals(name)) {
                throw new InflateException("<merge /> must be the root element");
            } else {
                final View view = createViewFromTag(parent, name, context, attrs);
                final ViewGroup viewGroup = (ViewGroup) parent;
                final ViewGroup.LayoutParams params = viewGroup.generateLayoutParams(attrs);
                rInflateChildren(parser, view, attrs, true);
                viewGroup.addView(view, params);
            }
        }
        if (pendingRequestFocus) parent.requestFocus();
        if (finishInflate) parent.onFinishInflate();
    }

    private void parseViewTag(XmlPullParser parser, View view, AttributeSet attrs)
            throws XmlPullParserException, java.io.IOException {
        final Context context = view.getContext();
        final TypedArray ta = context.obtainStyledAttributes(attrs,
                com.android.internal.R.styleable.ViewTag);
        final int key = ta.getResourceId(com.android.internal.R.styleable.ViewTag_id, 0);
        final CharSequence value = ta.getText(com.android.internal.R.styleable.ViewTag_value);
        view.setTag(key, value);
        ta.recycle();
        consumeChildElements(parser);
    }

    private void parseInclude(XmlPullParser parser, Context context, View parent, AttributeSet attrs)
            throws XmlPullParserException, java.io.IOException {
        int type;
        if (!(parent instanceof ViewGroup)) {
            throw new InflateException("<include /> can only be used inside of a ViewGroup");
        }
        // Apply a theme wrapper, if requested. This is sort of a weird precedence order, because
        // the apply-theme attribute is only honored if no layout attribute overrides it.
        final TypedArray ta = context.obtainStyledAttributes(attrs, ATTRS_THEME);
        final int themeResId = ta.getResourceId(0, 0);
        final boolean hasThemeOverride = themeResId != 0;
        if (hasThemeOverride) context = new ContextThemeWrapper(context, themeResId);
        ta.recycle();

        int layout = attrs.getAttributeResourceValue(null, ATTR_LAYOUT, 0);
        if (layout == 0) {
            final String value = attrs.getAttributeValue(null, ATTR_LAYOUT);
            throw new InflateException("You must specify a valid layout reference. The layout ID "
                    + value + " is not valid.");
        }
        final XmlResourceParser childParser = context.getResources().getLayout(layout);
        try {
            final AttributeSet childAttrs = Xml.asAttributeSet(childParser);
            while ((type = childParser.next()) != XmlPullParser.START_TAG
                    && type != XmlPullParser.END_DOCUMENT) {
                // Empty.
            }
            if (type != XmlPullParser.START_TAG) {
                throw new InflateException(childParser.getPositionDescription() + ": No start tag found!");
            }
            final String childName = childParser.getName();
            if (TAG_MERGE.equals(childName)) {
                // The <merge> tag doesn't support android:theme, so nothing special to do here.
                rInflate(childParser, parent, context, childAttrs, false);
            } else {
                final View view = createViewFromTag(parent, childName, context, childAttrs,
                        hasThemeOverride);
                final ViewGroup group = (ViewGroup) parent;
                final TypedArray a = context.obtainStyledAttributes(attrs,
                        com.android.internal.R.styleable.Include);
                final int id = a.getResourceId(com.android.internal.R.styleable.Include_id, View.NO_ID);
                final int visibility = a.getInt(com.android.internal.R.styleable.Include_visibility, -1);
                a.recycle();

                // The <include> tag's own layout_* attributes win; otherwise the included
                // layout's root supplies them.
                ViewGroup.LayoutParams params = null;
                try {
                    params = group.generateLayoutParams(attrs);
                } catch (RuntimeException e) {
                    params = group.generateLayoutParams(childAttrs);
                } finally {
                    if (params != null) view.setLayoutParams(params);
                }
                rInflateChildren(childParser, view, childAttrs, true);
                if (id != View.NO_ID) view.setId(id);
                switch (visibility) {
                    case 0:
                        view.setVisibility(View.VISIBLE);
                        break;
                    case 1:
                        view.setVisibility(View.INVISIBLE);
                        break;
                    case 2:
                        view.setVisibility(View.GONE);
                        break;
                }
                group.addView(view);
            }
        } finally {
            childParser.close();
        }
        consumeChildElements(parser);
    }

    static final void consumeChildElements(XmlPullParser parser)
            throws XmlPullParserException, java.io.IOException {
        int type;
        final int currentDepth = parser.getDepth();
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > currentDepth)
                && type != XmlPullParser.END_DOCUMENT) {
            // Empty.
        }
    }
}
