package org.xmlpull.v1;

public class XmlPullParserFactory {
    public static final String PROPERTY_NAME = "org.xmlpull.v1.XmlPullParserFactory";

    protected XmlPullParserFactory() {}
    public static XmlPullParserFactory newInstance() throws XmlPullParserException {
        return new XmlPullParserFactory();
    }
    public static XmlPullParserFactory newInstance(String classNames, Class context) throws XmlPullParserException {
        return newInstance();
    }
    public void setFeature(String name, boolean state) throws XmlPullParserException {}
    public boolean getFeature(String name) { return false; }
    public void setNamespaceAware(boolean awareness) {}
    public boolean isNamespaceAware() { return true; }
    public void setValidating(boolean validating) {}
    public boolean isValidating() { return false; }
    public XmlPullParser newPullParser() throws XmlPullParserException {
        return null;
    }
    public XmlSerializer newSerializer() throws XmlPullParserException {
        return null;
    }
}
