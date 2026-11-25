#include "ui/UiApp.hpp"

#include "app/App.hpp"
#include "app/resource.h"
#include "core/util/Log.hpp"

#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vd {

namespace {

constexpr UINT WM_APP_DRAIN = WM_APP + 1;
constexpr UINT kTimerMeters = 1;
constexpr wchar_t kWindowClass[] = L"VoiceDeckUI";

// Proper UTF-8 <-> UTF-16 (byte-wise expansion garbles Cyrillic names).
std::wstring utf8ToUtf16(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], len);
    return w;
}
std::string utf16ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr,
                                        nullptr);
    std::string s(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], len, nullptr, nullptr);
    return s;
}

// ---------------------------------------------------------------------------
// Minimal COM handler base (the WebView2 completion/event callbacks).
// ---------------------------------------------------------------------------

template <typename IFace>
class ComHandler : public IFace {
public:
    ComHandler() = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IFace)) {
            *ppv = static_cast<IFace*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

private:
    std::atomic<ULONG> refs_{1};
};

} // namespace

// ---------------------------------------------------------------------------
// UiApp
// ---------------------------------------------------------------------------

class UiApp {
public:
    explicit UiApp(App& app) : app_(app) {}

    int run();

private:
    // --- completion / event handlers (they call back into UiApp) ---
    friend class EnvCreatedHandler;
    friend class ControllerCreatedHandler;
    friend class MessageReceivedHandler;
    friend class NavigationCompletedHandler;

    void createWindow();
    void createWebView();
    void onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env);
    void onControllerCreated(HRESULT hr, ICoreWebView2Controller* controller);
    void onWebMessageReceived(ICoreWebView2WebMessageReceivedEventArgs* args);
    // Extracts DOM File objects posted by the JS drop handler via WebView2's
    // AdditionalObjects collection and adds each as a sound.
    void addDroppedFiles(ICoreWebView2WebMessageReceivedEventArgs* args);
    void onNavigationCompleted();
    void handleMessage(const std::string& json);
    void openAddSoundDialog();
    void openAmbientDialog(int presetIdx);
    void openPthDialog();
    void convertPth(const nlohmann::json& j);

    void postJson(const nlohmann::json& j); // any thread
    void notify(const std::string& text);   // toast in the web UI
    const wchar_t* uiText(const wchar_t* ru, const wchar_t* en) const {
        return app_.settings.language == "en" ? en : ru;
    }
    void drainPending();                     // UI thread
    void pushState();
    void pushMeters();
    void requestExit();
    void suspendWebView();
    void resumeWebView();

public:
    // Used by the free runUi() entry point.
    void postEvent(const nlohmann::json& j) { postJson(j); }
    void exit() { requestExit(); }

private:

    static LRESULT CALLBACK wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    App& app_;
    HWND hwnd_ = nullptr;
    ICoreWebView2Environment* env_ = nullptr;
    ICoreWebView2Controller* controller_ = nullptr;
    ICoreWebView2* web_ = nullptr;
    EventRegistrationToken msgToken_{};
    EventRegistrationToken navToken_{};
    bool metersPaused_ = false; // minimized: no 15 Hz WebView2 traffic
    bool suspendRequested_ = false; // TrySuspend issued; Resume on restore

    // Cross-thread message queue (workers -> UI thread).
    std::mutex pendingMtx_;
    std::vector<std::wstring> pending_;
    // Voice switching: one loader at a time, stale clicks are ignored.
    std::mutex aiLoadMtx_;
    std::atomic<int> aiLoadGen_{0};
};

// ---------------------------------------------------------------------------
// Handler implementations
// ---------------------------------------------------------------------------

class EnvCreatedHandler final
    : public ComHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    explicit EnvCreatedHandler(UiApp* owner) : owner_(owner) {}

    STDMETHODIMP Invoke(HRESULT errorCode, ICoreWebView2Environment* env) override {
        owner_->onEnvironmentCreated(errorCode, env);
        return S_OK;
    }

private:
    UiApp* owner_;
};

class ControllerCreatedHandler final
    : public ComHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    ControllerCreatedHandler(UiApp* owner, ICoreWebView2Environment* env)
        : owner_(owner), env_(env) {
        if (env_) env_->AddRef();
    }

    STDMETHODIMP Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) override {
        owner_->onControllerCreated(errorCode, controller);
        if (env_) env_->Release();
        return S_OK;
    }

private:
    UiApp* owner_;
    ICoreWebView2Environment* env_;
};

class MessageReceivedHandler final : public ComHandler<ICoreWebView2WebMessageReceivedEventHandler> {
public:
    explicit MessageReceivedHandler(UiApp* owner) : owner_(owner) {}

    STDMETHODIMP Invoke(ICoreWebView2* /*sender*/,
                        ICoreWebView2WebMessageReceivedEventArgs* args) override {
        owner_->onWebMessageReceived(args);
        return S_OK;
    }

private:
    UiApp* owner_;
};

class SuspendCompletedHandler final
    : public ComHandler<ICoreWebView2TrySuspendCompletedHandler> {
public:
    STDMETHODIMP Invoke(HRESULT errorCode, BOOL result) override {
        if (FAILED(errorCode) || !result)
            log::warn("WebView2 TrySuspend rejected (0x%08lX, result=%d) - "
                      "keeping the window alive with meters paused",
                      errorCode, int(result));
        else
            log::info("WebView2 suspended while minimized");
        return S_OK;
    }
};

