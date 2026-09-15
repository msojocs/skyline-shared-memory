#ifndef _WIN32
#include <sys/types.h>
#endif
#include "./memory/memory.hh"
#include <napi.h>
#include <cstdlib>
#include "logger.hh"

Napi::Value version(const Napi::CallbackInfo &info) {
  
  return Napi::String::New(info.Env(), "1352");
}

// 模块卸载时的清理函数
static void Cleanup() {
  
}

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
  Logger::Init();
  exports.Set(Napi::String::New(env, "setMemory"),
              Napi::Function::New(env, SharedMemory::set_memory));
  exports.Set(Napi::String::New(env, "getMemory"),
              Napi::Function::New(env, SharedMemory::get_memory));
  exports.Set(Napi::String::New(env, "removeMemory"),
              Napi::Function::New(env, SharedMemory::remove_memory));
  exports.Set(Napi::String::New(env, "copyMemoryToBuffer"),
              Napi::Function::New(env, SharedMemory::copy_memory_to_buffer));
  exports.Set(Napi::String::New(env, "deleteCopyCache"),
              Napi::Function::New(env, SharedMemory::delete_copy_cache));
#ifdef _WIN32
  // Keep the Windows surface compatible with the official Skyline module.
  exports.Set(Napi::String::New(env, "getMemoryByAddress"),
              Napi::Function::New(env, SharedMemory::get_memory_by_address));
  exports.Set(Napi::String::New(env, "setMemoryByAddress"),
              Napi::Function::New(env, SharedMemory::set_memory_by_address));
  exports.Set(Napi::String::New(env, "deleteSetCache"),
              Napi::Function::New(env, SharedMemory::delete_set_cache));
  exports.Set(Napi::String::New(env, "deleteGetCache"),
              Napi::Function::New(env, SharedMemory::delete_get_cache));
#endif
  exports.Set(Napi::String::New(env, "version"),
              Napi::Function::New(env, version));

  // 注册程序退出时的清理函数
  std::atexit(Cleanup);

  return exports;
}

NODE_API_MODULE(cmnative, Init)
