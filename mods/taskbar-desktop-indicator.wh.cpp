// ==WindhawkMod==
// @id              taskbar-desktop-indicator
// @name            Taskbar Desktop Indicator
// @description     Displays the current virtual desktop as a number or marker in the Windows 11 taskbar clock area
// @version         2.0
// @author          Simon Benedict
// @github          https://github.com/simon-ami
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lversion
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Desktop Indicator v2.0

Displays the current virtual desktop as an independent marker, number, or desktop name next to the Windows 11 taskbar clock area.

## Technical Architecture & Injection Details
* Injects a dedicated **`TextBlock`** element **`TextBlock#DesktopIndicatorTextBlock`** into the system tray clock's native XAML hierarchy (`ContainerGrid`).
* Works side-by-side with native controls like `DateInnerTextBlock` and `TimeInnerTextBlock` without breaking existing taskbar alignments or core OS clock behaviors.
* Uses high-efficiency, low-overhead native WinUI/XAML rendering running completely inside the `explorer.exe` process space.

## Key Features
* **Dedicated UI Injection:** Uses its own distinct label control (`DesktopIndicatorTextBlock`), completely avoiding string concatenation or truncation bugs.
* **Multiple Indicator Modes:**
  * **Workspace Markers:** Displays a sequential map of symbols representing your virtual desktops (e.g., `⬤ ○ ○`), dynamically tracking and highlighting the active workspace.
  * **Current Desktop Name:** Automatically fetches and displays user-configured names from the Windows virtual desktop registry.
  * **Arabic Numerals:** Simple numeric format (`1`, `2`, `3`).
  * **Roman Numerals:** Traditional formatting (`I`, `II`, `III`) with dynamic auto-padding to remain perfectly centered regardless of character length variations.
* **Dynamic Adaptive Colors:** Integrates directly with native Windows 11 WinUI Adaptive Colors (`TextFillColorPrimary` / `TextFillColorSecondary`) for automated light/dark theme tracking. Fully supports custom HEX color strings (`#RRGGBB` or `#AARRGGBB`) and accent color overrides.
* **Full Marker Customization:** Independent configuration for active/inactive character glyphs (supports Unicode Symbols and Segoe Fluent Icons), custom opacity percentages, character spacing gaps, and orientation rotation.
* **Text Layout Controls:** Extensive configuration for custom font families, font sizes, font weights (Normal vs. Bold), and custom directional layout padding using standard four-sided margin strings (`left, top, right, bottom`).
* **Smart Desktop Change Tracking:** Dual-mode design combining active real-time COM notifications (`IVirtualDesktopNotificationService`) with a configurable Registry background polling fallback worker to guarantee accurate state tracking.
* **Strict Compatibility Mode:** A one-click safety switch to disable layout customizations, falling back to pure text styling inherited straight from the native clock text.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- indicatorMode: markers
  $name: Indicator Mode
  $description: "Choose between workspace indicators, desktop name, roman numeral and Arabic numerals."
  $options:
  - markers: Workspace Markers
  - desktopName: Current Desktop Name
  - roman: Roman Numerals
  - arabic: Arabic Numerals
- workspace:
  - activeMarker: "⬤"
    $name: Active Marker
    $description: |
      Symbol for the active desktop. Sample values: ⬤, ○, ⬛, ⬜, ✦, ✧, ◆, ◇
      Accepts Unicode Symbols and Segoe Fluent Icons characters.
  - inactiveMarker: "○"
    $name: Inactive Marker
    $description: |
      Symbol for the inactive desktop. Sample values: ⬤, ○, ⬛, ⬜, ✦, ✧, ◆, ◇
      Accepts Unicode Symbols and Segoe Fluent Icons characters.
  - inactiveMarkerOpacity: 25
    $name: Inactive Indicator Opacity
    $description: "Opacity percentage (0-100) for non-active markers."
  - rotation: "0"
    $name: Rotation
    $description: "Rotation applied to workspace markers mode."
    $options:
    - "0": 0°
    - "90": 90°
    - "270": -90°
    - "180": 180°
  $name: Workspace Marker Settings 
- global:
  - hideWhenSingleDesktop: false
    $name: Hide when only 1 desktop
    $description: "Hides the indicator if only one virtual desktop is active."
  - activeColor: ""
    $name: Active Color
    $description: "1 for TextFillColorPrimary, 2 for TextFillColorSecondary, -1 for SystemAccentColorLight2 (dark mode) / SystemAccentColorDark1 (light mode). Or custom (#RRGGBB)."
  - inactiveColor: ""
    $name: Inactive Color
    $description: "1 for TextFillColorPrimary, 2 for TextFillColorSecondary, -1 for SystemAccentColorLight2 (dark mode) / SystemAccentColorDark1 (light mode). Or custom (#RRGGBB)."
  - charSpacing: 1
    $name: Character Spacing
    $description: "Number of spaces inserted between indicator characters."
  - fontSize: 12
    $name: Font Size
    $description: "Direct font size adjustment."
  - fontWeight: normal
    $name: Font Weight
    $description: "Font weight adjustment."
    $options:
    - normal: Normal
    - bold: Bold
  - marginStr: "6, 0, -2, 0"
    $name: Margin
    $description: "Spacing around the indicator (left, top, right, bottom)."
  $name: Appearance Settings
- fontFamily: ""
  $name: Font Family
  $description: "Font for Desktop Name and Number modes. Leave empty to default to the standard clock font."
- other:
  - compatibilityMode: false
    $name: Compatibility Mode
    $description: "Disable custom fonts, colors, size, rotation, and margins to inherit strictly from the clock text."
  - pollIntervalMs: 0
    $name: Polling fallback (ms)
    $description: "Polling interval fallback for desktop changes. 0 disables polling."
  $name: Other Settings
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#undef GetCurrentTime

#include <servprov.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/base.h>

using namespace winrt::Windows::UI::Xaml;
using WindhawkUtils::StringSetting;
using namespace winrt::Windows::UI::Text;
namespace Documents = winrt::Windows::UI::Xaml::Documents;

enum class IndicatorMode { Arabic, Roman, Markers, DesktopName };
enum class FontWeightMode { Normal, Bold };
enum class RotationMode { Deg0, Deg90, DegMinus90, Deg180 };
enum class IndicatorSegmentStyle { Normal, Dimmed, Transparent };

struct IndicatorSegment {
    std::wstring text;
    IndicatorSegmentStyle style = IndicatorSegmentStyle::Normal;
};

struct IndicatorLayout {
    double widestSuffixWidth = 0;
    std::vector<IndicatorSegment> suffixSegments;
};

enum class WinVersion { Unsupported, Win10, Win11, Win11_22H2, Win11_24H2 };

struct ModSettings {
    IndicatorMode indicatorMode = IndicatorMode::Markers;
    bool hideWhenSingleDesktop = false;
    int inactiveMarkerOpacity = 25;
    std::wstring activeColor;
    std::wstring inactiveColor;
    int charSpacing = 1;
    double fontSize = 12.0;
    std::wstring fontFamily;
    bool compatibilityMode = false;
    FontWeightMode fontWeight = FontWeightMode::Normal;
    std::wstring marginStr = L"6, 0, -2, 0";
    int pollIntervalMs = 0;
    std::wstring activeMarker = L"\u2B24";   // ⬤
    std::wstring inactiveMarker = L"\u25CB"; // ○
    RotationMode rotation = RotationMode::Deg0;
};

struct ClockEntry {
    winrt::weak_ref<Controls::TextBlock> dateTextBlock;
    winrt::weak_ref<Controls::TextBlock> timeTextBlock;
    winrt::weak_ref<Controls::Grid> containerGrid;
    winrt::weak_ref<Controls::StackPanel> stackPanel;
    winrt::weak_ref<Controls::TextBlock> indicatorTextBlock;

    std::vector<GridLength> originalContainerGridColumnWidths;
    int originalStackPanelColumn = 0;
    bool originalContainerGridColumnsCaptured = false;
    bool originalStackPanelColumnCaptured = false;
    bool usingSeparateIndicator = false;
    
    std::mutex mutex;
};

using ClockEntryPtr = std::shared_ptr<ClockEntry>;

struct NotificationInterfaceConfig {
    int64_t iidPart1 = 0;
    int64_t iidPart2 = 0;
    int methodCount = 0;
    int currentChangedIndex = -1;
    bool currentChangedHasMonitors = false;
};

