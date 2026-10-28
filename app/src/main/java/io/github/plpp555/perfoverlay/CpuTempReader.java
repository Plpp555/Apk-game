package io.github.plpp555.perfoverlay;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * CPU 温度を sysfs から読む（root 不要）。
 * 機種や Android のバージョンによっては SELinux で読めないため、その場合は NaN を返す。
 * ファイル読み込みがあるのでワーカースレッドから呼ぶこと。
 */
final class CpuTempReader {
    private static final Pattern CPU_ZONE = Pattern.compile(
            "cpu|tsens_tz_sensor|mtktscpu|soc_thermal|cluster|apcs|^ap$|^ap_|big|little|mid|prime|gold|silver|exynos");
    private static final Pattern NOT_CPU_ZONE = Pattern.compile(
            "gpu|g3d|bat|chg|charg|usb|skin|modem|pmic|bms|xo|pa_|wifi|wlan|camera|disp|ddr|npu|nsp|tpu|isp");
    private static final String[] FALLBACK_PATHS = {
            "/sys/devices/system/cpu/cpu0/cpufreq/cpu_temp",
            "/sys/devices/system/cpu/cpu0/cpufreq/FakeShmoo_cpu_temp",
            "/sys/devices/platform/s5p-tmu/temperature",
            "/sys/devices/platform/s5p-tmu/curr_temp",
            "/sys/devices/platform/tegra_tmon/temp1_input",
            "/sys/devices/platform/omap/omap_temp_sensor.0/temperature",
            "/sys/class/i2c-adapter/i2c-4/4-004c/temperature",
            "/sys/class/hwmon/hwmon0/device/temp1_input",
            "/sys/class/hwmon/hwmon0/temp1_input",
            "/proc/mtktscpu/mtktscpu_temperature",
    };

    private final List<File> sources = new ArrayList<File>();

    CpuTempReader() {
        File[] zones = new File("/sys/class/thermal").listFiles();
        if (zones != null) {
            for (File zone : zones) {
                if (!zone.getName().startsWith("thermal_zone")) {
                    continue;
                }
                String type = readLine(new File(zone, "type"));
                if (type == null) {
                    continue;
                }
                type = type.toLowerCase(Locale.US);
                if (!CPU_ZONE.matcher(type).find() || NOT_CPU_ZONE.matcher(type).find()) {
                    continue;
                }
                File temp = new File(zone, "temp");
                if (!Float.isNaN(readCelsius(temp))) {
                    sources.add(temp);
                }
            }
        }
        if (sources.isEmpty()) {
            for (String path : FALLBACK_PATHS) {
                File f = new File(path);
                if (!Float.isNaN(readCelsius(f))) {
                    sources.add(f);
                    break;
                }
            }
        }
    }

    boolean isAvailable() {
        return !sources.isEmpty();
    }

    /** CPU 系センサーの最高温度（℃）。読めない場合は NaN。 */
    float read() {
        float max = Float.NaN;
        for (File f : sources) {
            float c = readCelsius(f);
            if (!Float.isNaN(c) && (Float.isNaN(max) || c > max)) {
                max = c;
            }
        }
        return max;
    }

    /** ミリ度・0.1 度・度のどの単位で書かれていても ℃ に直す。 */
    static float toCelsius(long raw) {
        float c;
        if (raw > 1000) {
            c = raw / 1000f;
        } else if (raw > 200) {
            c = raw / 10f;
        } else {
            c = raw;
        }
        return (c >= 5f && c <= 150f) ? c : Float.NaN;
    }

    private static float readCelsius(File f) {
        String line = readLine(f);
        if (line == null) {
            return Float.NaN;
        }
        try {
            // "45000" や "45.5" のほか、"45 C" のような書式もあるので先頭の数値だけ使う
            String num = line.split("[^0-9.\\-]", 2)[0];
            if (num.isEmpty()) {
                return Float.NaN;
            }
            return toCelsius(Math.round(Double.parseDouble(num)));
        } catch (NumberFormatException e) {
            return Float.NaN;
        }
    }

    private static String readLine(File f) {
        BufferedReader br = null;
        try {
            br = new BufferedReader(new FileReader(f), 64);
            String line = br.readLine();
            return line != null ? line.trim() : null;
        } catch (IOException | SecurityException e) {
            return null;
        } finally {
            if (br != null) {
                try {
                    br.close();
                } catch (IOException ignored) {
                }
            }
        }
    }
}