class NavigationCompletedHandler final
    : public ComHandler<ICoreWebView2NavigationCompletedEventHandler> {
public:
    explicit NavigationCompletedHandler(UiApp* owner) : owner_(owner) {}

    STDMETHODIMP Invoke(ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* args) override {
        BOOL ok = FALSE;
        if (args) args->get_IsSuccess(&ok);
        if (ok)
            owner_->onNavigationCompleted();
        else
            log::error("web UI failed to load");
        return S_OK;
    }

private:
    UiApp* owner_;
};

// ---------------------------------------------------------------------------
// UiApp implementation
// ---------------------------------------------------------------------------

int UiApp::run() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        log::error("CoInitializeEx failed");
        return 1;
    }
    createWindow();
    createWebView();

    // Push the initial state once the web view signals readiness (it asks
    // via {"cmd":"init"} on load; nothing else to do here).

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}

void UiApp::createWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = &UiApp::wndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    const auto loadAppIcon = [&](int width, int height) {
        return reinterpret_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_VOICEDECK),
                                                   IMAGE_ICON, width, height, LR_SHARED));
    };
    wc.hIcon = loadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = loadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hbrBackground = CreateSolidBrush(RGB(0x0b, 0x0b, 0x0d));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    const int defaultW = 1120, defaultH = 740;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int w = std::min(defaultW, int(work.right - work.left) - 40);
    const int h = std::min(defaultH, int(work.bottom - work.top) - 40);
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;

    // Frameless custom chrome: no native title bar; the HTML title bar drags,
    // WM_NCHITTEST exposes the resize edges, double-click maximizes.
    hwnd_ = CreateWindowExW(0, kWindowClass, L"VoiceDeck",
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, x, y, w,
                            h, nullptr, nullptr, wc.hInstance, this);
    // Explicit window icons keep Alt-Tab and the taskbar correct even for the
    // frameless window and when Windows does not reuse the class icon.
    const HICON appIcon = loadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    const HICON appIconSm = loadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSm));
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    DragAcceptFiles(hwnd_, TRUE);
}

void UiApp::createWebView() {
    // User data stays in the portable data folder.
    const std::wstring userData = (app_.dataDir / L"webview2").wstring();
    // Enable draggable regions in the WebView2 runtime so CSS
    // -webkit-app-region: drag can move the frameless window.
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--enable-features=msWebView2EnableDraggableRegions=true");
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), options.Get(),
        new EnvCreatedHandler(this));
    if (FAILED(hr)) {
        log::error("WebView2 init failed (0x%08lX) - is the WebView2 Runtime installed?", hr);
        requestExit();
    }
}

void UiApp::onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment* env) {
    if (FAILED(hr) || !env) {
        log::error("WebView2 environment failed (0x%08lX)", hr);
        requestExit();
        return;
    }
    env_ = env;
    env_->AddRef();
    HRESULT ok = env_->CreateCoreWebView2Controller(hwnd_, new ControllerCreatedHandler(this, env));
    if (FAILED(ok)) {
        log::error("CreateCoreWebView2Controller failed (0x%08lX)", ok);
        requestExit();
    }
}

void UiApp::onControllerCreated(HRESULT hr, ICoreWebView2Controller* controller) {
    if (FAILED(hr) || !controller) {
        log::error("WebView2 controller failed (0x%08lX)", hr);
        requestExit();
        return;
    }
    controller_ = controller;
    controller_->AddRef();

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    controller_->put_Bounds(rc);

    // Dark background before the page paints (avoids a white flash).
    if (ICoreWebView2Controller2* c2{};
        SUCCEEDED(controller_->QueryInterface(IID_PPV_ARGS(&c2)))) {
        COREWEBVIEW2_COLOR bg{};
        bg.A = 0xFF;
        bg.R = 0x17;
        bg.G = 0x14;
        bg.B = 0x25;
        c2->put_DefaultBackgroundColor(bg);
        c2->Release();
    }

    if (FAILED(controller_->get_CoreWebView2(&web_)) || !web_) {
        log::error("get_CoreWebView2 failed");
        requestExit();
        return;
    }
    web_->AddRef();

    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(web_->get_Settings(&settings)) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        ICoreWebView2Settings9* settings9 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings9))) && settings9) {
            const HRESULT regionHr = settings9->put_IsNonClientRegionSupportEnabled(TRUE);
            if (FAILED(regionHr))
                log::warn("WebView2 non-client regions unavailable (0x%08lX)", regionHr);
            settings9->Release();
        }
        settings->Release();
    }

    // Accept external file drops inside WebView2. The JS drop handler posts
    // the DOM File objects back to the host; WebView2 exposes their full paths
    // through ICoreWebView2WebMessageReceivedEventArgs2::AdditionalObjects
    // (ICoreWebView2File::Path). This is more reliable than WM_DROPFILES on
    // the parent window, which WebView2 does not always forward.
    {
        ICoreWebView2Controller4* c4 = nullptr;
        if (SUCCEEDED(controller_->QueryInterface(IID_PPV_ARGS(&c4)))) {
            c4->put_AllowExternalDrop(TRUE);
            c4->Release();
        }
    }

    web_->add_WebMessageReceived(new MessageReceivedHandler(this), &msgToken_);
    web_->add_NavigationCompleted(new NavigationCompletedHandler(this), &navToken_);

    // Navigate to the embedded web UI next to the exe.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring url = exePath;
    const std::size_t slash = url.find_last_of(L"\\/");
    url = L"file:///" + url.substr(0, slash) + L"/web/index.html";
    web_->Navigate(url.c_str());

    SetTimer(hwnd_, kTimerMeters, 66, nullptr);
}

