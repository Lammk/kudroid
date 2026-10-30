package android.app;

import android.content.Context;
import android.content.Intent;
import android.content.LocusId;
import android.graphics.Bitmap;
import android.graphics.drawable.Icon;
import android.media.AudioAttributes;
import android.net.Uri;
import android.os.Bundle;
import android.widget.RemoteViews;

/**
 * Notification data holder. Nothing is posted to a system tray; the public fields and the
 * Builder keep what callers set so that compat builders can read and write them.
 */
public class Notification implements android.os.Parcelable, Cloneable {
    /** Notifications are process-local here: a Parcel round trip yields a fresh empty one. */
    public static final android.os.Parcelable.Creator<Notification> CREATOR =
            new android.os.Parcelable.Creator<Notification>() {
                @Override
                public Notification createFromParcel(android.os.Parcel in) {
                    return new Notification();
                }

                @Override
                public Notification[] newArray(int size) {
                    return new Notification[size];
                }
            };

    public void writeToParcel(android.os.Parcel out, int flags) {}

    public Notification(android.os.Parcel in) {}

    @Deprecated public static final int PRIORITY_DEFAULT = 0;
    @Deprecated public static final int PRIORITY_LOW = -1;
    @Deprecated public static final int PRIORITY_MIN = -2;
    @Deprecated public static final int PRIORITY_HIGH = 1;
    @Deprecated public static final int PRIORITY_MAX = 2;

    public static final int VISIBILITY_PUBLIC = 1;
    public static final int VISIBILITY_PRIVATE = 0;
    public static final int VISIBILITY_SECRET = -1;

    @Deprecated public static final int STREAM_DEFAULT = -1;
    public static final int COLOR_DEFAULT = 0;

    public static final int DEFAULT_ALL = ~0;
    public static final int DEFAULT_SOUND = 1;
    public static final int DEFAULT_VIBRATE = 2;
    public static final int DEFAULT_LIGHTS = 4;

    public static final int FLAG_SHOW_LIGHTS = 0x00000001;
    public static final int FLAG_ONGOING_EVENT = 0x00000002;
    public static final int FLAG_INSISTENT = 0x00000004;
    public static final int FLAG_ONLY_ALERT_ONCE = 0x00000008;
    public static final int FLAG_AUTO_CANCEL = 0x00000010;
    public static final int FLAG_NO_CLEAR = 0x00000020;
    public static final int FLAG_FOREGROUND_SERVICE = 0x00000040;
    public static final int FLAG_HIGH_PRIORITY = 0x00000080;
    public static final int FLAG_LOCAL_ONLY = 0x00000100;
    public static final int FLAG_GROUP_SUMMARY = 0x00000200;
    public static final int FLAG_BUBBLE = 0x00001000;

    public static final int GROUP_ALERT_ALL = 0;
    public static final int GROUP_ALERT_SUMMARY = 1;
    public static final int GROUP_ALERT_CHILDREN = 2;

    public static final int BADGE_ICON_NONE = 0;
    public static final int BADGE_ICON_SMALL = 1;
    public static final int BADGE_ICON_LARGE = 2;

    public static final String CATEGORY_CALL = "call";
    public static final String CATEGORY_NAVIGATION = "navigation";
    public static final String CATEGORY_MESSAGE = "msg";
    public static final String CATEGORY_EMAIL = "email";
    public static final String CATEGORY_EVENT = "event";
    public static final String CATEGORY_PROMO = "promo";
    public static final String CATEGORY_ALARM = "alarm";
    public static final String CATEGORY_PROGRESS = "progress";
    public static final String CATEGORY_SOCIAL = "social";
    public static final String CATEGORY_ERROR = "err";
    public static final String CATEGORY_TRANSPORT = "transport";
    public static final String CATEGORY_SYSTEM = "sys";
    public static final String CATEGORY_SERVICE = "service";
    public static final String CATEGORY_RECOMMENDATION = "recommendation";
    public static final String CATEGORY_STATUS = "status";
    public static final String CATEGORY_REMINDER = "reminder";

