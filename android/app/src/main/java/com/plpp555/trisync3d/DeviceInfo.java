package com.plpp555.trisync3d;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Build;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/** Human readable device description shown in the UI and stored with results. */
final class DeviceInfo {
    private DeviceInfo() {}

    static String model() {
        String m = Build.MODEL == null ? "" : Build.MODEL;
        String mf = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER;
        if (m.toLowerCase(Locale.US).startsWith(mf.toLowerCase(Locale.US))) return m;
        return mf + " " + m;
    }

    static String soc() {
        // Build.SOC_MANUFACTURER / SOC_MODEL exist from API 31; read them reflectively.
        try {
            String mf = (String) Build.class.getField("SOC_MANUFACTURER").get(null);
            String md = (String) Build.class.getField("SOC_MODEL").get(null);
            if (md != null && !md.isEmpty() && !"unknown".equalsIgnoreCase(md)) {
                return (mf != null && !"unknown".equalsIgnoreCase(mf) ? mf + " " : "") + md;
            }
        } catch (Exception ignored) {
            // older Android
        }
        return Build.HARDWARE;
    }

    static String cpu() {
        int n = Runtime.getRuntime().availableProcessors();
        TreeMap<Integer, Integer> clusters = new TreeMap<Integer, Integer>();
        for (int i = 0; i < n; i++) {
            int khz = readInt("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
            if (khz <= 0) continue;
            Integer c = clusters.get(khz);
            clusters.put(khz, c == null ? 1 : c + 1);
        }
        StringBuilder sb = new StringBuilder();
        sb.append(n).append(" cores");
        if (!clusters.isEmpty()) {
            sb.append(" (");
            boolean first = true;
            for (Map.Entry<Integer, Integer> e : clusters.entrySet()) {
                if (!first) sb.append(" + ");
                sb.append(e.getValue()).append("×")
                        .append(String.format(Locale.US, "%.2f GHz", e.getKey() / 1.0e6));
                first = false;
            }
            sb.append(")");
        }
        String abi = Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "";
        if (!abi.isEmpty()) sb.append(" · ").append(abi);
        return sb.toString();
    }

    static String memory(Context ctx) {
        ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return "";
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return String.format(Locale.US, "%.1f GB RAM", mi.totalMem / (1024.0 * 1024.0 * 1024.0));
    }

    static String os() {
        return "Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")";
    }

    static String summary(Context ctx) {
        return model() + "\n" + soc() + " · " + cpu() + "\n" + memory(ctx) + " · " + os();
    }

    private static int readInt(String path) {
        BufferedReader r = null;
        try {
            r = new BufferedReader(new FileReader(path));
            String line = r.readLine();
            return line == null ? -1 : Integer.parseInt(line.trim());
        } catch (Exception e) {
            return -1;
        } finally {
            if (r != null) {
                try {
                    r.close();
                } catch (Exception ignored) {
                    // ignore
                }
            }
        }
    }
}