void UiApp::addDroppedFiles(ICoreWebView2WebMessageReceivedEventArgs* args) {
    ICoreWebView2WebMessageReceivedEventArgs2* args2 = nullptr;
    if (FAILED(args->QueryInterface(IID_PPV_ARGS(&args2))) || !args2) {
        log::error("drop: WebView2 runtime too old for file objects");
        return;
    }
    ICoreWebView2ObjectCollectionView* objs = nullptr;
    const HRESULT hr = args2->get_AdditionalObjects(&objs);
    args2->Release();
    if (FAILED(hr) || !objs) {
        log::error("drop: no additional objects (0x%08lX)", hr);
        return;
    }
    UINT32 count = 0;
    objs->get_Count(&count);
    std::size_t added = 0, duplicates = 0, failed = 0;
    std::string firstError;
    for (UINT32 i = 0; i < count; ++i) {
        IUnknown* unk = nullptr;
        if (FAILED(objs->GetValueAtIndex(i, &unk)) || !unk) continue;
        ICoreWebView2File* file = nullptr;
        const HRESULT hrFile = unk->QueryInterface(IID_PPV_ARGS(&file));
        unk->Release();
        if (FAILED(hrFile) || !file) continue;
        PWSTR wpath = nullptr;
        if (SUCCEEDED(file->get_Path(&wpath)) && wpath) {
            const std::string npath = utf16ToUtf8(wpath);
            CoTaskMemFree(wpath);
            std::string err;
            // addSound() copies the clip into data\sounds and resolves
            // filename collisions.
            if (app_.addSound(npath, "", err) != 0) {
                ++added;
                log::info("dropped: %s", npath.c_str());
            } else {
                log::error("drop add failed (%s): %s", npath.c_str(), err.c_str());
                if (err.find("уже в библиотеке") != std::string::npos) ++duplicates;
                else ++failed;
                if (firstError.empty()) firstError = err;
            }
        }
        file->Release();
    }
    objs->Release();
    log::info("drop: %zu of %u file(s) added (%zu duplicates, %zu failed)", added, count,
              duplicates, failed);
    if (duplicates)
        notify("Пропущено дубликатов: " + std::to_string(duplicates));
    else if (failed)
        notify(firstError.empty() ? "Не удалось добавить файлы" : firstError);
    if (added) app_.saveAll();
    pushState(); // the UI toast reports how many clips appeared
}

void UiApp::onWebMessageReceived(ICoreWebView2WebMessageReceivedEventArgs* args) {
    PWSTR raw = nullptr;
    if (FAILED(args->get_WebMessageAsJson(&raw)) || !raw) return;
    std::string json;
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, raw, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            json.resize(size_t(len - 1));
            WideCharToMultiByte(CP_UTF8, 0, raw, -1, json.data(), len, nullptr, nullptr);
        }
        CoTaskMemFree(raw);
    }
    if (json.empty()) return;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_discarded() && j.value("cmd", std::string()) == "dropFiles") {
        addDroppedFiles(args); // File paths live in AdditionalObjects, not JSON
        return;
    }
    handleMessage(json);
}

void UiApp::postJson(const nlohmann::json& j) {
    std::string s = j.dump();
    std::wstring w = utf8ToUtf16(s);
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        pending_.push_back(std::move(w));
    }
    if (hwnd_) PostMessageW(hwnd_, WM_APP_DRAIN, 0, 0);
}

void UiApp::notify(const std::string& text) {
    postJson({{"type", "notice"}, {"text", text}});
}

void UiApp::drainPending() {
    std::vector<std::wstring> local;
    {
        std::lock_guard<std::mutex> lk(pendingMtx_);
        local.swap(pending_);
    }
    if (!web_) return;
    for (const std::wstring& w : local) {
        PWSTR json = static_cast<PWSTR>(CoTaskMemAlloc(sizeof(wchar_t) * (w.size() + 1)));
        if (!json) continue;
        std::memcpy(json, w.c_str(), sizeof(wchar_t) * (w.size() + 1));
        web_->PostWebMessageAsJson(json);
        CoTaskMemFree(json);
    }
}

void UiApp::pushState() {
    if (!web_) return;
    const std::wstring w = utf8ToUtf16(app_.statusJson().dump());
    PWSTR json = static_cast<PWSTR>(CoTaskMemAlloc(sizeof(wchar_t) * (w.size() + 1)));
    if (!json) return;
    std::memcpy(json, w.c_str(), sizeof(wchar_t) * (w.size() + 1));
    const HRESULT hr = web_->PostWebMessageAsJson(json);
    if (FAILED(hr)) log::error("PostWebMessageAsJson failed: 0x%08lX", hr);
    CoTaskMemFree(json);
}

void UiApp::pushMeters() {
    if (!web_ || metersPaused_) return;
    const std::wstring w = utf8ToUtf16(app_.metersJson().dump());
    PWSTR json = static_cast<PWSTR>(CoTaskMemAlloc(sizeof(wchar_t) * (w.size() + 1)));
    if (!json) return;
    std::memcpy(json, w.c_str(), sizeof(wchar_t) * (w.size() + 1));
    web_->PostWebMessageAsJson(json);
    CoTaskMemFree(json);
}