    public static final String EXTRA_TITLE = "android.title";
    public static final String EXTRA_TITLE_BIG = "android.title.big";
    public static final String EXTRA_TEXT = "android.text";
    public static final String EXTRA_SUB_TEXT = "android.subText";
    public static final String EXTRA_INFO_TEXT = "android.infoText";
    public static final String EXTRA_SUMMARY_TEXT = "android.summaryText";
    public static final String EXTRA_BIG_TEXT = "android.bigText";
    public static final String EXTRA_TEXT_LINES = "android.textLines";
    public static final String EXTRA_SMALL_ICON = "android.icon";
    public static final String EXTRA_LARGE_ICON = "android.largeIcon";
    public static final String EXTRA_LARGE_ICON_BIG = "android.largeIcon.big";
    public static final String EXTRA_PICTURE = "android.picture";
    public static final String EXTRA_PROGRESS = "android.progress";
    public static final String EXTRA_PROGRESS_MAX = "android.progressMax";
    public static final String EXTRA_PROGRESS_INDETERMINATE = "android.progressIndeterminate";
    public static final String EXTRA_SHOW_CHRONOMETER = "android.showChronometer";
    public static final String EXTRA_CHRONOMETER_COUNT_DOWN = "android.chronometerCountDown";
    public static final String EXTRA_SHOW_WHEN = "android.showWhen";
    public static final String EXTRA_COLORIZED = "android.colorized";
    public static final String EXTRA_TEMPLATE = "android.template";
    public static final String EXTRA_PEOPLE = "android.people";
    public static final String EXTRA_PEOPLE_LIST = "android.people.list";
    public static final String EXTRA_MEDIA_SESSION = "android.mediaSession";
    public static final String EXTRA_COMPACT_ACTIONS = "android.compactActions";
    public static final String EXTRA_MESSAGES = "android.messages";
    public static final String EXTRA_HISTORIC_MESSAGES = "android.messages.historic";
    public static final String EXTRA_CONVERSATION_TITLE = "android.conversationTitle";
    public static final String EXTRA_IS_GROUP_CONVERSATION = "android.isGroupConversation";
    public static final String EXTRA_MESSAGING_PERSON = "android.messagingUser";
    public static final String EXTRA_SELF_DISPLAY_NAME = "android.selfDisplayName";
    public static final String EXTRA_REMOTE_INPUT_HISTORY = "android.remoteInputHistory";
    public static final String EXTRA_REMOTE_INPUT_DRAFT = "android.remoteInputDraft";
    public static final String EXTRA_BACKGROUND_IMAGE_URI = "android.backgroundImageUri";
    public static final String EXTRA_AUDIO_CONTENTS_URI = "android.audioContents";
    public static final String EXTRA_NOTIFICATION_ID = "android.intent.extra.NOTIFICATION_ID";
    public static final String EXTRA_NOTIFICATION_TAG = "android.intent.extra.NOTIFICATION_TAG";
    public static final String EXTRA_CHANNEL_ID = "android.intent.extra.CHANNEL_ID";
    public static final String EXTRA_CHANNEL_GROUP_ID = "android.intent.extra.CHANNEL_GROUP_ID";
    public static final String INTENT_CATEGORY_NOTIFICATION_PREFERENCES =
            "android.intent.category.NOTIFICATION_PREFERENCES";

    public static final AudioAttributes AUDIO_ATTRIBUTES_DEFAULT = null;

    public long when = System.currentTimeMillis();
    @Deprecated public int icon;
    public int iconLevel;
    public int number;
    public PendingIntent contentIntent;
    public PendingIntent deleteIntent;
    public PendingIntent fullScreenIntent;
    public CharSequence tickerText;
    @Deprecated public RemoteViews tickerView;
    @Deprecated public RemoteViews contentView;
    @Deprecated public RemoteViews bigContentView;
    @Deprecated public RemoteViews headsUpContentView;
    @Deprecated public Bitmap largeIcon;
    @Deprecated public int defaults;
    public int flags;
    @Deprecated public Uri sound;
    @Deprecated public int audioStreamType = STREAM_DEFAULT;
    @Deprecated public AudioAttributes audioAttributes;
    @Deprecated public long[] vibrate;
    @Deprecated public int ledARGB;
    @Deprecated public int ledOnMS;
    @Deprecated public int ledOffMS;
    @Deprecated public int priority = PRIORITY_DEFAULT;
    public String category;
    public int color = COLOR_DEFAULT;
    public int visibility;
    public Notification publicVersion;
    public Action[] actions;
    public Bundle extras = new Bundle();

