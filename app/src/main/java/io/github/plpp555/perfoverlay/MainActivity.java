package io.github.plpp555.perfoverlay;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Toast;

/**
 * 画面を持たない起動用アクティビティ。
 * アイコンを押すと右上の表示を開始し（表示中なら停止し）、すぐに閉じる。
 */
public class MainActivity extends Activity {
    private static final int REQ_CAPTURE = 1;

    /** 「重ねて表示」の設定画面から戻ってくるのを待っている。 */
    private boolean awaitingOverlayPermission;
    private boolean paused;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (OverlayService.sMode != OverlayService.MODE_NONE) {
            stopService(new Intent(this, OverlayService.class));
            Toast.makeText(this, R.string.toast_stopped, Toast.LENGTH_SHORT).show();
            finish();
            return;
        }
        if (OverlayService.canDrawOverlays(this)) {
            requestCapture();
        } else {
            Toast.makeText(this, R.string.toast_need_overlay, Toast.LENGTH_LONG).show();
            awaitingOverlayPermission = openOverlaySettings();
            if (!awaitingOverlayPermission) {
                finish();
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        paused = true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (awaitingOverlayPermission && paused) {
            awaitingOverlayPermission = false;
            if (OverlayService.canDrawOverlays(this)) {
                requestCapture();
            } else {
                finish();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_CAPTURE) {
            return;
        }
        if (resultCode == RESULT_OK && data != null) {
            OverlayService.start(this, OverlayService.MODE_CAPTURE, resultCode, data);
        } else {
            // 画面キャプチャを断られたら FPS の代わりに Hz を表示する
            OverlayService.start(this, OverlayService.MODE_SIMPLE, 0, null);
        }
        finish();
    }

    /** FPS を実測するための画面キャプチャの許可をシステムに求める。 */
    private void requestCapture() {
        MediaProjectionManager mpm = (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        try {
            startActivityForResult(mpm.createScreenCaptureIntent(), REQ_CAPTURE);
        } catch (ActivityNotFoundException e) {
            OverlayService.start(this, OverlayService.MODE_SIMPLE, 0, null);
            finish();
        }
    }

    private boolean openOverlaySettings() {
        if (Build.VERSION.SDK_INT < 23) {
            return false;
        }
        Uri pkg = Uri.parse("package:" + getPackageName());
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, pkg));
            return true;
        } catch (ActivityNotFoundException e) {
            try {
                startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkg));
                return true;
            } catch (ActivityNotFoundException e2) {
                return false;
            }
        }
    }
}
