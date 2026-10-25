package com.plpp555.trisync3d;

import android.content.Context;
import android.content.SharedPreferences;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Keeps the most recent results (newest first) in SharedPreferences. */
final class HistoryStore {
    private static final String PREFS = "history";
    private static final String KEY = "runs";
    private static final int MAX = 20;
    private final SharedPreferences prefs;

    HistoryStore(Context ctx) {
        prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    JSONArray load() {
        try {
            return new JSONArray(prefs.getString(KEY, "[]"));
        } catch (JSONException e) {
            return new JSONArray();
        }
    }

    void add(JSONObject entry) {
        JSONArray old = load();
        JSONArray out = new JSONArray();
        out.put(entry);
        for (int i = 0; i < old.length() && out.length() < MAX; i++) {
            out.put(old.opt(i));
        }
        prefs.edit().putString(KEY, out.toString()).apply();
    }

    void clear() {
        prefs.edit().remove(KEY).apply();
    }
}