    /** Kept for callers of the earlier thin API. */
    public CharSequence contentTitle;
    public CharSequence contentText;

    private Icon mSmallIcon;
    private Icon mLargeIcon;
    private String mChannelId;
    private String mGroupKey;
    private String mSortKey;
    private String mShortcutId;
    private CharSequence mSettingsText;
    private LocusId mLocusId;
    private BubbleMetadata mBubbleMetadata;
    private long mTimeoutAfter;
    private int mBadgeIconType = BADGE_ICON_NONE;
    private int mGroupAlertBehavior = GROUP_ALERT_ALL;
    private boolean mAllowContextualActions = true;

    public Notification() {
    }

    @Deprecated
    public Notification(int icon, CharSequence tickerText, long when) {
        this.when = when;
        this.icon = icon;
        this.tickerText = tickerText;
    }

    @Override
    public Notification clone() {
        Notification copy = new Notification();
        copy.when = when;
        copy.icon = icon;
        copy.iconLevel = iconLevel;
        copy.number = number;
        copy.contentIntent = contentIntent;
        copy.deleteIntent = deleteIntent;
        copy.fullScreenIntent = fullScreenIntent;
        copy.tickerText = tickerText;
        copy.tickerView = tickerView;
        copy.contentView = contentView;
        copy.bigContentView = bigContentView;
        copy.headsUpContentView = headsUpContentView;
        copy.largeIcon = largeIcon;
        copy.defaults = defaults;
        copy.flags = flags;
        copy.sound = sound;
        copy.audioStreamType = audioStreamType;
        copy.audioAttributes = audioAttributes;
        copy.vibrate = vibrate;
        copy.ledARGB = ledARGB;
        copy.ledOnMS = ledOnMS;
        copy.ledOffMS = ledOffMS;
        copy.priority = priority;
        copy.category = category;
        copy.color = color;
        copy.visibility = visibility;
        copy.publicVersion = publicVersion;
        copy.actions = actions;
        copy.extras = new Bundle(extras);
        copy.contentTitle = contentTitle;
        copy.contentText = contentText;
        copy.mSmallIcon = mSmallIcon;
        copy.mLargeIcon = mLargeIcon;
        copy.mChannelId = mChannelId;
        copy.mGroupKey = mGroupKey;
        copy.mSortKey = mSortKey;
        copy.mShortcutId = mShortcutId;
        copy.mSettingsText = mSettingsText;
        copy.mLocusId = mLocusId;
        copy.mBubbleMetadata = mBubbleMetadata;
        copy.mTimeoutAfter = mTimeoutAfter;
        copy.mBadgeIconType = mBadgeIconType;
        copy.mGroupAlertBehavior = mGroupAlertBehavior;
        copy.mAllowContextualActions = mAllowContextualActions;
        return copy;
    }

    public Icon getSmallIcon() { return mSmallIcon; }
    public Icon getLargeIcon() { return mLargeIcon; }
    public String getChannelId() { return mChannelId; }
    public String getGroup() { return mGroupKey; }
    public String getSortKey() { return mSortKey; }
    public String getShortcutId() { return mShortcutId; }
    public CharSequence getSettingsText() { return mSettingsText; }
    public LocusId getLocusId() { return mLocusId; }
    public BubbleMetadata getBubbleMetadata() { return mBubbleMetadata; }
    public long getTimeoutAfter() { return mTimeoutAfter; }
    public int getBadgeIconType() { return mBadgeIconType; }
    public int getGroupAlertBehavior() { return mGroupAlertBehavior; }
    public boolean getAllowSystemGeneratedContextualActions() { return mAllowContextualActions; }
    @Override
    public int describeContents() { return 0; }

    public static class Builder {
        private final Context mContext;
        private final Notification mNotification = new Notification();
        private Style mStyle;
        private boolean mShowWhen = true;

        public Builder(Context context, String channelId) {
            mContext = context;
            mNotification.mChannelId = channelId;
        }

        @Deprecated
        public Builder(Context context) {
            this(context, null);
        }

        public Builder setChannelId(String channelId) {
            mNotification.mChannelId = channelId;
            return this;
        }

        public Builder setWhen(long when) {
            mNotification.when = when;
            return this;
        }

        public Builder setShowWhen(boolean show) {
            mShowWhen = show;
            mNotification.extras.putBoolean(EXTRA_SHOW_WHEN, show);
            return this;
        }

