#include "napi.h"
#include "memory.hh"
#include "manager.hh"

#include <cstring>

namespace SharedMemory {
    namespace {
#ifdef _WIN32
        bool is_known_data_range(void *data, size_t length) {
            const auto start = reinterpret_cast<uintptr_t>(data);
            for (const auto &entry : managerMap) {
                const auto &manager = entry.second;
                if (!manager || !manager->get_address()) {
                    continue;
                }
                const auto base = reinterpret_cast<uintptr_t>(manager->get_address()) +
                                  sizeof(SharedMemoryHeader);
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

        Napi::Value undefined(const Napi::CallbackInfo &info) {
            return info.Env().Undefined();
        }
    }

#ifdef _WIN32
    Napi::Value set_memory_by_address(const Napi::CallbackInfo &info) {
        Napi::Env env = info.Env();
        if (info.Length() < 2) {
            throw Napi::TypeError::New(env, "Expected handle and data arguments");
        }

        void *address = nullptr;
        size_t capacity = 0;
        if (!decode_memory_handle(info[0], &address, &capacity) ||
            !address || capacity == 0 ||
            !is_known_data_range(address, capacity)) {
            throw Napi::TypeError::New(env, "Invalid shared memory handle");
        }

        BinaryView source;
        if (!get_binary_view(info[1], &source)) {
            throw Napi::TypeError::New(
                env, "Expected Buffer, TypedArray, or ArrayBuffer");
        }
        if (source.size > capacity) {
            throw Napi::RangeError::New(
                env, "Data length exceeds shared memory size");
        }
        if (source.size != 0) {
            std::memmove(address, source.data, source.size);
        }
        return Napi::Boolean::New(env, true);
    }

    Napi::Value delete_set_cache(const Napi::CallbackInfo &info) {
#ifdef _WIN32
        if (info.Length() != 1 || !info[0].IsString()) {
            throw Napi::TypeError::New(info.Env(), "Expected shared memory key");
        }
        const auto key = info[0].As<Napi::String>().Utf8Value();
        if (!validate_key(key)) {
            throw Napi::TypeError::New(info.Env(), "invalid shared memory key");
        }
        const auto it = managerMap.find(key);
        if (it != managerMap.end() && it->second &&
            it->second->owns_backing_file() && it->second->get_address()) {
            // A pointer-only handle has no generation field.  Retain the
            // invalidated mapping so a future generation cannot reuse the
            // same address and accidentally make the old handle valid.
            invalidate_manager_handle(it->second);
        }
#endif
        return undefined(info);
    }

    Napi::Value delete_get_cache(const Napi::CallbackInfo &info) {
#ifdef _WIN32
        if (info.Length() != 1 || !info[0].IsString()) {
            throw Napi::TypeError::New(info.Env(), "Expected shared memory key");
        }
        const auto key = info[0].As<Napi::String>().Utf8Value();
        auto current = managerMap.find(key);
        if (current != managerMap.end() && current->second &&
            !current->second->owns_backing_file()) {
            managerMap.erase(current);
        }
#endif
        return undefined(info);
    }
#endif

    Napi::Value delete_copy_cache(const Napi::CallbackInfo &info) {
        if (info.Length() != 1 || !info[0].IsString()) {
            throw Napi::TypeError::New(info.Env(), "Expected shared memory key");
        }
        const auto key = info[0].As<Napi::String>().Utf8Value();
        if (!validate_key(key)) {
            throw Napi::TypeError::New(info.Env(), "invalid shared memory key");
        }
        auto cached = copyCache.find(key);
        if (cached != copyCache.end()) {
#ifdef _WIN32
            auto current = managerMap.find(key);
            if (current != managerMap.end() &&
                current->second == cached->second &&
                current->second &&
                !current->second->owns_backing_file()) {
                managerMap.erase(current);
            }
#endif
            copyCache.erase(cached);
        }
        return undefined(info);
    }
}
