#include "../logger.hh"
#include <cstring>
#include <sys/stat.h>
#include "manager.hh"

#ifdef _WIN32
#include <atomic>
#include <direct.h> // 用于Windows目录创建
#include <shlobj.h> // 用于获取用户目录
#include <vector>
#else
#include <errno.h>    // 用于错误处理
#include <sys/mman.h>
#endif

namespace SharedMemory {
    using Logger::logger;

#ifdef _WIN32
    namespace {
        std::atomic<unsigned long long> retired_file_counter{0};

        bool rename_file_handle(HANDLE file_handle,
                                const std::string &target_path) {
            const int wide_length = MultiByteToWideChar(
                CP_ACP, 0, target_path.c_str(), -1, NULL, 0);
            if (wide_length <= 1) {
                return false;
            }

            std::vector<wchar_t> wide_path(static_cast<size_t>(wide_length));
            if (MultiByteToWideChar(
                    CP_ACP, 0, target_path.c_str(), -1,
                    wide_path.data(), wide_length) != wide_length) {
                return false;
            }

            const DWORD path_bytes = static_cast<DWORD>(
                (wide_path.size() - 1) * sizeof(wchar_t));
            // FileNameLength excludes the terminator, but the kernel still
            // reads it.  Sizing the buffer to end exactly at the last
            // character let that read land on adjacent heap bytes, so the
            // rename failed intermittently with ERROR_INVALID_NAME or
            // ERROR_PATH_NOT_FOUND depending on what happened to be there.
            // Keep a terminator inside the buffer and copy it explicitly.
            const size_t name_bytes = wide_path.size() * sizeof(wchar_t);
            const size_t info_size = offsetof(FILE_RENAME_INFO, FileName) +
                                     name_bytes;
            std::vector<unsigned char> storage(info_size, 0);
            auto *rename_info = reinterpret_cast<FILE_RENAME_INFO *>(
                storage.data());
            rename_info->ReplaceIfExists = FALSE;
            rename_info->RootDirectory = NULL;
            rename_info->FileNameLength = path_bytes;
            std::memcpy(rename_info->FileName, wide_path.data(), name_bytes);
            return SetFileInformationByHandle(
                file_handle, FileRenameInfo, rename_info,
                static_cast<DWORD>(storage.size())) != FALSE;
        }

        bool file_handles_match(HANDLE first, HANDLE second) {
            BY_HANDLE_FILE_INFORMATION first_info{};
            BY_HANDLE_FILE_INFORMATION second_info{};
            return first != INVALID_HANDLE_VALUE &&
                   second != INVALID_HANDLE_VALUE &&
                   GetFileInformationByHandle(first, &first_info) &&
                   GetFileInformationByHandle(second, &second_info) &&
                   first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
                   first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
                   first_info.nFileIndexLow == second_info.nFileIndexLow;
        }
    }
#endif

    bool validate_key(const std::string& key) {
        if (key.empty() || key.size() > 200) return false;
        for (unsigned char ch : key) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.') {
                continue;
            }
            return false;
        }
        return true;
    }

    // 检测是否在Wine环境下运行
    bool is_running_under_wine() {
#ifdef _WIN32
        HMODULE hntdll = GetModuleHandleA("ntdll.dll");
        if (hntdll) {
            typedef const char* (*wine_get_version)();
            wine_get_version wine_get_version_func = (wine_get_version)GetProcAddress(hntdll, "wine_get_version");
            if (wine_get_version_func) {
                logger->debug("Running under Wine: %s", wine_get_version_func());
                return true;
            }
        }
#endif
        return false;
    }

    // 创建目录的跨平台函数
    bool create_directory(const std::string& path) {
#ifdef _WIN32
        return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
        return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
    }

    SharedMemoryManager::SharedMemoryManager(const std::string& key, bool create, size_t size) 
        : key_(key), size_(size), address_(nullptr)
#ifdef _WIN32
        , file_mapping_(nullptr)
        , file_handle_(INVALID_HANDLE_VALUE)
        , remove_on_destroy_(create)
        , remove_on_destroy_before_retire_(create)
#else