        public Builder setUsesChronometer(boolean b) {
            mNotification.extras.putBoolean(EXTRA_SHOW_CHRONOMETER, b);
            return this;
        }

        public Builder setChronometerCountDown(boolean countDown) {
            mNotification.extras.putBoolean(EXTRA_CHRONOMETER_COUNT_DOWN, countDown);
            return this;
        }

        public Builder setSmallIcon(int icon) {
            mNotification.icon = icon;
            return this;
        }

        public Builder setSmallIcon(int icon, int level) {
            mNotification.icon = icon;
            mNotification.iconLevel = level;
            return this;
        }

        public Builder setSmallIcon(Icon icon) {
            mNotification.mSmallIcon = icon;
            return this;
        }

        public Builder setContentTitle(CharSequence title) {
            mNotification.contentTitle = title;
            mNotification.extras.putCharSequence(EXTRA_TITLE, title);
            return this;
        }

        public Builder setContentText(CharSequence text) {
            mNotification.contentText = text;
            mNotification.extras.putCharSequence(EXTRA_TEXT, text);
            return this;
        }

        public Builder setSubText(CharSequence text) {
            mNotification.extras.putCharSequence(EXTRA_SUB_TEXT, text);
            return this;
        }

        @Deprecated
        public Builder setContentInfo(CharSequence info) {
            mNotification.extras.putCharSequence(EXTRA_INFO_TEXT, info);
            return this;
        }

        public Builder setSettingsText(CharSequence text) {
            mNotification.mSettingsText = text;
            return this;
        }

        public Builder setNumber(int number) {
            mNotification.number = number;
            return this;
        }

        public Builder setProgress(int max, int progress, boolean indeterminate) {
            mNotification.extras.putInt(EXTRA_PROGRESS, progress);
            mNotification.extras.putInt(EXTRA_PROGRESS_MAX, max);
            mNotification.extras.putBoolean(EXTRA_PROGRESS_INDETERMINATE, indeterminate);
            return this;
        }

        public Builder setTicker(CharSequence tickerText) {
            mNotification.tickerText = tickerText;
            return this;
        }

        @Deprecated
        public Builder setTicker(CharSequence tickerText, RemoteViews views) {
            mNotification.tickerText = tickerText;
            mNotification.tickerView = views;
            return this;
        }

        public Builder setLargeIcon(Bitmap icon) {
            mNotification.largeIcon = icon;
            return this;
        }

        public Builder setLargeIcon(Icon icon) {
            mNotification.mLargeIcon = icon;
            return this;
        }

        public Builder setContentIntent(PendingIntent intent) {
            mNotification.contentIntent = intent;
            return this;
        }

        public Builder setDeleteIntent(PendingIntent intent) {
            mNotification.deleteIntent = intent;
            return this;
        }

        public Builder setFullScreenIntent(PendingIntent intent, boolean highPriority) {
            mNotification.fullScreenIntent = intent;
            setFlag(FLAG_HIGH_PRIORITY, highPriority);
            return this;
        }

        @Deprecated
        public Builder setSound(Uri sound) {
            mNotification.sound = sound;
            mNotification.audioAttributes = AUDIO_ATTRIBUTES_DEFAULT;
            return this;
        }

        @Deprecated
        public Builder setSound(Uri sound, int streamType) {
            mNotification.sound = sound;
            mNotification.audioStreamType = streamType;
            return this;
        }

        @Deprecated
        public Builder setSound(Uri sound, AudioAttributes audioAttributes) {
            mNotification.sound = sound;
            mNotification.audioAttributes = audioAttributes;
            return this;
        }

        @Deprecated
        public Builder setVibrate(long[] pattern) {
            mNotification.vibrate = pattern;
            return this;
        }

        @Deprecated
        public Builder setLights(int argb, int onMs, int offMs) {
            mNotification.ledARGB = argb;
            mNotification.ledOnMS = onMs;
            mNotification.ledOffMS = offMs;
            setFlag(FLAG_SHOW_LIGHTS, onMs != 0 && offMs != 0);
            return this;
        }

        public Builder setOngoing(boolean ongoing) {
            setFlag(FLAG_ONGOING_EVENT, ongoing);
            return this;
        }