void UiApp::requestExit() {
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void UiApp::suspendWebView() {
    if (!web_ || suspendRequested_) return;
    ICoreWebView2_3* w3 = nullptr;
    if (FAILED(web_->QueryInterface(IID_PPV_ARGS(&w3))) || !w3) return;
    suspendRequested_ = true;
    const HRESULT hr = w3->TrySuspend(new SuspendCompletedHandler());
    if (FAILED(hr)) {
        suspendRequested_ = false;
        log::warn("TrySuspend call failed (0x%08lX) - WebView2 stays active", hr);
    }
    w3->Release();
}

void UiApp::resumeWebView() {
    if (!web_ || !suspendRequested_) return;
    ICoreWebView2_3* w3 = nullptr;
    if (SUCCEEDED(web_->QueryInterface(IID_PPV_ARGS(&w3))) && w3) {
        w3->Resume();
        w3->Release();
    }
    suspendRequested_ = false;
}

void UiApp::openAddSoundDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) {
        log::error("cannot open the file dialog");
        return;
    }
    {
        COMDLG_FILTERSPEC filters[] = {
            {uiText(L"Аудио (WAV, MP3, FLAC)", L"Audio (WAV, MP3, FLAC)"), L"*.wav;*.mp3;*.flac"},
            {uiText(L"Все файлы", L"All files"), L"*.*"},
        };
        dlg->SetFileTypes(2, filters);
        dlg->SetOptions(FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(uiText(L"Добавить звуки в VoiceDeck", L"Add sounds to VoiceDeck"));
    }
    if (SUCCEEDED(dlg->Show(hwnd_))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items)) && items) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    std::wstring wpath = path;
                    CoTaskMemFree(path);
                    std::string npath = utf16ToUtf8(wpath);
                    std::string err;
                    const std::uint64_t id = app_.addSound(npath, "", err);
                    if (id == 0) {
                        log::error("add failed: %s", err.c_str());
                        notify(err.empty() ? "Не удалось добавить звук" : err);
                    }
                }
                item->Release();
            }
            items->Release();
            app_.saveAll();
        }
    }
    dlg->Release();
    pushState();
}

void UiApp::openAmbientDialog(int presetIdx) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) {
        notify("Не удалось открыть диалог выбора эмбиента");
        return;
    }
    {
        COMDLG_FILTERSPEC filters[] = {
            {uiText(L"Аудио (WAV, MP3, FLAC, OGG)", L"Audio (WAV, MP3, FLAC, OGG)"),
             L"*.wav;*.mp3;*.flac;*.ogg;*.opus;*.m4a"},
            {uiText(L"Все файлы", L"All files"), L"*.*"},
        };
        dlg->SetFileTypes(2, filters);
        dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
        dlg->SetTitle(uiText(L"Выберите фоновый эмбиент для персонажа",
                             L"Choose a background ambience"));
    }
    if (SUCCEEDED(dlg->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                const std::string err = app_.importAmbient(presetIdx, utf16ToUtf8(path));
                if (!err.empty()) notify(err);
                else notify("Эмбиент назначен персонажу");
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    pushState();
}

void UiApp::openPthDialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg)))) {
        notify("Не удалось открыть диалог выбора модели");
        return;
    }
    COMDLG_FILTERSPEC filters[] = {
        {uiText(L"Голос RVC (*.pth)", L"RVC voice (*.pth)"), L"*.pth"},
        {uiText(L"Все файлы", L"All files"), L"*.*"}};
    dlg->SetFileTypes(2, filters);
    dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
    dlg->SetTitle(uiText(L"Выберите RVC v2 модель (.pth)",
                         L"Select an RVC v2 model (.pth)"));
    if (SUCCEEDED(dlg->Show(hwnd_))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                const std::wstring wpath = path;
                CoTaskMemFree(path);
                std::filesystem::path p(wpath);
                postJson({{"type", "pthSelected"},
                          {"path", utf16ToUtf8(wpath)},
                          {"name", utf16ToUtf8(p.stem().wstring())}});
            }
            item->Release();
        }
    }
    dlg->Release();
}

namespace {
std::wstring exeDirectory() {
    wchar_t buf[32768] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return L".";
    return std::filesystem::path(buf).parent_path().wstring();
}

// Returns a usable bootstrap interpreter. The converter pins torch 2.5.1,
// which has no wheels for Python 3.13+, so prefer the py-launcher versions.
std::wstring bootstrapPython() {
    auto works = [](const wchar_t* cmd) {
        std::wstring line = cmd;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi))
            return false;
        WaitForSingleObject(pi.hProcess, 8000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code == 0;
    };
    if (works(L"py -3.12 -c \"pass\"")) return L"py -3.12";
    if (works(L"py -3.10 -c \"pass\"")) return L"py -3.10";
    return L"python";
}
} // namespace

