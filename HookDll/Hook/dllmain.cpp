#include "stdafx.h"
#include <chrono>
#include <codecvt> // wstring_convert
#include <windows.h>
#include <stdlib.h>
#include <objbase.h>
#include <fstream>
#include <thread>
#include "DataQueue.h"
#include "MessageType.h"
#include "InventorySack_AddItem.h"
#include "Exports.h"
#include "OnDemandSeedInfo.h"
#include "GameEngineUpdate.h"
#include "HookLog.h"
#include "Logger.h"
#include "SetHardcore.h"
#include "SettingsReader.h"
#include "TcpClient.h"
#include "JsonSerializer.h"
#include "DebugLog.h"
#include "HookConfig.h"

// Construct-on-first-use logger. The old global "HookLog g_log;" had undefined
// construction order relative to static initializers in other translation units
// that call LogToFile - UB during CRT init.
HookLog& GetHookLog() {
	static HookLog instance;
	return instance;
}
#define g_log GetHookLog()

// CRT static-init reachability marker: proves in dinput8_debug.log that the
// CRT ran our static initializers (and survived them) before DllMain.
static struct CrtInitMarker {
	CrtInitMarker() { DBGLOG("CRT static initializers executing (pre-DllMain)"); }
} g_crtInitMarker;

#pragma region Variables
// Switches hook logging on/off
#if 1
#define LOG(streamdef) \
{ \
    std::wstring msg = (((std::wostringstream&)(std::wostringstream().flush() << streamdef)).str()); \
	g_log.out(logStartupTime() + msg); \
    msg += _T("\n"); \
    OutputDebugString(msg.c_str()); \
}
#else
#define LOG(streamdef) \
    __noop;
#endif




DWORD g_lastThreadTick = 0;
HANDLE g_hEvent;
HANDLE g_thread;

DataQueue g_dataQueue;
InventorySack_AddItem* g_InventorySack_AddItemInstance = NULL;
TcpClientPtr g_tcpClient = nullptr;  // TCP client for Rust backend

HWND g_targetWnd = NULL;

bool g_isRunningInWine = false;
std::wstring g_linuxHackFolder;

#pragma endregion

#pragma region CORE


std::wstring logStartupTime() {
	__time64_t rawtime;
	struct tm timeinfo;
	wchar_t buffer[80];

	_time64(&rawtime);
	localtime_s(&timeinfo, &rawtime);

	wcsftime(buffer, sizeof(buffer), L"%Y-%m-%d %H:%M:%S ", &timeinfo);
	std::wstring str(buffer);

	return str;
}

std::string logStartupTimeChar() {
	__time64_t rawtime;
	struct tm timeinfo;
	char buffer[80];

	_time64(&rawtime);
	localtime_s(&timeinfo, &rawtime);

	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", &timeinfo);
	std::string str(buffer);

	return str;
}

std::wstring LogLevelToString(LogLevel level) {
	switch (level) {
	case LogLevel::INFO:
		return L"INFO ";

	case LogLevel::WARNING:
		return L"WARN ";

	case LogLevel::FATAL:
		return L"ERROR ";
	}

	return L"UNKNOWN";
}
std::string LogLevelToStringA(LogLevel level) {
	switch (level) {
	case LogLevel::INFO:
		return "INFO ";

	case LogLevel::WARNING:
		return "WARN ";

	case LogLevel::FATAL:
		return "ERROR ";
	}

	return "UNKNOWN";
}
void LogToFile(LogLevel level, const wchar_t* message) {
	g_log.out(logStartupTime() + LogLevelToString(level) + message);
}
void LogToFile(LogLevel level, const char* message) {
	g_log.out((logStartupTimeChar() + LogLevelToStringA(level).c_str() + std::string(message)).c_str());
}
void LogToFile(LogLevel level, const std::string message) {
	g_log.out((logStartupTimeChar() + LogLevelToStringA(level) + message).c_str());
}
void LogToFile(LogLevel level, std::wstring message) {
	g_log.out(logStartupTime() + LogLevelToString(level) + message);
}
void LogToFile(LogLevel level, std::wstringstream message) {
	g_log.out(logStartupTime() + LogLevelToString(level) + message.str());
}