        public Builder setOnlyAlertOnce(boolean onlyAlertOnce) {
            setFlag(FLAG_ONLY_ALERT_ONCE, onlyAlertOnce);
            return this;
        }

        public Builder setAutoCancel(boolean autoCancel) {
            setFlag(FLAG_AUTO_CANCEL, autoCancel);
            return this;
        }

        public Builder setLocalOnly(boolean localOnly) {
            setFlag(FLAG_LOCAL_ONLY, localOnly);
            return this;
        }

        @Deprecated
        public Builder setDefaults(int defaults) {
            mNotification.defaults = defaults;
            return this;
        }

        @Deprecated
        public Builder setPriority(int priority) {
            mNotification.priority = priority;
            return this;
        }

        public Builder setCategory(String category) {
            mNotification.category = category;
            return this;
        }

        public Builder setColor(int argb) {
            mNotification.color = argb;
            return this;
        }

        public Builder setColorized(boolean colorize) {
            mNotification.extras.putBoolean(EXTRA_COLORIZED, colorize);
            return this;
        }

        public Builder setVisibility(int visibility) {
            mNotification.visibility = visibility;
            return this;
        }

        public Builder setPublicVersion(Notification n) {
            mNotification.publicVersion = n;
            return this;
        }

        public Builder setGroup(String groupKey) {
            mNotification.mGroupKey = groupKey;
            return this;
        }

        public Builder setGroupSummary(boolean isGroupSummary) {
            setFlag(FLAG_GROUP_SUMMARY, isGroupSummary);
            return this;
        }

        public Builder setGroupAlertBehavior(int behavior) {
            mNotification.mGroupAlertBehavior = behavior;
            return this;
        }

        public Builder setSortKey(String sortKey) {
            mNotification.mSortKey = sortKey;
            return this;
        }

        public Builder setShortcutId(String shortcutId) {
            mNotification.mShortcutId = shortcutId;
            return this;
        }

        public Builder setLocusId(LocusId locusId) {
            mNotification.mLocusId = locusId;
            return this;
        }

        public Builder setBadgeIconType(int icon) {
            mNotification.mBadgeIconType = icon;
            return this;
        }

        public Builder setTimeoutAfter(long durationMs) {
            mNotification.mTimeoutAfter = durationMs;
            return this;
        }

        public Builder setAllowSystemGeneratedContextualActions(boolean allowed) {
            mNotification.mAllowContextualActions = allowed;
            return this;
        }

        public Builder setBubbleMetadata(BubbleMetadata data) {
            mNotification.mBubbleMetadata = data;
            return this;
        }

        @Deprecated
        public Builder setContent(RemoteViews views) {
            mNotification.contentView = views;
            return this;
        }

        public Builder setCustomContentView(RemoteViews views) {
            mNotification.contentView = views;
            return this;
        }

        public Builder setCustomBigContentView(RemoteViews views) {
            mNotification.bigContentView = views;
            return this;
        }

        public Builder setCustomHeadsUpContentView(RemoteViews views) {
            mNotification.headsUpContentView = views;
            return this;
        }

        public Builder setRemoteInputHistory(CharSequence[] text) {
            mNotification.extras.putCharSequenceArray(EXTRA_REMOTE_INPUT_HISTORY, text);
            return this;
        }

        public Builder setStyle(Style style) {
            if (mStyle != style) {
                mStyle = style;
                if (style != null) style.setBuilder(this);
            }
            return this;
        }

        public Style getStyle() {
            return mStyle;
        }

        public Builder setExtras(Bundle extras) {
            mNotification.extras = extras != null ? extras : new Bundle();
            return this;
        }

        public Builder addExtras(Bundle extras) {
            if (extras != null) mNotification.extras.putAll(extras);
            return this;
        }

        public Bundle getExtras() {
            return mNotification.extras;
        }

        @Deprecated
        public Builder addPerson(String uri) {
            return this;
        }

        public Builder addPerson(Person person) {
            return this;
        }

        @Deprecated
        public Builder addAction(int icon, CharSequence title, PendingIntent intent) {
            return addAction(new Action(icon, title, intent));
        }

        public Builder addAction(Action action) {
            if (action == null) return this;
            Action[] old = mNotification.actions;
            Action[] grown = new Action[old == null ? 1 : old.length + 1];
            if (old != null) System.arraycopy(old, 0, grown, 0, old.length);
            grown[grown.length - 1] = action;
            mNotification.actions = grown;
            return this;
        }