void UiApp::convertPth(const nlohmann::json& j) {
    const std::string sp = j.value("path", std::string());
    if (sp.empty()) { notify("Не выбран файл модели"); return; }
    std::wstring pth = utf8ToUtf16(sp);
    if (!std::filesystem::exists(pth)) { notify("Файл модели не найден"); return; }

    std::wstring name = utf8ToUtf16(j.value("name", std::string()));
    for (wchar_t& c : name)
        if (c == L'"' || c == L'<' || c == L'>' || c == L'|' || c == L'\n') c = L'_';
    if (name.empty()) name = std::filesystem::path(pth).stem().wstring();
    std::string gender = j.value("gender", std::string("other"));
    if (gender != "male" && gender != "female" && gender != "other") gender = "other";
    int pitch = std::max(-24, std::min(24, j.value("pitch", 0)));
    const bool fp16 = j.value("fp16", false);

    const std::filesystem::path exeDir = exeDirectory();
    const std::wstring script = (exeDir / "tools" / "aivoice" / "voicedeck_convert.py").wstring();
    if (!std::filesystem::exists(script)) {
        notify("Конвертер не найден: tools\\aivoice\\voicedeck_convert.py");
        return;
    }
    const std::wstring dataDir = app_.dataDir.wstring();
    const std::wstring logPath =
        (app_.dataDir / "aivoice" / "convert.log").wstring();

    std::wstring cmd = bootstrapPython();
    cmd += L" \"" + script + L"\" \"" + dataDir + L"\" \"" + pth + L"\" \"" + name + L"\" " +
           utf8ToUtf16(gender) + L" " + std::to_wstring(pitch) + (fp16 ? L" fp16" : L"");

    notify("Конвертация запущена. При первом запуске установка зависимостей займёт несколько минут.");
    log::info("starting .pth conversion: %s", utf16ToUtf8(name).c_str());
    std::thread([this, cmd, logPath]() {
        std::filesystem::create_directories(std::filesystem::path(logPath).parent_path());
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE hLog = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        PROCESS_INFORMATION pi{};
        bool ok = false;
        std::wstring line = cmd;
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1;
            ok = GetExitCodeProcess(pi.hProcess, &code) && code == 0;
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);

        std::string text;
        if (FILE* f = _wfopen(logPath.c_str(), L"rb")) {
            if (fseek(f, 0, SEEK_END) == 0) {
                const long n = ftell(f);
                if (n > 0) {
                    std::string all(size_t(n), '\0');
                    fseek(f, 0, SEEK_SET);
                    const std::size_t got = std::fread(all.data(), 1, all.size(), f);
                    all.resize(got);
                    const std::size_t from = all.size() > 600 ? all.size() - 600 : 0;
                    text = all.substr(from);
                }
            }
            fclose(f);
        }
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        // postJson is thread-safe and queues the message to the UI thread.
        nlohmann::json out;
        out["type"] = "voiceConvertDone";
        out["ok"] = ok;
        out["text"] = text;
        postJson(out);
    }).detach();
}

void UiApp::onNavigationCompleted() {
    // The page is up - deliver the full state immediately (also covers the
    // case where the page's own "init" request raced the bridge).
    pushState();
}

