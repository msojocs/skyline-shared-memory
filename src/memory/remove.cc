#include "napi.h"
#include "memory.hh"
#include <memory>

#ifdef _WIN32
#include <shlobj.h> // 用于CSIDL_PERSONAL
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#endif
#include "../logger.hh"

namespace SharedMemory {
    using Logger::logger;
    Napi::Boolean remove_memory(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        
        // 参数检查
        if (info.Length() < 1) {
            throw Napi::Error::New(env, "需要一个参数: key");
        }
        
        if (!info[0].IsString()) {
            throw Napi::Error::New(env, "参数必须是字符串类型的key");
        }
        
        std::string key = info[0].As<Napi::String>().Utf8Value();
        
        try {
            logger->info("Remove memory call.");
            logger->debug("Read arguments.");
            
            if (auto target = managerMap.find(key);target == managerMap.end()) {
                logger->debug("No shared memory manager found for key: %s", key.c_str());
                return Napi::Boolean::New(env, false);
            }
            // auto manager = managerMap[key];
            // managerMap.erase(key);
            
        } catch (const std::exception& e) {
            logger->debug("Error: %s", e.what());
            throw Napi::Error::New(env, e.what());
        } catch (...) {
            logger->debug("Unknown error occurred");
            throw Napi::Error::New(env, "删除共享内存时发生未知错误");
        }
        return Napi::Boolean::New(env, true);
    }
} 