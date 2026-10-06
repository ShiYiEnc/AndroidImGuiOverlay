package dev.imgui.overlay;

final class OverlayOpacity {
    private OverlayOpacity() {}
    static float forSurfaceView(float maximum) {
        if (!Float.isFinite(maximum) || maximum <= 0) return 0;
        float budget = Math.max(0, Math.min(0.8f, maximum) - 0.01f);
        // InputDispatcher may count both the host window and its inherited-alpha SurfaceView.
        // 1 - (1 - alpha)^2 must stay below the system's obscuring-opacity budget.
        return (float) (1 - Math.sqrt(1 - budget));
    }
}