struct VirtualDesktopNotificationObject {
    void** vtable = nullptr;
    LONG refCount = 1;
};

WinVersion g_winVersion = WinVersion::Unsupported;
WORD g_explorerBuildNumber = 0;
WORD g_explorerRevisionNumber = 0;
std::atomic<bool> g_systemTrayModuleHooked = false;
std::atomic<bool> g_unloading = false;
std::atomic<int> g_currentDesktopNumber = 1;
std::atomic<int> g_pollIntervalMs = 0;
std::atomic<bool> g_virtualDesktopNotificationsRegistered = false;

std::mutex g_clockEntriesMutex;
std::vector<ClockEntryPtr> g_clockEntries;

HANDLE g_stopEvent = nullptr;
HANDLE g_pollThread = nullptr;
HANDLE g_notificationStopEvent = nullptr;
HANDLE g_notificationThread = nullptr;
HANDLE g_notificationReadyEvent = nullptr;
ModSettings g_settings;
DWORD g_virtualDesktopNotificationCookie = 0;
VirtualDesktopNotificationObject* g_virtualDesktopNotificationObject = nullptr;

using ClockSystemTrayIconDataModel_RefreshIcon_t = void(WINAPI*)(LPVOID, LPVOID);
ClockSystemTrayIconDataModel_RefreshIcon_t ClockSystemTrayIconDataModel_RefreshIcon_Original;
ClockSystemTrayIconDataModel_RefreshIcon_t ClockSystemTrayIconDataModel2_RefreshIcon_Original;

using DateTimeIconContent_OnApplyTemplate_t = void(WINAPI*)(LPVOID);
DateTimeIconContent_OnApplyTemplate_t DateTimeIconContent_OnApplyTemplate_Original;

using BadgeIconContent_get_ViewModel_t = HRESULT(WINAPI*)(LPVOID, LPVOID);
BadgeIconContent_get_ViewModel_t BadgeIconContent_get_ViewModel_Original;

using ClockButton_v_OnDisplayStateChange_t = void(WINAPI*)(LPVOID, bool);
ClockButton_v_OnDisplayStateChange_t ClockButton_v_OnDisplayStateChange_Original;

int ReadCurrentDesktopNumberFromRegistry();
void UpdateAllClockEntries();

