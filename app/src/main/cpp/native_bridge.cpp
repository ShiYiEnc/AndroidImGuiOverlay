#include "vulkan_renderer.h"
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
struct Command {
    std::function<void()> action;
    std::shared_ptr<std::promise<void>> completed;
};

class Runtime {
public:
    Runtime(JavaVM* vm, jobject listener, jmethodID error, jmethodID editor,
            std::vector<unsigned char> font, float density, float refresh)
        : vm_(vm), listener_(listener), errorMethod_(error), editorMethod_(editor), renderer_(std::move(font), density) {
        metrics_.density = density; refresh_ = refresh;
    }
    ~Runtime() { close(); }
    void start() {
        auto ready = ready_.get_future();
        worker_ = std::thread([this] { run(); });
        if (!ready.get()) throw std::runtime_error("Unable to attach render thread to JVM");
    }
    jobject listener() const { return listener_; }
    void queue(std::function<void()> action, bool synchronize = false) {
        auto completion = synchronize ? std::make_shared<std::promise<void>>() : nullptr;
        std::future<void> future;
        if (completion) future = completion->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            commands_.push_back({std::move(action), completion});
        }
        wake_.notify_one();
        if (completion) future.wait();
    }
    void attach(int slot, WindowPtr window, uint64_t generation) {
        queue([this, slot, window = std::move(window), generation] {
            if (failed_) return;
            renderer_.attach(slot, window, generation);
            if (slot == 1) { panelGeneration_ = generation; state_.editingField = -1; }
        });
    }
    void resize(int slot, uint64_t generation, int width, int height) {
        queue([=] { if (!failed_) renderer_.resize(slot, generation, width, height); });
    }
    void detach(int slot, uint64_t generation) {
        // UI thread waits until all older work for this Surface has retired.
        queue([=] {
            renderer_.detach(slot, generation);
            if (slot == 1 && generation == panelGeneration_) { state_.editingField = -1; panelGeneration_ = 0; }
        }, true);
    }
    void touch(uint64_t generation, int action, float x, float y) {
        queue([=] { if (!failed_ && !paused_) renderer_.touch(generation, action, x, y); });
    }
    void metrics(float density, float refresh, int left, int top, int right, int bottom) {
        queue([=] {
            metrics_.density = density; metrics_.left = left; metrics_.top = top; metrics_.right = right; metrics_.bottom = bottom;
            refresh_ = std::clamp(refresh, 1.0f, 240.0f);
        });
    }
    void pause(bool paused) {
        queue([=] {
            if (paused && !paused_) renderer_.touch(panelGeneration_, 3, -FLT_MAX, -FLT_MAX);
            paused_ = paused;
        });
    }
    void text(int field, std::string value, bool finished) {
        queue([this, field, value = std::move(value), finished] {
            if (field != 0 || state_.editingField != field) return;
            size_t length = std::min(value.size(), sizeof(state_.text) - 1);
            if (length != value.size()) return;
            if (value != state_.text) state_.changedField = field;
            std::memcpy(state_.text, value.data(), length); state_.text[length] = '\0';
            if (finished) state_.editingField = -1;
        });
    }
    void close() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        wake_.notify_one(); if (worker_.joinable()) worker_.join();
    }
private:
    JavaVM* vm_;
    jobject listener_;
    jmethodID errorMethod_, editorMethod_;
    JNIEnv* env_ = nullptr;
    VulkanRenderer renderer_;
    TemplateState state_;
    OverlayMetrics metrics_;
    uint64_t panelGeneration_ = 0;
    float refresh_ = 60;
    bool paused_ = false, failed_ = false, stopping_ = false;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::thread worker_;
    std::promise<bool> ready_;
    void error(const std::string& message) {
        __android_log_print(ANDROID_LOG_ERROR, "ImGuiOverlay", "%s", message.c_str());
        failed_ = true;
        if (!env_) return;
        jstring text = env_->NewStringUTF(message.c_str());
        env_->CallVoidMethod(listener_, errorMethod_, text); env_->DeleteLocalRef(text);
        if (env_->ExceptionCheck()) { env_->ExceptionDescribe(); env_->ExceptionClear(); }
    }
    void requestEditor(int field, const std::string& value, ImVec2 position, ImVec2 size, int capacity) {
        if (!env_) return;
        jbyteArray text = env_->NewByteArray(static_cast<jsize>(value.size()));
        if (!text) { env_->ExceptionClear(); error("Unable to allocate editor text"); return; }
        env_->SetByteArrayRegion(text, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data()));
        env_->CallVoidMethod(listener_, editorMethod_, field, static_cast<jlong>(panelGeneration_), text,
                             position.x, position.y, size.x, size.y, capacity);
        env_->DeleteLocalRef(text);
        if (env_->ExceptionCheck()) { env_->ExceptionDescribe(); env_->ExceptionClear(); state_.editingField = -1; }
    }
    void run() {
        if (vm_->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
            std::lock_guard<std::mutex> lock(mutex_); stopping_ = true;
            for (auto& cmd : commands_) if (cmd.completed) cmd.completed->set_value();
            commands_.clear(); ready_.set_value(false); return;
        }
        ready_.set_value(true);
        SetEditorRequest([this](int field, const std::string& value, ImVec2 position, ImVec2 size, int capacity) {
            requestEditor(field, value, position, size, capacity);
        });
        auto previous = Clock::now(), next = previous;
        while (true) {
            std::deque<Command> pending;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (commands_.empty() && !stopping_) {
                    if (paused_ || failed_ || !renderer_.hasSurface()) wake_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
                    else wake_.wait_until(lock, next, [this] { return stopping_ || !commands_.empty(); });
                }
                pending.swap(commands_);
                if (stopping_ && pending.empty()) break;
            }
            for (auto& command : pending) {
                try { command.action(); } catch (const std::exception& e) { error(e.what()); renderer_.shutdown(); }
                if (command.completed) command.completed->set_value();
            }
            auto now = Clock::now();
            if (paused_ || failed_ || !renderer_.hasSurface()) { previous = now; next = now; continue; }
            if (now < next) continue;
            float delta = std::chrono::duration<float>(now - previous).count(); previous = now;
            try { renderer_.render(state_, metrics_, delta); }
            catch (const std::exception& e) { error(e.what()); renderer_.shutdown(); }
            float fps = std::min(60.0f, std::max(1.0f, refresh_));
            next = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(1 / fps));
        }
        renderer_.shutdown(); SetEditorRequest({}); vm_->DetachCurrentThread(); env_ = nullptr;
    }
};
Runtime* get(jlong handle) { return reinterpret_cast<Runtime*>(handle); }
}

