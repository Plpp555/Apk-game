package io.github.plpp555.perfoverlay;

import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.graphics.PixelFormat;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Toast;

import java.util.Locale;

/** オーバーレイを表示し続けるフォアグラウンドサービス。 */
public class OverlayService extends Service implements SharedPreferences.OnSharedPreferenceChangeListener {
    static final String ACTION_START = "io.github.plpp555.perfoverlay.START";
    static final String ACTION_STOP = "io.github.plpp555.perfoverlay.STOP";
    static final String EXTRA_MODE = "mode";
    static final String EXTRA_RESULT_CODE = "result_code";
    static final String EXTRA_RESULT_DATA = "result_data";

    static final int MODE_NONE = -1;
    static final int MODE_SIMPLE = 0;
    static final int MODE_CAPTURE = 1;

    /** MainActivity の状態表示用。 */
    static volatile int sMode = MODE_NONE;

    private static final String TAG = "PerfOverlay";
    private static final String CHANNEL_ID = "overlay";
    private static final int NOTIFICATION_ID = 1;
    private static final long INTERVAL_MS = 1000;
    private static final float GIB = 1024f * 1024f * 1024f;

    private static final int GOOD = 0xFF5CFF6B;
    private static final int WARN = 0xFFFFD54F;
    private static final int BAD = 0xFFFF5252;
    private static final int NEUTRAL = 0xFFFFFFFF;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final ActivityManager.MemoryInfo memInfo = new ActivityManager.MemoryInfo();
    private final String[] rowLabels = new String[OverlayView.MAX_ROWS];
    private final String[] rowValues = new String[OverlayView.MAX_ROWS];
    private final int[] rowColors = new int[OverlayView.MAX_ROWS];

    private WindowManager windowManager;
    private ActivityManager activityManager;
    private SharedPreferences prefs;
    private HandlerThread worker;
    private Handler workerHandler;
    private OverlayView overlay;
    private WindowManager.LayoutParams params;
    private FpsMeter fpsMeter;
    private boolean screenOn = true;