const CLSID CLSID_ImmersiveShell = {0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
const GUID SID_VirtualDesktopNotificationService = {0xa501fdec, 0x4a09, 0x464c, {0xae, 0x4e, 0x1b, 0x9c, 0x21, 0xb8, 0x49, 0x18}};
const GUID IID_IUnknown_Local = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IVirtualDesktopNotificationService = {0x0cd45e71, 0xd927, 0x4f15, {0x8b, 0x0a, 0x8f, 0xef, 0x52, 0x53, 0x37, 0xbf}};

MIDL_INTERFACE("0CD45E71-D927-4F15-8B0A-8FEF525337BF")
IVirtualDesktopNotificationService : public IUnknown {
   public:
    virtual HRESULT STDMETHODCALLTYPE Register(IUnknown* pNotification, DWORD* pdwCookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unregister(DWORD dwCookie) = 0;
};

// -- String Utilities --

std::wstring ToRomanNumeral(int value) {
    if (value <= 0) return L"?";
    struct RomanPart { int value; PCWSTR numeral; };
    static constexpr RomanPart kRomanParts[] = {
        {1000, L"M"}, {900, L"CM"}, {500, L"D"}, {400, L"CD"}, {100, L"C"},
        {90, L"XC"},  {50, L"L"},   {40, L"XL"}, {10, L"X"},   {9, L"IX"},
        {5, L"V"},    {4, L"IV"},   {1, L"I"},
    };
    std::wstring result;
    for (const auto& part : kRomanParts) {
        while (value >= part.value) {
            result += part.numeral;
            value -= part.value;
        }
    }
    return result;
}

std::wstring ToArabicNumeral(int value) {
    return std::to_wstring(std::max(value, 0));
}

std::wstring BuildPadding(int count) {
    return std::wstring(std::max(count, 0), L' ');
}

std::wstring ApplyIndicatorCharacterSpacing(const std::wstring& text) {
    int spacing = std::max(g_settings.charSpacing, 0);
    if (spacing == 0 || text.size() < 2) return text;
    std::wstring gap = BuildPadding(spacing);
    std::wstring result;
    result.reserve(text.size() + (text.size() - 1) * gap.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (i > 0) result += gap;
        result += text[i];
    }
    return result;
}

// Applies dynamic padding to keep Roman Numerals perfectly centered
std::wstring FormatDesktopNumber(int value, int desktopCount) {
    if (g_settings.indicatorMode == IndicatorMode::Arabic) {
        return ApplyIndicatorCharacterSpacing(ToArabicNumeral(value));
    } else {
        std::wstring roman = ToRomanNumeral(value);
        int maxLen = 0;
        for (int i = 1; i <= desktopCount; ++i) {
            int len = static_cast<int>(ToRomanNumeral(i).length());
            if (len > maxLen) maxLen = len;
        }
        int diff = maxLen - static_cast<int>(roman.length());
        if (diff > 0) {
            int leftPad = diff / 2;
            int rightPad = diff - leftPad;
            roman = std::wstring(leftPad, L' ') + roman + std::wstring(rightPad, L' ');
        }
        return ApplyIndicatorCharacterSpacing(roman);
    }
}

std::wstring BuildMarkerSequenceText(int desktopCount, const std::wstring& markerSymbol) {
    desktopCount = std::max(desktopCount, 1);
    std::wstring gap = BuildPadding(std::max(g_settings.charSpacing, 0));
    std::wstring result;
    for (int i = 0; i < desktopCount; ++i) {
        if (i > 0) result += gap;
        result += markerSymbol;
    }
    return result;
}

std::wstring FormatDesktopNameOrNumber(int value, const std::vector<std::wstring>& desktopNames, int desktopCount) {
    if (value > 0 && static_cast<size_t>(value) <= desktopNames.size() && !desktopNames[value - 1].empty()) {
        return desktopNames[value - 1];
    }
    return FormatDesktopNumber(value, desktopCount);
}

bool TryParseColorString(const std::wstring& value, winrt::Windows::UI::Color* color) {
    if (!color) return false;
    const wchar_t* start = value.c_str();
    while (*start && iswspace(*start)) ++start;
    const wchar_t* end = start + wcslen(start);
    while (end > start && iswspace(*(end - 1))) --end;
    if (start == end) return false;

    std::wstring trimmed(start, end);
    if (trimmed.rfind(L"#", 0) == 0) trimmed.erase(0, 1);
    else if (trimmed.rfind(L"0x", 0) == 0 || trimmed.rfind(L"0X", 0) == 0) trimmed.erase(0, 2);

    if (trimmed.size() != 6 && trimmed.size() != 8) return false;
    if (!std::all_of(trimmed.begin(), trimmed.end(), [](wchar_t ch) { return iswxdigit(ch) != 0; })) return false;

    unsigned long parsed = wcstoul(trimmed.c_str(), nullptr, 16);
    if (trimmed.size() == 6) {
        color->A = 0xFF;
        color->R = static_cast<BYTE>((parsed >> 16) & 0xFF);
        color->G = static_cast<BYTE>((parsed >> 8) & 0xFF);
        color->B = static_cast<BYTE>(parsed & 0xFF);
    } else {
        color->A = static_cast<BYTE>((parsed >> 24) & 0xFF);
        color->R = static_cast<BYTE>((parsed >> 16) & 0xFF);
        color->G = static_cast<BYTE>((parsed >> 8) & 0xFF);
        color->B = static_cast<BYTE>(parsed & 0xFF);
    }
    return true;
}

// -- WinRT & COM Utilities --

NotificationInterfaceConfig GetNotificationInterfaceConfig() {
    if (g_explorerBuildNumber < 22000) return {};
    if (g_explorerBuildNumber < 22483 || (g_explorerBuildNumber == 22621 && g_explorerRevisionNumber < 2215)) {
        return {5481970284372180562ll, -1679294552252794956ll, 13, 11, true};
    }
    if (g_explorerBuildNumber < 22631 || (g_explorerBuildNumber == 22631 && g_explorerRevisionNumber < 3085)) {
        return {5123538856297626140ll, 8491238173783613346ll, 14, 10, false};
    }
    return {5308375338100058445ll, -2401892766147978065ll, 14, 10, false};
}

bool IsCurrentNotificationInterface(REFIID riid) {
    auto config = GetNotificationInterfaceConfig();
    if (config.methodCount == 0) return false;
    auto riidParts = reinterpret_cast<const int64_t*>(&riid);
    return riidParts[0] == config.iidPart1 && riidParts[1] == config.iidPart2;
}

void HandleVirtualDesktopChangedNotification() {
    if (g_unloading) return;
    int currentDesktopNumber = ReadCurrentDesktopNumberFromRegistry();
    int previousDesktopNumber = g_currentDesktopNumber.exchange(currentDesktopNumber);
    if (currentDesktopNumber != previousDesktopNumber) {
        UpdateAllClockEntries();
    }
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_QueryInterface(VirtualDesktopNotificationObject* pThis, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;
    if (InlineIsEqualGUID(riid, IID_IUnknown_Local) || IsCurrentNotificationInterface(riid)) {
        *ppvObject = pThis;
        InterlockedIncrement(&pThis->refCount);
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_AddRef(VirtualDesktopNotificationObject* pThis) {
    return static_cast<ULONG>(InterlockedIncrement(&pThis->refCount));
}

ULONG STDMETHODCALLTYPE VirtualDesktopNotification_Release(VirtualDesktopNotificationObject* pThis) {
    LONG refCount = InterlockedDecrement(&pThis->refCount);
    if (refCount == 0) {
        delete[] pThis->vtable;
        delete pThis;
    }
    return static_cast<ULONG>(std::max<LONG>(refCount, 0));
}

HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_NoOp() { return S_OK; }
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_CurrentChanged(VirtualDesktopNotificationObject*) {
    HandleVirtualDesktopChangedNotification(); return S_OK;
}
HRESULT STDMETHODCALLTYPE VirtualDesktopNotification_CurrentChangedWithMonitors(VirtualDesktopNotificationObject*, void*, void*, void*) {
    HandleVirtualDesktopChangedNotification(); return S_OK;
}

VirtualDesktopNotificationObject* CreateVirtualDesktopNotificationObject() {
    auto config = GetNotificationInterfaceConfig();
    if (config.methodCount == 0 || config.currentChangedIndex < 0 || config.currentChangedIndex >= config.methodCount) return nullptr;
    auto object = new (std::nothrow) VirtualDesktopNotificationObject();
    if (!object) return nullptr;
    object->vtable = new (std::nothrow) void*[config.methodCount];
    if (!object->vtable) { delete object; return nullptr; }
    for (int i = 0; i < config.methodCount; ++i) {
        object->vtable[i] = reinterpret_cast<void*>(&VirtualDesktopNotification_NoOp);
    }
    object->vtable[0] = reinterpret_cast<void*>(&VirtualDesktopNotification_QueryInterface);
    object->vtable[1] = reinterpret_cast<void*>(&VirtualDesktopNotification_AddRef);
    object->vtable[2] = reinterpret_cast<void*>(&VirtualDesktopNotification_Release);
    if (config.currentChangedHasMonitors) {
        object->vtable[config.currentChangedIndex] = reinterpret_cast<void*>(&VirtualDesktopNotification_CurrentChangedWithMonitors);
    } else {
        object->vtable[config.currentChangedIndex] = reinterpret_cast<void*>(&VirtualDesktopNotification_CurrentChanged);
    }
    return object;
}

// -- Registry Lookups --

std::vector<BYTE> ReadRegistryValue(HKEY root, const std::wstring& subKey, const std::wstring& valueName, DWORD* valueType) {
    DWORD type = 0, size = 0;
    if (RegGetValueW(root, subKey.c_str(), valueName.c_str(), RRF_RT_ANY, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) return {};
    std::vector<BYTE> buffer(size);
    if (RegGetValueW(root, subKey.c_str(), valueName.c_str(), RRF_RT_ANY, &type, buffer.data(), &size) != ERROR_SUCCESS) return {};
    buffer.resize(size);
    if (valueType) *valueType = type;
    return buffer;
}

bool ParseGuidValue(const std::vector<BYTE>& buffer, DWORD type, GUID* guid) {
    if (!guid || buffer.empty()) return false;
    if (type == REG_BINARY && buffer.size() >= sizeof(GUID)) {
        memcpy(guid, buffer.data(), sizeof(GUID)); return true;
    }
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        const wchar_t* text = reinterpret_cast<const wchar_t*>(buffer.data());
        if (!text || !*text) return false;
        return SUCCEEDED(CLSIDFromString(text, guid));
    }
    return false;
}

std::vector<GUID> ReadVirtualDesktopIds() {
    std::vector<GUID> ids;
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    std::wstring sessionPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" + std::to_wstring(sessionId) + L"\\VirtualDesktops";
    for (const auto& path : { std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops"), sessionPath }) {
        DWORD type = 0;
        auto buffer = ReadRegistryValue(HKEY_CURRENT_USER, path, L"VirtualDesktopIDs", &type);
        if (type != REG_BINARY || buffer.size() < sizeof(GUID)) continue;
        size_t count = buffer.size() / sizeof(GUID);
        ids.resize(count);
        memcpy(ids.data(), buffer.data(), count * sizeof(GUID));
        return ids;
    }
    return ids;
}

bool ReadCurrentDesktopGuid(GUID* guid) {
    if (!guid) return false;
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    std::wstring sessionPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" + std::to_wstring(sessionId) + L"\\VirtualDesktops";
    for (const auto& path : { sessionPath, std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops") }) {
        DWORD type = 0;
        auto buffer = ReadRegistryValue(HKEY_CURRENT_USER, path, L"CurrentVirtualDesktop", &type);
        if (ParseGuidValue(buffer, type, guid)) return true;
    }
    return false;
}

int ReadCurrentDesktopNumberFromRegistry() {
    auto desktopIds = ReadVirtualDesktopIds();
    if (desktopIds.empty()) return 1;
    GUID currentGuid{};
    if (!ReadCurrentDesktopGuid(&currentGuid)) return 1;
    for (size_t i = 0; i < desktopIds.size(); ++i) {
        if (InlineIsEqualGUID(desktopIds[i], currentGuid)) return static_cast<int>(i + 1);
    }
    return 1;
}

int ReadDesktopCountFromRegistry() {
    auto desktopIds = ReadVirtualDesktopIds();
    return desktopIds.empty() ? 1 : static_cast<int>(desktopIds.size());
}

std::wstring ReadRegistryStringValue(HKEY root, const std::wstring& subKey, const std::wstring& valueName) {
    DWORD type = 0;
    auto buffer = ReadRegistryValue(root, subKey, valueName, &type);
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || buffer.size() < sizeof(wchar_t)) return {};
    std::wstring value(reinterpret_cast<const wchar_t*>(buffer.data()), buffer.size() / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

std::wstring GuidToRegistryString(const GUID& guid) {
    wchar_t guidText[39] = {};
    if (StringFromGUID2(guid, guidText, ARRAYSIZE(guidText)) == 0) return {};
    return guidText;
}

std::wstring ReadVirtualDesktopName(const GUID& desktopId) {
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    std::wstring desktopIdText = GuidToRegistryString(desktopId);
    if (desktopIdText.empty()) return {};
    std::wstring sessionPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" + std::to_wstring(sessionId) + L"\\VirtualDesktops\\Desktops\\" + desktopIdText;
    for (const auto& path : { std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops\\Desktops\\") + desktopIdText, sessionPath }) {
        std::wstring name = ReadRegistryStringValue(HKEY_CURRENT_USER, path, L"Name");
        if (!name.empty()) return name;
    }
    return {};
}

std::vector<std::wstring> ReadVirtualDesktopNames() {
    std::vector<std::wstring> names;
    auto desktopIds = ReadVirtualDesktopIds();
    names.reserve(desktopIds.size());
    for (const auto& desktopId : desktopIds) names.push_back(ReadVirtualDesktopName(desktopId));
    return names;
}

// -- Mod Setup & Hooks --

void LoadSettings() {
    StringSetting indicatorMode = StringSetting::make(L"indicatorMode");
    if (wcscmp(indicatorMode.get(), L"arabic") == 0) g_settings.indicatorMode = IndicatorMode::Arabic;
    else if (wcscmp(indicatorMode.get(), L"roman") == 0) g_settings.indicatorMode = IndicatorMode::Roman;
    else if (wcscmp(indicatorMode.get(), L"desktopName") == 0) g_settings.indicatorMode = IndicatorMode::DesktopName;
    else g_settings.indicatorMode = IndicatorMode::Markers;

    StringSetting fontWeightStr = StringSetting::make(L"fontWeight");
    if (wcscmp(fontWeightStr.get(), L"bold") == 0) g_settings.fontWeight = FontWeightMode::Bold;
    else g_settings.fontWeight = FontWeightMode::Normal;

    StringSetting rotationStr = StringSetting::make(L"rotation");
    if (wcscmp(rotationStr.get(), L"90") == 0) g_settings.rotation = RotationMode::Deg90;
    else if (wcscmp(rotationStr.get(), L"270") == 0) g_settings.rotation = RotationMode::DegMinus90;
    else if (wcscmp(rotationStr.get(), L"180") == 0) g_settings.rotation = RotationMode::Deg180;
    else g_settings.rotation = RotationMode::Deg0;

    g_settings.hideWhenSingleDesktop = Wh_GetIntSetting(L"hideWhenSingleDesktop");
    g_settings.inactiveMarkerOpacity = std::clamp(Wh_GetIntSetting(L"inactiveMarkerOpacity"), 0, 100);
    g_settings.activeColor = StringSetting::make(L"activeColor").get();
    g_settings.inactiveColor = StringSetting::make(L"inactiveColor").get();
    g_settings.charSpacing = std::max(0, Wh_GetIntSetting(L"charSpacing"));
    
    // Direct font size reading
    g_settings.fontSize = (double)Wh_GetIntSetting(L"fontSize");
    
    // Configurable font family for name and number modes
    g_settings.fontFamily = StringSetting::make(L"fontFamily").get();
    
    g_settings.compatibilityMode = Wh_GetIntSetting(L"compatibilityMode");
    g_settings.marginStr = StringSetting::make(L"marginStr").get();
    g_settings.pollIntervalMs = std::clamp(Wh_GetIntSetting(L"pollIntervalMs"), 0, 2000);
    
    StringSetting activeMarker = StringSetting::make(L"activeMarker");
    g_settings.activeMarker = activeMarker.get();
    if (g_settings.activeMarker.empty()) g_settings.activeMarker = L"\u2B24";

    StringSetting inactiveMarker = StringSetting::make(L"inactiveMarker");
    g_settings.inactiveMarker = inactiveMarker.get();
    if (g_settings.inactiveMarker.empty()) g_settings.inactiveMarker = L"\u25EF";
    
    g_pollIntervalMs.store(g_settings.pollIntervalMs);
}

VS_FIXEDFILEINFO* GetModuleVersionInfo(HMODULE module, UINT* length) {
    void* fixedFileInfo = nullptr;
    UINT fixedFileInfoLength = 0;
    HRSRC versionResource = FindResourceW(module, MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION);
    if (versionResource) {
        HGLOBAL loadedResource = LoadResource(module, versionResource);
        if (loadedResource) {
            void* lockedResource = LockResource(loadedResource);
            if (lockedResource && (!VerQueryValueW(lockedResource, L"\\", &fixedFileInfo, &fixedFileInfoLength) || fixedFileInfoLength == 0)) {
                fixedFileInfo = nullptr;
                fixedFileInfoLength = 0;
            }
        }
    }
    if (length) *length = fixedFileInfoLength;
    return static_cast<VS_FIXEDFILEINFO*>(fixedFileInfo);
}

WinVersion GetExplorerVersion() {
    VS_FIXEDFILEINFO* ffi = GetModuleVersionInfo(nullptr, nullptr);
    if (!ffi) return WinVersion::Unsupported;
    WORD major = HIWORD(ffi->dwFileVersionMS);
    WORD build = HIWORD(ffi->dwFileVersionLS);
    WORD revision = LOWORD(ffi->dwFileVersionLS);
    g_explorerBuildNumber = build;
    g_explorerRevisionNumber = revision;

    if (major != 10) return WinVersion::Unsupported;
    if (build < 22000) return WinVersion::Win10;
    if (build == 22000) return WinVersion::Win11;
    if (build < 26100) return WinVersion::Win11_22H2;
    return WinVersion::Win11_24H2;
}

bool RegisterVirtualDesktopNotificationsOnCurrentThread() {
    if (g_virtualDesktopNotificationsRegistered) return true;
    auto config = GetNotificationInterfaceConfig();
    if (config.methodCount == 0) return false;

    IServiceProvider* serviceProvider = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&serviceProvider)))) return false;

    IVirtualDesktopNotificationService* notificationService = nullptr;
    HRESULT hr = serviceProvider->QueryService(SID_VirtualDesktopNotificationService, IID_IVirtualDesktopNotificationService, reinterpret_cast<void**>(&notificationService));
    serviceProvider->Release();
    if (FAILED(hr)) return false;

    auto notificationObject = CreateVirtualDesktopNotificationObject();
    if (!notificationObject) {
        notificationService->Release();
        return false;
    }

    DWORD cookie = 0;
    hr = notificationService->Register(reinterpret_cast<IUnknown*>(notificationObject), &cookie);
    notificationService->Release();

    if (FAILED(hr) || cookie == 0) {
        VirtualDesktopNotification_Release(notificationObject);
        return false;
    }

    g_virtualDesktopNotificationObject = notificationObject;
    g_virtualDesktopNotificationCookie = cookie;
    g_virtualDesktopNotificationsRegistered = true;
    return true;
}

void UnregisterVirtualDesktopNotificationsOnCurrentThread() {
    if (!g_virtualDesktopNotificationsRegistered) return;
    IServiceProvider* serviceProvider = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ImmersiveShell, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&serviceProvider)))) {
        IVirtualDesktopNotificationService* notificationService = nullptr;
        if (SUCCEEDED(serviceProvider->QueryService(SID_VirtualDesktopNotificationService, IID_IVirtualDesktopNotificationService, reinterpret_cast<void**>(&notificationService)))) {
            notificationService->Unregister(g_virtualDesktopNotificationCookie);
            notificationService->Release();
        }
        serviceProvider->Release();
    }
    if (g_virtualDesktopNotificationObject) {
        VirtualDesktopNotification_Release(g_virtualDesktopNotificationObject);
        g_virtualDesktopNotificationObject = nullptr;
    }
    g_virtualDesktopNotificationCookie = 0;
    g_virtualDesktopNotificationsRegistered = false;
}

DWORD WINAPI VirtualDesktopNotificationThreadProc(LPVOID) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        if (g_notificationReadyEvent) SetEvent(g_notificationReadyEvent);
        return 0;
    }
    RegisterVirtualDesktopNotificationsOnCurrentThread();
    if (g_notificationReadyEvent) SetEvent(g_notificationReadyEvent);
    if (g_notificationStopEvent) {
        bool stopping = false;
        while (!stopping) {
            if (!g_virtualDesktopNotificationsRegistered) RegisterVirtualDesktopNotificationsOnCurrentThread();
            DWORD waitTimeout = g_virtualDesktopNotificationsRegistered ? INFINITE : 1000;
            switch (MsgWaitForMultipleObjects(1, &g_notificationStopEvent, FALSE, waitTimeout, QS_ALLINPUT)) {
                case WAIT_OBJECT_0: stopping = true; break;
                case WAIT_OBJECT_0 + 1: { MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } break; }
                case WAIT_TIMEOUT: break;
                default: stopping = true; break;
            }
        }
    }
    if (g_virtualDesktopNotificationsRegistered) UnregisterVirtualDesktopNotificationsOnCurrentThread();
    CoUninitialize();
    return 0;
}

