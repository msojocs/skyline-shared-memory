#include "napi.h"
#include "memory.hh"
#include "../logger.hh"
#include <cstring>
#include <memory>
#include "manager.hh"
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>

namespace SharedMemory {
    using Logger::logger;
    std::map<std::string, std::shared_ptr<SharedMemoryManager>> managerMap;
    std::map<std::string, std::shared_ptr<SharedMemoryManager>> copyCache;
#ifdef _WIN32
    std::map<uintptr_t, std::shared_ptr<SharedMemoryManager>> invalidatedManagers;

    void invalidate_manager_handle(
        const std::shared_ptr<SharedMemoryManager> &manager) {
        if (!manager || !manager->get_address()) return;
        const auto address = reinterpret_cast<uintptr_t>(manager->get_address()) +
                             sizeof(SharedMemoryHeader);
        invalidatedManagers.emplace(address, manager);
    }
#endif
    Napi::Value set_memory(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        
        // 参数检查
        if (info.Length() < 2) {
            throw Napi::Error::New(env, "需要两个参数: key和length");
        }
        
        if (!info[0].IsString()) {
            throw Napi::Error::New(env, "第一个参数必须是字符串类型的key");
        }
        
        if (!info[1].IsNumber()) {
            throw Napi::Error::New(env, "第二个参数必须是数字类型的length");
        }
        
        std::string key = info[0].As<Napi::String>().Utf8Value();
        const double requested_length = info[1].As<Napi::Number>().DoubleValue();
#ifdef _WIN32
        const double max_length = static_cast<double>(
            std::numeric_limits<uint32_t>::max() - sizeof(SharedMemoryHeader));
#else
        const double max_length = static_cast<double>(
            std::numeric_limits<uint32_t>::max());
#endif
        if (!std::isfinite(requested_length) ||
            requested_length < 1 ||
            std::floor(requested_length) != requested_length ||
            requested_length > max_length) {
            throw Napi::RangeError::New(env, "length超出支持范围");
        }
        size_t length = static_cast<size_t>(requested_length);
        
        if (length <= 0) {
            throw Napi::Error::New(env, "length必须大于0");
        }
        
        try {
            logger->debug("Set memory call.");
            logger->debug("Creating SharedMemoryManager...");
            
            // 创建共享内存管理器
#ifdef _WIN32
            std::shared_ptr<SharedMemoryManager> previous_manager;
            bool previous_retired = false;
            bool previous_had_set_handle = false;
#endif
            if (auto previous = managerMap.find(key); previous != managerMap.end()) {
#ifdef _WIN32
                previous_manager = previous->second;
                if (previous_manager) {
                    previous_had_set_handle =
                        previous_manager->owns_backing_file();
                    previous_retired = previous_manager->retire_backing_file();
                    if (!previous_retired) {
                        throw std::runtime_error(
                            "Failed to replace shared memory backing file");
                    }
                }
#else
                managerMap.erase(previous);
#endif
            }
#ifdef _WIN32
            if (!previous_manager) {
                try {
                    previous_manager =
                        std::make_shared<SharedMemoryManager>(key, false);
                } catch (const std::exception &) {
                    previous_manager.reset();
                }
                if (previous_manager) {
                    previous_retired = previous_manager->retire_backing_file();
                }
            }
            std::shared_ptr<SharedMemoryManager> manager;
            try {
                manager = std::make_shared<SharedMemoryManager>(key, true, length);
                managerMap[key] = manager;
            } catch (...) {
                manager.reset();
                if (previous_retired && previous_manager &&
                    !previous_manager->restore_backing_file_if_missing()) {
                    if (previous_had_set_handle) {
                        invalidate_manager_handle(previous_manager);
                    }
                    auto current = managerMap.find(key);
                    if (current != managerMap.end() &&
                        current->second == previous_manager) {
                        managerMap.erase(current);
                    }
                    throw std::runtime_error(
                        "Failed to restore shared memory backing file");
                }
                throw;
            }
            if (previous_had_set_handle) {
                invalidate_manager_handle(previous_manager);
            }
#else
            auto manager = std::make_shared<SharedMemoryManager>(key, true, length);
#endif
#ifndef _WIN32
            managerMap[key] = manager;
#endif
            logger->debug("SharedMemoryManager created successfully.");
            
            // 获取共享内存的地址和大小
            void* addr = manager->get_address();
            size_t size = manager->get_size();
            
            logger->debug("Shared memory created: key=%s, size=%zu, address=%p", 
                key.c_str(), size, addr);
            
            // 获取数据区域的地址
            void* data_addr = static_cast<char*>(addr) + sizeof(SharedMemoryHeader);
            memset(data_addr, 0, length);
#ifndef _WIN32
            // Keep the initial payload zeroed.  The first eight bytes are the
            // Skyline frame header; copying an unbounded key here could both
            // corrupt that protocol and overflow a small allocation.
#endif
            
#ifdef _WIN32
            // SkylineShell expects a process-local pointer/size descriptor,
            // rather than a JavaScript view over the mapping.
            return encode_memory_handle(env, data_addr, length);
#else
            // The Linux pageframe path consumes the existing external
            // ArrayBuffer representation.
            return Napi::ArrayBuffer::New(env, data_addr, length);
#endif
            
        } catch (const std::exception& e) {
            logger->debug("Error: %s", e.what());
            throw Napi::Error::New(env, e.what());
        } catch (...) {
            logger->debug("Unknown error occurred");
            throw Napi::Error::New(env, "设置共享内存时发生未知错误");
        }
    }
}