    /** ワーカースレッド専用。 */
    private CpuTempReader cpuTempReader;
    private volatile float cpuTemp = Float.NaN;
    private float batteryTemp = Float.NaN;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            update(false);
            workerHandler.post(readCpuTemp);
            handler.postDelayed(this, INTERVAL_MS);
        }
    };

    private final Runnable readCpuTemp = new Runnable() {
        @Override
        public void run() {
            if (cpuTempReader == null) {
                cpuTempReader = new CpuTempReader();
            }
            cpuTemp = cpuTempReader.read();
        }
    };

    private final BroadcastReceiver batteryReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            updateBattery(intent);
        }
    };

    private final BroadcastReceiver screenReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            setScreenOn(Intent.ACTION_SCREEN_ON.equals(intent.getAction()));
        }
    };

    static void start(Context context, int mode, int resultCode, Intent resultData) {
        Intent intent = new Intent(context, OverlayService.class)
                .setAction(ACTION_START)
                .putExtra(EXTRA_MODE, mode);
        if (resultData != null) {
            intent.putExtra(EXTRA_RESULT_CODE, resultCode);
            intent.putExtra(EXTRA_RESULT_DATA, resultData);
        }
        if (Build.VERSION.SDK_INT >= 26) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    static boolean canDrawOverlays(Context context) {
        return Build.VERSION.SDK_INT < 23 || Settings.canDrawOverlays(context);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        activityManager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        prefs = Prefs.get(this);
        prefs.registerOnSharedPreferenceChangeListener(this);

        worker = new HandlerThread("perf-sampler");
        worker.start();
        workerHandler = new Handler(worker.getLooper());
        workerHandler.post(readCpuTemp);

        Intent battery = registerReceiver(batteryReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            updateBattery(battery);
        }
        IntentFilter screenFilter = new IntentFilter(Intent.ACTION_SCREEN_ON);
        screenFilter.addAction(Intent.ACTION_SCREEN_OFF);
        registerReceiver(screenReceiver, screenFilter);
        screenOn = ((PowerManager) getSystemService(POWER_SERVICE)).isInteractive();

        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID,
                    getString(R.string.channel_name), NotificationManager.IMPORTANCE_LOW);
            channel.setShowBadge(false);
            ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(channel);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopSelf();
            return START_NOT_STICKY;
        }

        // Android 15 以降は「オーバーレイが表示中」であることがバックグラウンドからの
        // 再起動（START_STICKY）で startForeground する条件になるので、先に表示する。
        boolean overlayShown = showOverlay();

        int requested = intent != null ? intent.getIntExtra(EXTRA_MODE, MODE_SIMPLE) : MODE_SIMPLE;
        Intent resultData = intent != null ? getResultData(intent) : null;
        int resultCode = intent != null ? intent.getIntExtra(EXTRA_RESULT_CODE, 0) : 0;
        boolean capture = requested == MODE_CAPTURE && resultData != null;

        if (!startForegroundCompat(capture)) {
            if (!capture || !startForegroundCompat(false)) {
                stopSelf();
                return START_NOT_STICKY;
            }
            capture = false;
            Toast.makeText(this, R.string.toast_capture_failed, Toast.LENGTH_LONG).show();
        }
        if (!overlayShown) {
            Toast.makeText(this, R.string.toast_no_overlay_permission, Toast.LENGTH_LONG).show();
            stopSelf();
            return START_NOT_STICKY;
        }

        if (capture) {
            switchToCapture(resultCode, resultData);
        } else {
            switchToSimple();
        }
        handler.removeCallbacks(tick);
        update(true);
        if (screenOn) {
            handler.postDelayed(tick, INTERVAL_MS);
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        sMode = MODE_NONE;
        handler.removeCallbacks(tick);
        releaseFpsMeter();
        prefs.unregisterOnSharedPreferenceChangeListener(this);
        unregisterReceiver(batteryReceiver);
        unregisterReceiver(screenReceiver);
        if (overlay != null) {
            try {
                windowManager.removeView(overlay);
            } catch (RuntimeException ignored) {
            }
            overlay = null;
        }
        worker.quitSafely();
        super.onDestroy();
    }

    @Override
    public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
        if (overlay != null) {
            applyStyle();
            applyPosition();
        }
    }

    // ---- FPS 計測方式の切り替え ----

    private void switchToCapture(int resultCode, Intent resultData) {
        releaseFpsMeter();
        final FpsMeter[] self = new FpsMeter[1];
        try {
            self[0] = new CaptureFpsMeter(this, resultCode, resultData, new Runnable() {
                @Override
                public void run() {
                    if (fpsMeter == self[0]) {
                        onCaptureStopped();
                    }
                }
            });
        } catch (RuntimeException e) {
            Log.w(TAG, "Screen capture could not be started", e);
            Toast.makeText(this, R.string.toast_capture_failed, Toast.LENGTH_LONG).show();
            startForegroundCompat(false);
            switchToSimple();
            return;
        }
        fpsMeter = self[0];
        fpsMeter.setActive(screenOn);
        sMode = MODE_CAPTURE;
    }

    private void switchToSimple() {
        releaseFpsMeter();
        fpsMeter = new ChoreographerFpsMeter();
        fpsMeter.setActive(screenOn);
        sMode = MODE_SIMPLE;
    }

    /** ステータスバーのチップ等から画面キャプチャが止められた。 */
    private void onCaptureStopped() {
        Toast.makeText(this, R.string.toast_capture_stopped, Toast.LENGTH_LONG).show();
        startForegroundCompat(false);
        switchToSimple();
        update(true);
    }

    private void releaseFpsMeter() {
        if (fpsMeter != null) {
            fpsMeter.release();
            fpsMeter = null;
        }
    }

    // ---- 表示 ----

    private boolean showOverlay() {
        if (overlay != null) {
            return true;
        }
        // 権限の有無は addView の成否で判断する（Android 8.0 では許可直後の canDrawOverlays が false を返す不具合がある）
        overlay = new OverlayView(this);
        applyStyle();
        params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                overlayWindowType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.RIGHT;
        if (Build.VERSION.SDK_INT >= 31) {
            // Android 12 以降は不透明度 0.8 以下でないと、重なった部分のタッチが下のアプリに届かない
            params.alpha = 0.8f;
        }
        params.setTitle("PerfOverlay");
        applyPosition();
        try {
            windowManager.addView(overlay, params);
        } catch (RuntimeException e) {
            Log.w(TAG, "Overlay could not be added", e);
            overlay = null;
            return false;
        }
        return true;
    }

    @SuppressWarnings("deprecation")
    private static int overlayWindowType() {
        return Build.VERSION.SDK_INT >= 26
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
    }

    private void applyStyle() {
        overlay.setStyle(prefs.getInt(Prefs.KEY_TEXT_SIZE_SP, Prefs.DEF_TEXT_SIZE_SP),
                prefs.getInt(Prefs.KEY_BG_ALPHA_PCT, Prefs.DEF_BG_ALPHA_PCT));
    }

    private void applyPosition() {
        int x = dp(prefs.getInt(Prefs.KEY_OFFSET_X_DP, Prefs.DEF_OFFSET_X_DP));
        int y = dp(prefs.getInt(Prefs.KEY_OFFSET_Y_DP, Prefs.DEF_OFFSET_Y_DP));
        if (params.x == x && params.y == y) {
            return;
        }
        params.x = x;
        params.y = y;
        if (overlay.isAttachedToWindow()) {
            windowManager.updateViewLayout(overlay, params);
        }
    }

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
                getResources().getDisplayMetrics()));
    }

    /** @param placeholderFps 計測直後で FPS がまだ出せないときは "--" を表示する */
    private void update(boolean placeholderFps) {
        if (overlay == null) {
            return;
        }
        int n = 0;

        rowLabels[n] = fpsMeter != null ? fpsMeter.label() : "FPS";
        if (placeholderFps || fpsMeter == null) {
            rowValues[n] = "--";
            rowColors[n] = NEUTRAL;
        } else {
            float fps = fpsMeter.sample();
            rowValues[n] = String.valueOf(Math.round(fps));
            rowColors[n] = fps >= 50f ? GOOD : fps >= 30f ? WARN : BAD;
        }
        n++;

        activityManager.getMemoryInfo(memInfo);
        long total = memInfo.totalMem;
        long used = total - memInfo.availMem;
        float usedPct = total > 0 ? used * 100f / total : 0f;
        rowLabels[n] = "RAM";
        rowValues[n] = String.format(Locale.US, "%.1f/%.1fG", used / GIB, total / GIB);
        rowColors[n] = usedPct < 70f ? GOOD : usedPct < 85f ? WARN : BAD;
        n++;

        float cpu = cpuTemp;
        if (!Float.isNaN(cpu)) {
            rowLabels[n] = "CPU";
            rowValues[n] = String.format(Locale.US, "%.0f°C", cpu);
            rowColors[n] = cpu < 55f ? GOOD : cpu < 70f ? WARN : BAD;
            n++;
        }

        float bat = batteryTemp;
        if (!Float.isNaN(bat)) {
            rowLabels[n] = "BAT";
            rowValues[n] = String.format(Locale.US, "%.1f°C", bat);
            rowColors[n] = bat < 38f ? GOOD : bat < 43f ? WARN : BAD;
            n++;
        }

        overlay.setRows(n, rowLabels, rowValues, rowColors);
    }

    private void updateBattery(Intent intent) {
        int tenths = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
        batteryTemp = tenths == Integer.MIN_VALUE ? Float.NaN : tenths / 10f;
    }

    private void setScreenOn(boolean on) {
        if (screenOn == on) {
            return;
        }
        screenOn = on;
        if (fpsMeter != null) {
            fpsMeter.setActive(on);
        }
        handler.removeCallbacks(tick);
        if (on) {
            update(true);
            handler.postDelayed(tick, INTERVAL_MS);
        }
    }

    // ---- フォアグラウンド化と通知 ----

    private boolean startForegroundCompat(boolean capture) {
        Notification notification = buildNotification(capture);
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                int type = 0;
                if (Build.VERSION.SDK_INT >= 34) {
                    type |= ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE;
                }
                if (capture) {
                    type |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION;
                }
                startForeground(NOTIFICATION_ID, notification, type);
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
            return true;
        } catch (RuntimeException e) {
            Log.w(TAG, "startForeground failed (capture=" + capture + ")", e);
            return false;
        }
    }

    @SuppressWarnings("deprecation")
    private Notification buildNotification(boolean capture) {
        int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) {
            piFlags |= PendingIntent.FLAG_IMMUTABLE;
        }
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class), piFlags);
        PendingIntent stop = PendingIntent.getService(this, 1,
                new Intent(this, OverlayService.class).setAction(ACTION_STOP), piFlags);

        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this).setPriority(Notification.PRIORITY_LOW);
        return builder
                .setSmallIcon(R.drawable.ic_stat)
                .setContentTitle(getString(R.string.notif_title))
                .setContentText(getString(capture ? R.string.notif_text_capture : R.string.notif_text_simple))
                .setCategory(Notification.CATEGORY_SERVICE)
                .setOngoing(true)
                .setShowWhen(false)
                .setContentIntent(open)
                .addAction(new Notification.Action.Builder(R.drawable.ic_stat,
                        getString(R.string.action_stop), stop).build())
                .build();
    }

    @SuppressWarnings("deprecation")
    private static Intent getResultData(Intent intent) {
        // API 33 の型指定版 getParcelableExtra には不具合があるため 34 以降のみ使う
        if (Build.VERSION.SDK_INT >= 34) {
            return intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent.class);
        }
        return intent.getParcelableExtra(EXTRA_RESULT_DATA);
    }
}