void UiApp::handleMessage(const std::string& jsonText) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonText);
        // Defensive: a pre-stringified message arrives as a JSON string
        // value - unwrap it.
        while (j.is_string()) j = nlohmann::json::parse(j.get<std::string>());
    } catch (...) {
        return;
    }
    if (!j.is_object() || !j.contains("cmd")) return;
    const std::string cmd = j.at("cmd").get<std::string>();
    log::info("UI cmd: %s", cmd.c_str());

    if (cmd == "init" || cmd == "refresh") {
        pushState();
        return;
    }
    if (cmd == "uiReady") {
        log::info("UI bridge OK (state delivered and handshake returned)");
        return;
    }
    if (cmd == "setLatencyMode") {
        const std::string err = app_.setLatencyMode(j.value("mode", 1));
        if (!err.empty()) log::error("latency: %s", err.c_str());
        pushState();
        return;
    }

    if (cmd == "winDrag") {
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return;
    }
    if (cmd == "winMin") {
        ShowWindow(hwnd_, SW_MINIMIZE);
        return;
    }
    if (cmd == "winMax") {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        GetWindowPlacement(hwnd_, &wp);
        ShowWindow(hwnd_, (wp.showCmd == SW_MAXIMIZE) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    }
    if (cmd == "exit") {
        requestExit();
        return;
    }

    // --- voice characters ---------------------------------------------------
    if (cmd == "preset") {
        if (j.contains("idx")) {
            app_.applyPreset(j.at("idx").get<int>());
            // Publish the target index only after the audio callback has
            // consumed SetPreset; otherwise the first click can be overwritten
            // by a stale meter frame carrying the previous active preset.
            app_.engine.drainCommands();
        }
        pushMeters();
        return;
    }
    if (cmd == "dspOn") {
        app_.setDsp(j.value("on", true));
        pushMeters();
        return;
    }
    if (cmd == "bypassOn") {
        app_.setBypass(j.value("on", true));
        pushMeters();
        return;
    }
    if (cmd == "duckingOn") {
        app_.setDucking(j.value("on", true));
        pushMeters();
        return;
    }
    if (cmd == "setLanguage") {
        app_.setLanguage(j.value("language", "ru"));
        pushState();
        return;
    }
    if (cmd == "ambientOn") {
        const std::string err = app_.setAmbientEnabled(j.value("on", true));
        if (!err.empty()) notify(err);
        pushState();
        return;
    }
    if (cmd == "soundFxSend") {
        const std::string err = app_.setSoundFxSend(j.value("on", false));
        if (!err.empty()) notify(err);
        pushState();
        return;
    }
    if (cmd == "soundboardMonitor") {
        const std::size_t idx = j.value("idx", std::size_t(SIZE_MAX));
        if (!app_.setSoundboardMonitor(j.value("on", false), idx))
            notify("включи оригинальный выход в настройках");
        pushState();
        return;
    }
    if (cmd == "setPresetAmbient") {
        const std::string err = app_.setPresetAmbient(j.value("idx", -1), j);
        if (!err.empty()) notify(err);
        pushMeters();
        return;
    }
    if (cmd == "ambientFileDialog") {
        openAmbientDialog(j.value("idx", -1));
        return;
    }
    if (cmd == "vadThreshold") {
        const std::string err = app_.setVadThresholdDb(float(j.value("db", -48.0)));
        if (!err.empty()) notify(err);
        pushMeters();
        return;
    }
    if (cmd == "pitch") {
        app_.setPitchOverride(float(j.value("semi", 0.0)), j.value("on", true));
        pushMeters();
        return;
    }
    if (cmd == "formant") {
        app_.setFormantOverride(float(j.value("semi", 0.0)), j.value("on", true));
        pushMeters();
        return;
    }

    // --- user presets -----------------------------------------------------------
    if (cmd == "setPresetEq") {
        const int idx = j.value("idx", -1);
        float eq[8] = {0,0,0,0,0,0,0,0};
        if (j.contains("eq") && j.at("eq").is_array()) {
            for (int i = 0; i < 8 && std::size_t(i) < j.at("eq").size(); ++i)
                eq[i] = float(j.at("eq")[std::size_t(i)].get<double>());
        }
        const std::string err = app_.setPresetEq(idx, eq);
        if (!err.empty()) log::error("preset eq: %s", err.c_str());
        pushMeters();
        return;
    }
    if (cmd == "livePreset") {
        const int idx = j.value("idx", -1);
        if (idx < 0 || std::size_t(idx) >= app_.presets.size()) return;
        app_.liveUpdatePreset(idx, j);
        // No pushState: re-render would reset the editor sliders.
        pushMeters();
        return;
    }
    if (cmd == "resetPreset") {
        if (app_.resetLivePreset())
            pushState();
        else
            notify("Аудиодвижок занят — повтори выход из редактора");
        return;
    }
    if (cmd == "addPreset") {
        if (!j.contains("def") || !j.at("def").is_object()) {
            log::error("addPreset: missing def");
            return;
        }
        std::string err;
        const int idx = app_.addPreset(j.at("def"), err);
        if (idx < 0) log::error("preset: %s", err.c_str());
        pushState();
        return;
    }
    if (cmd == "removePreset") {
        const std::string err = app_.removePreset(j.value("idx", -1));
        if (!err.empty()) log::error("removePreset: %s", err.c_str());
        pushState();
        return;
    }

    // --- soundboard ---------------------------------------------------------
    if (cmd == "play") {
        if (j.contains("id")) app_.playSound(j.at("id").get<std::uint64_t>());
        pushMeters();
        return;
    }
    if (cmd == "pause") {
        Command c;
        c.type = Command::Type::PauseSound;
        c.soundId = j.at("id").get<std::uint64_t>();
        app_.engine.submit(c);
        pushMeters();
        return;
    }
    if (cmd == "seek") {
        Command c;
        c.type = Command::Type::SeekSound;
        c.soundId = j.at("id").get<std::uint64_t>();
        c.value = float(j.value("frac", 0.0));
        app_.engine.submit(c);
        pushMeters();
        return;
    }
    if (cmd == "setSoundEq") {
        Command c;
        c.type = Command::Type::SetSoundEq;
        c.soundId = j.at("id").get<std::uint64_t>();
        c.value = float(j.value("b", 0.0));
        c.valueB = float(j.value("m", 0.0));
        c.valueC = float(j.value("t", 0.0));
        app_.engine.submit(c);
        // No pushState: re-render would close open EQ panels.
        return;
    }
    if (cmd == "rename") {
        const std::string err =
            app_.renameSound(j.at("id").get<std::uint64_t>(), j.value("name", std::string()));
        if (!err.empty()) log::error("rename failed: %s", err.c_str());
        pushState();
        return;
    }
    if (cmd == "preview") {
        const std::size_t idx = j.value("idx", std::size_t(SIZE_MAX));
        const std::string err = app_.previewSound(j.at("id").get<std::uint64_t>(), idx);
        if (!err.empty()) log::error("preview failed: %s", err.c_str());
        pushState(); // sync the original-output selector and monitor state
        return;
    }
    if (cmd == "stopPreview") {
        app_.stopPreview();
        pushState(); // monitor state may have changed — sync the toggle
        return;
    }
    if (cmd == "stop") {
        if (j.contains("id")) app_.stopSound(j.at("id").get<std::uint64_t>());
        pushMeters();
        return;
    }
    if (cmd == "stopall") {
        app_.stopAllSounds();
        pushMeters();
        return;
    }
    if (cmd == "volume") {
        app_.setSoundVolume(j.at("id").get<std::uint64_t>(), j.value("pct", 100));
        pushMeters();
        return;
    }
    if (cmd == "loop") {
        app_.setSoundLoop(j.at("id").get<std::uint64_t>(), j.value("on", false));
        pushState();
        return;
    }
    if (cmd == "remove") {
        const std::string err = app_.removeSound(j.at("id").get<std::uint64_t>());
        if (!err.empty()) log::error("remove failed: %s", err.c_str());
        pushState();
        return;
    }
    if (cmd == "addFileDialog") {
        openAddSoundDialog();
        return;
    }
    if (cmd == "refreshSounds") {
        const auto [added, removed] = app_.refreshSounds();
        log::info("UI refreshSounds: %zu added, %zu removed", added, removed);
        pushState();
        return;
    }
    if (cmd == "openModelsHelp") {
        ShellExecuteA(hwnd_, "open",
                      "https://github.com/bhoirpushpak/voice-ctx/releases/latest/download/"
                      "VoiceCtx-AiVoice-Models-v1.zip",
                      nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // --- hotkeys --------------------------------------------------------------
    if (cmd == "hotkey") {
        const std::string target = j.value("target", std::string());
        const std::string combo = j.value("combo", std::string());
        std::string err;
        if (target == "sound")
            err = app_.bindSoundHotkey(j.at("id").get<std::uint64_t>(), combo);
        else if (target == "stopall")
            err = app_.hotkeys.bindStopAll(combo);
        else if (target == "preset")
            err = app_.hotkeys.bindPreset(j.at("idx").get<std::uint64_t>(), combo);
        else if (target == "voice")
            err = app_.hotkeys.bindVoiceToggle(combo);
        else if (target == "ai")
            err = app_.hotkeys.bindAiToggle(combo);
        else
            err = "unknown hotkey target";
        if (!err.empty()) log::error("hotkey: %s", err.c_str());
        app_.saveAll();
        pushState();
        return;
    }
    if (cmd == "hotkeyOff") {
        const std::string target = j.value("target", std::string());
        std::string err;
        if (target == "voice")
            app_.hotkeys.bindVoiceToggle("");
        else if (target == "ai")
            app_.hotkeys.bindAiToggle("");
        else if (target == "stopall")
            app_.hotkeys.bindStopAll("");
        else if (target == "preset")
            app_.hotkeys.bindPreset(j.at("idx").get<std::uint64_t>(), "");
        else if (target == "sound")
            err = app_.bindSoundHotkey(j.at("id").get<std::uint64_t>(), "");
        app_.saveAll();
        if (!err.empty()) notify(err);
        pushState();
        return;
    }

    // --- tags ---------------------------------------------------------------
    if (cmd == "setSoundTags") {
        const std::string err = app_.setSoundTags(j.at("id").get<std::uint64_t>(), j.at("tags"));
        if (!err.empty()) notify(err);
        pushState();
        return;
    }
    if (cmd == "addSoundTag") {
        const std::string err = app_.addSoundTag(j.at("id").get<std::uint64_t>(),
                                                 j.value("tag", std::string()));
        if (!err.empty()) notify(err);
        pushState();
        return;
    }
    if (cmd == "removeSoundTag") {
        const std::string err = app_.removeSoundTag(j.at("id").get<std::uint64_t>(),
                                                    j.value("tag", std::string()));
        if (!err.empty()) notify(err);
        pushState();
        return;
    }

    // --- in-app audio editor ------------------------------------------------
    if (cmd == "editSound") {
        const std::string err = app_.editSound(j.at("id").get<std::uint64_t>(), j);
        if (!err.empty()) notify(err);
        else notify("Звук отредактирован");
        pushState();
        return;
    }

    // --- Windows integration ------------------------------------------------
    if (cmd == "autoStart") {
        const std::string err = app_.setAutoStart(j.value("on", false));
        if (!err.empty()) notify(err);
        else notify(j.value("on", false) ? "Автозапуск включён" : "Автозапуск выключен");
        pushState();
        return;
    }
    if (cmd == "runAsAdmin") {
        bool relaunching = false;
        const std::string err = app_.setRunAsAdmin(j.value("on", false), relaunching);
        if (relaunching) {
            requestExit();
            return;
        }
        if (!err.empty()) notify(err);
        else notify(j.value("on", false) ? "Запуск от имени администратора включён"
                                         : "Запуск от имени администратора выключен");
        pushState();
        return;
    }

    // --- AI voice ---------------------------------------------------------------
    if (cmd == "aivoiceConvertPth") {
        openPthDialog();
        return;
    }
    if (cmd == "convertPth") {
        convertPth(j);
        return;
    }
    if (cmd == "aivoiceImport") {
        // Import a user voice pair (x.onnx + x.json) into data\AiVoice\voices.
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dlg)))) {
            log::error("cannot open the file dialog");
            return;
        }
        {
            COMDLG_FILTERSPEC filters[] = {
                {uiText(L"AI-голос (ONNX + JSON рядом)", L"AI voice (ONNX + matching JSON)"),
                 L"*.onnx"},
                {uiText(L"Все файлы", L"All files"), L"*.*"},
            };
            dlg->SetFileTypes(2, filters);
            dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
            dlg->SetTitle(uiText(L"Импорт AI-голоса (нужен .onnx и его .json)",
                                 L"Import AI voice (.onnx with matching .json)"));
        }
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    std::wstring wpath = path;
                    CoTaskMemFree(path);
                    std::string npath = utf16ToUtf8(wpath);
                    const std::string err = app_.importAiVoice(npath);
                    if (!err.empty())
                        log::error("import failed: %s", err.c_str());
                    else
                        log::info("AI voice imported");
                }
                item->Release();
            }
        }
        dlg->Release();
        pushState();
        return;
    }
    if (cmd == "aiOn") {
        app_.setAi(j.value("on", true));
        pushMeters();
        return;
    }
    if (cmd == "aivoice") {
        // Model load takes seconds - keep the UI responsive, but never run two
        // loads concurrently (rapid clicking used to crash ONNX Runtime).
        const std::string name = j.value("name", std::string());
        const int gen = ++aiLoadGen_;
        std::thread([this, name, gen] {
            std::lock_guard<std::mutex> lk(aiLoadMtx_);
            if (gen != aiLoadGen_.load()) return; // a newer click superseded us
            const std::string err = app_.loadAiVoice(name);
            if (gen != aiLoadGen_.load()) return;
            nlohmann::json ev;
            ev["type"] = "aiLoaded";
            ev["ok"] = err.empty();
            ev["err"] = err;
            postJson(ev);
            postJson(app_.statusJson());
        }).detach();
        return;
    }
    if (cmd == "aipitch") {
        app_.setAiPitch(float(j.value("semi", 0.0)));
        pushMeters();
        return;
    }
    if (cmd == "aiMargin") {
        app_.setAiMargin(j.value("ms", 100));
        pushState();
        return;
    }
    if (cmd == "aiFade") {
        app_.setAiFade(j.value("ms", 30));
        pushState();
        return;
    }
    if (cmd == "aiAutolimit") {
        app_.setAiAutolimit(j.value("on", true), j.value("n", 8));
        pushState();
        return;
    }
    if (cmd == "aiProvider") {
        const std::string err = app_.setAiProvider(j.value("name", std::string()));
        if (!err.empty()) log::error("provider: %s", err.c_str());
        pushState();
        return;
    }

    // --- routing ------------------------------------------------------------------
    if (cmd == "select") {
        app_.selectDevices(j.at("cap").get<std::size_t>(), j.at("play").get<std::size_t>());
        pushState();
        return;
    }
    if (cmd == "monitor") {
        const std::size_t idx = j.value("idx", std::size_t(SIZE_MAX));
        if (j.value("on", true) && !app_.setMonitor(true, idx))
            notify("включи оригинальный выход в настройках");
        else if (!j.value("on", true))
            app_.setMonitor(false, idx);
        pushState();
        return;
    }
    if (cmd == "monitorOutput") {
        if (!app_.setMonitorOutput(j.value("idx", std::size_t(SIZE_MAX))))
            notify("не удалось открыть выбранный оригинальный выход");
        pushState();
        return;
    }
}

