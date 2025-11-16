#include <sys/types.h>
#include "./memory.hh"
#include <napi.h>
#include <cstdlib>
#include "logger.hh"

Napi::Value version(const Napi::CallbackInfo &info) {
  
  return Napi::String::New(info.Env(), "1352");
}

// 模块卸载时的清理函数
static void Cleanup() {
  SharedMemory::cleanup_console();
}

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
  Logger::Init();
  exports.Set(Napi::String::New(env, "setConsole"),
              Napi::Function::New(env, SharedMemory::set_console));
  exports.Set(Napi::String::New(env, "setMemory"),
              Napi::Function::New(env, SharedMemory::set_memory));
  exports.Set(Napi::String::New(env, "getMemory"),
              Napi::Function::New(env, SharedMemory::get_memory));
  exports.Set(Napi::String::New(env, "removeMemory"),
              Napi::Function::New(env, SharedMemory::remove_memory));
  exports.Set(Napi::String::New(env, "version"),
              Napi::Function::New(env, version));

  // 注册程序退出时的清理函数
  std::atexit(Cleanup);

  return exports;
}

NODE_API_MODULE(cmnative, Init)