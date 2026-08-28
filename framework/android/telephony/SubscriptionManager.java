package android.telephony;

import android.content.Context;
import java.util.List;
import java.util.Collections;

public class SubscriptionManager {
    public static final int INVALID_SUBSCRIPTION_ID = -1;
    public static final int DEFAULT_SUBSCRIPTION_ID = 1;

    public SubscriptionManager(Context context) {}
    public static SubscriptionManager from(Context context) { return new SubscriptionManager(context); }
    public List<SubscriptionInfo> getActiveSubscriptionInfoList() { return Collections.singletonList(new SubscriptionInfo()); }
    public int getActiveSubscriptionInfoCount() { return 1; }
    public static int getDefaultSubscriptionId() { return DEFAULT_SUBSCRIPTION_ID; }
}
