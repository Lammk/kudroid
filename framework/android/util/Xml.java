package android.util;

public class Xml {
    public static AttributeSet asAttributeSet(org.xmlpull.v1.XmlPullParser parser) {
        return parser instanceof AttributeSet ? (AttributeSet) parser : null;
    }
}
