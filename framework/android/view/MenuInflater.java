package android.view;

import android.content.Context;
import android.content.res.XmlResourceParser;
import android.util.AttributeSet;
import android.util.Xml;
import java.io.IOException;
import java.lang.reflect.Constructor;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;

public class MenuInflater {
    private final Context mContext;

    public MenuInflater(Context context) {
        mContext = context;
    }

    public void inflate(int menuRes, Menu menu) {
        if (menuRes == 0 || menu == null || mContext == null) return;
        XmlResourceParser parser = null;
        try {
            parser = mContext.getResources().getLayout(menuRes);
            AttributeSet attrs = Xml.asAttributeSet(parser);
            parseMenu(parser, attrs, menu);
        } catch (Throwable t) {
            android.util.Log.w("MenuInflater", "Failed to inflate menu resource 0x" + Integer.toHexString(menuRes), t);
        } finally {
            if (parser != null) {
                parser.close();
            }
        }
    }

    private void parseMenu(XmlPullParser parser, AttributeSet attrs, Menu menu)
            throws XmlPullParserException, IOException {
        int eventType = parser.getEventType();
        String tagName;
        boolean lookingForRoot = true;

        while (eventType != XmlPullParser.END_DOCUMENT) {
            if (eventType == XmlPullParser.START_TAG) {
                tagName = parser.getName();
                if ("menu".equals(tagName)) {
                    eventType = parser.next();
                    break;
                }
            }
            eventType = parser.next();
        }

        int groupId = 0;
        int groupCategory = 0;
        int groupOrder = 0;
        boolean groupVisible = true;
        boolean groupEnabled = true;

        boolean reachedEndOfMenu = false;
        while (!reachedEndOfMenu && eventType != XmlPullParser.END_DOCUMENT) {
            switch (eventType) {
                case XmlPullParser.START_TAG:
                    tagName = parser.getName();
                    if ("group".equals(tagName)) {
                        groupId = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res/android", "id", 0);
                    } else if ("item".equals(tagName)) {
                        readItem(parser, attrs, menu, groupId);
                    } else if ("menu".equals(tagName)) {
                        // Nested menu is handled inside readItem if submenu
                    }
                    break;

                case XmlPullParser.END_TAG:
                    tagName = parser.getName();
                    if ("group".equals(tagName)) {
                        groupId = 0;
                    } else if ("menu".equals(tagName)) {
                        reachedEndOfMenu = true;
                    }
                    break;
            }
            eventType = parser.next();
        }
    }

    private void readItem(XmlPullParser parser, AttributeSet attrs, Menu menu, int groupId)
            throws XmlPullParserException, IOException {
        int itemId = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res/android", "id", 0);
        int itemOrder = attrs.getAttributeIntValue("http://schemas.android.com/apk/res/android", "orderInCategory", 0);
        int titleRes = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res/android", "title", 0);
        CharSequence itemTitle = titleRes != 0 ? mContext.getString(titleRes) :
                attrs.getAttributeValue("http://schemas.android.com/apk/res/android", "title");
        int iconRes = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res/android", "icon", 0);
        int showAsAction = attrs.getAttributeIntValue("http://schemas.android.com/apk/res/android", "showAsAction", 0);

        String actionViewClassName = attrs.getAttributeValue("http://schemas.android.com/apk/res-auto", "actionViewClass");
        if (actionViewClassName == null) {
            actionViewClassName = attrs.getAttributeValue("http://schemas.android.com/apk/res/android", "actionViewClass");
        }
        if (actionViewClassName == null) {
            actionViewClassName = attrs.getAttributeValue(null, "actionViewClass");
        }

        int actionLayoutRes = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res-auto", "actionLayout", 0);
        if (actionLayoutRes == 0) {
            actionLayoutRes = attrs.getAttributeResourceValue("http://schemas.android.com/apk/res/android", "actionLayout", 0);
        }
        if (actionLayoutRes == 0) {
            actionLayoutRes = attrs.getAttributeResourceValue(null, "actionLayout", 0);
        }

        MenuItem item = menu.add(groupId, itemId, itemOrder, itemTitle != null ? itemTitle : "");
        if (iconRes != 0) {
            item.setIcon(iconRes);
        }
        if (showAsAction != 0) {
            item.setShowAsAction(showAsAction);
        }

        if (actionLayoutRes != 0) {
            item.setActionView(actionLayoutRes);
        } else if (actionViewClassName != null && !actionViewClassName.isEmpty()) {
            View actionView = createActionView(actionViewClassName);
            if (actionView != null) {
                item.setActionView(actionView);
            }
        }

        int eventType = parser.next();
        while (eventType != XmlPullParser.END_DOCUMENT) {
            if (eventType == XmlPullParser.START_TAG && "menu".equals(parser.getName())) {
                SubMenu subMenu = item.getSubMenu();
                if (subMenu == null) {
                    subMenu = menu.addSubMenu(groupId, itemId, itemOrder, itemTitle != null ? itemTitle : "");
                }
                parseMenu(parser, attrs, subMenu);
            } else if (eventType == XmlPullParser.END_TAG && "item".equals(parser.getName())) {
                break;
            }
            eventType = parser.next();
        }
    }

    private View createActionView(String className) {
        try {
            ClassLoader cl = mContext.getClassLoader();
            Class<?> clazz = cl.loadClass(className);
            Constructor<?> constructor = clazz.getConstructor(Context.class);
            return (View) constructor.newInstance(mContext);
        } catch (Throwable t) {
            android.util.Log.w("MenuInflater", "Could not instantiate actionViewClass " + className + ", using fallback", t);
            if (className != null && className.toLowerCase().contains("searchview")) {
                return new android.widget.SearchView(mContext);
            }
        }
        return null;
    }
}
