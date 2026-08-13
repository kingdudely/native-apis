// addon.cpp
//
// This is a NEW file — it does not modify virtual_screen.h, mouse.h, or
// keyboard.h. It just #includes them and wraps each free function in a
// thin N-API (node-addon-api) binding. Add this file to binding.gyp's
// "sources" and you're done; the originals stay untouched.

#include <napi.h>
#include "shared/include/virtual_screen.hpp"   // CreateVirtualScreen, ResizeVirtualScreen
#include "shared/include/mouse.hpp"            // ScrollMouse, SetMouseButton, SetMousePosition, MoveMousePosition
#include "shared/include/keyboard.hpp"         // SetKeyboardKey

// ---- virtual_screen.h -------------------------------------------------

Napi::Value JS_CreateVirtualScreen(const Napi::CallbackInfo& info) {
    CreateVirtualScreen();
    return info.Env().Undefined();
}

Napi::Value JS_ResizeVirtualScreen(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "ResizeVirtualScreen(width: number, height: number)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto width  = info[0].As<Napi::Number>().Uint32Value();
    auto height = info[1].As<Napi::Number>().Uint32Value();
    ResizeVirtualScreen(width, height);
    return env.Undefined();
}

// ---- mouse.h ------------------------------------------------------------

Napi::Value JS_ScrollMouse(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "ScrollMouse(deltaMode, deltaX, deltaY, deltaZ)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto deltaMode = static_cast<std::uint8_t>(info[0].As<Napi::Number>().Uint32Value());
    auto deltaX = info[1].As<Napi::Number>().FloatValue();
    auto deltaY = info[2].As<Napi::Number>().FloatValue();
    auto deltaZ = info[3].As<Napi::Number>().FloatValue();
    ScrollMouse(deltaMode, deltaX, deltaY, deltaZ);
    return env.Undefined();
}

Napi::Value JS_SetMouseButton(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "SetMouseButton(button, isDown)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto button = static_cast<std::uint8_t>(info[0].As<Napi::Number>().Uint32Value());
    bool isDown = info[1].As<Napi::Boolean>().Value();
    SetMouseButton(button, isDown);
    return env.Undefined();
}

Napi::Value JS_SetMousePosition(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "SetMousePosition(absoluteX, absoluteY)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto x = info[0].As<Napi::Number>().Uint32Value();
    auto y = info[1].As<Napi::Number>().Uint32Value();
    SetMousePosition(x, y);
    return env.Undefined();
}

Napi::Value JS_MoveMousePosition(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "MoveMousePosition(deltaX, deltaY)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto dx = info[0].As<Napi::Number>().Int32Value();
    auto dy = info[1].As<Napi::Number>().Int32Value();
    // NOTE: on macOS this now targets the Karabiner VHID path and is
    // expected to throw (from inside MoveMousePosition itself) if the
    // VHID pointing device isn't ready, rather than falling back to
    // CGEvent — that behavior lives in mouse.mm, not here.
    MoveMousePosition(dx, dy);
    return env.Undefined();
}

// ---- keyboard.h -----------------------------------------------------

Napi::Value JS_SetKeyboardKey(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2) {
        Napi::TypeError::New(env, "SetKeyboardKey(codeValue, isDown)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto codeValue = static_cast<std::uint8_t>(info[0].As<Napi::Number>().Uint32Value());
    bool isDown = info[1].As<Napi::Boolean>().Value();
    SetKeyboardKey(codeValue, isDown);
    return env.Undefined();
}

// ---- module init ------------------------------------------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("createVirtualScreen", Napi::Function::New(env, JS_CreateVirtualScreen));
    exports.Set("resizeVirtualScreen", Napi::Function::New(env, JS_ResizeVirtualScreen));

    exports.Set("scrollMouse",     Napi::Function::New(env, JS_ScrollMouse));
    exports.Set("setMouseButton",  Napi::Function::New(env, JS_SetMouseButton));
    exports.Set("setMousePosition",Napi::Function::New(env, JS_SetMousePosition));
    exports.Set("moveMousePosition", Napi::Function::New(env, JS_MoveMousePosition));

    exports.Set("setKeyboardKey", Napi::Function::New(env, JS_SetKeyboardKey));

    return exports;
}

NODE_API_MODULE(native_apis, Init)