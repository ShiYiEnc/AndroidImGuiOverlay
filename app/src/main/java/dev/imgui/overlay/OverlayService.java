package dev.imgui.overlay;

import android.app.AppOpsManager;
import android.app.KeyguardManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ServiceInfo;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.InputFilter;
import android.text.Spanned;
import android.text.TextWatcher;
import android.text.Editable;
import android.view.Display;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.nio.charset.StandardCharsets;

public final class OverlayService extends Service implements NativeBridge.Listener, DisplayManager.DisplayListener {
    static final String START = "dev.imgui.overlay.START", TOGGLE = "dev.imgui.overlay.TOGGLE", STOP = "dev.imgui.overlay.STOP";
    static volatile boolean running;
    static volatile String lastError;
    private static final String CHANNEL = "overlay";
    private static final int NOTICE = 1;
    final Handler main = new Handler();
    long handle;
    private WindowManager windows;
    private Context windowContext;
    private DisplayManager displays;
    private AppOpsManager appOps;
    private OverlaySurface drawing, panelSurface;
    private FrameLayout panel, content;
    private TextView bubble;
    private WindowManager.LayoutParams drawingParams, panelParams, bubbleParams;
    private Editor editor;
    private int editorField = -1, imeBottom;
    private int panelWidth, panelHeight;
    private boolean closing, receiversRegistered, panelWanted = true, imeWasVisible;
    private Rect safe = new Rect();
    private final AppOpsManager.OnOpChangedListener permissionWatcher = (op, pkg) -> main.post(this::checkPermission);
    private final Runnable watchdog = new Runnable() {
        @Override public void run() {
            if (closing || handle == 0) return;
            checkPermission(); updatePaused(); main.postDelayed(this, 1000);
        }
    };
    private final BroadcastReceiver screenReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (Intent.ACTION_SCREEN_OFF.equals(intent.getAction())) finishEditor();
            updatePaused();
        }
    };
    @Override public void onCreate() {
        super.onCreate();
        windowContext = this;
        displays = getSystemService(DisplayManager.class);
        appOps = getSystemService(AppOpsManager.class);
        NotificationChannel channel = new NotificationChannel(CHANNEL, "悬浮窗", NotificationManager.IMPORTANCE_LOW);
        getSystemService(NotificationManager.class).createNotificationChannel(channel);
    }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent == null ? null : intent.getAction();
        if (STOP.equals(action)) { stopSelf(); return START_NOT_STICKY; }
        if (handle == 0 && !START.equals(action)) { stopSelf(); return START_NOT_STICKY; }
        if (handle == 0) {
            lastError = null;
            if (!Settings.canDrawOverlays(this)) { fail("悬浮窗权限未授予"); return START_NOT_STICKY; }
            try {
                if (Build.VERSION.SDK_INT >= 34) startForeground(NOTICE, notification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
                else startForeground(NOTICE, notification());
                if (Build.VERSION.SDK_INT >= 30) {
                    Display display = displays.getDisplay(Display.DEFAULT_DISPLAY);
                    if (display == null) throw new IllegalStateException("Main display unavailable");
                    windowContext = createDisplayContext(display).createWindowContext(WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, null);
                }
                windows = windowContext.getSystemService(WindowManager.class);
                handle = NativeBridge.create(getAssets(), this, density(), refreshRate());
                if (handle == 0) throw new IllegalStateException("Native renderer creation failed");
                createDrawing(); showPanel();
                displays.registerDisplayListener(this, main);
                appOps.startWatchingMode(AppOpsManager.OPSTR_SYSTEM_ALERT_WINDOW, getPackageName(), permissionWatcher);
                IntentFilter screen = new IntentFilter();
                screen.addAction(Intent.ACTION_SCREEN_OFF); screen.addAction(Intent.ACTION_SCREEN_ON); screen.addAction(Intent.ACTION_USER_PRESENT);
                if (Build.VERSION.SDK_INT >= 33) registerReceiver(screenReceiver, screen, Context.RECEIVER_NOT_EXPORTED);
                else registerReceiver(screenReceiver, screen);
                receiversRegistered = true;
                running = true; updateGeometry(); updatePaused(); main.post(watchdog);
            } catch (RuntimeException | LinkageError e) { fail(e.toString()); }
        } else if (TOGGLE.equals(action)) {
            try { if (panel != null) collapsePanel(); else showPanel(); }
            catch (RuntimeException e) { fail(e.toString()); }
        }
        return START_NOT_STICKY;
    }
    private Notification notification() {
        PendingIntent open = PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent toggle = PendingIntent.getService(this, 1, new Intent(this, OverlayService.class).setAction(TOGGLE), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop = PendingIntent.getService(this, 2, new Intent(this, OverlayService.class).setAction(STOP), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this, CHANNEL).setSmallIcon(R.drawable.ic_overlay).setContentTitle("ImGui Overlay")
                .setContentText(panelWanted ? "悬浮面板已展开" : "悬浮面板已收起").setContentIntent(open).setOngoing(true)
                .addAction(new Notification.Action.Builder(null, panelWanted ? "收起" : "展开", toggle).build())
                .addAction(new Notification.Action.Builder(null, "停止", stop).build()).build();
    }
    private void updateNotification() { getSystemService(NotificationManager.class).notify(NOTICE, notification()); }
    private WindowManager.LayoutParams params(int width, int height, int flags) {
        WindowManager.LayoutParams p = new WindowManager.LayoutParams(width, height, WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                flags, PixelFormat.TRANSLUCENT);
        p.gravity = Gravity.TOP | Gravity.LEFT;
        if (Build.VERSION.SDK_INT >= 30) p.setFitInsetsTypes(0);
        p.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE;
        return p;
    }
    private void createDrawing() {
        drawing = new OverlaySurface(windowContext, this, 0);
        drawing.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        drawingParams = params(-1, -1, WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
        if (Build.VERSION.SDK_INT >= 30) drawingParams.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        else if (Build.VERSION.SDK_INT >= 28) drawingParams.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        drawingParams.alpha = Build.VERSION.SDK_INT >= 31 ? OverlayOpacity.forSurfaceView(
                getSystemService(InputManager.class).getMaximumObscuringOpacityForTouch()) : 1f;
        drawing.setOnApplyWindowInsetsListener((view, insets) -> { updateGeometry(); return insets; });
        windows.addView(drawing, drawingParams);
    }
    private void showPanel() {
        if (panel != null || closing) return;
        if (bubble != null) { remove(bubble); bubble = null; }
        panelWanted = true;
        panel = new FrameLayout(windowContext);
        panel.setOnTouchListener((view, event) -> {
            if (event.getActionMasked() == MotionEvent.ACTION_OUTSIDE) { finishEditor(); return true; }
            return false;
        });
        LinearLayout column = new LinearLayout(windowContext); column.setOrientation(LinearLayout.VERTICAL);
        panel.addView(column, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout header = new LinearLayout(windowContext); header.setGravity(Gravity.CENTER_VERTICAL); header.setBackgroundColor(Color.rgb(24, 44, 42));
        TextView title = new TextView(windowContext); title.setText("ImGui"); title.setTextColor(Color.WHITE); title.setTextSize(15); title.setPadding(dp(12), 0, 0, 0);
        header.addView(title, new LinearLayout.LayoutParams(0, -1, 1));
        ImageButton collapse = icon(android.R.drawable.ic_menu_close_clear_cancel, "收起", v -> collapsePanel());
        collapse.setImageTintList(android.content.res.ColorStateList.valueOf(Color.WHITE));
        header.addView(collapse, new LinearLayout.LayoutParams(dp(44), -1));
        column.addView(header, new LinearLayout.LayoutParams(-1, dp(44)));
        content = new FrameLayout(windowContext);
        panelSurface = new OverlaySurface(windowContext, this, 1); content.addView(panelSurface, new FrameLayout.LayoutParams(-1, -1));
        column.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));
        ImageButton resize = icon(android.R.drawable.ic_menu_crop, "调整大小", null);
        FrameLayout.LayoutParams grip = new FrameLayout.LayoutParams(dp(40), dp(40), Gravity.BOTTOM | Gravity.RIGHT);
        panel.addView(resize, grip);
        if (panelParams == null) {
            panelWidth = dp(340); panelHeight = dp(460);
            panelParams = params(panelWidth, panelHeight, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH);
            panelParams.x = dp(16); panelParams.y = dp(80);
        }
        title.setOnTouchListener(new Drag(false, false)); resize.setOnTouchListener(new Drag(true, false));
        panel.setOnApplyWindowInsetsListener((view, insets) -> {
            int bottom = Build.VERSION.SDK_INT >= 30 ? insets.getInsets(WindowInsets.Type.ime()).bottom : 0;
            if (editor != null && Build.VERSION.SDK_INT >= 30) {
                if (bottom > 0) imeWasVisible = true;
                else if (imeWasVisible) main.post(this::finishEditor);
            }
            if (bottom != imeBottom) { imeBottom = bottom; main.post(this::updateGeometry); }
            return insets;
        });
        panel.getViewTreeObserver().addOnGlobalLayoutListener(() -> {
            if (editor != null && content != null && content.getHeight() > 0) {
                FrameLayout.LayoutParams inputLayout = (FrameLayout.LayoutParams) editor.getLayoutParams();
                int top = Math.min(inputLayout.topMargin, Math.max(0, content.getHeight() - inputLayout.height));
                if (inputLayout.topMargin != top) { inputLayout.topMargin = top; editor.setLayoutParams(inputLayout); }
            }
            if (Build.VERSION.SDK_INT < 30 && panel != null && editor != null) {
                Rect visible = new Rect(); panel.getWindowVisibleDisplayFrame(visible);
                int bottom = Math.max(0, screenBounds().bottom - visible.bottom);
                if (bottom > dp(100)) imeWasVisible = true;
                else if (imeWasVisible) main.post(this::finishEditor);
                if (bottom != imeBottom) { imeBottom = bottom; main.post(this::updateGeometry); }
            }
        });
        clamp(panelParams, false); windows.addView(panel, panelParams); updateNotification();
    }
    private void collapsePanel() {
        if (closing) return;
        finishEditor();
        panelWanted = false;
        remove(panel); panel = null; content = null; panelSurface = null;
        bubble = new TextView(windowContext); bubble.setText("I"); bubble.setGravity(Gravity.CENTER); bubble.setTextSize(22); bubble.setTextColor(Color.WHITE);
        GradientDrawable bg = new GradientDrawable(); bg.setColor(Color.rgb(0, 127, 113)); bg.setShape(GradientDrawable.OVAL); bubble.setBackground(bg);
        bubble.setContentDescription("展开 ImGui 面板");
        if (bubbleParams == null) { bubbleParams = params(dp(52), dp(52), WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
            bubbleParams.x = panelParams.x; bubbleParams.y = panelParams.y; }
        bubble.setOnTouchListener(new Drag(false, true)); bubble.setOnClickListener(v -> showPanel());
        clamp(bubbleParams, true); windows.addView(bubble, bubbleParams); updateNotification();
    }
    private ImageButton icon(int resource, String description, View.OnClickListener listener) {
        ImageButton button = new ImageButton(windowContext); button.setImageResource(resource); button.setContentDescription(description);
        button.setBackgroundColor(Color.TRANSPARENT); if (listener != null) button.setOnClickListener(listener); return button;
    }
    private final class Drag implements View.OnTouchListener {
        private final boolean resize, ball;
        private float originX, originY;
        private int x, y, width, height;
        private boolean moved;
        Drag(boolean resize, boolean ball) { this.resize = resize; this.ball = ball; }
        @Override public boolean onTouch(View view, MotionEvent event) {
            WindowManager.LayoutParams p = ball ? bubbleParams : panelParams;
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                finishEditor(); originX = event.getRawX(); originY = event.getRawY();
                x = p.x; y = p.y; width = p.width; height = p.height; moved = false; return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
                int dx = Math.round(event.getRawX() - originX), dy = Math.round(event.getRawY() - originY);
                if (Math.abs(dx) + Math.abs(dy) > dp(6)) moved = true;
                if (resize) { panelWidth = Math.max(dp(240), width + dx); panelHeight = Math.max(dp(220), height + dy); }
                else { p.x = x + dx; p.y = y + dy; }
                clamp(p, ball);
                try { windows.updateViewLayout(ball ? bubble : panel, p); } catch (RuntimeException e) { fail(e.toString()); }
                return true;
            }
            if (event.getActionMasked() == MotionEvent.ACTION_UP) { if (!moved) view.performClick(); return true; }
            return true;
        }
    }
    private float density() { return windowContext.getResources().getDisplayMetrics().density; }
    private int dp(int value) { return Math.round(value * density()); }
    private float refreshRate() { Display display = displays.getDisplay(Display.DEFAULT_DISPLAY); return display == null ? 60 : display.getRefreshRate(); }
    private Rect screenBounds() {
        if (Build.VERSION.SDK_INT >= 30) return new Rect(windows.getMaximumWindowMetrics().getBounds());
        android.graphics.Point size = new android.graphics.Point(); windows.getDefaultDisplay().getRealSize(size); return new Rect(0, 0, size.x, size.y);
    }
    private Rect safeBounds() {
        Rect bounds = screenBounds();
        if (Build.VERSION.SDK_INT >= 30) {
            Insets inset = windows.getMaximumWindowMetrics().getWindowInsets().getInsetsIgnoringVisibility(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            bounds.left += inset.left; bounds.top += inset.top; bounds.right -= inset.right; bounds.bottom -= Math.max(inset.bottom, imeBottom);
        } else {
            WindowInsets rootInsets = drawing == null ? null : drawing.getRootWindowInsets();
            if (rootInsets != null) {
                bounds.left += rootInsets.getStableInsetLeft(); bounds.top += rootInsets.getStableInsetTop();
                bounds.right -= rootInsets.getStableInsetRight(); bounds.bottom -= Math.max(imeBottom, rootInsets.getStableInsetBottom());
            } else bounds.bottom -= imeBottom;
            if (drawing != null && Build.VERSION.SDK_INT >= 28 && drawing.getRootWindowInsets() != null
                    && drawing.getRootWindowInsets().getDisplayCutout() != null) {
                android.view.DisplayCutout c = drawing.getRootWindowInsets().getDisplayCutout();
                bounds.left = Math.max(bounds.left, c.getSafeInsetLeft()); bounds.top = Math.max(bounds.top, c.getSafeInsetTop());
                bounds.right = Math.min(bounds.right, screenBounds().right - c.getSafeInsetRight());
                bounds.bottom = Math.min(bounds.bottom, screenBounds().bottom - c.getSafeInsetBottom());
            }
        }
        return bounds;
    }
    private void clamp(WindowManager.LayoutParams p, boolean ball) {
        safe = safeBounds(); int width = Math.max(1, safe.width()), height = Math.max(1, safe.height());
        if (!ball) { p.width = panelWidth; p.height = panelHeight; }
        p.width = Math.min(p.width, width); p.height = Math.min(p.height, height);
        p.x = Math.max(safe.left, Math.min(p.x, safe.right - p.width));
        p.y = Math.max(safe.top, Math.min(p.y, safe.bottom - p.height));
    }
    private void updateGeometry() {
        if (closing || handle == 0) return;
        try {
            if (panel != null) { clamp(panelParams, false); windows.updateViewLayout(panel, panelParams); }
            if (bubble != null) { clamp(bubbleParams, true); windows.updateViewLayout(bubble, bubbleParams); }
            Rect bounds = screenBounds(), area = safeBounds();
            NativeBridge.metrics(handle, density(), refreshRate(), area.left, area.top, bounds.right - area.right, bounds.bottom - area.bottom);
        } catch (RuntimeException e) { fail(e.toString()); }
    }
    private void updatePaused() {
        if (handle == 0 || closing) return;
        boolean paused = !getSystemService(PowerManager.class).isInteractive() || getSystemService(KeyguardManager.class).isKeyguardLocked();
        NativeBridge.pause(handle, paused);
    }
    private void checkPermission() { if (!closing && !Settings.canDrawOverlays(this)) fail("悬浮窗权限已撤销"); }
    @Override public void onConfigurationChanged(Configuration config) { super.onConfigurationChanged(config); finishEditor(); updateGeometry(); }
    @Override public void onDisplayAdded(int id) {}
    @Override public void onDisplayRemoved(int id) { if (id == Display.DEFAULT_DISPLAY) fail("主显示器不可用"); }
    @Override public void onDisplayChanged(int id) { if (id == Display.DEFAULT_DISPLAY) { finishEditor(); updateGeometry(); } }
    @Override public void onNativeError(String message) { main.post(() -> { if (!closing) fail(message); }); }
    private void fail(String message) { lastError = message; running = false; stopSelf(); }

    @Override public void onEditorRequested(int field, long generation, byte[] text, float x, float y, float width, float height, int maxBytes) {
        main.post(() -> {
            if (closing || content == null || panel == null || panelSurface == null || panelSurface.getGeneration() != generation) return;
            finishEditor(); editorField = field; imeWasVisible = false;
            editor = new Editor(windowContext); editor.setSingleLine(true); editor.setTextSize(16); editor.setTextColor(Color.rgb(20, 35, 35));
            editor.setBackgroundColor(Color.rgb(229, 244, 241)); editor.setPadding(dp(5), 0, dp(5), 0);
            editor.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            editor.setFilters(new InputFilter[]{new Utf8Limit(maxBytes)});
            editor.setText(new String(text, StandardCharsets.UTF_8)); editor.setSelection(editor.length());
            FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(Math.max(1, Math.round(width)), Math.max(dp(32), Math.round(height)));
            layout.leftMargin = Math.max(0, Math.round(x)); layout.topMargin = Math.max(0, Math.round(y));
            content.addView(editor, layout);
            editor.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                @Override public void onTextChanged(CharSequence s, int start, int before, int count) { sendEditor(false); }
                @Override public void afterTextChanged(Editable s) {}
            });
            editor.setOnEditorActionListener((v, action, event) -> { finishEditor(); return true; });
            editor.setOnFocusChangeListener((v, focus) -> { if (!focus && editor == v) main.post(this::finishEditor); });
            panelParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            try { windows.updateViewLayout(panel, panelParams); }
            catch (RuntimeException e) { fail(e.toString()); return; }
            editor.requestFocus();
            Editor requested = editor;
            requested.post(() -> showKeyboard(requested));
        });
    }
    private void showKeyboard(Editor requested) {
        if (editor == requested && requested.hasWindowFocus() && !closing)
            windowContext.getSystemService(InputMethodManager.class).showSoftInput(requested, InputMethodManager.SHOW_IMPLICIT);
    }
    private void sendEditor(boolean finished) {
        if (editor != null && handle != 0) NativeBridge.text(handle, editorField, editor.getText().toString().getBytes(StandardCharsets.UTF_8), finished);
    }
    private void finishEditor() {
        if (editor == null) return;
        sendEditor(true); Editor previous = editor; editor = null; editorField = -1;
        windowContext.getSystemService(InputMethodManager.class).hideSoftInputFromWindow(previous.getWindowToken(), 0);
        if (content != null) content.removeView(previous);
        imeBottom = 0; imeWasVisible = false;
        if (panel != null && !closing) {
            panelParams.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            try { windows.updateViewLayout(panel, panelParams); } catch (RuntimeException e) { fail(e.toString()); }
            main.post(this::updateGeometry);
        }
    }
    private final class Editor extends EditText {
        Editor(Context context) { super(context); }
        @Override public void onWindowFocusChanged(boolean focused) {
            super.onWindowFocusChanged(focused);
            if (focused) post(() -> showKeyboard(this));
            else if (editor == this && imeWasVisible) main.post(() -> { if (editor == this) finishEditor(); });
        }
        @Override public boolean onKeyPreIme(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP) { finishEditor(); return true; }
            return super.onKeyPreIme(keyCode, event);
        }
    }
    static final class Utf8Limit implements InputFilter {
        private final int maxBytes;
        Utf8Limit(int maxBytes) { this.maxBytes = maxBytes; }
        @Override public CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend) {
            String prefix = dest.subSequence(0, dstart).toString(), suffix = dest.subSequence(dend, dest.length()).toString();
            if ((prefix + source.subSequence(start, end) + suffix).getBytes(StandardCharsets.UTF_8).length <= maxBytes) return null;
            // Reject the replacement as a whole so composing spans and surrogate pairs stay intact.
            return dest.subSequence(dstart, dend);
        }
    }
    private void remove(View view) { if (view != null && view.isAttachedToWindow()) windows.removeViewImmediate(view); }
    @Override public void onDestroy() {
        closing = true; running = false; main.removeCallbacksAndMessages(null);
        displays.unregisterDisplayListener(this); appOps.stopWatchingMode(permissionWatcher);
        if (receiversRegistered) unregisterReceiver(screenReceiver);
        finishEditor();
        remove(panel); panel = null; remove(bubble); bubble = null; remove(drawing); drawing = null;
        if (handle != 0) { NativeBridge.destroy(handle); handle = 0; }
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }
    @Override public IBinder onBind(Intent intent) { return null; }
}
