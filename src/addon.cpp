// addon.cpp
// would have added screen resizing but too buggy (parsec-vdd windows, cgvirtualdisplay macos, xrandr linux)

#include <napi.h>
#include "shared/include/mouse.hpp"
#include "shared/include/keyboard.hpp"

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
    MoveMousePosition(dx, dy);
    return env.Undefined();
}

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

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("scrollMouse",     Napi::Function::New(env, JS_ScrollMouse));
    exports.Set("setMouseButton",  Napi::Function::New(env, JS_SetMouseButton));
    exports.Set("setMousePosition",Napi::Function::New(env, JS_SetMousePosition));
    exports.Set("moveMousePosition", Napi::Function::New(env, JS_MoveMousePosition));

    exports.Set("setKeyboardKey", Napi::Function::New(env, JS_SetKeyboardKey));

    return exports;
}

NODE_API_MODULE(native_apis, Init)