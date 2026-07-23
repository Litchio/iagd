/*
 * DebugLog.h - Loader-lock-safe, CRT-init-safe debug logger for the dinput8 proxy.
 *
 * Why this exists:
 *  - The regular HookLog uses std::wofstream and depends on %appdata% folder
 *    resolution + directory existence. It cannot be trusted during CRT static
 *    initialization, inside DllMain (loader lock), or when the game hangs.
 *  - This logger uses ONLY Win32 calls (CreateFileA/WriteFile), opens and
 *    closes the file per line (no buffering to lose on a hang/crash), and
 *    resolves its own path from the module handle - so the log always lands
 *    next to dinput8.dll (i.e. the Grim Dawn x64 folder), no config needed.
 *
 * Usage: DBGLOG("format %d", value);
 */
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace GdiaDebugLog {

	// Function-local statics: constructed on first use, which makes this safe
	// to call from CRT static initializers regardless of TU init order.
	inline CRITICAL_SECTION& Lock() {
		static CRITICAL_SECTION cs;
		static bool initialized = []() { InitializeCriticalSection(&cs); return true; }();
		(void)initialized;
		return cs;
	}

	inline const char* Path() {
		static char path[MAX_PATH] = { 0 };
		if (path[0] == 0) {
			HMODULE self = NULL;
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCSTR)&Path, &self) && self != NULL) {
				char modPath[MAX_PATH];
				DWORD len = GetModuleFileNameA(self, modPath, MAX_PATH);
				if (len > 0 && len < MAX_PATH) {
					for (DWORD i = len; i > 0; --i) {
						if (modPath[i - 1] == '\\' || modPath[i - 1] == '/') {
							modPath[i] = 0;
							break;
						}
					}
					_snprintf(path, MAX_PATH - 1, "%sdinput8_debug.log", modPath);
					path[MAX_PATH - 1] = 0;
				}
			}
			if (path[0] == 0) {
				lstrcpyA(path, "C:\\dinput8_debug.log");
			}
		}
		return path;
	}

	inline void WriteRaw(const char* line, size_t len) {
		EnterCriticalSection(&Lock());
		// Try the primary path (next to the DLL), then %TEMP%, then C:\.
		// Guarantees we get a signal even if the game folder is not writable.
		HANDLE h = CreateFileA(Path(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			char tmp[MAX_PATH];
			DWORD n = GetTempPathA(MAX_PATH, tmp);
			if (n > 0 && n < MAX_PATH - 20) {
				lstrcatA(tmp, "dinput8_debug.log");
				h = CreateFileA(tmp, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			}
		}
		if (h == INVALID_HANDLE_VALUE) {
			h = CreateFileA("C:\\dinput8_debug.log", FILE_APPEND_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		}
		if (h != INVALID_HANDLE_VALUE) {
			DWORD written = 0;
			WriteFile(h, line, (DWORD)len, &written, NULL);
			CloseHandle(h);
		}
		LeaveCriticalSection(&Lock());
		OutputDebugStringA(line);
	}

	inline void Log(const char* fmt, ...) {
		char msg[2048];
		va_list args;
		va_start(args, fmt);
		int n = _vsnprintf(msg, sizeof(msg) - 1, fmt, args);
		va_end(args);
		if (n < 0) n = 0;
		msg[sizeof(msg) - 1] = 0;

		char line[2400];
		int m = _snprintf(line, sizeof(line) - 1, "[%10lu ms][pid %5lu tid %5lu] %s\n",
			(unsigned long)GetTickCount(),
			(unsigned long)GetCurrentProcessId(),
			(unsigned long)GetCurrentThreadId(),
			msg);
		line[sizeof(line) - 1] = 0;
		if (m < 0) m = (int)std::strlen(line);

		WriteRaw(line, std::strlen(line));
	}

} // namespace GdiaDebugLog

#define DBGLOG(...) ::GdiaDebugLog::Log(__VA_ARGS__)