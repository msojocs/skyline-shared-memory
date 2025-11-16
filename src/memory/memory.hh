#pragma once

#ifndef MEMORY_HH
#define MEMORY_HH
#include "napi.h"
#include "manager.hh"
#include <map>

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
    // 全局变量来保存共享内存资源
    extern std::map<std::string, std::shared_ptr<SharedMemoryManager>> managerMap;
    /**
     * 设置共享内存 
     * @param info 回调信息
     * @return 共享内存的视图
     */
    Napi::Value set_memory(const Napi::CallbackInfo &info);

    /**
     * 获取共享内存
     * @param info 回调信息
     * @return 共享内存的视图
     */
    Napi::Value get_memory(const Napi::CallbackInfo &info);

    /**
     * 删除共享内存
     * @param info 回调信息
     * @return 是否成功
     */
    Napi::Boolean remove_memory(const Napi::CallbackInfo &info);
}
#endif