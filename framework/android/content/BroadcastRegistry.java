package android.content;

import android.content.pm.PackageManager;
import android.os.Handler;
import java.util.ArrayList;
import java.util.Set;

final class BroadcastRegistry {
    private static final ArrayList<Registration> receivers = new ArrayList<Registration>();

    private static final class Subscription {
        final IntentFilter filter;
        final String permission;
        Subscription(IntentFilter filter, String permission) {
            this.filter = new IntentFilter(filter);
            this.permission = permission;
        }
    }

    private static final class Registration {
        final Context context;
        final BroadcastReceiver receiver;
        final Handler handler;
        final ArrayList<Subscription> filters = new ArrayList<Subscription>();
        volatile boolean registered = true;
        Registration(Context context, BroadcastReceiver receiver, Handler handler) {
            this.context = context;
            this.receiver = receiver;
            this.handler = handler;
        }
    }

    static void register(Context context, BroadcastReceiver receiver, IntentFilter filter,
                         String permission, Handler handler) {
        synchronized (receivers) {
            Registration registration = null;
            for (Registration candidate : receivers) {
                if (candidate.context == context && candidate.receiver == receiver) {
                    if (candidate.handler != handler)
                        throw new IllegalStateException("Receiver registered with a different Handler");
                    registration = candidate;
                    break;
                }
            }
            if (registration == null) {
                registration = new Registration(context, receiver, handler);
                receivers.add(registration);
            }
            registration.filters.add(new Subscription(filter, permission));
        }
    }

    static void unregister(Context context, BroadcastReceiver receiver) {
        if (receiver == null) throw new IllegalArgumentException("receiver == null");
        synchronized (receivers) {
            for (int i = 0; i < receivers.size(); i++) {
                Registration registration = receivers.get(i);
                if (registration.context == context && registration.receiver == receiver) {
                    registration.registered = false;
                    receivers.remove(i);
                    return;
                }
            }
        }
        throw new IllegalArgumentException("Receiver not registered");
    }

    private static boolean matches(IntentFilter filter, Intent intent) {
        String action = intent.getAction();
        if (action != null && !filter.hasAction(action)) return false;
        // Filters without data rules match only intents without data or a type.
        if (intent.getData() != null || intent.getType() != null) return false;
        Set<String> categories = intent.getCategories();
        if (categories != null) {
            for (String category : categories) if (!filter.hasCategory(category)) return false;
        }
        return true;
    }

    private static boolean permitted(Context context, String permission) {
        return permission == null
                || context.checkCallingOrSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    static void send(Context sender, Intent intent, String receiverPermission) {
        if (intent == null) throw new NullPointerException("intent == null");
        if (intent.getComponent() != null) return;
        final Intent sent = new Intent(intent);
        Registration[] snapshot;
        Subscription[][] filters;
        synchronized (receivers) {
            snapshot = receivers.toArray(new Registration[receivers.size()]);
            filters = new Subscription[snapshot.length][];
            for (int i = 0; i < snapshot.length; i++) {
                ArrayList<Subscription> subscriptions = snapshot[i].filters;
                filters[i] = subscriptions.toArray(new Subscription[subscriptions.size()]);
            }
        }
        for (int i = 0; i < snapshot.length; i++) {
            final Registration registration = snapshot[i];
            if (!registration.registered || !permitted(registration.context, receiverPermission)) continue;
            String target = sent.getPackage();
            if (target != null && !target.equals(registration.context.getPackageName())) continue;
            boolean match = false;
            for (Subscription subscription : filters[i]) {
                if (matches(subscription.filter, sent) && permitted(sender, subscription.permission)) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;
            final Intent delivered = new Intent(sent);
            registration.handler.post(new Runnable() {
                public void run() {
                    if (registration.registered)
                        registration.receiver.onReceive(registration.context, delivered);
                }
            });
        }
    }
}