LRESULT CALLBACK UiApp::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiApp* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<UiApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<UiApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UiApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // Remove the native frame: the client area covers the whole window.
            if (wp) return 0;
            break;
        case WM_NCHITTEST: {
            // Resize edges for the frameless window.
            RECT rw{};
            GetWindowRect(hwnd, &rw);
            const LONG x = GET_X_LPARAM(lp);
            const LONG y = GET_Y_LPARAM(lp);
            const int b = 6;
            const bool left = x < rw.left + b;
            const bool right = x >= rw.right - b;
            const bool top = y < rw.top + b;
            const bool bottom = y >= rw.bottom - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            return HTCLIENT;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            log::info("file drop: %u file(s)", count);
            std::size_t added = 0;
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[MAX_PATH] = {};
                if (DragQueryFileW(drop, i, path, MAX_PATH) > 0) {
                    const std::string npath = utf16ToUtf8(path);
                    std::string err;
                    // addSound() copies the clip into data\sounds itself and
                    // resolves filename collisions (" (2)", " (3)", ...).
                    const std::uint64_t id = app_.addSound(npath, "", err);
                    if (id != 0) {
                        ++added;
                        log::info("dropped: %s (id %llu)", npath.c_str(),
                                  static_cast<unsigned long long>(id));
                    } else {
                        log::error("drop add failed (%s): %s", npath.c_str(), err.c_str());
                    }
                }
            }
            log::info("file drop done: %zu of %u added", added, count);
            DragFinish(drop);
            app_.saveAll();
            pushState();
            return 0;
        }
        case WM_SIZE: {
            if (controller_) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                controller_->put_Bounds(rc);
            }
            // Minimized: stop the 15 Hz meters/state traffic to WebView2.
            if (wp == SIZE_MINIMIZED) {
                if (!metersPaused_) {
                    KillTimer(hwnd, kTimerMeters);
                    metersPaused_ = true;
                }
                suspendWebView();
            } else {
                resumeWebView();
                if (metersPaused_) {
                    SetTimer(hwnd, kTimerMeters, 66, nullptr);
                    metersPaused_ = false;
                }
            }
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 780;
            mmi->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerMeters) {
                drainPending();
                pushMeters();
                return 0;
            }
            return 0;
        case WM_APP_DRAIN:
            drainPending();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerMeters);
            if (web_) {
                web_->remove_WebMessageReceived(msgToken_);
                web_->remove_NavigationCompleted(navToken_);
                web_->Release();
                web_ = nullptr;
            }
            if (controller_) {
                controller_->Release();
                controller_ = nullptr;
            }
            if (env_) {
                env_->Release();
                env_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int runUi(App& app) {
    UiApp ui(app);
    // Refresh the UI whenever hotkeys/tray change something.
    app.onHotkeyEvent = [&ui](const char*) { ui.postEvent(nlohmann::json{{"type", "refresh"}}); };
    app.onExitRequested = [&ui] { ui.exit(); };
    const int code = ui.run();
    app.onHotkeyEvent = nullptr;
    app.onExitRequested = nullptr;
    return code;
}

} // namespace vd
