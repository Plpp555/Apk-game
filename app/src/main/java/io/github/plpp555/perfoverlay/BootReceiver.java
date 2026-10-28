package io.github.plpp555.perfoverlay;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/** 「起動時に自動で開始」がオンなら、端末起動時・アプリ更新時にキャプチャなしモードで開始する。 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action) && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            return;
        }
        if (!Prefs.get(context).getBoolean(Prefs.KEY_AUTOSTART, false)
                || !OverlayService.canDrawOverlays(context)) {
            return;
        }
        try {
            OverlayService.start(context, OverlayService.MODE_SIMPLE, 0, null);
        } catch (RuntimeException e) {
            Log.w("PerfOverlay", "Auto start failed", e);
        }
    }
}
