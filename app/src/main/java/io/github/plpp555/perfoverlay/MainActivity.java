package io.github.plpp555.perfoverlay;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.media.projection.MediaProjectionManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final int REQ_CAPTURE = 1;
    private static final int REQ_NOTIFICATIONS = 2;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            refreshState();
        }
    };

    private SharedPreferences prefs;
    private Button overlayButton;
    private Button notificationButton;
    private Button stopButton;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        prefs = Prefs.get(this);

        overlayButton = findViewById(R.id.btn_overlay_permission);
        notificationButton = findViewById(R.id.btn_notification_permission);
        stopButton = findViewById(R.id.btn_stop);
        status = findViewById(R.id.status);

        overlayButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                openOverlaySettings();
            }
        });
        notificationButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                requestNotificationPermission();
            }
        });
        findViewById(R.id.btn_start_capture).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startWithCapture();
            }
        });
        findViewById(R.id.btn_start_simple).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (ensureOverlayPermission()) {
                    OverlayService.start(MainActivity.this, OverlayService.MODE_SIMPLE, 0, null);
                    scheduleRefresh();
                }
            }
        });
        stopButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                stopService(new Intent(MainActivity.this, OverlayService.class));
                scheduleRefresh();
            }
        });

        bindSeekBar(R.id.seek_text_size, R.id.label_text_size, R.string.text_size, Prefs.KEY_TEXT_SIZE_SP,
                Prefs.MIN_TEXT_SIZE_SP, Prefs.MAX_TEXT_SIZE_SP, Prefs.DEF_TEXT_SIZE_SP);
        bindSeekBar(R.id.seek_bg_alpha, R.id.label_bg_alpha, R.string.bg_alpha, Prefs.KEY_BG_ALPHA_PCT,
                0, 100, Prefs.DEF_BG_ALPHA_PCT);
        bindSeekBar(R.id.seek_offset_x, R.id.label_offset_x, R.string.offset_x, Prefs.KEY_OFFSET_X_DP,
                0, Prefs.MAX_OFFSET_DP, Prefs.DEF_OFFSET_X_DP);
        bindSeekBar(R.id.seek_offset_y, R.id.label_offset_y, R.string.offset_y, Prefs.KEY_OFFSET_Y_DP,
                0, Prefs.MAX_OFFSET_DP, Prefs.DEF_OFFSET_Y_DP);

        CheckBox autostart = findViewById(R.id.check_autostart);
        autostart.setChecked(prefs.getBoolean(Prefs.KEY_AUTOSTART, false));
        autostart.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                prefs.edit().putBoolean(Prefs.KEY_AUTOSTART, isChecked).apply();
            }
        });

        if (savedInstanceState == null && !hasNotificationPermission()) {
            requestNotificationPermission();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshState();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(refresh);
        super.onDestroy();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_CAPTURE) {
            return;
        }
        if (resultCode == RESULT_OK && data != null) {
            OverlayService.start(this, OverlayService.MODE_CAPTURE, resultCode, data);
            scheduleRefresh();
        } else {
            Toast.makeText(this, R.string.toast_capture_denied, Toast.LENGTH_LONG).show();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        refreshState();
    }

    private void startWithCapture() {
        if (!ensureOverlayPermission()) {
            return;
        }
        MediaProjectionManager mpm = (MediaProjectionManager) getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        try {
            startActivityForResult(mpm.createScreenCaptureIntent(), REQ_CAPTURE);
        } catch (ActivityNotFoundException e) {
            Toast.makeText(this, R.string.toast_capture_failed, Toast.LENGTH_LONG).show();
            OverlayService.start(this, OverlayService.MODE_SIMPLE, 0, null);
            scheduleRefresh();
        }
    }

    private boolean ensureOverlayPermission() {
        if (OverlayService.canDrawOverlays(this)) {
            return true;
        }
        Toast.makeText(this, R.string.toast_need_overlay, Toast.LENGTH_LONG).show();
        openOverlaySettings();
        return false;
    }

    private void openOverlaySettings() {
        if (Build.VERSION.SDK_INT < 23) {
            return;
        }
        Uri pkg = Uri.parse("package:" + getPackageName());
        try {
            startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, pkg));
        } catch (ActivityNotFoundException e) {
            try {
                startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, pkg));
            } catch (ActivityNotFoundException e2) {
                Toast.makeText(this, R.string.toast_overlay_settings_missing, Toast.LENGTH_LONG).show();
            }
        }
    }

    private boolean hasNotificationPermission() {
        return Build.VERSION.SDK_INT < 33
                || checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFICATIONS);
        }
    }

    private void bindSeekBar(int seekId, int labelId, final int formatRes, final String key,
                             final int min, int max, int def) {
        SeekBar seek = findViewById(seekId);
        final TextView label = findViewById(labelId);
        int value = Math.max(min, Math.min(max, prefs.getInt(key, def)));
        seek.setMax(max - min);
        seek.setProgress(value - min);
        label.setText(getString(formatRes, value));
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int v = progress + min;
                label.setText(getString(formatRes, v));
                if (fromUser) {
                    prefs.edit().putInt(key, v).apply();
                }
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });
    }

    /** サービスの開始・停止は非同期なので少し待ってから状態表示を更新する。 */
    private void scheduleRefresh() {
        handler.removeCallbacks(refresh);
        handler.postDelayed(refresh, 400);
        handler.postDelayed(refresh, 1500);
    }

    private void refreshState() {
        boolean overlayOk = OverlayService.canDrawOverlays(this);
        overlayButton.setEnabled(!overlayOk);
        overlayButton.setText(overlayOk ? R.string.overlay_granted : R.string.grant_overlay);

        if (Build.VERSION.SDK_INT >= 33) {
            boolean notifOk = hasNotificationPermission();
            notificationButton.setEnabled(!notifOk);
            notificationButton.setText(notifOk ? R.string.notifications_granted : R.string.grant_notifications);
        } else {
            notificationButton.setVisibility(View.GONE);
        }

        int mode = OverlayService.sMode;
        status.setText(mode == OverlayService.MODE_CAPTURE ? R.string.status_capture
                : mode == OverlayService.MODE_SIMPLE ? R.string.status_simple
                : R.string.status_stopped);
        stopButton.setEnabled(mode != OverlayService.MODE_NONE);
    }
}