bool EnsureVirtualDesktopNotificationThread() {
    if (g_notificationThread && WaitForSingleObject(g_notificationThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_notificationThread); g_notificationThread = nullptr;
        if (g_notificationReadyEvent) { CloseHandle(g_notificationReadyEvent); g_notificationReadyEvent = nullptr; }
        if (g_notificationStopEvent) { CloseHandle(g_notificationStopEvent); g_notificationStopEvent = nullptr; }
    }
    if (g_notificationThread) return g_virtualDesktopNotificationsRegistered.load();

    g_notificationStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_notificationStopEvent) return false;

    g_notificationReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_notificationReadyEvent) {
        CloseHandle(g_notificationStopEvent); g_notificationStopEvent = nullptr;
        return false;
    }

    g_notificationThread = CreateThread(nullptr, 0, VirtualDesktopNotificationThreadProc, nullptr, 0, nullptr);
    if (!g_notificationThread) {
        CloseHandle(g_notificationReadyEvent); g_notificationReadyEvent = nullptr;
        CloseHandle(g_notificationStopEvent); g_notificationStopEvent = nullptr;
        return false;
    }
    WaitForSingleObject(g_notificationReadyEvent, 3000);
    return g_virtualDesktopNotificationsRegistered.load();
}

void StopVirtualDesktopNotificationThread() {
    if (g_notificationStopEvent) SetEvent(g_notificationStopEvent);
    if (g_notificationThread) {
        WaitForSingleObject(g_notificationThread, 3000);
        CloseHandle(g_notificationThread);
        g_notificationThread = nullptr;
    }
    if (g_notificationReadyEvent) { CloseHandle(g_notificationReadyEvent); g_notificationReadyEvent = nullptr; }
    if (g_notificationStopEvent) { CloseHandle(g_notificationStopEvent); g_notificationStopEvent = nullptr; }
}

