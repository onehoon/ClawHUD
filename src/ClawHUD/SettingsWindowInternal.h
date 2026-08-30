#pragma once

#include <windows.h>

namespace clawhud::settings::internal
{
inline constexpr wchar_t kSettingsClassName[] = L"ClawHUD.SettingsWindow";
inline constexpr int kTabSettings = 0;
inline constexpr int kTabTweaks = 1;
inline constexpr int kTabAbout = 2;
inline constexpr int kTabDiagnostics = 3;
inline constexpr int kTabCount = 4;
inline constexpr int kStartWithWindows = 1001;
inline constexpr int kStartEc = 1101;
inline constexpr int kStartIgcl = 1105;
inline constexpr int kStartApi2 = 1106;
inline constexpr int kOpenLogs = 1102;
inline constexpr int kStartVrr = 1103;
inline constexpr int kStopVrr = 1104;
inline constexpr int kAlignmentLeft = 1201;
inline constexpr int kAlignmentCenter = 1202;
inline constexpr int kAlignmentRight = 1203;
inline constexpr int kBackgroundFull = 1204;
inline constexpr int kBackgroundContent = 1205;
inline constexpr int kOpacitySlider = 1206;
inline constexpr int kIntelVrrToggle = 1301;
inline constexpr int kDebugLoggingToggle = 1302;
inline constexpr int kEnableHud = 1207;
inline constexpr int kVisibilityAlways = 1208;
inline constexpr int kVisibilityInGameOnly = 1209;
inline constexpr int kHudSizeMinus = 1210;
inline constexpr int kHudSizePlus = 1211;
inline constexpr int kFontUnispace = 1212;
inline constexpr int kFontSegoeUiVariable = 1213;
inline constexpr int kGeneralHeading = 2001;
inline constexpr int kHudHeading = 2002;
inline constexpr int kVisibilityLabel = 2003;
inline constexpr int kHudSizeLabel = 2004;
inline constexpr int kFontLabel = 2005;
inline constexpr int kAlignmentLabel = 2006;
inline constexpr int kBackgroundWidthLabel = 2007;
inline constexpr int kOpacityLabel = 2008;
inline constexpr int kTweaksHeading = 2101;
inline constexpr int kTweaksDescription = 2102;
inline constexpr int kDiagnosticsVrrHeading = 2201;
inline constexpr int kDiagnosticsVrrDescription = 2202;
inline constexpr int kDiagnosticsEcHeading = 2203;
inline constexpr int kDiagnosticsEcDescription = 2204;
inline constexpr int kDiagnosticsIgclHeading = 2205;
inline constexpr int kDiagnosticsIgclDescription = 2206;
inline constexpr int kDiagnosticsApi2Heading = 2207;
inline constexpr int kDiagnosticsApi2Description = 2208;
inline constexpr int kAboutTitle = 2301;
inline constexpr int kAboutDescription = 2302;
inline constexpr int kAboutVersion = 2303;
inline constexpr int kAboutHowToUse = 2304;
inline constexpr int kAboutInstructions = 2305;
inline constexpr int kWheelStep = 48;
inline constexpr int kDefaultWindowWidthDip = 680;
inline constexpr int kDefaultWindowHeightDip = 600;
inline constexpr int kMinimumWindowWidthDip = 600;
inline constexpr int kMinimumWindowHeightDip = 420;

LRESULT CALLBACK ForwardPanelNotifications(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
LRESULT CALLBACK ForwardPanGesture(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
void ConfigureVerticalPan(HWND window);
void MoveControl(HWND parent, int id, int x, int y, int width, int height);
void EnableMouseWheelForwarding(HWND control);
void EnableStaticPanForwarding(HWND panel);
}
