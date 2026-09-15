/*
 * Resolve the stable Node-API imports emitted as a delayed NODE.EXE
 * dependency. Electron ships those symbols from node.dll (and may rename the
 * host executable), so loading NODE.EXE by filename is not reliable.
 *
 * cmake-js ships an equivalent hook for MSVC. This copy is used for direct
 * MinGW cross-builds, where cmake-js cannot detect the target Windows host.
 */
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <delayimp.h>
#include <cstring>

namespace {
    HMODULE node_dll = nullptr;
    HMODULE nw_dll = nullptr;

    FARPROC WINAPI load_exe_hook(unsigned int event, DelayLoadInfo *info) {
        if (!info) {
            return nullptr;
        }

        if (event == dliStartProcessing) {
            node_dll = GetModuleHandleA("node.dll");
            nw_dll = GetModuleHandleA("nw.dll");
            return nullptr;
        }

        if (event == dliNotePreGetProcAddress) {
            if (node_dll && info->dlp.szProcName) {
                FARPROC result = GetProcAddress(node_dll, info->dlp.szProcName);
                if (result) {
                    return result;
                }
            }
            if (nw_dll && info->dlp.szProcName) {
                return GetProcAddress(nw_dll, info->dlp.szProcName);
            }
            return nullptr;
        }

        if (event != dliNotePreLoadLibrary ||
            !info->szDll ||
            _stricmp(info->szDll, "node.exe") != 0) {
            return nullptr;
        }

        // The Electron host has already loaded node.dll before it loads a
        // native addon. Falling back to the process image also supports the
        // standalone Node executable used by the Windows CI smoke test.
        if (!node_dll) {
            node_dll = GetModuleHandleA("node.dll");
        }
        if (!node_dll) {
            node_dll = GetModuleHandleA(nullptr);
        }
        return reinterpret_cast<FARPROC>(node_dll);
    }
}

extern "C" {
    PfnDliHook __pfnDliNotifyHook2 = load_exe_hook;
}

#endif
