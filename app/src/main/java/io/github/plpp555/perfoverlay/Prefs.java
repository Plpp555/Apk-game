package io.github.plpp555.perfoverlay;

import android.content.Context;
import android.content.SharedPreferences;

/** 設定値（MainActivity で編集し、OverlayService が変更を監視する）。 */
final class Prefs {
    static final String KEY_TEXT_SIZE_SP = "text_size_sp";
    static final String KEY_OFFSET_X_DP = "offset_x_dp";
    static final String KEY_OFFSET_Y_DP = "offset_y_dp";
    static final String KEY_AUTOSTART = "autostart";

    static final int DEF_TEXT_SIZE_SP = 20;
    static final int DEF_OFFSET_X_DP = 4;
    static final int DEF_OFFSET_Y_DP = 4;

    static final int MIN_TEXT_SIZE_SP = 12;
    static final int MAX_TEXT_SIZE_SP = 60;
    static final int MAX_OFFSET_DP = 300;

    private Prefs() {
    }

    static SharedPreferences get(Context context) {
        return context.getSharedPreferences("settings", Context.MODE_PRIVATE);
    }
}