// Wine/Proton file-based IPC: write message as binary file to linuxhack folder
// Format: [int32 type][int32 dataLength][raw data bytes]
// Write as .tmp then rename to .msg for atomic visibility
void WriteMessageToFile(DWORD dwData, void* lpData, DWORD cbData) {
	GUID guid;
	if (CoCreateGuid(&guid) != S_OK) {
		LogToFile(LogLevel::FATAL, L"Failed to create GUID for message file");
		return;
	}

	wchar_t guidStr[64];
	swprintf_s(guidStr, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

	std::wstring tmpPath = g_linuxHackFolder + guidStr + L".tmp";
	std::wstring msgPath = g_linuxHackFolder + guidStr + L".msg";

	HANDLE hFile = CreateFile(tmpPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		LogToFile(LogLevel::FATAL, L"Failed to create temp message file: " + tmpPath);
		return;
	}

	DWORD written;
	int32_t type = static_cast<int32_t>(dwData);
	int32_t dataLength = static_cast<int32_t>(cbData);

	WriteFile(hFile, &type, sizeof(type), &written, NULL);
	WriteFile(hFile, &dataLength, sizeof(dataLength), &written, NULL);
	if (cbData > 0 && lpData != nullptr) {
		WriteFile(hFile, lpData, cbData, &written, NULL);
	}
	CloseHandle(hFile);

	if (!MoveFile(tmpPath.c_str(), msgPath.c_str())) {
		LogToFile(LogLevel::FATAL, L"Failed to move message file to: " + msgPath);
		DeleteFile(tmpPath.c_str());
	}
}


/// Thread function that dispatches queued message blocks to the Rust backend via TCP
void WorkerThreadMethod() {
	try {
		bool wineSetActiveDone = false;
		LogToFile(LogLevel::INFO, L"Worker thread started, connecting to TCP server at 127.0.0.1:1337");

		while ((g_hEvent != NULL) && (WaitForSingleObject(g_hEvent, INFINITE) == WAIT_OBJECT_0)) {
			if (g_hEvent == NULL) {
				break;
			}

			if (g_isRunningInWine) {
				// In Wine mode, keep InventorySack_AddItem permanently active (no FindWindow)
				if (!wineSetActiveDone && g_InventorySack_AddItemInstance != NULL) {
					g_InventorySack_AddItemInstance->SetActive(true);
					wineSetActiveDone = true;
				}
			}
			else {
				DWORD tick = GetTickCount();
				if (tick < g_lastThreadTick) {
					// Overflow
					g_lastThreadTick = tick;
				}

				if ((tick - g_lastThreadTick > 1000) || (g_targetWnd == NULL)) {
					g_targetWnd = FindWindow(L"GDIAWindowClass", NULL);
					g_lastThreadTick = GetTickCount();

					if (g_InventorySack_AddItemInstance != NULL) {
						g_InventorySack_AddItemInstance->SetActive(g_targetWnd != NULL);
					}
				}
			}

			while (!g_dataQueue.empty()) {
				DataItemPtr item = g_dataQueue.pop();

				// Send via TCP to Rust backend
				if (g_tcpClient != nullptr) {
					std::string json = JsonSerializer::MessageToJson(
						static_cast<MessageType>(item->type()),
						item->data(),
						item->size()
					);
					if (!g_tcpClient->SendMessage(json)) {
						LogToFile(LogLevel::WARNING, L"Failed to send message via TCP");
					}
				}
			}

		}
	}
	catch (std::exception& ex) {
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());
		LogToFile(LogLevel::FATAL, L"ERROR In the worker thread.." + wide);
	}
	catch (...) {
		LogToFile(LogLevel::FATAL, L"ERROR In the worker thread.. (triple-dot)");
	}
}

OnDemandSeedInfo* listener = nullptr;
unsigned __stdcall WorkerThreadMethodWrap(void* argss) {


	LogToFile(LogLevel::INFO, L"Initiating class for seed info..");

	if (listener != nullptr && g_hookConfig.startSeedInfoThread) {
		listener->Start();
	}
	WorkerThreadMethod();
	return 0;
}

