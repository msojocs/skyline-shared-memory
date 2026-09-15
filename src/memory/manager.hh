#pragma once

#ifndef __MANAGER_HH__
#define __MANAGER_HH__
#include <memory>
#include <string>
#include <cstddef>

// 平台特定的头文件
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#endif

namespace SharedMemory {

    constexpr size_t kMaxSharedMemorySize = static_cast<size_t>(1024) * 1024 * 1024;

    // Keys become POSIX shared-memory names or filesystem paths.  Keep the
    // accepted alphabet deliberately narrow so an API caller cannot escape
    // the shared-memory directory or create ambiguous names.
    bool validate_key(const std::string& key);

    // 共享内存头部结构
    struct SharedMemoryHeader {
        size_t size;          // 用户数据大小
        int version;          // 版本号
    };

    // 共享内存管理器类
    class SharedMemoryManager : public std::enable_shared_from_this<SharedMemoryManager> {
    public:
        // 构造函数
        SharedMemoryManager(const std::string& key, bool create = false, size_t size = 0);
        
        // 析构函数
        ~SharedMemoryManager();
        
        // 获取共享内存地址
        void* get_address() const { return address_; }
        
        // 获取共享内存大小
        size_t get_size() const { return size_; }
        
        // 获取文件路径
        const std::string& get_file_path() const { return file_path_; }

#ifdef _WIN32
        bool owns_backing_file() const { return remove_on_destroy_; }
#endif
        
        // 获取版本号
        int get_version() const { 
            if (address_) {
                return static_cast<SharedMemoryHeader*>(address_)->version;
            }
            return 0;
        }

#ifdef _WIN32
        // Move the current generation aside while keeping its mapped view
        // alive, so a new generation can reuse the canonical key path.
        bool retire_backing_file();
        bool restore_backing_file_if_missing();
#endif
        
    private:
        std::string key_;           // 共享内存键名
        size_t size_;               // 数据区大小
        void* address_;             // 共享内存地址
        std::string file_path_;     // 文件路径

#ifdef _WIN32
        HANDLE file_mapping_;       // 文件映射句柄
        HANDLE file_handle_;        // 标识实际映射的文件对象
        bool remove_on_destroy_;    // 仅创建者负责删除 backing
        bool remove_on_destroy_before_retire_;
        std::string canonical_file_path_;
        
        // 创建文件映射
        bool create_mapping(HANDLE file_handle, size_t mapping_size);
#else
#endif
    };
}
#endif