extern "C" JNIEXPORT jlong JNICALL Java_dev_imgui_overlay_NativeBridge_create(JNIEnv* env, jclass, jobject assets,
                                                                              jobject listener, jfloat density, jfloat refresh) {
    jobject global = nullptr;
    try {
        AAssetManager* manager = AAssetManager_fromJava(env, assets);
        if (!manager) throw std::runtime_error("Asset manager unavailable");
        std::unique_ptr<AAsset, decltype(&AAsset_close)> asset(AAssetManager_open(manager, "noto_sans_sc.otf", AASSET_MODE_BUFFER), AAsset_close);
        if (!asset) throw std::runtime_error("assets/noto_sans_sc.otf is missing");
        std::vector<unsigned char> font(AAsset_getLength(asset.get()));
        size_t offset = 0;
        while (offset < font.size()) {
            int read = AAsset_read(asset.get(), font.data() + offset, font.size() - offset);
            if (read <= 0) throw std::runtime_error("Unable to read Chinese font"); offset += read;
        }
        if (font.empty()) throw std::runtime_error("Chinese font is empty");
        JavaVM* vm; env->GetJavaVM(&vm);
        jclass type = env->GetObjectClass(listener);
        jmethodID error = env->GetMethodID(type, "onNativeError", "(Ljava/lang/String;)V");
        jmethodID editor = env->GetMethodID(type, "onEditorRequested", "(IJ[BFFFFI)V"); env->DeleteLocalRef(type);
        if (!error || !editor) return 0;
        global = env->NewGlobalRef(listener);
        if (!global) return 0;
        auto runtime = std::make_unique<Runtime>(vm, global, error, editor, std::move(font), density, refresh);
        runtime->start(); return reinterpret_cast<jlong>(runtime.release());
    } catch (const std::exception& e) {
        if (global) env->DeleteGlobalRef(global);
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), e.what()); return 0;
    }
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_attach(JNIEnv* env, jclass, jlong handle,
                                                                            jint slot, jobject surface, jlong generation) {
    if (!handle || slot < 0 || slot > 1) return;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) { env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Invalid Surface"); return; }
    get(handle)->attach(slot, WindowPtr(window, ANativeWindow_release), generation);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_resize(JNIEnv*, jclass, jlong handle, jint slot, jlong generation, jint w, jint h) {
    if (handle && slot >= 0 && slot <= 1) get(handle)->resize(slot, generation, w, h);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_detach(JNIEnv*, jclass, jlong handle, jint slot, jlong generation) {
    if (handle && slot >= 0 && slot <= 1) get(handle)->detach(slot, generation);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_touch(JNIEnv*, jclass, jlong handle, jlong generation, jint action, jfloat x, jfloat y) {
    if (handle) get(handle)->touch(generation, action, x, y);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_metrics(JNIEnv*, jclass, jlong handle, jfloat density, jfloat refresh, jint l, jint t, jint r, jint b) {
    if (handle) get(handle)->metrics(density, refresh, l, t, r, b);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_pause(JNIEnv*, jclass, jlong handle, jboolean paused) {
    if (handle) get(handle)->pause(paused);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_text(JNIEnv* env, jclass, jlong handle, jint field, jbyteArray text, jboolean finished) {
    if (!handle || !text) return;
    jsize length = env->GetArrayLength(text); std::string value(length, '\0');
    env->GetByteArrayRegion(text, 0, length, reinterpret_cast<jbyte*>(value.data()));
    if (!env->ExceptionCheck()) get(handle)->text(field, std::move(value), finished);
}
extern "C" JNIEXPORT void JNICALL Java_dev_imgui_overlay_NativeBridge_destroy(JNIEnv* env, jclass, jlong handle) {
    if (!handle) return;
    auto runtime = get(handle); runtime->close(); env->DeleteGlobalRef(runtime->listener()); delete runtime;
}