FrameworkElement FindDescendantByName(const DependencyObject& parent, const winrt::hstring& name) {
    auto fe = parent.try_as<FrameworkElement>();
    if (fe && fe.Name() == name) return fe;
    int count = Media::VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < count; ++i) {
        auto child = Media::VisualTreeHelper::GetChild(parent, i);
        auto result = FindDescendantByName(child, name);
        if (result) return result;
    }
    return nullptr;
}

bool TryGetClockTextBlocks(FrameworkElement root, Controls::TextBlock* dateTextBlock, Controls::TextBlock* timeTextBlock) {
    if (!dateTextBlock || !timeTextBlock) return false;
    *dateTextBlock = FindDescendantByName(root, L"DateInnerTextBlock").try_as<Controls::TextBlock>();
    *timeTextBlock = FindDescendantByName(root, L"TimeInnerTextBlock").try_as<Controls::TextBlock>();
    return static_cast<bool>(*dateTextBlock || *timeTextBlock);
}

Controls::Grid TryGetClockContainerGrid(FrameworkElement root) {
    return FindDescendantByName(root, L"ContainerGrid").try_as<Controls::Grid>();
}

Controls::StackPanel TryGetClockStackPanel(Controls::Grid containerGrid) {
    if (!containerGrid) return nullptr;
    for (const auto& child : containerGrid.Children()) {
        auto stackPanel = child.try_as<Controls::StackPanel>();
        if (stackPanel) return stackPanel;
    }
    return nullptr;
}

ClockEntryPtr AddOrGetClockEntry(Controls::TextBlock dateTextBlock, Controls::TextBlock timeTextBlock) {
    auto sameBlock = [&](const ClockEntryPtr& entry) {
        return (entry->dateTextBlock.get() && dateTextBlock && entry->dateTextBlock.get() == dateTextBlock) ||
               (entry->timeTextBlock.get() && timeTextBlock && entry->timeTextBlock.get() == timeTextBlock);
    };

    std::lock_guard<std::mutex> g_lock(g_clockEntriesMutex);
    g_clockEntries.erase(
        std::remove_if(g_clockEntries.begin(), g_clockEntries.end(), [](const ClockEntryPtr& entry) {
            return !entry->dateTextBlock.get() && !entry->timeTextBlock.get();
        }), g_clockEntries.end());

    auto it = std::find_if(g_clockEntries.begin(), g_clockEntries.end(), sameBlock);
    if (it != g_clockEntries.end()) {
        (*it)->dateTextBlock = dateTextBlock;
        (*it)->timeTextBlock = timeTextBlock;
        return *it;
    }

    auto entry = std::make_shared<ClockEntry>();
    entry->dateTextBlock = dateTextBlock;
    entry->timeTextBlock = timeTextBlock;
    g_clockEntries.push_back(entry);
    return entry;
}

std::vector<ClockEntryPtr> GetClockEntriesSnapshot() {
    std::lock_guard<std::mutex> g_lock(g_clockEntriesMutex);
    return g_clockEntries;
}

