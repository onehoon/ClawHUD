#pragma once

#include <windows.h>

#include <string>

class App;

class SettingsWindow
{
public:
    explicit SettingsWindow(App& app);
    ~SettingsWindow();

    bool Show(HINSTANCE instance);
    HWND Window() const { return window_; }
    void UpdateHudControls();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void UpdateGeneralControls();
    void CreateTabs();
    void CreateSettingsControls();
    void CreateTweaksControls();
    void CreateAboutControls();
    void Layout();
    void LayoutSettings();
    void LayoutTweaks();
    void LayoutAbout();
    void ShowTab(int index);
    void UpdateTweaksControls();
    void ApplyWindowStyle();
    void RecreateFont();
    void ApplyFont();
    void ApplyHeadingFont();
    void NormalizeWindowToWorkArea();
    void RefreshDpiAndLayout();
    int Scale(int value) const noexcept;
    void ScrollActivePanel(int delta);
    void ApplyScrollPosition();
    void ClampScrollOffsets();
    int ActiveTab() const noexcept;
    int ContentHeightForTab(int tab) const noexcept;
    int ViewportHeight() const noexcept;
    int& ScrollOffsetForTab(int tab) noexcept;

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HWND tabs_{};
    HWND settingsPanel_{};
    HWND startWithWindows_{};
    HWND enableHud_{};
    HWND visibilityAlways_{};
    HWND visibilityInGameOnly_{};
    HWND tweaksPanel_{};
    HWND intelVrrToggle_{};
    HWND intelVrrPanel_{};
    HWND intelVrrRange_{};
    HWND intelVrrResult_{};
    HWND alignmentLeft_{};
    HWND alignmentCenter_{};
    HWND alignmentRight_{};
    HWND backgroundFull_{};
    HWND backgroundContent_{};
    HWND opacitySlider_{};
    HWND opacityLabel_{};
    HWND hudSizeMinus_{};
    HWND hudSizeValue_{};
    HWND hudSizePlus_{};
    HWND fontUnispace_{};
    HWND fontSegoeUiVariable_{};
    HWND aboutPanel_{};
    HWND aboutIcon_{};
    int settingsScrollY_{};
    int tweaksScrollY_{};
    int aboutScrollY_{};
    int wheelRemainder_{};
    bool panActive_{};
    LONG lastPanY_{};
    UINT dpi_{ 96 };
    HFONT uiFont_{};
    HFONT headingFont_{};
    bool systemBackdropActive_{};
};
