# Android ImGui Overlay

免 root 的 Android ImGui 悬浮窗模板。Java 管理权限、前台服务和系统窗口，C++ 通过 Vulkan 渲染 ImGui。直接生成并安装 APK，不需要启动 ELF、使用 shell 特权或读取物理输入设备。

**许可：源码公开，限非商业用途。** 本项目沿用原工程的非商业许可证，允许学习、个人使用和非商业修改、再分发；商业使用需要取得版权所有者的单独书面授权。它不属于允许商用的 OSI 开源许可。完整条款见 [LICENSE](LICENSE)。

## 环境与构建

| 项目 | 固定配置 |
| --- | --- |
| Android | 最低 API 26 / Android 8，目标 API 36 |
| 编译 SDK | API 36.1 |
| 架构 | arm64-v8a |
| 图形 | Vulkan 1.0，透明 RGBA Surface |
| Android Gradle Plugin / Gradle | 8.13.2 / 8.13 |
| Java | JDK 17，Java 源码级别 17 |
| NDK / CMake | 28.2.13676358 / 3.22.1 |

Android Studio 打开本目录，安装上述 SDK、Build Tools 36.0.0、NDK、CMake，并将 Gradle JDK 设为 JDK 17。`local.properties` 是本机 SDK 路径，不应提交到版本控制。Gradle wrapper 包含发行包 SHA-256 校验。

PowerShell（按自己的安装位置设置路径）：

```powershell
$env:JAVA_HOME = 'C:/path/to/jdk17'
$env:ANDROID_HOME = 'C:/path/to/Android/Sdk'
.\gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest
```

产物：`app/build/outputs/apk/debug/app-debug.apk`。Debug APK 使用本机 Android debug keystore 签名。Release 签名需开发者自行配置。

```powershell
& "$env:ANDROID_HOME/platform-tools/adb.exe" install -r .\app\build\outputs\apk\debug\app-debug.apk
& "$env:ANDROID_HOME/platform-tools/adb.exe" logcat -s ImGuiOverlay AndroidRuntime
```

## 使用

1. 打开 App，授予“显示在其他应用上层”权限。
2. Android 13+ 可授予通知权限；拒绝通知不会阻止前台服务启动，但系统可能不在通知栏显示通知，仍可从 App 停止。
3. 点击启动，打开局部面板和全屏绘制层。
4. 拖动标题区域移动面板，拖动右下角图标调整大小。关闭图标收起为悬浮球，点击球再次展开。
5. 点击文字输入框，用 Android 输入法编辑；完成、返回、收起或拖动面板均结束编辑。
6. 从 App 或常驻通知停止。系统终止进程后不自动恢复；没有开机启动或其他后台自动启动入口。

## 架构与二次开发

```text
MainActivity -> OverlayService -> WindowManager (TYPE_APPLICATION_OVERLAY)
                               |-- drawing SurfaceView: full-screen, non-interactive
                               |-- panel SurfaceView: local, interactive
                               `-- floating bubble / Android EditText
Surface / touch / configuration -> JNI command queue -> one render thread
                                                    |-- shared Vulkan instance/device/queue
                                                    |-- drawing swapchain + ImGui context
                                                    `-- panel swapchain + ImGui context
```

主要扩展入口在 `app/src/main/cpp/template_ui.h`、`template_ui.cpp`：

```cpp
void DrawPanel(TemplateState& state);
void DrawOverlay(ImDrawList& draw, const OverlayMetrics& metrics,
                 const TemplateState& state);
bool InputTextAndroid(const char* label, char* buffer, size_t capacity,
                      int field, TemplateState& state);
```

- `DrawPanel` 编写面板控件；当前样例使用填满局部 Surface 的单一 ImGui 窗口。弹出菜单也受该 Surface 边界约束。
- `DrawOverlay` 使用 DrawList 绘制全屏图形；`TemplateState` 由渲染线程拥有，面板修改值后绘制层读取。
- 坐标使用 Surface **像素**。面板触摸原点是面板 Surface 左上角，不包含 Android 标题栏；绘制层原点是全屏 Surface 左上角。`OverlayMetrics` 提供像素尺寸、dp 到像素密度及系统栏/刘海/键盘边距。
- 示例文字缓冲为 512 字节，最大 UTF-8 内容为 511 字节；过滤器按完整输入片段校验，避免截断中文或 emoji。字体是否包含对应字形与输入是否正确是两回事，内置字体不保证 emoji 字形。
- `InputTextAndroid` 的非编辑状态使用只读 ImGui InputText；编辑状态覆盖 Android EditText。输入法组合文本、光标和选择由 Android 管理，文本通过 UTF-8 字节数组实时回写 native。返回值表示文本有变化。新增字段时需在 `Runtime::text()` 中映射字段编号和缓冲，Java 同时只管理一个编辑器。
- 默认渲染目标为 `min(60, 显示刷新率)`。收起时面板 Surface 与 context 被销毁，全屏绘制继续；再次展开重建面板资源，示例数据仍保留。
- 密度变化会重新创建字体和 ImGui context；旋转及窗口尺寸变化会重建 swapchain。锁屏和熄屏暂停绘制。

