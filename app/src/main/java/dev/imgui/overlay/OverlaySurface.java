package dev.imgui.overlay;

import android.content.Context;
import android.graphics.PixelFormat;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import java.util.concurrent.atomic.AtomicLong;

final class OverlaySurface extends SurfaceView implements SurfaceHolder.Callback {
    private static final AtomicLong NEXT = new AtomicLong();
    private final int slot;
    private final OverlayService owner;
    private long generation;
    private int pointer = -1;
    long getGeneration() { return generation; }
    OverlaySurface(Context context, OverlayService owner, int slot) {
        super(context); this.owner = owner; this.slot = slot;
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(false);
        getHolder().addCallback(this);
    }
    @Override public void surfaceCreated(SurfaceHolder holder) {
        generation = NEXT.incrementAndGet();
        if (owner.handle != 0) NativeBridge.attach(owner.handle, slot, holder.getSurface(), generation);
    }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (owner.handle != 0) NativeBridge.resize(owner.handle, slot, generation, width, height);
    }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        pointer = -1;
        if (owner.handle != 0) NativeBridge.detach(owner.handle, slot, generation);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (slot != 1 || owner.handle == 0) return false;
        int action = event.getActionMasked(), index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN) {
            pointer = event.getPointerId(0); send(0, event.getX(), event.getY());
        } else if (action == MotionEvent.ACTION_MOVE && pointer != -1) {
            int current = event.findPointerIndex(pointer);
            if (current >= 0) send(2, event.getX(current), event.getY(current));
        } else if (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(index) == pointer) {
            send(1, event.getX(index), event.getY(index));
            int next = index == 0 ? 1 : 0;
            pointer = event.getPointerId(next); send(0, event.getX(next), event.getY(next));
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            send(action == MotionEvent.ACTION_CANCEL ? 3 : 1, event.getX(), event.getY()); pointer = -1;
            if (action == MotionEvent.ACTION_UP) performClick();
        }
        return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
    private void send(int action, float x, float y) { NativeBridge.touch(owner.handle, generation, action, x, y); }
}