double MeasureSingleRunTextWidth(const Controls::TextBlock& source, const std::wstring& text) {
    Controls::TextBlock measureBlock;
    measureBlock.FontFamily(source.FontFamily());
    measureBlock.FontSize(source.FontSize());
    measureBlock.FontStretch(source.FontStretch());
    measureBlock.FontStyle(source.FontStyle());
    measureBlock.FontWeight(source.FontWeight());
    measureBlock.CharacterSpacing(source.CharacterSpacing());
    measureBlock.TextWrapping(TextWrapping::NoWrap);
    
    auto inlines = measureBlock.Inlines();
    inlines.Clear();
    Documents::Run run;
    run.Text(winrt::hstring(text));
    inlines.Append(run);

    measureBlock.Measure(winrt::Windows::Foundation::Size{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
    return measureBlock.DesiredSize().Width;
}

std::vector<IndicatorSegment> BuildMarkerSuffixSegments(int currentDesktopNumber, int desktopCount) {
    std::vector<IndicatorSegment> segments;
    std::wstring gap = BuildPadding(std::max(g_settings.charSpacing, 0));
    desktopCount = std::max(desktopCount, 1);
    currentDesktopNumber = std::clamp(currentDesktopNumber, 1, desktopCount);

    for (int i = 1; i <= desktopCount; ++i) {
        if (i > 1 && !gap.empty()) segments.push_back({gap, IndicatorSegmentStyle::Normal});
        segments.push_back({
            i == currentDesktopNumber ? g_settings.activeMarker : g_settings.inactiveMarker,
            i == currentDesktopNumber ? IndicatorSegmentStyle::Normal : IndicatorSegmentStyle::Dimmed
        });
    }
    return segments;
}

IndicatorLayout BuildIndicatorLayout(const Controls::TextBlock& indicatorTb, int currentDesktopNumber, int desktopCount, const std::vector<std::wstring>& desktopNames) {
    IndicatorLayout layout;

    if (g_settings.indicatorMode == IndicatorMode::Markers) {
        std::wstring testActive = BuildMarkerSequenceText(desktopCount, g_settings.activeMarker);
        std::wstring testInactive = BuildMarkerSequenceText(desktopCount, g_settings.inactiveMarker);
        layout.widestSuffixWidth = std::max(MeasureSingleRunTextWidth(indicatorTb, testActive), MeasureSingleRunTextWidth(indicatorTb, testInactive));
        layout.suffixSegments = BuildMarkerSuffixSegments(currentDesktopNumber, desktopCount);
    } else {
        auto getCandidateText = [&](int desktopNumber) {
            return (g_settings.indicatorMode == IndicatorMode::DesktopName) 
                ? FormatDesktopNameOrNumber(desktopNumber, desktopNames, desktopCount) 
                : FormatDesktopNumber(desktopNumber, desktopCount);
        };
        std::wstring visibleText = getCandidateText(currentDesktopNumber);
        double currentWidth = MeasureSingleRunTextWidth(indicatorTb, visibleText);
        layout.widestSuffixWidth = currentWidth;
        for (int i = 1; i <= desktopCount; ++i) {
            double candidateWidth = MeasureSingleRunTextWidth(indicatorTb, getCandidateText(i));
            if (candidateWidth > layout.widestSuffixWidth) layout.widestSuffixWidth = candidateWidth;
        }

        struct SpacerCandidate { wchar_t ch; double width; };
        const wchar_t spacerChars[] = { L'\u00A0', L'\u2007', L'\u2005', L'\u2009', L'\u200A' };
        auto buildInvisibleFillerText = [&](double targetWidth) {
            if (targetWidth <= 0.1) return std::wstring{};
            std::vector<SpacerCandidate> candidates;
            for (wchar_t ch : spacerChars) {
                double width = MeasureSingleRunTextWidth(indicatorTb, std::wstring(1, ch));
                if (width > 0.01) candidates.push_back({ch, width});
            }
            std::sort(candidates.begin(), candidates.end(), [](const SpacerCandidate& a, const SpacerCandidate& b) { return a.width > b.width; });
            std::wstring result;
            double usedWidth = 0;
            for (const auto& candidate : candidates) {
                while (usedWidth + candidate.width <= targetWidth + 0.01) {
                    result += candidate.ch;
                    usedWidth += candidate.width;
                }
            }
            return result;
        };

        double fillerWidth = std::max(0.0, layout.widestSuffixWidth - currentWidth);
        std::wstring leftFiller = buildInvisibleFillerText(fillerWidth / 2.0);
        double leftFillerWidth = MeasureSingleRunTextWidth(indicatorTb, leftFiller);
        std::wstring rightFiller = buildInvisibleFillerText(std::max(0.0, fillerWidth - leftFillerWidth));

        layout.suffixSegments.push_back({leftFiller, IndicatorSegmentStyle::Transparent});
        layout.suffixSegments.push_back({visibleText, IndicatorSegmentStyle::Normal});
        layout.suffixSegments.push_back({rightFiller, IndicatorSegmentStyle::Transparent});
    }
    return layout;
}

Media::Brush GetSystemBrush(const wchar_t* resourceKey) {
    try {
        auto app = Application::Current();
        if (app) {
            auto resKey = winrt::box_value(resourceKey);
            if (app.Resources().HasKey(resKey)) {
                return app.Resources().Lookup(resKey).try_as<Media::Brush>();
            }
        }
    } catch (...) {}
    return nullptr;
}

Media::Brush CreateIndicatorBrush(const std::wstring& colorStr, const wchar_t* defaultSysRes, int opacityPct, Controls::TextBlock sourceTb) {
    Media::Brush brush = nullptr;
    winrt::Windows::UI::Color color{};
    bool parsed = false;

    // Apply color logic mappings
    if (colorStr == L"1") {
        brush = GetSystemBrush(L"TextFillColorPrimaryBrush");
    } else if (colorStr == L"2") {
        brush = GetSystemBrush(L"TextFillColorSecondaryBrush");
    } else if (colorStr == L"-1") {
        brush = GetSystemBrush(L"AccentTextFillColorPrimaryBrush"); 
        if (!brush) brush = GetSystemBrush(L"SystemControlForegroundAccentBrush");
    } else if (!colorStr.empty()) {
        parsed = TryParseColorString(colorStr, &color);
    }

    if (brush) {
        auto solidBrush = brush.try_as<Media::SolidColorBrush>();
        if (solidBrush) {
            color = solidBrush.Color();
            parsed = true;
        } else {
            return brush; 
        }
    }

    if (!parsed) {
        if (!defaultSysRes && opacityPct == 100) return nullptr;
        auto sysBrush = defaultSysRes ? GetSystemBrush(defaultSysRes).try_as<Media::SolidColorBrush>() : nullptr;
        if (sysBrush) color = sysBrush.Color();
        else {
            auto solidBrush = sourceTb.Foreground().try_as<Media::SolidColorBrush>();
            if (solidBrush) color = solidBrush.Color();
        }
    }

    if (opacityPct < 100) {
        color.A = static_cast<BYTE>((color.A * std::clamp(opacityPct, 0, 100)) / 100);
    }
    return Media::SolidColorBrush(color);
}

void SetIndicatorTextBlockContent(Controls::TextBlock indicatorTb, Controls::TextBlock sourceTb, const IndicatorLayout& layout) {
    auto inlines = indicatorTb.Inlines();
    inlines.Clear();

    Media::Brush transparentBrush = Media::SolidColorBrush(winrt::Windows::UI::Color{0, 255, 255, 255});
    Media::Brush activeBrush = nullptr;
    Media::Brush dimmedBrush = nullptr;

    if (g_settings.compatibilityMode) {
        auto solidBrush = sourceTb.Foreground().try_as<Media::SolidColorBrush>();
        if (solidBrush) {
            winrt::Windows::UI::Color baseColor = solidBrush.Color();
            baseColor.A = static_cast<BYTE>((baseColor.A * std::clamp(g_settings.inactiveMarkerOpacity, 0, 100)) / 100);
            dimmedBrush = Media::SolidColorBrush(baseColor);
        }
    } else {
        activeBrush = CreateIndicatorBrush(g_settings.activeColor, nullptr, 100, sourceTb);
        dimmedBrush = CreateIndicatorBrush(g_settings.inactiveColor, L"TextFillColorSecondaryBrush", g_settings.inactiveMarkerOpacity, sourceTb);
    }

    for (const auto& segment : layout.suffixSegments) {
        if (segment.text.empty()) continue;
        Documents::Run run;
        run.Text(winrt::hstring(segment.text));
        
        if (segment.style == IndicatorSegmentStyle::Transparent) {
            run.Foreground(transparentBrush);
        } else if (segment.style == IndicatorSegmentStyle::Dimmed) {
            if (dimmedBrush) run.Foreground(dimmedBrush);
        } else if (activeBrush) {
            run.Foreground(activeBrush);
        }
        
        inlines.Append(run);
    }
}

void RestoreSeparateIndicatorOnly(const ClockEntryPtr& entry) {
    auto containerGrid = entry->containerGrid.get();
    auto indicatorTextBlock = entry->indicatorTextBlock.get();

    if (containerGrid && indicatorTextBlock) {
        auto children = containerGrid.Children();
        for (uint32_t i = 0; i < children.Size(); ++i) {
            if (children.GetAt(i) == indicatorTextBlock) {
                children.RemoveAt(i);
                break;
            }
        }
    }

    auto stackPanel = entry->stackPanel.get();
    if (stackPanel && entry->originalStackPanelColumnCaptured) {
        Controls::Grid::SetColumn(stackPanel, entry->originalStackPanelColumn);
    }

    if (containerGrid && entry->originalContainerGridColumnsCaptured) {
        auto columnDefinitions = containerGrid.ColumnDefinitions();
        columnDefinitions.Clear();
        for (const auto& width : entry->originalContainerGridColumnWidths) {
            Controls::ColumnDefinition cd; cd.Width(width);
            columnDefinitions.Append(cd);
        }
    }

    entry->indicatorTextBlock = nullptr;
    entry->usingSeparateIndicator = false;
}

Controls::TextBlock EnsureSeparateIndicatorTextBlock(const ClockEntryPtr& entry, Controls::TextBlock sourceTextBlock) {
    auto containerGrid = entry->containerGrid.get();
    if (!containerGrid || !sourceTextBlock) return nullptr;

    auto stackPanel = entry->stackPanel.get();
    if (!stackPanel) {
        stackPanel = TryGetClockStackPanel(containerGrid);
        entry->stackPanel = stackPanel;
    }
    if (!stackPanel) return nullptr;

    if (!entry->originalContainerGridColumnsCaptured) {
        auto columnDefinitions = containerGrid.ColumnDefinitions();
        entry->originalContainerGridColumnWidths.clear();
        for (const auto& cd : columnDefinitions) entry->originalContainerGridColumnWidths.push_back(cd.Width());
        entry->originalContainerGridColumnsCaptured = true;
    }

    if (!entry->originalStackPanelColumnCaptured) {
        entry->originalStackPanelColumn = Controls::Grid::GetColumn(stackPanel);
        entry->originalStackPanelColumnCaptured = true;
    }

    auto columnDefinitions = containerGrid.ColumnDefinitions();
    if (columnDefinitions.Size() < 2) {
        columnDefinitions.Clear();
        Controls::ColumnDefinition mainCol; mainCol.Width(GridLengthHelper::Auto());
        columnDefinitions.Append(mainCol);
        Controls::ColumnDefinition indCol; indCol.Width(GridLengthHelper::Auto());
        columnDefinitions.Append(indCol);
    }

    Controls::Grid::SetColumn(stackPanel, 0);

    auto indicatorTextBlock = entry->indicatorTextBlock.get();
    if (!indicatorTextBlock) {
        for (const auto& child : containerGrid.Children()) {
            auto childTextBlock = child.try_as<Controls::TextBlock>();
            if (childTextBlock && childTextBlock.Name() == L"DesktopIndicatorTextBlock") {
                indicatorTextBlock = childTextBlock;
                break;
            }
        }
    }

    if (!indicatorTextBlock) {
        indicatorTextBlock = Controls::TextBlock();
        indicatorTextBlock.Name(L"DesktopIndicatorTextBlock");
        containerGrid.Children().Append(indicatorTextBlock);
    }

    Controls::Grid::SetColumn(indicatorTextBlock, 1);
    entry->indicatorTextBlock = indicatorTextBlock;
    entry->usingSeparateIndicator = true;
    return indicatorTextBlock;
}

void ApplyIndicator(const ClockEntryPtr& entry) {
    if (!entry || g_unloading) return;

    auto dateTb = entry->dateTextBlock.get();
    auto timeTb = entry->timeTextBlock.get();
    auto sourceTb = dateTb ? dateTb : timeTb;
    if (!sourceTb) return;

    int currentDesktopNumber = g_currentDesktopNumber.load();
    int desktopCount = ReadDesktopCountFromRegistry();
    std::vector<std::wstring> desktopNames = (g_settings.indicatorMode == IndicatorMode::DesktopName) ? ReadVirtualDesktopNames() : std::vector<std::wstring>{};

    std::lock_guard<std::mutex> g_lock(entry->mutex);

    if (g_settings.hideWhenSingleDesktop && desktopCount <= 1) {
        RestoreSeparateIndicatorOnly(entry);
        return;
    }

    auto indicatorTb = EnsureSeparateIndicatorTextBlock(entry, sourceTb);
    if (!indicatorTb) return;

    // Apply Fonts and Styling based on Compatibility Mode
    if (g_settings.compatibilityMode) {
        indicatorTb.FontFamily(sourceTb.FontFamily());
        indicatorTb.FontSize(sourceTb.FontSize());
        indicatorTb.FontWeight(sourceTb.FontWeight());
        indicatorTb.Margin(ThicknessHelper::FromLengths(0, 0, 0, 0));
        indicatorTb.RenderTransform(nullptr);
    } else {
        indicatorTb.FontSize(std::max(1.0, g_settings.fontSize));
        indicatorTb.FontWeight({ g_settings.fontWeight == FontWeightMode::Bold ? (uint16_t)700 : (uint16_t)400 });
        
        if (g_settings.indicatorMode == IndicatorMode::Markers) {
            indicatorTb.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
        } else {
            if (!g_settings.fontFamily.empty()) {
                indicatorTb.FontFamily(Media::FontFamily(g_settings.fontFamily));
            } else {
                indicatorTb.FontFamily(sourceTb.FontFamily());
            }
        }

        // Apply Rotation
        int rotAngle = 0;
        if (g_settings.rotation == RotationMode::Deg90) rotAngle = 90;
        else if (g_settings.rotation == RotationMode::DegMinus90) rotAngle = -90;
        else if (g_settings.rotation == RotationMode::Deg180) rotAngle = 180;

        if (g_settings.indicatorMode == IndicatorMode::Markers && rotAngle != 0) {
            Media::RotateTransform rot;
            rot.Angle(rotAngle);
            indicatorTb.RenderTransform(rot);
            indicatorTb.RenderTransformOrigin({0.5f, 0.5f});
        } else {
            indicatorTb.RenderTransform(nullptr);
        }

        // Apply Margin
        double l = 0, t = 0, r = 0, b = 0;
        swscanf_s(g_settings.marginStr.c_str(), L"%lf, %lf, %lf, %lf", &l, &t, &r, &b);
        indicatorTb.Margin(ThicknessHelper::FromLengths(l, t, r, b));
    }

    // Default Foreground bind (runs in segment overwrites will take over where active)
    indicatorTb.Foreground(sourceTb.Foreground());
    indicatorTb.TextWrapping(TextWrapping::NoWrap);
    indicatorTb.TextAlignment(TextAlignment::Start);
    indicatorTb.VerticalAlignment(VerticalAlignment::Center);
    indicatorTb.HorizontalAlignment(HorizontalAlignment::Left);

    IndicatorLayout layout = BuildIndicatorLayout(indicatorTb, currentDesktopNumber, desktopCount, desktopNames);
    indicatorTb.MinWidth(layout.widestSuffixWidth);
    SetIndicatorTextBlockContent(indicatorTb, sourceTb, layout);
}

void RestoreClockText(const ClockEntryPtr& entry) {
    if (!entry) return;
    std::lock_guard<std::mutex> g_lock(entry->mutex);
    RestoreSeparateIndicatorOnly(entry);
}

void DispatchToEntry(const ClockEntryPtr& entry) {
    auto source = entry->dateTextBlock.get() ? entry->dateTextBlock.get() : entry->timeTextBlock.get();
    if (!source || !source.Dispatcher()) return;
    source.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Low, [entry] {
        if (!g_unloading) ApplyIndicator(entry);
    });
}

void UpdateAllClockEntries() {
    for (const auto& entry : GetClockEntriesSnapshot()) DispatchToEntry(entry);
}

void UpdateAllClockEntriesCurrentThread() {
    for (const auto& entry : GetClockEntriesSnapshot()) ApplyIndicator(entry);
}

void DispatchRestoreToEntry(const ClockEntryPtr& entry) {
    auto source = entry->dateTextBlock.get() ? entry->dateTextBlock.get() : entry->timeTextBlock.get();
    if (!source || !source.Dispatcher()) return;
    source.Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal, [entry] {
        RestoreClockText(entry);
    });
}