void StartWorkerThread() {
	LogToFile(LogLevel::INFO, L"Starting worker thread..");
	unsigned int pid;
	g_thread = (HANDLE)_beginthreadex(NULL, 0, &WorkerThreadMethodWrap, NULL, 0, &pid);


	DataItemPtr item(new DataItem(TYPE_REPORT_WORKER_THREAD_LAUNCHED, 0, NULL));
	g_dataQueue.push(item);
	SetEvent(g_hEvent);
	LogToFile(LogLevel::INFO, L"Started worker thread..");
}


void EndWorkerThread() {
	LogToFile(LogLevel::INFO, L"Ending worker thread..");
	if (g_hEvent != NULL) {
		SetEvent(g_hEvent);
		HANDLE h = g_hEvent;

		g_hEvent = NULL;
		Sleep(1500); // The worker thread might have just read from g_hEvent, seen that it is not NULL, then sent it in to WaitForSingleObject right after we close it.		
		CloseHandle(h);

		//WaitForSingleObject(g_thread, INFINITE);
		CloseHandle(g_thread);
	}
}

#pragma endregion

static void ConfigureInstalootHooks(std::vector<BaseMethodHook*>& hooks) {
	try {
		LogToFile(LogLevel::INFO, L"Configuring instaloot hook..");
		g_InventorySack_AddItemInstance = new InventorySack_AddItem(&g_dataQueue, g_hEvent);
		hooks.push_back(g_InventorySack_AddItemInstance); // Includes GetPrivateStash internally

		// In Wine mode, set active immediately since there's no FindWindow
		if (g_isRunningInWine && g_InventorySack_AddItemInstance != NULL) {
			g_InventorySack_AddItemInstance->SetActive(true);
		}
	}
	catch (std::exception& ex) {
		// For now just let it be. Known issue inside InventorySack_AddItem

		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());

		LogToFile(LogLevel::FATAL, L"ERROR Configuring instaloot hook.." + wide);

	}
	catch (...) {
		// For now just let it be. Known issue inside InventorySack_AddItem
		LogToFile(LogLevel::FATAL, L"ERROR Configuring instaloot hook.. (triple-dot)");
	}

	LogToFile(LogLevel::INFO, L"Configuring hc detection hook..");
	hooks.push_back(new SetHardcore(&g_dataQueue, g_hEvent));
}
/*
bool GetProductAndVersion()
{
	// get the filename of the executable containing the version resource
	TCHAR szFilename[MAX_PATH + 1] = { 0 };
	if (GetModuleFileName(NULL, szFilename, MAX_PATH) == 0)
	{
		LogToFile("GetModuleFileName failed with error");
		return false;
	}

	// allocate a block of memory for the version info
	DWORD dummy;
	DWORD dwSize = GetFileVersionInfoSize(szFilename, &dummy);
	if (dwSize == 0)
	{
		LogToFile(L"GetFileVersionInfoSize failed with error");
		return false;
	}
	std::vector<BYTE> data(dwSize);

	// load the version info
	if (!GetFileVersionInfo(szFilename, NULL, dwSize, &data[0]))
	{
		LogToFile("GetFileVersionInfo failed with error");
		return false;
	}

	// get the name and version strings
	LPVOID pvProductName = NULL;
	unsigned int iProductNameLen = 0;
	LPVOID pvProductVersion = NULL;
	unsigned int iProductVersionLen = 0;

	// replace "040904e4" with the language ID of your resources
	if (!VerQueryValue(&data[0], _T("\\StringFileInfo\\040904e4\\ProductName"), &pvProductName, &iProductNameLen) ||
		!VerQueryValue(&data[0], _T("\\StringFileInfo\\040904e4\\ProductVersion"), &pvProductVersion, &iProductVersionLen))
	{
		LogToFile("Can't obtain ProductName and ProductVersion from resources");
		return false;
	}

	LogToFile((wchar_t*)pvProductVersion);

	//strProductName.SetString((LPCSTR)pvProductName, iProductNameLen);
	//strProductVersion.SetString((LPCSTR)pvProductVersion, iProductVersionLen);

	return true;
}
*/

