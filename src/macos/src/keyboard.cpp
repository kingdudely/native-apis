#include "shared/include/keyboard.hpp"

#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include "macos/include/GetHIDConnect.hpp"
#include "macos/include/PostNXEvent.hpp"
#include <array>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <optional>
#include <unordered_map>

namespace {
// NX_KEYTYPE
inline constexpr std::array<std::optional<CGKeyCode>, 174> kMacVirtualKeyMap = {
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt, kVK_Function, std::nullopt,
    kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F,
    kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L,
    kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R,
    kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X,
    kVK_ANSI_Y, kVK_ANSI_Z, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
    kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9, kVK_ANSI_0,
    kVK_Return, kVK_Escape, kVK_Delete, kVK_Tab, kVK_Space, kVK_ANSI_Minus,
    kVK_ANSI_Equal, kVK_ANSI_LeftBracket, kVK_ANSI_RightBracket,
    kVK_ANSI_Backslash, kVK_ANSI_Semicolon, kVK_ANSI_Quote, kVK_ANSI_Grave,
    kVK_ANSI_Comma, kVK_ANSI_Period, kVK_ANSI_Slash, kVK_CapsLock,
    kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9,
    kVK_F10, kVK_F11, kVK_F12, std::nullopt, std::nullopt, std::nullopt,
    kVK_Help, kVK_Home, kVK_PageUp, kVK_ForwardDelete, kVK_End, kVK_PageDown,
    kVK_RightArrow, kVK_LeftArrow, kVK_DownArrow, kVK_UpArrow,
    kVK_ANSI_KeypadClear, kVK_ANSI_KeypadDivide, kVK_ANSI_KeypadMultiply,
    kVK_ANSI_KeypadMinus, kVK_ANSI_KeypadPlus, kVK_ANSI_KeypadEnter,
    kVK_ANSI_Keypad1, kVK_ANSI_Keypad2, kVK_ANSI_Keypad3, kVK_ANSI_Keypad4,
    kVK_ANSI_Keypad5, kVK_ANSI_Keypad6, kVK_ANSI_Keypad7, kVK_ANSI_Keypad8,
    kVK_ANSI_Keypad9, kVK_ANSI_Keypad0, kVK_ANSI_KeypadDecimal,
    kVK_ISO_Section, std::nullopt, std::nullopt, kVK_ANSI_KeypadEquals,
    kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    kVK_Help, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt, kVK_Mute, kVK_VolumeUp,
    kVK_VolumeDown, std::nullopt, std::nullopt, kVK_JIS_Kana, kVK_JIS_Yen,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, kVK_Control, kVK_Shift, kVK_Option, kVK_Command,
    kVK_RightControl, kVK_RightShift, kVK_RightOption, kVK_RightCommand,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
    std::nullopt, std::nullopt, std::nullopt,
};

// Reads the live system key-repeat timing (respects whatever the user has
// set in System Settings > Keyboard, including values beyond the slider's
// UI range set via `defaults write`). Values come back in nanoseconds
// directly from IOKit, no *15ms plist-int conversion needed.
// Falls back to macOS's documented defaults if the param is unavailable
// (e.g. "off" / press-and-hold-to-accent mode).
struct RepeatTiming {
    uint64_t initialDelayNs = 500'000'000ULL; // ~500ms default
    uint64_t repeatIntervalNs = 83'333'333ULL; // ~83ms default (~12/sec)
};

uint64_t ReadRepeatParam(io_connect_t conn, CFStringRef key, uint64_t fallback) {
    uint64_t value = 0;
    IOByteCount size = sizeof(value);
    if (IOHIDGetParameter(conn, key, sizeof(value), &value, &size) == KERN_SUCCESS &&
        size == sizeof(value) && value > 0) {
        return value;
    }
    return fallback;
}

RepeatTiming GetSystemRepeatTiming(io_connect_t conn) {
    RepeatTiming timing;
    timing.initialDelayNs = ReadRepeatParam(conn, CFSTR(kIOHIDInitialKeyRepeatKey), timing.initialDelayNs);
    timing.repeatIntervalNs = ReadRepeatParam(conn, CFSTR(kIOHIDKeyRepeatKey), timing.repeatIntervalNs);
    return timing;
}

void PostKeyEvent(io_connect_t conn, CGKeyCode vk, bool isDown, bool isRepeat) {
    PostNXEvent(conn, isDown ? NX_KEYDOWN : NX_KEYUP, [&](NXEventData& ev) {
        ev.key.repeat = isRepeat;
        ev.key.keyCode = static_cast<UInt16>(vk);
    });
}

// One dispatch source per currently-held key, so multiple keys can repeat
// independently (matches real keyboard behavior with rollover).
std::unordered_map<std::uint8_t, dispatch_source_t>& RepeatTimers() {
    static std::unordered_map<std::uint8_t, dispatch_source_t> timers;
    return timers;
}

void StopRepeat(std::uint8_t codeValue) {
    auto& timers = RepeatTimers();
    auto it = timers.find(codeValue);
    if (it == timers.end()) return;

    dispatch_source_cancel(it->second);
    timers.erase(it);
}

void StartRepeat(std::uint8_t codeValue, CGKeyCode vk, io_connect_t conn) {
    StopRepeat(codeValue); // safety: cancel any stale timer for this key first

    RepeatTiming timing = GetSystemRepeatTiming(conn);

    dispatch_queue_t queue = dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0);
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);

    dispatch_source_set_timer(
        timer,
        dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timing.initialDelayNs)),
        timing.repeatIntervalNs,
        /*leeway*/ NSEC_PER_MSEC);

    dispatch_source_set_event_handler(timer, ^{
        PostKeyEvent(conn, vk, /*isDown=*/true, /*isRepeat=*/true);
    });

    RepeatTimers()[codeValue] = timer;
    dispatch_resume(timer);
}

} // namespace

void SetKeyboardKey(std::uint8_t codeValue, bool isDown) {
    if (codeValue >= kMacVirtualKeyMap.size()) return;
    auto vk = kMacVirtualKeyMap[codeValue];
    if (!vk) return;

    io_connect_t conn = GetHIDConnect();
    if (conn == MACH_PORT_NULL) return;

    if (isDown) {
        PostKeyEvent(conn, *vk, /*isDown=*/true, /*isRepeat=*/false);
        StartRepeat(codeValue, *vk, conn);
    } else {
        StopRepeat(codeValue);
        PostKeyEvent(conn, *vk, /*isDown=*/false, /*isRepeat=*/false);
    }
}