#include "shared/include/virtual_screen.hpp"
#include "windows/vendor/include/parsec-vdd.hpp"

#include <windows.h>
#include <cstdio>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

using namespace parsec_vdd;

// -----------------------------------------------------------------------------
// Assumes parsec-vdd-0.45.0.0.exe has already been run on the host, i.e. the
// "Parsec Virtual Display Adapter" driver (VDD_ADAPTER_GUID) is installed and
// enumerable. This file only talks to the already-installed driver.
//
// Resizing strategy:
// VDD does not accept an arbitrary resolution over IOCTL — it only ever
// offers whatever modes are listed in HKLM\SOFTWARE\Parsec\vdd (up to 5
// preset slots, keys "0".."4", each a REG_BINARY of {width,height,hz}).
// So to get an arbitrary WxH we:
//   1. Write the requested {width, height, 60} into preset slot 0.
//   2. Remove and re-add the virtual display so it re-reads the registry
//      and advertises the new mode.
//   3. Apply that mode via ChangeDisplaySettingsEx, matching on the
//      driver's known-good preset (now == our arbitrary size) instead of
//      hoping CDS accepts an out-of-list custom DEVMODE.
//
// Primary-display strategy:
// The VDD display is never primary by default, so anything that defaults to
// "the primary display" (capture APIs, some games/fullscreen apps) grabs the
// real physical monitor instead of the virtual one. SetDisplayAsPrimary()
// moves the VDD display to (0,0) and re-bases every other attached display
// by the same offset in a single atomic ChangeDisplaySettingsExW commit,
// since Windows requires the virtual desktop to stay contiguous. This is
// called once after creation, and again after every replug in
// ResizeVirtualScreen(), since removing/re-adding the display resets its
// primary status back to the physical monitor.
// -----------------------------------------------------------------------------

namespace {

constexpr wchar_t kVddRegPath[] = L"SOFTWARE\\Parsec\\vdd";

HANDLE            g_vdd        = nullptr;
int               g_displayIdx = -1;
std::atomic<bool> g_keepAlive  { false };
std::thread       g_pingThread;

void PingLoop() {
    while (g_keepAlive.load(std::memory_order_relaxed)) {
        VddUpdate(g_vdd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Write width/height/hz DWORD values into preset slot 0, i.e. the subkey
// HKLM\SOFTWARE\Parsec\vdd\0, each field its own named DWORD value. This
// matches Parsec's official "VDD Advanced Configuration" instructions:
// one subkey per slot (named "0".."4"), each containing three DWORD
// values named "width", "height", "hz".
bool WritePresetSlot0(std::uint32_t width, std::uint32_t height, std::uint32_t hz = 60) {
    const std::wstring slotPath = std::wstring(kVddRegPath) + L"\\0";

    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, slotPath.c_str(), 0, nullptr,
                               REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                               &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        std::fprintf(stderr, "virtual_screen: failed to open/create VDD preset key (err=%ld)\n", rc);
        return false;
    }

    DWORD w = width, h = height, r = hz;
    bool ok = true;
    ok &= RegSetValueExW(key, L"width",  0, REG_DWORD, reinterpret_cast<const BYTE*>(&w), sizeof(w)) == ERROR_SUCCESS;
    ok &= RegSetValueExW(key, L"height", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&h), sizeof(h)) == ERROR_SUCCESS;
    ok &= RegSetValueExW(key, L"hz",     0, REG_DWORD, reinterpret_cast<const BYTE*>(&r), sizeof(r)) == ERROR_SUCCESS;
    RegCloseKey(key);

    if (!ok) {
        std::fprintf(stderr, "virtual_screen: failed to write preset slot 0 values\n");
        return false;
    }
    return true;
}

bool FindVirtualDisplayDeviceName(wchar_t* outName, DWORD outLen) {
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (wcsstr(dd.DeviceString, L"ParsecVDA") != nullptr ||
            wcsstr(dd.DeviceString, L"Parsec Virtual Display") != nullptr) {
            wcsncpy_s(outName, outLen, dd.DeviceName, _TRUNCATE);
            return true;
        }
    }
    return false;
}

// Moves `targetDevice` to (0,0) and re-bases every other attached display by
// the same offset in one atomic commit, since Windows requires the combined
// virtual desktop formed by all displays to remain contiguous. This is the
// same operation the Display Settings UI performs when you tick
// "Make this my main display".
bool SetDisplayAsPrimary(const wchar_t* targetDevice) {
    DEVMODEW targetDm{};
    targetDm.dmSize = sizeof(targetDm);
    if (!EnumDisplaySettingsExW(targetDevice, ENUM_CURRENT_SETTINGS, &targetDm, 0)) {
        std::fprintf(stderr, "virtual_screen: EnumDisplaySettingsEx failed for target\n");
        return false;
    }

    if (targetDm.dmPosition.x == 0 && targetDm.dmPosition.y == 0) {
        // Already primary (or already at the origin) - nothing to do.
        return true;
    }

    int dx = -targetDm.dmPosition.x;
    int dy = -targetDm.dmPosition.y;

    targetDm.dmPosition = {0, 0};
    targetDm.dmFields = DM_POSITION;
    LONG rc = ChangeDisplaySettingsExW(targetDevice, &targetDm, nullptr,
                                        CDS_SET_PRIMARY | CDS_UPDATEREGISTRY | CDS_NORESET,
                                        nullptr);
    if (rc != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "virtual_screen: failed to set primary (err=%ld)\n", rc);
        return false;
    }