void ReportCancelledInjection() {
	if (g_isRunningInWine) {
		WriteMessageToFile(TYPE_INJECTION_CANCELLED, nullptr, 0);
		return;
	}

	auto hwnd = FindWindow(L"GDIAWindowClass", NULL);
	if (hwnd == nullptr) {
		return;
	}

	COPYDATASTRUCT data;
	data.dwData = TYPE_INJECTION_CANCELLED;
	data.lpData = nullptr;
	data.cbData = 0;

	// To avoid blocking the main thread, we should not have a lock on the queue while we process the message.
	SendMessage(hwnd, WM_COPYDATA, 0, (LPARAM)&data);
	auto lastErrorCode = GetLastError();
	if (lastErrorCode != 0)
		LOG(L"After SendMessage error code is " << lastErrorCode);
}

std::vector<BaseMethodHook*> hooks;
std::wstring GetIagdFolder();

/// Detect Wine/Proton without relying on settings.json: Wine's ntdll exports
/// wine_get_version. This makes Wine mode work even on a fresh prefix where
/// nobody created %appdata%\..\local\evilsoft\iagd\settings.json.
static bool DetectWine() {
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	return ntdll != NULL && GetProcAddress(ntdll, "wine_get_version") != NULL;
}

/// <summary>
/// Deferred initialization - runs on a detached thread, OUTSIDE the loader
/// lock. All potentially blocking or thread-suspending work (waiting for the
/// game engine, Detours transactions, Winsock connect) belongs here, never in
/// DllMain. The old code did everything inside DllMain and ABORTED when the
/// game wasn't fully up yet; this version instead WAITS for the game.
/// </summary>
static void DeferredInit() {
	DBGLOG("DeferredInit: thread started, waiting for game.dll + engine.dll...");

	// Phase 1: wait for the game modules to be mapped (up to 120 s).
	{
		int waitedMs = 0;
		const int maxWaitMs = 120000;
		while (waitedMs < maxWaitMs &&
			(GetModuleHandleW(L"game.dll") == NULL || GetModuleHandleW(L"engine.dll") == NULL)) {
			Sleep(250);
			waitedMs += 250;
		}
		DBGLOG("DeferredInit: game.dll=%p engine.dll=%p after ~%d ms",
			(void*)GetModuleHandleW(L"game.dll"), (void*)GetModuleHandleW(L"engine.dll"), waitedMs);
		if (waitedMs >= maxWaitMs) {
			DBGLOG("DeferredInit: TIMEOUT waiting for game modules - hooks NOT installed");
			LogToFile(LogLevel::FATAL, L"Timed out waiting for game.dll/engine.dll, aborting injection..");
			ReportCancelledInjection();
			return;
		}
	}

	// Phase 2: resolve all game/engine exports (retry while incomplete).
	{
		int attempts = 0;
		while (!ResolveGameApi() && attempts < 60) {
			Sleep(500);
			attempts++;
		}
		if (!IsGameApiResolved()) {
			DBGLOG("DeferredInit: game API resolution FAILED - wrong game version? Hooks NOT installed.");
			LogToFile(LogLevel::FATAL, L"Could not resolve game exports (wrong game version?), aborting injection..");
			ReportCancelledInjection();
			return;
		}
	}
	DBGLOG("DeferredInit: game API resolved");

	// Phase 3: wait until the engine reports online (up to 180 s).
	{
		int waitedMs = 0;
		const int maxWaitMs = 180000;
		bool online = false;
		while (waitedMs < maxWaitMs) {
			GAME::GameEngine* gameEngine = fnGetGameEngine();
			if (gameEngine != nullptr && IsGameLoading != nullptr && IsGameEngineOnline != nullptr) {
				bool loading = IsGameLoading(gameEngine);
				bool isOnline = IsGameEngineOnline(gameEngine);
				if (!loading && isOnline) {
					online = true;
					break;
				}
			}
			Sleep(500);
			waitedMs += 500;
		}
		DBGLOG("DeferredInit: engine online=%d after ~%d ms", (int)online, waitedMs);
		if (!online) {
			LogToFile(LogLevel::FATAL, L"Game engine never came online, aborting injection..");
			ReportCancelledInjection();
			return;
		}
	}

	LogToFile(LogLevel::INFO, L"Game is running and engine online, installing hooks.");
	g_hEvent = CreateEvent(NULL, FALSE, FALSE, L"IA_Worker");

	if (g_hookConfig.hookInstaloot) {
		ConfigureInstalootHooks(hooks);
	}
	else {
		DBGLOG("DeferredInit: instaloot hooks DISABLED by config");
	}

	if (g_hookConfig.hookEngineRender || g_hookConfig.startSeedInfoThread) {
		listener = new OnDemandSeedInfo(&g_dataQueue, g_hEvent);
		if (listener != nullptr) {
			hooks.push_back(listener);
		}
	}
	else {
		DBGLOG("DeferredInit: render hook + seed info DISABLED by config");
	}

	LogToFile(LogLevel::INFO, L"Starting hook enabling.. " + std::to_wstring(hooks.size()) + L" hooks.");
	for (unsigned int i = 0; i < hooks.size(); i++) {
		DBGLOG("DeferredInit: enabling hook %u of %zu", i, hooks.size());
		hooks[i]->EnableHook();
	}
	DBGLOG("DeferredInit: hook enabling done");

	if (g_hookConfig.startTcpClient) {
		g_tcpClient = std::make_shared<TcpClient>("127.0.0.1", 1337);
		std::thread tcpConnectThread([]() {
			DBGLOG("TcpConnect: thread started");
			try {
				if (!g_tcpClient->Connect()) {
					DBGLOG("TcpConnect: Connect() failed (backend not running?)");
					LogToFile(LogLevel::WARNING, L"Failed to connect to TCP server, will retry on first message");
				}
				else {
					DBGLOG("TcpConnect: connected to 127.0.0.1:1337");
				}
			}
			catch (...) {
				DBGLOG("TcpConnect: EXCEPTION during connect");
				LogToFile(LogLevel::WARNING, L"Exception during TCP connection attempt");
			}
			});
		tcpConnectThread.detach();
	}
	else {
		DBGLOG("DeferredInit: TCP client DISABLED by config");
	}

	StartWorkerThread();
	LogToFile(LogLevel::INFO, L"Initialization complete..");
	DBGLOG("DeferredInit: initialization COMPLETE - hooks active");
	g_log.setInitialized(true);
}