void RestoreAllClockEntries() {
    for (const auto& entry : GetClockEntriesSnapshot()) DispatchRestoreToEntry(entry);
}

void ProcessDateTimeIconContentElement(FrameworkElement root) {
    Controls::TextBlock dateTb = nullptr, timeTb = nullptr;
    if (!TryGetClockTextBlocks(root, &dateTb, &timeTb)) return;
    auto entry = AddOrGetClockEntry(dateTb, timeTb);
    entry->containerGrid = TryGetClockContainerGrid(root);
    if (auto containerGrid = entry->containerGrid.get()) entry->stackPanel = TryGetClockStackPanel(containerGrid);
    ApplyIndicator(entry);
}

void WINAPI ClockSystemTrayIconDataModel_RefreshIcon_Hook(LPVOID pThis, LPVOID param1) {
    ClockSystemTrayIconDataModel_RefreshIcon_Original(pThis, param1);
    UpdateAllClockEntriesCurrentThread();
}

void WINAPI ClockSystemTrayIconDataModel2_RefreshIcon_Hook(LPVOID pThis, LPVOID param1) {
    ClockSystemTrayIconDataModel2_RefreshIcon_Original(pThis, param1);
    UpdateAllClockEntriesCurrentThread();
}

void WINAPI DateTimeIconContent_OnApplyTemplate_Hook(LPVOID pThis) {
    DateTimeIconContent_OnApplyTemplate_Original(pThis);
    IUnknown* elementUnknown = *((IUnknown**)pThis + 1);
    if (!elementUnknown) return;
    FrameworkElement root = nullptr;
    elementUnknown->QueryInterface(winrt::guid_of<FrameworkElement>(), winrt::put_abi(root));
    if (root) ProcessDateTimeIconContentElement(root);
}

HRESULT WINAPI BadgeIconContent_get_ViewModel_Hook(LPVOID pThis, LPVOID pArgs) {
    HRESULT hr = BadgeIconContent_get_ViewModel_Original(pThis, pArgs);
    try {
        winrt::Windows::Foundation::IInspectable inspectable = nullptr;
        winrt::check_hresult(((IUnknown*)pThis)->QueryInterface(winrt::guid_of<winrt::Windows::Foundation::IInspectable>(), winrt::put_abi(inspectable)));
        if (winrt::get_class_name(inspectable) == L"SystemTray.DateTimeIconContent") {
            auto root = inspectable.try_as<FrameworkElement>();
            if (root && root.IsLoaded()) ProcessDateTimeIconContentElement(root);
        }
    } catch (...) {}
    return hr;
}

HMODULE GetSystemTrayModuleHandle() {
    HMODULE module = GetModuleHandleW(L"SystemTray.dll");
    if (!module) {
        module = GetModuleHandleW(L"Taskbar.View.dll");
        if (module) {
            VS_FIXEDFILEINFO* ffi = GetModuleVersionInfo(module, nullptr);
            WORD moduleMajor = ffi ? HIWORD(ffi->dwFileVersionMS) : 0;
            if (!moduleMajor || moduleMajor >= 2604) module = nullptr;
        }
    }
    if (!module) module = GetModuleHandleW(L"ExplorerExtensions.dll");
    return module;
}

