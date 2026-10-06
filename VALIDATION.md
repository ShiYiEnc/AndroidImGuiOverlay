# Validation Record

Date: 2026-10-06 (Asia/Shanghai)

## Build and Static Checks

- Debug arm64 APK build: PASS.
- Android Lint: PASS, 0 errors and 14 warnings. Remaining warnings cover pinned
  target/tool versions, arm64-only support, backup configuration, programmatic
  views/accessibility, localized literals and deliberate pixel-coordinate gravity.
- UTF-8 input and combined overlay opacity unit tests: PASS, 10 tests.
- APK v2 debug signature verification: PASS.
- APK ZIP 16 KB native library alignment: PASS.
- Packaged libimgui_overlay.so ELF LOAD alignment: PASS, 0x4000.
- All nine JNI exports and absence of libgui/libutils dependencies: PASS.
- Original ELF project's source files were not edited.

Reproduce with tools/verify.ps1. Reports, extracted ELF files and device captures
are under app/build/verification or app/build/reports and are not source artifacts.

## Connected Device

- Model/product: OPD2404; device: OP5D77L1.
- Android 16 / API 36; arm64-v8a; Vulkan feature level 1.
- Physical panel: 2120 x 3000, density 420 dpi. Captures include landscape mode.
- Actual memory page size: 4096 bytes. A 16 KB runtime was not available.
- Testing used standard adb installation/input/inspection commands; no su,
  root-only API, touch injection device or system-library patch was used.

## Confirmed Device Behavior

- APK installation and Activity launch passed.
- Foreground specialUse service started and remained running.
- Two native rendering targets displayed an interactive Chinese ImGui panel and
  transparent full-screen lines, rectangle and text, without a black backdrop.
- Device snapshots showed the panel, its collapsed floating bubble, moved panel
  placement and changed sample values. Some changes were made by the user, so
  they are observations rather than a fully automated interaction test.
- After the opacity fix, taps outside the overlay UI opened the Settings WLAN
  assistant page with the drawing still visible. This was checked with both
  the floating bubble and expanded panel. The recent logs for those actions
  contained no untrusted-touch rejection.

## Defects Found and Fixed

1. Android InputDispatcher counted the full-screen host window and inherited-alpha
   SurfaceView child separately. Alpha 0.8 on each combined to 0.96, blocking
   underlying touch. The implementation now budgets for two layers with
   alpha = 1 - sqrt(1 - budget), where budget is at most 0.79. dumpsys input
   confirmed both layers had alpha approximately 0.541504 after the update.
2. ImGui's Vulkan backend leaves sampler descriptor sets allocated in a
   caller-owned pool at shutdown. Repeated swapchain rebuilds could exhaust the
   pool (VK_ERROR_OUT_OF_POOL_MEMORY). The caller now resets the retired pool
   before backend reinitialization. Long-duration rebuild stress remains a
   manual acceptance item.

## User Manual Test Result

On 2026-10-06, after taking over testing on the connected device, the user
reported: "没有测试出问题 一切正常" (no issues found; everything works normally).
This confirms the user's manual acceptance on that device. Individual test
steps, cycle counts and memory measurements were not supplied, so this report
does not claim a quantified stress or memory-leak test.

## Public Distribution Preparation

Before publication, the original font (without a separate license) was replaced
with unmodified Noto Sans SC Regular from Noto CJK under SIL OFL 1.1. The font license
is included in the source and APK. The public version was rebuilt and checked;
the preceding manual device acceptance used the earlier font. Device appearance
with the replacement font has not been retested.

## Regression Checklist and Coverage Limits

The following checklist is retained for future changes and additional devices:

- Underlying tap, long-press and scrolling with panel expanded and collapsed.
- Checkbox, slider, color popup, counter, drag and resize behavior.
- Chinese composing text, selection/replacement, deletion, IME Done/Back,
  keyboard dismissal, outside tap, and editing while collapsing/stopping.
- Repeated portrait/landscape changes and resize/collapse cycles, without error
  or sustained native-memory growth.
- Lock/screen-off pauses, unlock resumes, stop removes all overlay windows.
- Permission denial/revocation, denied notification permission, notification
  actions and restart after a reported Vulkan failure.
- Android 8/12/14 devices, other ROMs and a 16 KB-page runtime remain untested.

Sensitive applications may reject obscured input independently. System pages
that hide application overlays are not expected to show the panel/drawing.
