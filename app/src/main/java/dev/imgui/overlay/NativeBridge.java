package dev.imgui.overlay;

import android.content.res.AssetManager;
import android.view.Surface;

final class NativeBridge {
    static { System.loadLibrary("imgui_overlay"); }
    interface Listener {
        void onNativeError(String message);
        void onEditorRequested(int field, long generation, byte[] text, float x, float y, float width, float height, int maxBytes);
    }
    static native long create(AssetManager assets, Listener listener, float density, float refreshRate);
    static native void attach(long handle, int slot, Surface surface, long generation);
    static native void resize(long handle, int slot, long generation, int width, int height);
    static native void detach(long handle, int slot, long generation);
    static native void touch(long handle, long generation, int action, float x, float y);
    static native void metrics(long handle, float density, float refreshRate, int left, int top, int right, int bottom);
    static native void pause(long handle, boolean paused);
    static native void text(long handle, int field, byte[] utf8, boolean finished);
    static native void destroy(long handle);
    private NativeBridge() {}
}