        public Builder setActions(Action... actions) {
            mNotification.actions = actions;
            return this;
        }

        public Builder extend(Extender extender) {
            if (extender != null) extender.extend(this);
            return this;
        }

        public RemoteViews createContentView() { return mNotification.contentView; }
        public RemoteViews createBigContentView() { return mNotification.bigContentView; }
        public RemoteViews createHeadsUpContentView() { return mNotification.headsUpContentView; }

        public Notification build() {
            Notification built = mNotification.clone();
            if (mStyle != null) mStyle.buildInto(built);
            return built;
        }

        @Deprecated
        public Notification getNotification() {
            return build();
        }

        public static Builder recoverBuilder(Context context, Notification n) {
            Builder builder = new Builder(context, n.getChannelId());
            Notification target = builder.mNotification;
            Notification copy = n.clone();
            target.when = copy.when;
            target.icon = copy.icon;
            target.contentIntent = copy.contentIntent;
            target.extras = copy.extras;
            target.flags = copy.flags;
            target.priority = copy.priority;
            target.actions = copy.actions;
            return builder;
        }

        private void setFlag(int mask, boolean value) {
            if (value) mNotification.flags |= mask;
            else mNotification.flags &= ~mask;
        }
    }

    public interface Extender {
        Builder extend(Builder builder);
    }

    public static class Action {
        public int icon;
        public CharSequence title;
        public PendingIntent actionIntent;
        private Bundle mExtras = new Bundle();
        private boolean mAllowGeneratedReplies = true;
        private boolean mContextual;
        private int mSemanticAction;
        private RemoteInput[] mRemoteInputs;
        private Icon mIcon;

        public Action() {}

        public Action(int icon, CharSequence title, PendingIntent intent) {
            this.icon = icon;
            this.title = title;
            this.actionIntent = intent;
        }

        public Bundle getExtras() { return mExtras; }
        public Icon getIcon() { return mIcon; }
        public boolean getAllowGeneratedReplies() { return mAllowGeneratedReplies; }
        public RemoteInput[] getRemoteInputs() { return mRemoteInputs; }
        public int getSemanticAction() { return mSemanticAction; }
        public boolean isContextual() { return mContextual; }

        public static final class Builder {
            private final Action mAction = new Action();

            public Builder(int icon, CharSequence title, PendingIntent intent) {
                mAction.icon = icon;
                mAction.title = title;
                mAction.actionIntent = intent;
            }

            public Builder(Icon icon, CharSequence title, PendingIntent intent) {
                mAction.mIcon = icon;
                mAction.title = title;
                mAction.actionIntent = intent;
            }

            public Builder(Action action) {
                mAction.icon = action.icon;
                mAction.title = action.title;
                mAction.actionIntent = action.actionIntent;
                mAction.mIcon = action.mIcon;
                mAction.mExtras = new Bundle(action.mExtras);
            }

            public Builder addExtras(Bundle extras) {
                if (extras != null) mAction.mExtras.putAll(extras);
                return this;
            }

            public Builder addRemoteInput(RemoteInput remoteInput) {
                RemoteInput[] old = mAction.mRemoteInputs;
                RemoteInput[] grown = new RemoteInput[old == null ? 1 : old.length + 1];
                if (old != null) System.arraycopy(old, 0, grown, 0, old.length);
                grown[grown.length - 1] = remoteInput;
                mAction.mRemoteInputs = grown;
                return this;
            }

            public Builder setAllowGeneratedReplies(boolean allow) {
                mAction.mAllowGeneratedReplies = allow;
                return this;
            }

            public Builder setSemanticAction(int semanticAction) {
                mAction.mSemanticAction = semanticAction;
                return this;
            }

            public Builder setContextual(boolean contextual) {
                mAction.mContextual = contextual;
                return this;
            }

            public Action build() { return mAction; }
        }
    }

    public static class Style {
        protected Builder mBuilder;
        CharSequence mBigContentTitle;
        CharSequence mSummaryText;
        boolean mSummaryTextSet;

        public Style() {}

        public void setBuilder(Builder builder) {
            if (mBuilder != builder) {
                mBuilder = builder;
                if (mBuilder != null) mBuilder.setStyle(this);
            }
        }