    // Re-base every other attached display by the inverse offset so the
    // combined desktop stays contiguous (Windows rejects the commit below
    // otherwise).
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        if (wcscmp(dd.DeviceName, targetDevice) == 0) continue;

        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) continue;

        dm.dmPosition.x += dx;
        dm.dmPosition.y += dy;
        dm.dmFields = DM_POSITION;
        ChangeDisplaySettingsExW(dd.DeviceName, &dm, nullptr,
                                  CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    }

    // Commit every pending NORESET change from above atomically.
    rc = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    if (rc != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "virtual_screen: failed to commit primary-display change (err=%ld)\n", rc);
        return false;
    }

    return true;
}

bool ApplyMode(std::uint32_t width, std::uint32_t height, std::uint32_t hz = 60) {
    wchar_t deviceName[64]{};
    if (!FindVirtualDisplayDeviceName(deviceName, 64)) {
        std::fprintf(stderr, "virtual_screen: could not locate VDD device\n");
        return false;
    }

    DEVMODEW dm{};
    dm.dmSize             = sizeof(dm);
    dm.dmPelsWidth        = width;
    dm.dmPelsHeight       = height;
    dm.dmBitsPerPel       = 32;
    dm.dmDisplayFrequency = hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

    LONG result = ChangeDisplaySettingsExW(deviceName, &dm, nullptr, CDS_TEST, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "virtual_screen: mode %ux%u rejected (CDS_TEST=%ld)\n",
                     width, height, result);
        return false;
    }

    result = ChangeDisplaySettingsExW(deviceName, &dm, nullptr,
                                       CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        std::fprintf(stderr, "virtual_screen: failed to apply mode (err=%ld)\n", result);
        return false;
    }

    ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    return true;
}

// Re-plug the display so the driver re-reads the registry preset table
// and advertises the newly written slot-0 resolution.
bool ReplugDisplay() {
    if (g_displayIdx != -1) {
        VddRemoveDisplay(g_vdd, g_displayIdx);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    g_displayIdx = VddAddDisplay(g_vdd);
    if (g_displayIdx == -1) {
        std::fprintf(stderr, "virtual_screen: VddAddDisplay failed on replug\n");
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return true;
}

} // namespace

void CreateVirtualScreen() {
    if (g_vdd) {
        std::fprintf(stderr, "virtual_screen: already created\n");
        return;
    }

    DeviceStatus status = QueryDeviceStatus(&VDD_CLASS_GUID, VDD_HARDWARE_ID);
    if (status != DEVICE_OK) {
        std::fprintf(stderr, "virtual_screen: VDD driver not ready (status=%d). "
                              "Make sure parsec-vdd-0.45.0.0.exe was run to install it.\n",
                     static_cast<int>(status));
        return;
    }

    g_vdd = OpenDeviceHandle(&VDD_ADAPTER_GUID);
    if (g_vdd == nullptr || g_vdd == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "virtual_screen: failed to open VDD device handle\n");
        g_vdd = nullptr;
        return;
    }

    g_keepAlive  = true;
    g_pingThread = std::thread(PingLoop);

    g_displayIdx = VddAddDisplay(g_vdd);
    if (g_displayIdx == -1) {
        std::fprintf(stderr, "virtual_screen: VddAddDisplay failed\n");
        g_keepAlive = false;
        if (g_pingThread.joinable()) g_pingThread.join();
        CloseDeviceHandle(g_vdd);
        g_vdd = nullptr;
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::printf("virtual_screen: created display index %d\n", g_displayIdx);

    wchar_t deviceName[64]{};
    if (FindVirtualDisplayDeviceName(deviceName, 64)) {
        if (!SetDisplayAsPrimary(deviceName)) {
            std::fprintf(stderr, "virtual_screen: failed to set VDD display as primary\n");
        }
    } else {
        std::fprintf(stderr, "virtual_screen: could not find VDD device to set as primary\n");
    }
}

void ResizeVirtualScreen(std::uint32_t width, std::uint32_t height) {
    if (!g_vdd || g_displayIdx == -1) {
        std::fprintf(stderr, "virtual_screen: no virtual display to resize "
                              "(call CreateVirtualScreen first)\n");
        return;
    }

    // Stamp the arbitrary resolution into preset slot 0, then replug so the
    // driver's mode list actually contains it before we try to select it.
    if (!WritePresetSlot0(width, height)) {
        return;
    }

    if (!ReplugDisplay()) {
        return;
    }

    // Replugging resets primary status back to the physical monitor, so
    // reassert it before applying the new mode.
    wchar_t deviceName[64]{};
    if (FindVirtualDisplayDeviceName(deviceName, 64)) {
        if (!SetDisplayAsPrimary(deviceName)) {
            std::fprintf(stderr, "virtual_screen: failed to re-assert VDD display as primary after replug\n");
        }
    }

    if (!ApplyMode(width, height)) {
        std::fprintf(stderr, "virtual_screen: resize to %ux%u failed\n", width, height);
        return;
    }

    std::printf("virtual_screen: resized to %ux%u\n", width, height);
}