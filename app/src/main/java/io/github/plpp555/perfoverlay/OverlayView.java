package io.github.plpp555.perfoverlay;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.View;

/** 右上に出す「ラベル 値」の表。背景は描かず、どんな画面でも読めるよう文字に黒フチを付ける。 */
final class OverlayView extends View {
    static final int MAX_ROWS = 4;
    private static final String[] ALL_LABELS = {"FPS", "Hz", "RAM", "CPU", "BAT"};
    private static final int LABEL_COLOR = 0xFFE6E6E6;

    private final String[] labels = new String[MAX_ROWS];
    private final String[] values = new String[MAX_ROWS];
    private final int[] colors = new int[MAX_ROWS];
    private int rows;

    private final Paint labelPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint labelStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint valuePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint valueStroke = new Paint(Paint.ANTI_ALIAS_FLAG);

    private float padding;
    private float gap;
    private float lineHeight;
    private float labelWidth;
    /** 幅が 1 秒ごとに変わって表示が揺れないよう、これまでの最大幅を保持する。 */
    private float valueWidth;

    OverlayView(Context context, float textSizeSp) {
        super(context);
        Typeface mono = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD);
        valuePaint.setTypeface(mono);
        valueStroke.setTypeface(mono);
        labelPaint.setTypeface(Typeface.DEFAULT_BOLD);
        labelStroke.setTypeface(Typeface.DEFAULT_BOLD);
        labelPaint.setColor(LABEL_COLOR);
        for (Paint p : new Paint[]{labelStroke, valueStroke}) {
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeJoin(Paint.Join.ROUND);
            p.setColor(Color.BLACK);
        }

        float px = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, textSizeSp,
                getResources().getDisplayMetrics());
        valuePaint.setTextSize(px);
        valueStroke.setTextSize(px);
        valueStroke.setStrokeWidth(Math.max(2f, px * 0.14f));
        labelPaint.setTextSize(px * 0.62f);
        labelStroke.setTextSize(px * 0.62f);
        labelStroke.setStrokeWidth(Math.max(2f, px * 0.10f));

        // フチ取りがはみ出さない分だけ余白を取る
        padding = px * 0.12f;
        gap = px * 0.3f;
        Paint.FontMetrics fm = valuePaint.getFontMetrics();
        lineHeight = fm.descent - fm.ascent;

        for (String l : ALL_LABELS) {
            labelWidth = Math.max(labelWidth, labelPaint.measureText(l));
        }
    }

    void setRows(int count, String[] newLabels, String[] newValues, int[] newColors) {
        boolean relayout = count != rows;
        rows = count;
        for (int i = 0; i < count; i++) {
            labels[i] = newLabels[i];
            values[i] = newValues[i];
            colors[i] = newColors[i];
            float w = valuePaint.measureText(newValues[i]);
            if (w > valueWidth) {
                valueWidth = w;
                relayout = true;
            }
        }
        if (relayout) {
            requestLayout();
        }
        invalidate();
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int w = (int) Math.ceil(padding * 2 + labelWidth + gap + valueWidth);
        int h = (int) Math.ceil(padding * 2 + lineHeight * Math.max(rows, 1));
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float ascent = -valuePaint.getFontMetrics().ascent;
        float valueX = padding + labelWidth + gap;
        for (int i = 0; i < rows; i++) {
            float baseline = padding + ascent + lineHeight * i;
            canvas.drawText(labels[i], padding, baseline, labelStroke);
            canvas.drawText(labels[i], padding, baseline, labelPaint);
            valuePaint.setColor(colors[i]);
            canvas.drawText(values[i], valueX, baseline, valueStroke);
            canvas.drawText(values[i], valueX, baseline, valuePaint);
        }
    }
}