int ProcessAttach(HINSTANCE _hModule) {
	DBGLOG("=== ProcessAttach entered (DLL_PROCESS_ATTACH) ===");
	LogToFile(LogLevel::INFO, std::string("DLL Compiled: ") + std::string(__DATE__) + std::string(" ") + std::string(__TIME__));
	LogToFile(LogLevel::INFO, L"Attatching to process..");

	LoadHookConfig();

	bool wineDetected = DetectWine();
	DBGLOG("Wine auto-detect (ntdll!wine_get_version): %s", wineDetected ? "YES" : "no");
	try {
		SettingsReader settingsReader;
		g_isRunningInWine = settingsReader.GetIsRunningInWine() || wineDetected;
	}
	catch (...) {
		LogToFile(LogLevel::WARNING, L"Failed to read Wine setting, using auto-detect result");
		g_isRunningInWine = wineDetected;
	}

	if (g_isRunningInWine) {
		g_linuxHackFolder = GetIagdFolder() + L"linuxhack\\";
		CreateDirectory(g_linuxHackFolder.c_str(), NULL);
		LogToFile(LogLevel::INFO, L"Wine mode enabled, linuxhack folder: " + g_linuxHackFolder);

		// Write PID file to signal successful injection
		DWORD pid = GetCurrentProcessId();
		std::wstring pidFile = g_linuxHackFolder + std::to_wstring(pid) + L".PID";
		HANDLE hPid = CreateFile(pidFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hPid != INVALID_HANDLE_VALUE) {
			CloseHandle(hPid);
			LogToFile(LogLevel::INFO, L"Wrote PID file: " + pidFile);
		}
		else {
			LogToFile(LogLevel::WARNING, L"Failed to write PID file: " + pidFile);
		}
	}

	// PIN THE DLL IN MEMORY - Grim Dawn loads and immediately unloads
	// dinput8.dll to probe for controllers. Pinning keeps our code resident
	// even if the game calls FreeLibrary while our threads are running.
	HMODULE hDummy = NULL;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)&ProcessAttach, &hDummy)) {
		LogToFile(LogLevel::INFO, L"DLL pinned in memory successfully");
		DBGLOG("ProcessAttach: DLL pinned in memory");
	}
	else {
		LogToFile(LogLevel::WARNING, L"Failed to pin DLL in memory (GetModuleHandleEx failed), attempting fallback");
		LoadLibraryA("dinput8.dll");
		DBGLOG("ProcessAttach: DLL pinning fallback via LoadLibrary (err %lu)", (unsigned long)GetLastError());
	}

	if (g_hookConfig.passthroughOnly) {
		DBGLOG("Passthrough-only mode: DLL pinned, NO hooks, NO threads. "
			"If the game still black-screens now, the proxy/export forwarding is the problem.");
		LogToFile(LogLevel::INFO, L"Passthrough-only mode active (dinput8_config.json)");
		return TRUE;
	}

	// ALL heavy work (waiting for engine, Detours, Winsock) happens on a
	// detached thread, outside the loader lock held during DllMain.
	std::thread initThread(DeferredInit);
	initThread.detach();

	DBGLOG("ProcessAttach: returning TRUE, deferred init thread spawned");
	return TRUE;
}


