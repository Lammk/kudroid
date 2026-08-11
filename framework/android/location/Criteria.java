package android.location;

/**
 * mô phỏng android.location.criteria.
 *
 * mô tả các tiêu chí để chọn nhà cung cấp vị trí. đối với khuôn khổ tối thiểu
 * của kudroid, đây là một mô phỏng.
 */
public class Criteria {
    /** độ chính xác: không có yêu cầu. */
    public static final int NO_REQUIREMENT = 0;
    /** độ chính xác: thấp. */
    public static final int ACCURACY_LOW = 1;
    /** độ chính xác: trung bình. */
    public static final int ACCURACY_MEDIUM = 2;
    /** độ chính xác: cao. */
    public static final int ACCURACY_HIGH = 3;

    /** năng lượng: không có yêu cầu. */
    public static final int POWER_LOW = 1;
    /** năng lượng: trung bình. */
    public static final int POWER_MEDIUM = 2;
    /** năng lượng: cao. */
    public static final int POWER_HIGH = 3;

    private int mAccuracy = NO_REQUIREMENT;
    private int mPowerRequirement = NO_REQUIREMENT;

    public Criteria() {
    }

    public Criteria(Criteria criteria) {
        if (criteria != null) {
            mAccuracy = criteria.mAccuracy;
            mPowerRequirement = criteria.mPowerRequirement;
        }
    }

    public int getAccuracy() {
        return mAccuracy;
    }

    public void setAccuracy(int accuracy) {
        mAccuracy = accuracy;
    }

    public int getPowerRequirement() {
        return mPowerRequirement;
    }

    public void setPowerRequirement(int powerRequirement) {
        mPowerRequirement = powerRequirement;
    }
}