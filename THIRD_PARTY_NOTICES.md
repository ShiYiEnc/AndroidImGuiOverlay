# 第三方组件与许可

## Dear ImGui

- 上游项目：[ocornut/imgui](https://github.com/ocornut/imgui)。
- 内置版本：1.93.0 WIP（`IMGUI_VERSION_NUM` 为 19294）。
- 文件位置：`app/src/main/cpp/vendor/imgui/`，包含 ImGui 核心、Vulkan 后端及随附的 stb 实现头文件。这些文件未作修改，文件内的版权及许可声明均保留。
- Dear ImGui 采用 MIT 许可证，版权所有者为 Omar Cornut；完整许可证见 [LICENSE.txt](app/src/main/cpp/vendor/imgui/LICENSE.txt)。stb 组件的许可声明见对应 `imstb_*.h` 文件。

## Noto Sans SC

- 上游项目：[notofonts/noto-cjk](https://github.com/notofonts/noto-cjk)。
- 使用字体：未经修改的 Noto Sans SC Regular OpenType 字体。
- 上游文件：[NotoSansSC-Regular.otf](https://github.com/notofonts/noto-cjk/blob/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf)。
- 文件位置：`app/src/main/assets/noto_sans_sc.otf`。
- 采用 SIL Open Font License 1.1，完整许可证见 [OFL-NotoSansSC.txt](app/src/main/assets/OFL-NotoSansSC.txt)，并随 APK 打包。

## 模板实现与项目许可

本模板新增独立 Gradle APK 工程、Android 公共悬浮窗接口与触摸处理、双 Surface 共享 Vulkan 设备的渲染管理、JNI 生命周期处理、Android 原生文本编辑桥接，以及面板和全屏绘制示例。

项目的非商业许可及版权声明见 [LICENSE](LICENSE)。第三方组件分别遵循各自许可证；项目的非商业限制不改变这些组件本身的许可权利。
