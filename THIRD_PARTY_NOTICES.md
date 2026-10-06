# Source and Licenses

- `app/src/main/cpp/vendor/imgui/` is copied from the supplied
  `Android_imgui_Vulkan-main` project (Dear ImGui version 1.93.0 WIP / 19294).
  The source files, bundled stb implementation headers, and Vulkan backend are
  unchanged. Dear ImGui's MIT license is included as `vendor/imgui/LICENSE.txt`.
- `app/src/main/assets/noto_sans_sc.otf` is the unmodified Noto Sans SC Regular
  OpenType font from the official Noto CJK repository:
  https://github.com/notofonts/noto-cjk/blob/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
  It is distributed under the SIL Open Font License 1.1. The full license is
  included at `app/src/main/assets/OFL-NotoSansSC.txt` and packaged in the APK.
  The supplied project's font, which had no separate license, is not included
  in this public template.
- `LICENSE` preserves the original project's non-commercial license and
  copyright notice. Original supplied directory: `Android_imgui_Vulkan-main`.

Significant modifications in this template: a standalone Gradle APK project,
public Android overlay windows and input handling, shared-device Vulkan
rendering for two Surfaces, JNI lifecycle handling, a native Android text editor,
and a new sample panel/drawing interface. The Chinese font was replaced with
Noto Sans SC before public distribution. The original ELF project is retained
separately and has not been modified.
