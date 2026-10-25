package com.plpp555.trisync3d;

import android.content.Context;
import android.widget.FrameLayout;

/** FrameLayout that keeps the benchmark's 16:9 render aspect ratio. */
final class AspectFrameLayout extends FrameLayout {
    AspectFrameLayout(Context context) {
        super(context);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = MeasureSpec.getSize(widthMeasureSpec);
        int height = width * 9 / 16;
        int maxHeight = MeasureSpec.getSize(heightMeasureSpec);
        if (MeasureSpec.getMode(heightMeasureSpec) != MeasureSpec.UNSPECIFIED && height > maxHeight) {
            height = maxHeight;
            width = height * 16 / 9;
        }
        super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
    }
}
