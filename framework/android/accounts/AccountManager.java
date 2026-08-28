package android.accounts;

import android.content.Context;
import android.os.Handler;
import java.util.Map;
import java.util.HashMap;

public class AccountManager {
    public static final String KEY_ACCOUNT_NAME = "authAccount";
    public static final String KEY_ACCOUNT_TYPE = "accountType";
    public static final String KEY_AUTHTOKEN = "authtoken";

    private final Context mContext;

    public AccountManager(Context context) { mContext = context; }
    public static AccountManager get(Context context) { return new AccountManager(context); }
    public Account[] getAccounts() { return new Account[0]; }
    public Account[] getAccountsByType(String type) { return new Account[0]; }
    public String getPassword(Account account) { return null; }
    public String getUserData(Account account, String key) { return null; }
    public String peekAuthToken(Account account, String authTokenType) { return null; }
    public void setPassword(Account account, String password) {}
    public void clearPassword(Account account) {}
    public void setUserData(Account account, String key, String value) {}
    public void setAuthToken(Account account, String authTokenType, String authToken) {}
}