#endif
    {
        if (!validate_key(key)) {
            throw std::invalid_argument("invalid shared memory key");
        }
        if ((create && size == 0) || size > kMaxSharedMemorySize) {
            throw std::invalid_argument("invalid shared memory size");
        }
        // 计算实际需要分配的大小（包括头部）
        size_t total_size = sizeof(SharedMemoryHeader) + size;
        logger->debug("Size of header: {}", sizeof(SharedMemoryHeader));
        logger->debug("Size of header + size: {}", total_size);
        
#ifdef _WIN32
        // Windows实现
        
        // 检查是否在Wine环境下运行
        bool is_wine = is_running_under_wine();
        
        std::string file_path;
        
        if (is_wine) {
            // Wine环境下使用/dev/shm目录
            file_path = "/dev/shm/skyline_" + key + ".dat";
            logger->debug("Using Wine shared memory path: {}", file_path);
            
            // 确保/dev/shm目录存在
            // 在Wine环境下，这个目录应该已经存在，但为了安全起见，我们检查一下
            struct stat st;
            if (stat("/dev/shm", &st) != 0) {
                logger->debug("Warning: /dev/shm directory does not exist in Wine environment");
            }
        } else {
            // 原生Windows环境下使用用户目录
            // 获取用户目录
            char user_path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, user_path))) {
                logger->debug("User path: %s", user_path);
            } else {
                // 如果获取用户目录失败，使用当前目录
                GetCurrentDirectoryA(MAX_PATH, user_path);
                logger->debug("Using current directory: %s", user_path);
            }
            
            // 创建文件路径
            std::string shared_memory_dir = std::string(user_path) + "\\SharedMemory";
            file_path = shared_memory_dir + "\\skyline_" + key + ".dat";
            
            // 确保目录存在
            create_directory(shared_memory_dir);
        }
        
        HANDLE file_handle = INVALID_HANDLE_VALUE;
        LARGE_INTEGER existing_size{};
        bool created_file = false;
        try {
            // 创建或打开文件
            if (create) {
                // 创建新文件
                file_handle = CreateFileA(
                    file_path.c_str(),
                    GENERIC_READ | GENERIC_WRITE | DELETE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                
                if (file_handle == INVALID_HANDLE_VALUE) {
                    DWORD error = GetLastError();
                    logger->debug("Failed to create file, error code: %lu", error);
                    throw std::runtime_error("Failed to create file");
                }
                created_file = true;
                
                // 设置文件大小
                LARGE_INTEGER file_size;
                file_size.QuadPart = total_size;
                if (!SetFilePointerEx(file_handle, file_size, NULL, FILE_BEGIN) || 
                    !SetEndOfFile(file_handle)) {
                    DWORD error = GetLastError();
                    logger->debug("Failed to set file size, error code: %lu", error);
                    throw std::runtime_error("Failed to set file size");
                }
            } else {
                // 打开现有文件
                file_handle = CreateFileA(
                    file_path.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                
                if (file_handle == INVALID_HANDLE_VALUE) {
                    DWORD error = GetLastError();
                    logger->debug("Failed to open file, error code: %lu", error);
                    throw std::runtime_error("Failed to open file:" + file_path);
                }
                if (!GetFileSizeEx(file_handle, &existing_size) ||
                    existing_size.QuadPart < static_cast<LONGLONG>(sizeof(SharedMemoryHeader)) ||
                    existing_size.QuadPart > static_cast<LONGLONG>(kMaxSharedMemorySize + sizeof(SharedMemoryHeader))) {
                    throw std::runtime_error("invalid shared memory file size");
                }
            }
            
            // 创建文件映射
            size_t remaining_size = total_size;
            
            // 创建映射
            bool success = create_mapping(file_handle, remaining_size);
            if (!success) {
                throw std::runtime_error("Failed to create mapping");
            }
            
            // 如果是新创建的共享内存，初始化头部
            if (create) {
                SharedMemoryHeader* header = static_cast<SharedMemoryHeader*>(address_);
                header->size = size;
                header->version = 1;
                logger->debug("Initialized shared memory header: size={}, version={}", size, header->version);
            }
            else {
                // 读取头部信息
                SharedMemoryHeader* header = static_cast<SharedMemoryHeader*>(address_);
                size = header->size;
                if (size == 0 || size > kMaxSharedMemorySize) {
                    throw std::runtime_error("Invalid shared memory header size");
                }
                if (existing_size.QuadPart < static_cast<LONGLONG>(size + sizeof(SharedMemoryHeader))) {
                    throw std::runtime_error("shared memory header exceeds file size");
                }
                size_ = size;
                logger->debug("Read shared memory header: size={}, version={}", size, header->version);
                
                // 以头部信息为基准，重新映射
                success = create_mapping(file_handle, size + sizeof(SharedMemoryHeader));
                if (!success) {
                    throw std::runtime_error("Failed to create mapping with header size");
                }
            }
            
            // 存储文件路径
            file_path_ = file_path;
            
            logger->debug("Shared memory {}: key={}, size={}, address={}, file={}", 
                create ? "created" : "opened", 
                key.c_str(), 
                size, 
                address_,
                file_path_.c_str());

            // Transfer ownership only after all potentially-throwing setup.
            file_handle_ = file_handle;
            file_handle = INVALID_HANDLE_VALUE;
                
        } catch (...) {
            // 确保在发生异常时释放资源
            if (address_) {
                UnmapViewOfFile(address_);
                address_ = nullptr;
            }
            
            if (file_mapping_) {
                CloseHandle(file_mapping_);
                file_mapping_ = nullptr;
            }
            if (file_handle != INVALID_HANDLE_VALUE) {
                if (created_file) {
                    FILE_DISPOSITION_INFO disposition{};
                    disposition.DeleteFile = TRUE;
                    SetFileInformationByHandle(
                        file_handle, FileDispositionInfo,
                        &disposition, sizeof(disposition));
                }
                CloseHandle(file_handle);
                file_handle = INVALID_HANDLE_VALUE;
            }
            
            throw;
        }
        
#else
        // Linux实现
        // 创建共享内存名称
        std::string shm_name = "/skyline_" + key + ".dat";
        
        try {
            int flags = O_RDWR;
            if (create) {
                flags |= O_CREAT;
            }
            
            // 创建或打开共享内存
            logger->debug("Call shm_open");
            int fd = shm_open(shm_name.c_str(), flags, 0600);
            if (fd == -1) {
                logger->debug("Failed to open shared memory, error: %s", strerror(errno));

                throw std::runtime_error("Failed to open shared memory");
            }
            
            if (create) {
                // 设置共享内存大小
                if (ftruncate(fd, total_size) == -1) {
                    logger->debug("Failed to set shared memory size, error: %s", strerror(errno));
                    close(fd);

                    throw std::runtime_error("Failed to set shared memory size");
                }
            }
            else {
                struct stat file_stat{};
                if (fstat(fd, &file_stat) != 0 ||
                    file_stat.st_size < static_cast<off_t>(sizeof(SharedMemoryHeader)) ||
                    file_stat.st_size > static_cast<off_t>(kMaxSharedMemorySize + sizeof(SharedMemoryHeader))) {
                    close(fd);
                    throw std::runtime_error("invalid shared memory file size");
                }
                
                logger->debug("Call mmap first.");
                // 映射共享内存
                address_ = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (address_ == MAP_FAILED) {
                    logger->debug("Failed to map shared memory, error: %s", strerror(errno));
                    close(fd);
                    throw std::runtime_error("Failed to map shared memory");
                }
                // 读取头部信息
                SharedMemoryHeader* header = static_cast<SharedMemoryHeader*>(address_);
                size = header->size;
                if (size == 0 || size > kMaxSharedMemorySize ||
                    size + sizeof(SharedMemoryHeader) > static_cast<size_t>(file_stat.st_size)) {
                    munmap(address_, total_size);
                    address_ = nullptr;
                    close(fd);
                    throw std::runtime_error("Invalid shared memory header size");
                }
                size_ = size;
                logger->debug("Read shared memory header: size=%zu, version=%d", size, header->version);
                logger->debug("Call munmap.");
                munmap(address_, total_size);
                logger->debug("Call munmap end.");
                total_size = size + sizeof(SharedMemoryHeader);
            }
            
            // 映射共享内存
            logger->debug("Call mmap second.");
            address_ = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close(fd);
            
            if (address_ == MAP_FAILED) {
                logger->debug("Failed to map shared memory, error: %s", strerror(errno));

                throw std::runtime_error("Failed to map shared memory");
            }
            
            // 如果是新创建的共享内存，初始化头部
            if (create) {
                SharedMemoryHeader* header = static_cast<SharedMemoryHeader*>(address_);
                header->size = size;
                header->version = 1;
                logger->debug("Initialized shared memory header: size=%zu, version=%d", size, header->version);
            }
            
            // 存储共享内存名称
            file_path_ = shm_name;
            
            logger->debug("Shared memory %s: key=%s, size=%zu, address=%p", 
                create ? "created" : "opened", 
                key.c_str(), 
                size, 
                address_);
                
        } catch (...) {
            // 确保在发生异常时释放资源
            if (address_ && address_ != MAP_FAILED) {
                munmap(address_, total_size);
                address_ = nullptr;
            }
            
            throw;
        }
        
#endif
    }
    
    SharedMemoryManager::~SharedMemoryManager() {
        printf("Destroying shared memory manager: key=%s, file=%s\n", 
            key_.c_str(), 
            file_path_.c_str());
#ifdef _WIN32
        // Windows实现
        // 释放资源
        if (address_) {
            UnmapViewOfFile(address_);
            address_ = nullptr;
        }
        
        if (file_mapping_) {
            CloseHandle(file_mapping_);
            file_mapping_ = nullptr;
        }
        
        if (file_handle_ != INVALID_HANDLE_VALUE) {
            if (remove_on_destroy_) {
                FILE_DISPOSITION_INFO disposition{};
                disposition.DeleteFile = TRUE;
                if (!SetFileInformationByHandle(
                        file_handle_, FileDispositionInfo,
                        &disposition, sizeof(disposition))) {
                    logger->debug(
                        "Failed to delete shared memory file by handle, error code: %lu",
                        GetLastError());
                }
            }
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
        }
#else
        // Linux实现
        // 释放资源
        if (address_ && address_ != MAP_FAILED) {
            size_t total_size = sizeof(SharedMemoryHeader) + size_;
            munmap(address_, total_size);
            address_ = nullptr;
        }
        
#endif
        
        printf("Shared memory manager destroyed: key=%s, file=%s\n", 
            key_.c_str(), 
            file_path_.c_str());
    }

    #ifdef _WIN32
    bool SharedMemoryManager::retire_backing_file() {
        if (file_path_.empty() || !canonical_file_path_.empty()) {
            return false;
        }

        const size_t separator = file_path_.find_last_of("\\/");
        const std::string directory = separator == std::string::npos
            ? std::string()
            : file_path_.substr(0, separator + 1);
        for (size_t attempt = 0; attempt < 32; ++attempt) {
            const auto counter = retired_file_counter.fetch_add(1);
            const std::string retired_path = directory +
                ".skyline-retired-" + std::to_string(GetCurrentProcessId()) +
                "-" + std::to_string(reinterpret_cast<uintptr_t>(this)) +
                "-" + std::to_string(counter) + ".dat";
            std::string canonical_path = file_path_;
            std::string owned_retired_path = retired_path;
            HANDLE rename_handle = CreateFileA(
                file_path_.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (rename_handle == INVALID_HANDLE_VALUE) {
                logger->debug(
                    "Failed to open shared memory file for retirement, error code: %lu",
                    GetLastError());
                return false;
            }
            if (!file_handles_match(file_handle_, rename_handle)) {
                CloseHandle(rename_handle);
                logger->debug("Canonical shared memory path changed before retirement");
                return false;
            }
            if (rename_file_handle(rename_handle, retired_path)) {
                canonical_file_path_.swap(canonical_path);
                file_path_.swap(owned_retired_path);
                remove_on_destroy_before_retire_ = remove_on_destroy_;
                remove_on_destroy_ = true;
                CloseHandle(file_handle_);
                file_handle_ = rename_handle;
                return true;
            }

            const DWORD error = GetLastError();
            CloseHandle(rename_handle);
            if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
                logger->debug("Failed to retire shared memory file, error code: %lu", error);
                return false;
            }
        }
        return false;
    }

    bool SharedMemoryManager::restore_backing_file_if_missing() {
        if (canonical_file_path_.empty()) {
            return false;
        }
        if (!rename_file_handle(file_handle_, canonical_file_path_)) {
            logger->debug("Failed to restore shared memory file, error code: %lu", GetLastError());
            return false;
        }
        file_path_.swap(canonical_file_path_);
        canonical_file_path_.clear();
        remove_on_destroy_ = remove_on_destroy_before_retire_;
        return true;
    }

    bool SharedMemoryManager::create_mapping(HANDLE file_handle, size_t mapping_size) {
        // 如果已存在映射，先清理
        if (file_mapping_) {
            CloseHandle(file_mapping_);
            file_mapping_ = nullptr;
        }
        if (address_) {
            UnmapViewOfFile(address_);
            address_ = nullptr;
        }
        
        // 创建文件映射
        file_mapping_ = CreateFileMappingA(
            file_handle,          // 使用实际文件
            NULL,                 // 默认安全属性
            PAGE_READWRITE,       // 读写权限
            0,                    // 最大大小的高32位
            mapping_size,         // 最大大小的低32位
            NULL                  // 不使用命名映射
        );
        
        if (!file_mapping_) {
            DWORD error = GetLastError();
            logger->debug("Failed to create file mapping, error code: %lu, size: %zu", error, mapping_size);
            return false;
        }
        
        // 映射视图
        address_ = MapViewOfFile(
            file_mapping_,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            mapping_size
        );
        
        if (!address_) {
            DWORD error = GetLastError();
            logger->debug("Failed to map view of file, error code: %lu, size: %zu", error, mapping_size);
            CloseHandle(file_mapping_);
            file_mapping_ = nullptr;
            return false;
        }
        
        return true;
    }
    #endif

}