        protected void internalSetBigContentTitle(CharSequence title) { mBigContentTitle = title; }

        protected void internalSetSummaryText(CharSequence cs) {
            mSummaryText = cs;
            mSummaryTextSet = true;
        }

        /** Copies the style's data into the built notification's extras. */
        void buildInto(Notification n) {
            if (mBigContentTitle != null) n.extras.putCharSequence(EXTRA_TITLE_BIG, mBigContentTitle);
            if (mSummaryTextSet) n.extras.putCharSequence(EXTRA_SUMMARY_TEXT, mSummaryText);
        }
    }

    public static class BigTextStyle extends Style {
        private CharSequence mBigText;

        public BigTextStyle() {}

        public BigTextStyle setBigContentTitle(CharSequence title) {
            internalSetBigContentTitle(title);
            return this;
        }

        public BigTextStyle setSummaryText(CharSequence cs) {
            internalSetSummaryText(cs);
            return this;
        }

        public BigTextStyle bigText(CharSequence cs) {
            mBigText = cs;
            return this;
        }

        @Override
        void buildInto(Notification n) {
            super.buildInto(n);
            n.extras.putCharSequence(EXTRA_BIG_TEXT, mBigText);
            n.extras.putString(EXTRA_TEMPLATE, "android.app.Notification$BigTextStyle");
        }
    }

    public static class BigPictureStyle extends Style {
        private Bitmap mPicture;
        private Bitmap mBigLargeIcon;

        public BigPictureStyle() {}

        public BigPictureStyle setBigContentTitle(CharSequence title) {
            internalSetBigContentTitle(title);
            return this;
        }

        public BigPictureStyle setSummaryText(CharSequence cs) {
            internalSetSummaryText(cs);
            return this;
        }

        public BigPictureStyle bigPicture(Bitmap b) {
            mPicture = b;
            return this;
        }

        public BigPictureStyle bigLargeIcon(Bitmap b) {
            mBigLargeIcon = b;
            return this;
        }

        @Override
        void buildInto(Notification n) {
            super.buildInto(n);
            n.extras.putString(EXTRA_TEMPLATE, "android.app.Notification$BigPictureStyle");
        }
    }

    public static class InboxStyle extends Style {
        private final java.util.ArrayList<CharSequence> mLines = new java.util.ArrayList<>(5);

        public InboxStyle() {}

        public InboxStyle setBigContentTitle(CharSequence title) {
            internalSetBigContentTitle(title);
            return this;
        }

        public InboxStyle setSummaryText(CharSequence cs) {
            internalSetSummaryText(cs);
            return this;
        }

        public InboxStyle addLine(CharSequence cs) {
            mLines.add(cs);
            return this;
        }

        @Override
        void buildInto(Notification n) {
            super.buildInto(n);
            n.extras.putCharSequenceArray(EXTRA_TEXT_LINES, mLines.toArray(new CharSequence[0]));
            n.extras.putString(EXTRA_TEMPLATE, "android.app.Notification$InboxStyle");
        }
    }

    public static class MediaStyle extends Style {
        private int[] mActionsToShowInCompact;
        private Object mToken;

        public MediaStyle() {}

        public MediaStyle setShowActionsInCompactView(int... actions) {
            mActionsToShowInCompact = actions;
            return this;
        }

        public MediaStyle setMediaSession(Object token) {
            mToken = token;
            return this;
        }

        @Override
        void buildInto(Notification n) {
            super.buildInto(n);
            n.extras.putString(EXTRA_TEMPLATE, "android.app.Notification$MediaStyle");
            if (mActionsToShowInCompact != null) {
                n.extras.putIntArray(EXTRA_COMPACT_ACTIONS, mActionsToShowInCompact);
            }
        }
    }

    public static class DecoratedCustomViewStyle extends Style {
        public DecoratedCustomViewStyle() {}
    }

    public static class DecoratedMediaCustomViewStyle extends MediaStyle {
        public DecoratedMediaCustomViewStyle() {}
    }

    public static class MessagingStyle extends Style {
        public MessagingStyle() {}
        public MessagingStyle(Person user) {}
    }

    public static class CallStyle extends Style {
        public CallStyle() {}
    }

    public static class Message {
        public Message() {}
    }

    public static class BubbleMetadata {
        public BubbleMetadata() {}
    }
}
