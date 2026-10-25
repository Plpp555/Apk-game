package com.plpp555.trisync3d;

import android.app.Activity;
import android.content.Intent;
import android.content.res.Resources;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Single-screen UI: device info, live preview, controls, results and history. */
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private static final long POLL_MS = 100;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private SurfaceView surfaceView;
    private TextView overlayText;
    private TextView gpuText;
    private TextView messageText;
    private ProgressBar progressBar;
    private Button startButton;
    private Button quickButton;
    private Button abortButton;
    private LinearLayout resultCard;
    private LinearLayout historyList;
    private HistoryStore history;
    private boolean polling;
    private int runMode = NativeBridge.MODE_STANDARD;
    private String gpuRenderer = "";

    private final Runnable statusPoller = new Runnable() {
        @Override
        public void run() {
            pollOnce();
            if (polling) handler.postDelayed(this, POLL_MS);
        }
    };

    private final Runnable gpuInfoPoller = new Runnable() {
        @Override
        public void run() {
            try {
                JSONObject gi = new JSONObject(NativeBridge.nativeGpuInfo());
                if (gi.optBoolean("ready")) {
                    gpuRenderer = gi.optString("renderer");
                    gpuText.setText(gi.optString("renderer") + "\n" + gi.optString("version"));
                    return;
                }
                if (gi.has("error")) {
                    gpuText.setText(gi.optString("error"));
                    startButton.setEnabled(false);
                    quickButton.setEnabled(false);
                    return;
                }
            } catch (JSONException ignored) {
                // not ready yet
            }
            handler.postDelayed(this, 200);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        NativeBridge.nativeInit(Runtime.getRuntime().availableProcessors());
        history = new HistoryStore(this);
        setContentView(buildUi());
        surfaceView.getHolder().addCallback(this);
        refreshHistory();
        handler.post(gpuInfoPoller);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Pick up a result that was produced (or an abort) while we were away.
        polling = true;
        handler.removeCallbacks(statusPoller);
        handler.post(statusPoller);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (!NativeBridge.nativeIsRunning()) {
            polling = false;
            handler.removeCallbacks(statusPoller);
        }
    }

    // ---------------------------------------------------------------- Surface

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        NativeBridge.nativeSetSurface(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // The render thread queries the EGL surface size every frame.
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        NativeBridge.nativeSetSurface(null);  // aborts a running benchmark
    }

    // ---------------------------------------------------------------- Control

    private void startRun(int mode) {
        if (!NativeBridge.nativeStart(mode)) return;
        runMode = mode;
        setRunning(true);
        messageText.setVisibility(View.GONE);
        overlayText.setText(getString(R.string.stage_loading, testName("gt1")));
        progressBar.setProgress(0);
        polling = true;
        handler.removeCallbacks(statusPoller);
        handler.post(statusPoller);
    }

    private void setRunning(boolean running) {
        startButton.setEnabled(!running);
        quickButton.setEnabled(!running);
        abortButton.setVisibility(running ? View.VISIBLE : View.GONE);
        if (running) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    private void pollOnce() {
        try {
            updateStatus(new JSONObject(NativeBridge.nativeStatus()));
        } catch (JSONException ignored) {
            // status not yet available
        }
        String res = NativeBridge.nativeTakeResult();
        if (res != null) {
            polling = false;
            onRunFinished(res);
        } else if (!NativeBridge.nativeIsRunning()) {
            polling = false;
            setRunning(false);
        }
    }

    private void updateStatus(JSONObject st) {
        String stage = st.optString("stage");
        String name = testName(st.optString("test", "gt1"));
        int frame = st.optInt("frame");
        int frames = st.optInt("frames");
        if ("loading".equals(stage)) {
            overlayText.setText(getString(R.string.stage_loading, name));
        } else if ("warmup".equals(stage)) {
            overlayText.setText(getString(R.string.stage_warmup, name, frame, frames));
        } else if ("running".equals(stage)) {
            overlayText.setText(getString(R.string.stage_running, name, frame, frames, st.optDouble("fps")));
        } else if ("cpu_single".equals(stage)) {
            overlayText.setText(getString(R.string.stage_cpu_single, frame, frames));
        } else if ("cpu_multi".equals(stage)) {
            overlayText.setText(getString(R.string.stage_cpu_multi, frame, frames));
        } else if ("done".equals(stage)) {
            overlayText.setText(R.string.stage_done);
        } else if ("failed".equals(stage)) {
            overlayText.setText(getString(R.string.stage_failed, st.optString("error")));
        }
        progressBar.setProgress((int) Math.round(st.optDouble("progress", 0) * 1000));
    }

    private void onRunFinished(String json) {
        setRunning(false);
        JSONObject r;
        try {
            r = new JSONObject(json);
        } catch (JSONException e) {
            return;
        }
        String stage = r.optString("stage");
        if ("aborted".equals(stage)) {
            overlayText.setText(R.string.stage_aborted);
            showMessage(getString(R.string.stage_aborted));
            return;
        }
        if (!"done".equals(stage)) {
            String err = getString(R.string.stage_failed, r.optString("error"));
            overlayText.setText(err);
            showMessage(err);
            return;
        }
        progressBar.setProgress(1000);
        overlayText.setText(R.string.stage_done);
        showResult(r);
        saveHistory(r);
    }

    // ---------------------------------------------------------------- Results

    private void showResult(final JSONObject r) {
        resultCard.removeAllViews();
        resultCard.setVisibility(View.VISIBLE);
        resultCard.addView(sectionTitle(R.string.section_result));

        JSONObject scores = r.optJSONObject("scores");
        if (scores == null) scores = new JSONObject();
        int verified = 0;
        int mismatched = 0;
        JSONArray tests = r.optJSONArray("tests");
        if (tests == null) tests = new JSONArray();
        JSONObject cpu = r.optJSONObject("cpu");
        if (cpu == null) cpu = new JSONObject();
        for (int i = 0; i < tests.length(); i++) {
            String v = tests.optJSONObject(i).optString("verify");
            if ("match".equals(v)) verified++;
            if ("mismatch".equals(v)) mismatched++;
        }
        if ("match".equals(cpu.optString("verify"))) verified++;
        if ("mismatch".equals(cpu.optString("verify"))) mismatched++;
        boolean official = r.optBoolean("official");

        TextView badge = text(13, true);
        if (mismatched > 0 || !cpu.optBoolean("threadsConsistent", true)) {
            badge.setText(R.string.badge_invalid);
            badge.setTextColor(color(R.color.bad));
        } else if (official) {
            badge.setText(R.string.badge_official);
            badge.setTextColor(color(R.color.ok));
        } else {
            badge.setText(R.string.badge_unofficial);
            badge.setTextColor(color(R.color.accent3));
        }
        resultCard.addView(badge);

        TextView overallLabel = text(14, false);
        overallLabel.setText(R.string.score_overall);
        overallLabel.setTextColor(color(R.color.text_dim));
        overallLabel.setPadding(0, dp(8), 0, 0);
        resultCard.addView(overallLabel);
        TextView overall = text(48, true);
        overall.setText(String.valueOf(scores.optInt("overall")));
        overall.setTextColor(color(R.color.accent));
        resultCard.addView(overall);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.addView(scoreCell(R.string.score_graphics, scores.optInt("graphics"), R.color.accent2));
        row.addView(scoreCell(R.string.score_cpu, scores.optInt("cpu"), R.color.accent3));
        row.addView(scoreCell(R.string.score_cpu_single, scores.optInt("cpuSingle"), R.color.text_dim));
        resultCard.addView(row);

        resultCard.addView(sectionTitle(R.string.section_tests));
        for (int i = 0; i < tests.length(); i++) {
            JSONObject t = tests.optJSONObject(i);
            if (t == null || !t.optBoolean("ran")) continue;
            resultCard.addView(testRow(t));
        }

        resultCard.addView(sectionTitle(R.string.section_cpu));
        TextView cpuRates = text(14, false);
        cpuRates.setText(getString(R.string.cpu_rates, cpu.optDouble("stRate") / 1e6, cpu.optDouble("mtRate") / 1e6));
        resultCard.addView(cpuRates);
        TextView cpuScale = text(13, false);
        cpuScale.setTextColor(color(R.color.text_dim));
        cpuScale.setText(getString(R.string.cpu_scaling, cpu.optDouble("scaling"), r.optInt("threads"),
                cpu.optDouble("utilization") * 100.0));
        resultCard.addView(cpuScale);

        resultCard.addView(sectionTitle(R.string.section_verify));
        TextView summary = text(14, false);
        summary.setText(getString(R.string.verify_summary, verified, tests.length() + 1));
        summary.setTextColor(color(mismatched > 0 ? R.color.bad : R.color.ok));
        resultCard.addView(summary);
        for (int i = 0; i < tests.length(); i++) {
            JSONObject t = tests.optJSONObject(i);
            resultCard.addView(verifyRow(testName(t.optString("id")), t.optString("hash"), t.optString("verify")));
        }
        resultCard.addView(verifyRow("CPU", cpu.optString("hash"), cpu.optString("verify")));
        TextView threads = text(12, false);
        boolean consistent = cpu.optBoolean("threadsConsistent");
        threads.setText((consistent ? "✓ " : "✗ ")
                + getString(consistent ? R.string.verify_threads_ok : R.string.verify_threads_bad));
        threads.setTextColor(color(consistent ? R.color.ok : R.color.bad));
        resultCard.addView(threads);

        JSONObject gpu = r.optJSONObject("gpu");
        TextView meta = text(12, false);
        meta.setTextColor(color(R.color.text_dim));
        meta.setPadding(0, dp(10), 0, 0);
        meta.setText("v" + r.optString("version") + " · " + r.optString("resolution") + " · "
                + (gpu != null ? gpu.optString("renderer") + (gpu.optBoolean("hdr") ? " · FP16" : " · RGB10A2") : "")
                + " · " + r.optInt("threads") + " threads");
        resultCard.addView(meta);

        Button share = button(R.string.btn_share, false);
        share.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                shareResult(r);
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46));
        lp.topMargin = dp(12);
        resultCard.addView(share, lp);
    }

    private View scoreCell(int labelRes, int value, int colorRes) {
        LinearLayout cell = new LinearLayout(this);
        cell.setOrientation(LinearLayout.VERTICAL);
        TextView v = text(22, true);
        v.setText(String.valueOf(value));
        v.setTextColor(color(colorRes));
        TextView l = text(12, false);
        l.setText(labelRes);
        l.setTextColor(color(R.color.text_dim));
        cell.addView(v);
        cell.addView(l);
        cell.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return cell;
    }

    private View testRow(JSONObject t) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, dp(6), 0, dp(6));
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        TextView name = text(15, true);
        name.setText(testName(t.optString("id")));
        name.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView fps = text(15, true);
        fps.setText(getString(R.string.fmt_fps, t.optDouble("fps")));
        fps.setTextColor(color(R.color.accent));
        head.addView(name);
        head.addView(fps);
        box.addView(head);
        TextView desc = text(12, false);
        desc.setTextColor(color(R.color.text_dim));
        desc.setText(testDesc(t.optString("id")));
        box.addView(desc);
        double gpuMs = t.optDouble("gpuMs", -1);
        String gpuStr = gpuMs > 0 ? String.format(Locale.US, "%.2f ms", gpuMs) : getString(R.string.gpu_na);
        TextView detail = text(12, false);
        detail.setTextColor(color(R.color.text_dim));
        detail.setText(getString(R.string.fmt_test_detail, t.optDouble("lowFps"), t.optDouble("cpuMs"), gpuStr,
                boundName(t.optString("bound"))));
        box.addView(detail);
        return box;
    }

    private View verifyRow(String name, String hash, String verify) {
        TextView tv = text(12, false);
        tv.setTypeface(Typeface.MONOSPACE);
        String mark;
        int c;
        if ("match".equals(verify)) {
            mark = "✓ " + getString(R.string.verify_match);
            c = R.color.ok;
        } else if ("mismatch".equals(verify)) {
            mark = "✗ " + getString(R.string.verify_mismatch);
            c = R.color.bad;
        } else {
            mark = "– " + getString(R.string.verify_unknown);
            c = R.color.text_dim;
        }
        tv.setText(name + "  " + hash + "\n   " + mark);
        tv.setTextColor(color(c));
        tv.setPadding(0, dp(3), 0, dp(3));
        return tv;
    }

    private void shareResult(JSONObject r) {
        JSONObject s = r.optJSONObject("scores");
        StringBuilder sb = new StringBuilder();
        sb.append("TriSync 3D v").append(r.optString("version")).append(" [")
                .append("quick".equals(r.optString("mode")) ? "Quick" : "Standard").append("]\n");
        if (s != null) {
            sb.append("Overall ").append(s.optInt("overall")).append(" | Graphics ").append(s.optInt("graphics"))
                    .append(" | CPU ").append(s.optInt("cpu")).append(" (1T ").append(s.optInt("cpuSingle"))
                    .append(")\n");
        }
        JSONArray tests = r.optJSONArray("tests");
        if (tests != null) {
            for (int i = 0; i < tests.length(); i++) {
                JSONObject t = tests.optJSONObject(i);
                sb.append(t.optString("id").toUpperCase(Locale.US)).append(' ')
                        .append(String.format(Locale.US, "%.1f fps", t.optDouble("fps")))
                        .append(" [").append(t.optString("verify")).append("]\n");
            }
        }
        JSONObject gpu = r.optJSONObject("gpu");
        sb.append(DeviceInfo.model()).append(" · ").append(DeviceInfo.soc()).append('\n');
        if (gpu != null) sb.append(gpu.optString("renderer")).append('\n');
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, getString(R.string.share_subject));
        send.putExtra(Intent.EXTRA_TEXT, sb.toString());
        startActivity(Intent.createChooser(send, getString(R.string.btn_share)));
    }

    private void saveHistory(JSONObject r) {
        try {
            JSONObject s = r.optJSONObject("scores");
            JSONObject e = new JSONObject();
            e.put("time", System.currentTimeMillis());
            e.put("mode", r.optString("mode"));
            e.put("official", r.optBoolean("official"));
            e.put("overall", s != null ? s.optInt("overall") : 0);
            e.put("graphics", s != null ? s.optInt("graphics") : 0);
            e.put("cpu", s != null ? s.optInt("cpu") : 0);
            e.put("gpu", gpuRenderer);
            history.add(e);
        } catch (JSONException ignored) {
            // history is best effort
        }
        refreshHistory();
    }

    private void refreshHistory() {
        historyList.removeAllViews();
        JSONArray runs = history.load();
        if (runs.length() == 0) {
            TextView none = text(13, false);
            none.setText(R.string.history_empty);
            none.setTextColor(color(R.color.text_dim));
            historyList.addView(none);
            return;
        }
        SimpleDateFormat fmt = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault());
        for (int i = 0; i < runs.length(); i++) {
            JSONObject e = runs.optJSONObject(i);
            if (e == null) continue;
            TextView tv = text(13, false);
            String mode = getString("quick".equals(e.optString("mode")) ? R.string.mode_quick : R.string.mode_standard);
            tv.setText(getString(R.string.history_item, fmt.format(new Date(e.optLong("time"))), mode,
                    e.optInt("overall"), e.optInt("graphics"), e.optInt("cpu")));
            tv.setPadding(0, dp(3), 0, dp(3));
            historyList.addView(tv);
        }
    }

    private void showMessage(String msg) {
        messageText.setText(msg);
        messageText.setVisibility(View.VISIBLE);
    }

    // ---------------------------------------------------------------- Layout

    private View buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(16), dp(16), dp(24));
        scroll.addView(root);

        TextView title = text(26, true);
        title.setText(R.string.app_name);
        root.addView(title);
        TextView subtitle = text(13, false);
        subtitle.setText(R.string.subtitle);
        subtitle.setTextColor(color(R.color.text_dim));
        root.addView(subtitle);

        // Device card
        LinearLayout device = card();
        device.addView(sectionTitle(R.string.section_device));
        TextView deviceText = text(14, false);
        deviceText.setText(DeviceInfo.summary(this));
        device.addView(deviceText);
        gpuText = text(13, false);
        gpuText.setTextColor(color(R.color.accent));
        gpuText.setText(R.string.gpu_waiting);
        gpuText.setPadding(0, dp(6), 0, 0);
        device.addView(gpuText);
        root.addView(device, cardParams());

        // Preview
        AspectFrameLayout preview = new AspectFrameLayout(this);
        preview.setBackgroundColor(Color.BLACK);
        surfaceView = new SurfaceView(this);
        preview.addView(surfaceView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        overlayText = text(13, true);
        overlayText.setBackgroundColor(0x99000000);
        overlayText.setPadding(dp(8), dp(4), dp(8), dp(4));
        overlayText.setText(R.string.idle_hint);
        FrameLayout.LayoutParams olp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START);
        olp.setMargins(dp(8), dp(8), dp(8), dp(8));
        preview.addView(overlayText, olp);
        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(1000);
        FrameLayout.LayoutParams plp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(6),
                Gravity.BOTTOM);
        preview.addView(progressBar, plp);
        LinearLayout.LayoutParams prevLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        prevLp.topMargin = dp(12);
        root.addView(preview, prevLp);

        // Controls
        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.HORIZONTAL);
        startButton = button(R.string.btn_start, true);
        quickButton = button(R.string.btn_quick, false);
        abortButton = button(R.string.btn_abort, false);
        abortButton.setVisibility(View.GONE);
        startButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startRun(NativeBridge.MODE_STANDARD);
            }
        });
        quickButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startRun(NativeBridge.MODE_QUICK);
            }
        });
        abortButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                NativeBridge.nativeAbort();
            }
        });
        controls.addView(startButton, weighted(2f, 0));
        controls.addView(quickButton, weighted(1f, dp(8)));
        controls.addView(abortButton, weighted(1f, dp(8)));
        LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(50));
        clp.topMargin = dp(12);
        root.addView(controls, clp);

        messageText = text(13, false);
        messageText.setTextColor(color(R.color.accent3));
        messageText.setVisibility(View.GONE);
        messageText.setPadding(0, dp(8), 0, 0);
        root.addView(messageText);

        resultCard = card();
        resultCard.setVisibility(View.GONE);
        root.addView(resultCard, cardParams());

        LinearLayout hist = card();
        LinearLayout histHead = new LinearLayout(this);
        histHead.setOrientation(LinearLayout.HORIZONTAL);
        histHead.setGravity(Gravity.CENTER_VERTICAL);
        View histTitle = sectionTitle(R.string.section_history);
        histHead.addView(histTitle, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView clear = text(13, false);
        clear.setText(R.string.btn_clear_history);
        clear.setTextColor(color(R.color.accent));
        clear.setPadding(dp(8), dp(8), 0, dp(4));
        clear.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                history.clear();
                refreshHistory();
            }
        });
        histHead.addView(clear);
        hist.addView(histHead);
        historyList = new LinearLayout(this);
        historyList.setOrientation(LinearLayout.VERTICAL);
        hist.addView(historyList);
        root.addView(hist, cardParams());

        LinearLayout about = card();
        about.addView(sectionTitle(R.string.section_about));
        TextView aboutText = text(13, false);
        aboutText.setTextColor(color(R.color.text_dim));
        aboutText.setText(R.string.about_text);
        about.addView(aboutText);
        root.addView(about, cardParams());
        return scroll;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackgroundResource(R.drawable.bg_card);
        c.setPadding(dp(14), dp(10), dp(14), dp(14));
        return c;
    }

    private LinearLayout.LayoutParams cardParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(12);
        return lp;
    }

    private LinearLayout.LayoutParams weighted(float weight, int leftMargin) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, weight);
        lp.leftMargin = leftMargin;
        return lp;
    }

    private TextView sectionTitle(int res) {
        TextView t = text(12, true);
        t.setText(getString(res).toUpperCase(Locale.getDefault()));
        t.setTextColor(color(R.color.text_dim));
        t.setLetterSpacing(0.08f);
        t.setPadding(0, dp(8), 0, dp(4));
        return t;
    }

    private TextView text(float sp, boolean bold) {
        TextView t = new TextView(this);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color(R.color.text));
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private Button button(int res, boolean primary) {
        Button b = new Button(this);
        b.setText(res);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackgroundResource(primary ? R.drawable.bg_button_primary : R.drawable.bg_button_secondary);
        b.setTextColor(primary ? color(R.color.bg) : color(R.color.accent));
        b.setStateListAnimator(null);
        return b;
    }

    private String testName(String id) {
        if ("gt1".equals(id)) return getString(R.string.test_gt1);
        if ("gt2".equals(id)) return getString(R.string.test_gt2);
        if ("gt3".equals(id)) return getString(R.string.test_gt3);
        return getString(R.string.section_cpu);
    }

    private String testDesc(String id) {
        if ("gt1".equals(id)) return getString(R.string.test_gt1_desc);
        if ("gt2".equals(id)) return getString(R.string.test_gt2_desc);
        return getString(R.string.test_gt3_desc);
    }

    private String boundName(String b) {
        if ("cpu".equals(b)) return getString(R.string.bound_cpu);
        if ("gpu".equals(b)) return getString(R.string.bound_gpu);
        if ("driver".equals(b)) return getString(R.string.bound_driver);
        return getString(R.string.bound_balanced);
    }

    private int color(int res) {
        return getResources().getColor(res, getTheme());
    }

    private int dp(int v) {
        return Math.round(v * Resources.getSystem().getDisplayMetrics().density);
    }
}