#pragma region Attach_Detatch
int ProcessDetach(HINSTANCE _hModule) {
	// Signal that we are shutting down
	// This message is not at all guaranteed to get sent.

	LOG(L"Detatching DLL..");
	OutputDebugString(L"ProcessDetach");

	// Disconnect TCP client
	if (g_tcpClient != nullptr) {
		g_tcpClient->Disconnect();
		g_tcpClient = nullptr;
	}

	for (unsigned int i = 0; i < hooks.size(); i++) {
		hooks[i]->DisableHook();
		delete hooks[i];
	}
	hooks.clear();

	if (listener != nullptr) {
		listener->Stop();
		delete listener;
		listener = nullptr;
	}

	EndWorkerThread();

	// Best-effort cleanup of PID file in Wine mode
	if (g_isRunningInWine && !g_linuxHackFolder.empty()) {
		DWORD pid = GetCurrentProcessId();
		std::wstring pidFile = g_linuxHackFolder + std::to_wstring(pid) + L".PID";
		DeleteFile(pidFile.c_str());
	}

	LOG(L"DLL detached..");
	return TRUE;
}


BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		return ProcessAttach(hModule);

		case DLL_PROCESS_DETACH:
			return ProcessDetach(hModule);
	}
	return TRUE;
}

// ============================================================================
// DirectInput8 Proxy Exports - CRITICAL for the game to work with this DLL
// ============================================================================
// Hardened forwarding to the real (Wine builtin) dinput8.dll:
//  - Caches the real module handle.
//  - REFUSES to forward to our own module. The old fallback
//    LoadLibraryA("dinput8.dll") could resolve back to this proxy DLL,
//    causing infinite recursion and a stack overflow (black screen).
//  - Exports the full dinput8 surface (DllGetClassObject etc.), not just
//    DirectInput8Create, so COM-based activation also works.
//  - Logs every step to dinput8_debug.log.

typedef HRESULT(WINAPI* DirectInput8CreateFunc)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
typedef HRESULT(WINAPI* DllGetClassObjectFunc)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT(WINAPI* DllSimpleFunc)();

static HMODULE g_realDinput8 = NULL;

static HMODULE GetOwnModule() {
	HMODULE self = NULL;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetOwnModule, &self);
	return self;
}

