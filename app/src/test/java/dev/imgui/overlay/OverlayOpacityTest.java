package dev.imgui.overlay;

import org.junit.Test;
import static org.junit.Assert.*;

public class OverlayOpacityTest {
    private float combined(float alpha) { return 1 - (1 - alpha) * (1 - alpha); }
    @Test public void hostAndSurfaceTogetherStayBelowAndroidLimit() {
        float alpha = OverlayOpacity.forSurfaceView(0.8f);
        assertTrue(alpha > 0.5f);
        assertTrue(combined(alpha) < 0.8f);
        assertEquals(0.79f, combined(alpha), 0.0001f);
    }
    @Test public void respectsStricterDeviceThreshold() {
        assertTrue(combined(OverlayOpacity.forSurfaceView(0.5f)) < 0.5f);
    }
    @Test public void retainsSafetyMarginEvenWithRounding() {
        float rounded = (float) (Math.ceil(OverlayOpacity.forSurfaceView(0.8f) * 1024) / 1024);
        assertTrue(combined(rounded) < 0.8f);
    }
    @Test public void invalidOrZeroThresholdDisablesObscuringOpacity() {
        assertEquals(0, OverlayOpacity.forSurfaceView(0), 0);
        assertEquals(0, OverlayOpacity.forSurfaceView(Float.NaN), 0);
        assertEquals(0, OverlayOpacity.forSurfaceView(-1), 0);
    }
}
