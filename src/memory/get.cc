#include "napi.h"
#include "memory.hh"
#include "../logger.hh"
#include <cstring>
#include <memory>
#include "manager.hh"
#include <algorithm>
#include <cmath>
#include <map>

namespace SharedMemory {
    using Logger::logger;

    namespace {
#ifdef _WIN32
        // Handles contain a raw process-local address.  Only accept addresses
        // that belong to a manager held by this module; this keeps malformed
        // RPC data from turning into an arbitrary memory read/write.
        bool is_known_data_range(void* data, size_t length) {
            const auto start = reinterpret_cast<uintptr_t>(data);
            for (const auto &entry : managerMap) {
                const auto &manager = entry.second;
                if (!manager || !manager->get_address()) {
                    continue;
                }
                const auto base = reinterpret_cast<uintptr_t>(manager->get_address())
                                  + sizeof(SharedMemoryHeader);
                if (invalidatedManagers.count(base) != 0) {
                    continue;
                }
                const auto available = manager->get_size();
                if (start >= base && start - base <= available &&
                    length <= available - (start - base)) {
                    return true;
                }
            }
            return false;
        }
#endif

        std::shared_ptr<SharedMemoryManager> get_manager_for_key(
            const std::string &key) {
            auto cached = copyCache.find(key);
            if (cached != copyCache.end()) return cached->second;
#ifdef _WIN32
            auto target = managerMap.find(key);
            if (target == managerMap.end()) {
                return nullptr;
            }
            return target->second;
#else
            // Legacy Linux getMemory/setMemory expose live ArrayBuffers.
            // Keep copy mappings separate so deleteCopyCache cannot unmap
            // memory still referenced by those views.
            return nullptr;
#endif
        }
    }

    Napi::Value get_memory(const Napi::CallbackInfo &info) {
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
            logger->info("Get memory call.");
            logger->debug("Creating SharedMemoryManager...");
            
            if (auto target = managerMap.find(key);target == managerMap.end()) {
#ifdef _WIN32
                // The official Windows module reports a missing key as null;
                // preserve the Linux module's historical exception behavior.
                try {
                    managerMap[key] = std::make_shared<SharedMemoryManager>(key, false);
                } catch (const std::exception &) {
                    return env.Null();
                }
#else
                managerMap[key] = std::make_shared<SharedMemoryManager>(key, false);
#endif
            }
            // 取共享内存管理器
            auto manager = managerMap[key];
            logger->debug("SharedMemoryManager created successfully.");
            
            // 获取共享内存的地址和大小
            void* addr = manager->get_address();
            size_t size = manager->get_size();
            
            logger->debug("Shared memory opened: key={}, size={}, address={}", 
                key, size, addr);
            
            // 读取头部信息
            SharedMemoryHeader* header = static_cast<SharedMemoryHeader*>(addr);
            size_t data_size = header->size;

            if (data_size == 0 || data_size > manager->get_size()) {
                throw std::runtime_error("共享内存头部大小无效");
            }
            
            logger->debug("Header information: size={}", data_size);
            
            // 获取数据区域的地址
            void* data_addr = static_cast<char*>(addr) + sizeof(SharedMemoryHeader);
            
#ifdef _WIN32
            // The Windows compatibility module follows the official
            // sharedMemory contract: reads return an ordinary Buffer copy.
            return Napi::Buffer<uint8_t>::Copy(
                env, static_cast<uint8_t *>(data_addr), data_size);
#else
            auto deleter = [](void* /*data*/, void* /*hint*/) {
                logger->debug("Buffer cleanup callback called.");
                // 这里不需要做任何事情，因为共享内存和互斥锁会在程序退出时自动清理
            };
            // 创建ArrayBuffer，直接映射到共享内存
            auto buffer = Napi::ArrayBuffer::New(env, data_addr, data_size, deleter);
            

            return buffer;
#endif
            
        } catch (const std::exception& e) {
            logger->debug("Error: %s", e.what());
            throw Napi::Error::New(env, e.what());
        } catch (...) {
            logger->debug("Unknown error occurred");
            throw Napi::Error::New(env, "获取共享内存时发生未知错误");
        }
    }

#ifdef _WIN32
    Napi::Value get_memory_by_address(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        if (info.Length() < 1) {
            throw Napi::TypeError::New(env, "Missing shared memory handle argument");
        }

        void *address = nullptr;
        size_t size = 0;
        if (!decode_memory_handle(info[0], &address, &size)) {
            throw Napi::TypeError::New(env, "Invalid shared memory handle");
        }
        // Match the official module's benign response for an all-zero
        // placeholder handle while still rejecting non-zero foreign pointers.
        if (!address || size == 0) {
            return env.Null();
        }
        if (!is_known_data_range(address, size)) {
            throw Napi::TypeError::New(env, "Invalid shared memory handle");
        }

        return Napi::Buffer<uint8_t>::Copy(
            env, static_cast<uint8_t *>(address), size);
    }
#endif

    Napi::Value copy_memory_to_buffer(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        if (info.Length() < 2 || !info[0].IsString()) {
            throw Napi::TypeError::New(
                env, "Expected shared memory name and target buffer");
        }

        BinaryView target;
        if (!get_binary_view(info[1], &target)) {
            throw Napi::TypeError::New(
                env, "Expected Buffer, TypedArray, or ArrayBuffer");
        }
        if (target.size == 0) {
            throw Napi::RangeError::New(env, "Target buffer must be non-empty");
        }

        const std::string key = info[0].As<Napi::String>().Utf8Value();
        if (!validate_key(key)) {
            throw Napi::TypeError::New(env, "invalid shared memory key");
        }
        auto manager = get_manager_for_key(key);
        if (!manager) {
            try {
                manager = std::make_shared<SharedMemoryManager>(key, false);
#ifdef _WIN32
                managerMap[key] = manager;
#endif
            } catch (const std::exception &) {
                return env.Null();
            }
        }
        if (copyCache.find(key) == copyCache.end()) copyCache[key] = manager;
        const size_t size = manager->get_size();
        size_t copy_size = size;
        if (info.Length() >= 3) {
            if (!info[2].IsNumber()) {
                throw Napi::TypeError::New(env, "Expected copy length");
            }
            const double requested = info[2].As<Napi::Number>().DoubleValue();
            if (!std::isfinite(requested) || requested < 0 ||
                std::floor(requested) != requested ||
                requested > static_cast<double>(size)) {
                throw Napi::RangeError::New(env, "Invalid copy length");
            }
            copy_size = static_cast<size_t>(requested);
        }
        if (target.size < copy_size) {
            auto error = Napi::RangeError::New(
                env, "Target buffer too small for shared memory contents");
            error.Set("requiredSize", Napi::Number::New(
                env, static_cast<double>(copy_size)));
            throw error;
        }
        const auto *header = static_cast<const SharedMemoryHeader *>(
            manager->get_address());
        if (!header || header->size == 0 || header->size > size) {
            throw Napi::Error::New(env, "共享内存头部大小无效");
        }
        copy_size = std::min(copy_size, header->size);
        if (copy_size != 0) {
            std::memcpy(target.data,
                        static_cast<const char *>(manager->get_address()) +
                            sizeof(SharedMemoryHeader),
                        copy_size);
        }
        return Napi::Number::New(env, static_cast<double>(copy_size));
    }
}