static HMODULE GetRealDinput8() {
	if (g_realDinput8 != NULL) {
		return g_realDinput8;
	}

	HMODULE self = GetOwnModule();
	DBGLOG("GetRealDinput8: own module handle = %p", (void*)self);

	// With WINEDLLOVERRIDES="dinput8=n,b", loading the System32 path resolves
	// to Wine's BUILTIN dinput8 (the file there is a builtin placeholder).
	HMODULE hReal = LoadLibraryA("C:\\Windows\\System32\\dinput8.dll");
	DBGLOG("GetRealDinput8: LoadLibrary(System32\\dinput8.dll) -> %p (err %lu)",
		(void*)hReal, (unsigned long)GetLastError());

	if (hReal == self) {
		// Resolved back to US - forwarding would recurse until stack overflow.
		DBGLOG("GetRealDinput8: CRITICAL - System32 path resolved to our own module, refusing");
		hReal = NULL;
	}

	if (hReal == NULL) {
		HMODULE hFallback = LoadLibraryA("dinput8.dll");
		DBGLOG("GetRealDinput8: fallback LoadLibrary(dinput8.dll) -> %p (err %lu)",
			(void*)hFallback, (unsigned long)GetLastError());
		if (hFallback != self) {
			hReal = hFallback;
		}
		else {
			DBGLOG("GetRealDinput8: fallback also resolved to ourselves - giving up");
		}
	}

	g_realDinput8 = hReal;
	return hReal;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
	HINSTANCE hinst,
	DWORD dwVersion,
	REFIID riidltf,
	LPVOID* ppvOut,
	LPUNKNOWN punkOuter
)
{
	DBGLOG("DirectInput8Create: called (hinst=%p version=0x%lx)", (void*)hinst, (unsigned long)dwVersion);
	LogToFile(LogLevel::INFO, L"DirectInput8Create called - forwarding to real dinput8.dll");

	HMODULE hReal = GetRealDinput8();
	if (hReal == NULL) {
		DBGLOG("DirectInput8Create: no real dinput8 available - returning E_FAIL");
		LogToFile(LogLevel::FATAL, L"Critical: Could not load real dinput8.dll at all");
		return E_FAIL;
	}

	DirectInput8CreateFunc pReal = (DirectInput8CreateFunc)GetProcAddress(hReal, "DirectInput8Create");
	if (pReal == nullptr) {
		DBGLOG("DirectInput8Create: GetProcAddress failed on real module (err %lu)", (unsigned long)GetLastError());
		LogToFile(LogLevel::FATAL, L"Critical: Could not get DirectInput8Create from real dinput8.dll");
		return E_FAIL;
	}

	HRESULT hr = pReal(hinst, dwVersion, riidltf, ppvOut, punkOuter);
	DBGLOG("DirectInput8Create: forwarded to real dinput8, hr=0x%08lx", (unsigned long)hr);
	return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
	HMODULE hReal = GetRealDinput8();
	DllSimpleFunc p = hReal ? (DllSimpleFunc)GetProcAddress(hReal, "DllCanUnloadNow") : nullptr;
	return p ? p() : S_FALSE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
	DBGLOG("DllGetClassObject: called");
	HMODULE hReal = GetRealDinput8();
	DllGetClassObjectFunc p = hReal ? (DllGetClassObjectFunc)GetProcAddress(hReal, "DllGetClassObject") : nullptr;
	if (p == nullptr) {
		return CLASS_E_CLASSNOTAVAILABLE;
	}
	return p(rclsid, riid, ppv);
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer() {
	HMODULE hReal = GetRealDinput8();
	DllSimpleFunc p = hReal ? (DllSimpleFunc)GetProcAddress(hReal, "DllRegisterServer") : nullptr;
	return p ? p() : E_FAIL;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer() {
	HMODULE hReal = GetRealDinput8();
	DllSimpleFunc p = hReal ? (DllSimpleFunc)GetProcAddress(hReal, "DllUnregisterServer") : nullptr;
	return p ? p() : E_FAIL;
}

#pragma endregion