## 生命周期与透明合成

Surface 创建携带递增代次。Java 将 attach、resize、触摸及 detach 排入 native 队列，渲染线程只接受当前代次。`surfaceDestroyed()` 同步等待旧 Surface 工作退役，再释放 ANativeWindow 引用。服务销毁时移除窗口、等待渲染线程退出，并释放 JNI 全局引用。

Vulkan instance/device/queue 在两个目标间共享；各目标独立拥有 swapchain、ImGui context、descriptor pool 与帧资源。提交前使用短超时检查 fence 和获取图像，绘制前透明清屏；优先 PRE_MULTIPLIED 合成，或使用 Android 透明 Surface 的 INHERIT 模式。RGBA 格式或透明合成不可用时停止并报告错误。

swapchain 过期和 suboptimal 触发重建。Surface 丢失、device lost 或其他 Vulkan 失败会停止窗口，错误显示在 App；用户可主动重新启动。正常退役及重建使用 `vkDeviceWaitIdle`；异常 GPU 驱动在系统调用内部卡死时，普通 App 无法强制保证该调用的返回时间。

## 系统边界

Android 12+ 对不接收触摸的悬浮窗实施遮挡检查。实测设备把宿主窗口和继承其 alpha 的 SurfaceView 子层都计入遮挡。绘制层使用 `alpha = 1 - sqrt(1 - budget)`，其中 `budget = max(0, min(0.8, 系统上限) - 0.01)`；系统上限为 0.8 时 alpha 约为 0.542，两层合并约为 0.79，保留浮点/系统量化余量。因此绘制内容会略透明。该限制按**整个窗口**计算，不能只根据透明像素决定穿透。面板或球在自己的局部边界内接收输入，外围继续交给底层 App。

普通 App 无法读取底层 App 触摸、注入全局输入、覆盖所有系统窗口，或强制显示在禁止悬浮窗的应用页面上。敏感应用还可能自行拒绝受到遮挡的触摸。前台通知、悬浮窗权限及系统/OEM 后台管理仍适用。本模板不声明截图/录屏隐藏能力，不依赖无障碍、Shizuku 或 root。

## 验证

```powershell
.\tools\verify.ps1
# SDK / JDK 不同的机器：
.\tools\verify.ps1 -Sdk 'C:/path/to/Android/Sdk' -Jdk 'C:/path/to/jdk17'
```

脚本构建 APK、运行 Lint 和 10 项 UTF-8/合并遮挡透明度单元测试，并从 APK 提取所有 arm64 `.so`，检查 ELF LOAD 段至少 16 KB 对齐、APK ZIP 对齐、JNI 导出和私有 Android 库依赖。NDK r28 与显式链接参数提供 16 KB 支持。

设备验收清单：

- 无 root 手机安装并授权启动，透明背景没有黑色全屏遮挡，面板控件可点击。
- 切换其他 App，在面板/球外点击、长按、滑动；Android 12+ 检查 logcat 是否出现 untrusted touch 遮挡拒绝。
- 拖动、缩放、收起、展开；验证全屏图形持续绘制，面板状态保留。
- 中文拼音组合输入、光标移动、选中替换、删除、emoji、完成、系统返回和键盘隐藏；横屏软键盘出现时编辑器仍可见。
- 横竖屏、分屏、显示密度变化、系统栏和刘海；锁屏/熄屏暂停，解锁恢复。
- 连续启动/停止、快速展开/收起、编辑时停止、通知停止、权限拒绝/撤销、通知权限拒绝。
- Android 14+ 前台服务可正常启动；不支持 Vulkan/透明合成或 GPU 失效时展示错误并释放窗口。
- 在 16 KB 内存页真机/系统镜像上加载 native 库；确认停止后服务和窗口均消失，native 堆不会持续增长。

本机验证结果及未验证项目见 `VALIDATION.md`。构建与静态检查通过不能替代设备侧验证，尤其是双 context 后端、厂商 Vulkan 驱动、触摸遮挡和输入法表现。

## 来源与许可

原 ELF 目录没有被修改。复用源码、字体的来源及显著修改见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。原工程非商业许可证保留在 [LICENSE](LICENSE)，Dear ImGui 的 MIT 许可证位于 `app/src/main/cpp/vendor/imgui/LICENSE.txt`。中文字体使用官方 Noto CJK 仓库提供的 Noto Sans SC Regular（SIL OFL 1.1），字体原文许可证随 APK 打包在 `assets/OFL-NotoSansSC.txt`。

## 自动构建

GitHub Actions 在提交到 `main`、提交 Pull Request 或手动触发时构建 Debug APK，执行 Lint、单元测试、签名检查以及 APK/ELF 16 KB 对齐检查。成功构建的 APK 与检查报告可从对应 Actions 运行的 `debug-apk-and-reports` artifact 下载。Actions 的 Debug 签名与本机签名可能不同，覆盖安装失败时请先卸载旧版本。
