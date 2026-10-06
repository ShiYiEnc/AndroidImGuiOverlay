package dev.imgui.overlay;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private final Handler handler = new Handler();
    private TextView status;
    private Button permission, start, stop, notifications;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { updateStatus(); handler.postDelayed(this, 750); }
    };
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(20), dp(24), dp(24));
        root.setBackgroundColor(Color.WHITE);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            root.setPadding(dp(24) + insets.getSystemWindowInsetLeft(), dp(20) + insets.getSystemWindowInsetTop(),
                    dp(24) + insets.getSystemWindowInsetRight(), dp(24) + insets.getSystemWindowInsetBottom());
            return insets;
        });
        TextView title = new TextView(this);
        title.setText("ImGui Overlay"); title.setTextSize(26); title.setTextColor(Color.rgb(20, 35, 35));
        root.addView(title);
        status = new TextView(this); status.setTextSize(16); status.setPadding(0, dp(24), 0, dp(24)); root.addView(status);
        permission = button(root, "授权悬浮窗", v -> startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName()))));
        notifications = button(root, "授权通知", v -> {
            if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1);
        });
        start = button(root, "启动", v -> {
            try { startForegroundService(new Intent(this, OverlayService.class).setAction(OverlayService.START)); }
            catch (RuntimeException e) { OverlayService.lastError = e.getMessage(); }
            updateStatus();
        });
        stop = button(root, "停止", v -> { stopService(new Intent(this, OverlayService.class)); updateStatus(); });
        setContentView(root);
    }
    private Button button(LinearLayout root, String label, View.OnClickListener listener) {
        Button b = new Button(this); b.setText(label); b.setOnClickListener(listener);
        root.addView(b, new LinearLayout.LayoutParams(-1, dp(52))); return b;
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void updateStatus() {
        boolean granted = Settings.canDrawOverlays(this);
        permission.setEnabled(!granted); start.setEnabled(granted && !OverlayService.running);
        stop.setEnabled(OverlayService.running);
        notifications.setVisibility(Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED ? View.VISIBLE : View.GONE);
        String text = !granted ? "未授权悬浮窗" : OverlayService.running ? "运行中" : "已停止";
        if (OverlayService.lastError != null) text += "\n" + OverlayService.lastError;
        status.setText(text);
    }
    @Override protected void onResume() { super.onResume(); handler.post(refresh); }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
}
