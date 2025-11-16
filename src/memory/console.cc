#include "../memory.hh"
#include <cstdarg>
#include <cstdio>
#include <spdlog/spdlog.h>

namespace SharedMemory {
    // 全局回调函数
    Napi::FunctionReference console_callback;

    // 设置控制台回调函数
    Napi::Value set_console(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        
        if (info.Length() != 1) {
            throw Napi::Error::New(env, "参数长度必须为1!");
        }
        if (!info[0].IsFunction()) {
            throw Napi::Error::New(env, "参数必须是函数!");
        }

        // 保存回调函数
        console_callback = Napi::Persistent(info[0].As<Napi::Function>());
        
        return env.Undefined();
    }

    // 清理控制台回调函数
    void cleanup_console() {
        if (!console_callback.IsEmpty()) {
            console_callback.Reset();
        }
    }

} 