bool HookSystemTraySymbols(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(private: void __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel::RefreshIcon(class SystemTrayTelemetry::ClockUpdate &))"}, &ClockSystemTrayIconDataModel_RefreshIcon_Original, ClockSystemTrayIconDataModel_RefreshIcon_Hook},
        {{LR"(private: void __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel2::RefreshIcon(class SystemTrayTelemetry::ClockUpdate &))"}, &ClockSystemTrayIconDataModel2_RefreshIcon_Original, ClockSystemTrayIconDataModel2_RefreshIcon_Hook, true},
        {{LR"(public: void __cdecl winrt::SystemTray::implementation::DateTimeIconContent::OnApplyTemplate(void))"}, &DateTimeIconContent_OnApplyTemplate_Original, DateTimeIconContent_OnApplyTemplate_Hook, true},
        {{LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::SystemTray::implementation::BadgeIconContent,struct winrt::SystemTray::IBadgeIconContent>::get_ViewModel(void * *))"}, &BadgeIconContent_get_ViewModel_Original, BadgeIconContent_get_ViewModel_Hook, true},
    };
    return HookSymbols(module, hooks, ARRAYSIZE(hooks));
}

bool TryHookSystemTrayModule(HMODULE module) {
    if (!module || g_systemTrayModuleHooked.exchange(true)) return false;
    if (!HookSystemTraySymbols(module)) {
        g_systemTrayModuleHooked = false;
        return false;
    }
    return true;
}

bool HookExplorerExeSymbols() {
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(protected: virtual void __cdecl ClockButton::v_OnDisplayStateChange(bool))"}, &ClockButton_v_OnDisplayStateChange_Original, nullptr, true},
    };
    return HookSymbols(GetModuleHandle(nullptr), hooks, ARRAYSIZE(hooks));
}

HWND FindCurrentProcessTaskbarWnd() {
    HWND taskbarWnd = nullptr;
    EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
        DWORD processId = 0;
        WCHAR className[32];
        if (GetWindowThreadProcessId(hWnd, &processId) && processId == GetCurrentProcessId() && GetClassNameW(hWnd, className, ARRAYSIZE(className)) && _wcsicmp(className, L"Shell_TrayWnd") == 0) {
            *reinterpret_cast<HWND*>(lParam) = hWnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&taskbarWnd));
    return taskbarWnd;
}

void RefreshClockButtonWindow(HWND hWnd) {
    if (!hWnd || !ClockButton_v_OnDisplayStateChange_Original) return;
    HWND clockButtonWnd = FindWindowExW(hWnd, nullptr, L"ClockButton", nullptr);
    if (!clockButtonWnd) return;
    LONG_PTR ptr = GetWindowLongPtrW(clockButtonWnd, 0);
    if (ptr) ClockButton_v_OnDisplayStateChange_Original((LPVOID)ptr, true);
}

void TriggerWin11ClockUpdateWatcher() {
    HKEY hSubKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\TimeDate\\AdditionalClocks", 0, KEY_WRITE, &hSubKey) != ERROR_SUCCESS) return;
    RegSetValueExW(hSubKey, L"_temp_windhawk_taskbar-desktop-indicator", 0, REG_SZ, (const BYTE*)L"", sizeof(WCHAR));
    RegDeleteValueW(hSubKey, L"_temp_windhawk_taskbar-desktop-indicator");
    RegCloseKey(hSubKey);
}

void RefreshLiveTaskbarClock() {
    if (g_winVersion < WinVersion::Win11) return;
    HWND taskbarWnd = FindCurrentProcessTaskbarWnd();
    if (!taskbarWnd) return;
    TriggerWin11ClockUpdateWatcher();
    if (!ClockButton_v_OnDisplayStateChange_Original) return;
    RefreshClockButtonWindow(taskbarWnd);
    DWORD taskbarThreadId = GetWindowThreadProcessId(taskbarWnd, nullptr);
    if (!taskbarThreadId) return;

    auto enumWindowsProc = [](HWND hWnd) {
        WCHAR className[32];
        if (!GetClassNameW(hWnd, className, ARRAYSIZE(className)) || _wcsicmp(className, L"Shell_SecondaryTrayWnd") != 0) return;
        RefreshClockButtonWindow(hWnd);
    };
    EnumThreadWindows(taskbarThreadId, [](HWND hWnd, LPARAM lParam) -> BOOL {
        (*reinterpret_cast<decltype(enumWindowsProc)*>(lParam))(hWnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&enumWindowsProc));
}

void HandleLoadedModuleIfSystemTray(HMODULE module, LPCWSTR moduleName) {
    if (g_winVersion < WinVersion::Win11 || g_systemTrayModuleHooked) return;
    if (GetSystemTrayModuleHandle() != module) return;
    if (TryHookSystemTrayModule(module)) Wh_ApplyHookOperations();
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR libFileName, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(libFileName, file, flags);
    if (module) HandleLoadedModuleIfSystemTray(module, libFileName);
    return module;
}

DWORD WINAPI DesktopPollThreadProc(LPVOID) {
    int lastDesktopNumber = ReadCurrentDesktopNumberFromRegistry();
    g_currentDesktopNumber.store(lastDesktopNumber);
    while (WaitForSingleObject(g_stopEvent, g_pollIntervalMs.load()) == WAIT_TIMEOUT) {
        int desktopNumber = ReadCurrentDesktopNumberFromRegistry();
        if (desktopNumber != lastDesktopNumber) {
            lastDesktopNumber = desktopNumber;
            g_currentDesktopNumber.store(desktopNumber);
            UpdateAllClockEntries();
        }
    }
    return 0;
}

bool StartDesktopPollThread() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) return false;
    g_pollThread = CreateThread(nullptr, 0, DesktopPollThreadProc, nullptr, 0, nullptr);
    if (!g_pollThread) {
        CloseHandle(g_stopEvent); g_stopEvent = nullptr;
        return false;
    }
    return true;
}

void StopDesktopPollThread() {
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_pollThread) {
        WaitForSingleObject(g_pollThread, 3000);
        CloseHandle(g_pollThread); g_pollThread = nullptr;
    }
    if (g_stopEvent) { CloseHandle(g_stopEvent); g_stopEvent = nullptr; }
}

void EnsureDesktopChangeTracking() {
    EnsureVirtualDesktopNotificationThread(); 
    
    if (g_settings.pollIntervalMs <= 0) {
        if (g_pollThread || g_stopEvent) StopDesktopPollThread();
    } else if (!g_pollThread && !g_stopEvent) {
        StartDesktopPollThread();
    }
}

BOOL Wh_ModInit() {
    g_unloading = false;
    LoadSettings();
    g_winVersion = GetExplorerVersion();
    if (g_winVersion < WinVersion::Win11) return FALSE;

    g_currentDesktopNumber.store(ReadCurrentDesktopNumberFromRegistry());
    HookExplorerExeSymbols();

    if (HMODULE systemTrayModule = GetSystemTrayModuleHandle()) {
        TryHookSystemTrayModule(systemTrayModule);
    }

    HMODULE kernelBaseModule = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(kernelBaseModule, "LoadLibraryExW");
    if (!loadLibraryExW) return FALSE;

    WindhawkUtils::SetFunctionHook(loadLibraryExW, LoadLibraryExW_Hook, &LoadLibraryExW_Original);
    EnsureDesktopChangeTracking();

    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    EnsureDesktopChangeTracking();
    RefreshLiveTaskbarClock();
    UpdateAllClockEntries();
}

void Wh_ModAfterInit() {
    if (g_winVersion >= WinVersion::Win11 && !g_systemTrayModuleHooked) {
        if (HMODULE systemTrayModule = GetSystemTrayModuleHandle()) {
            if (TryHookSystemTrayModule(systemTrayModule)) Wh_ApplyHookOperations();
        }
    }
    EnsureDesktopChangeTracking();
    RefreshLiveTaskbarClock();
    UpdateAllClockEntries();
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    StopVirtualDesktopNotificationThread();
    StopDesktopPollThread();
    RestoreAllClockEntries();
    Sleep(200);
}

void Wh_ModUninit() {
    std::lock_guard<std::mutex> g_lock(g_clockEntriesMutex);
    g_clockEntries.clear();
}
