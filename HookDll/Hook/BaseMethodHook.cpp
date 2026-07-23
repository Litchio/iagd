
#include "BaseMethodHook.h"
#include "MessageType.h"
#include "GrimTypes.h"
#include "DebugLog.h"
#include <detours.h>

BaseMethodHook::BaseMethodHook() = default;
BaseMethodHook::BaseMethodHook(DataQueue* dataQueue, HANDLE hEvent) {}
void BaseMethodHook::EnableHook() {}
void BaseMethodHook::DisableHook() {}

void BaseMethodHook::ReportHookError(DataQueue* m_dataQueue, HANDLE m_hEvent, int id) {
	DataItemPtr item(new DataItem(TYPE_ERROR_HOOKING_GENERIC, sizeof(id), (char*)&id));
	m_dataQueue->push(item);
	SetEvent(m_hEvent);
}

void BaseMethodHook::ReportHookSuccess(DataQueue* m_dataQueue, HANDLE m_hEvent, int id) {
	DataItemPtr item(new DataItem(TYPE_SUCCESS_HOOKING_GENERIC, sizeof(id), (char*)&id));
	m_dataQueue->push(item);
	SetEvent(m_hEvent);
}

void BaseMethodHook::TransferData(unsigned int size, const char* data) {
	DataItemPtr item(new DataItem(m_messageId, size, data));
	m_dataQueue->push(item);
	SetEvent(m_hEvent);
}

void* BaseMethodHook::HookDll(const wchar_t* dll, char* procAddress, void* HookedMethod, DataQueue* m_dataQueue, HANDLE m_hEvent, int id) {
	void* originalMethod = GetProcAddressOrLogToFile(dll, procAddress);
	m_messageId = id;
	if (originalMethod == NULL) {
		// CRITICAL: never call DetourAttach with a NULL target. The old code
		// attached anyway and ignored every Detours return code.
		DBGLOG("HookDll: SKIPPING hook of %s - export not found in %ls", procAddress, dll);
		ReportHookError(m_dataQueue, m_hEvent, id);
		return nullptr;
	}

	DBGLOG("HookDll: attaching %ls!%s at %p -> hook %p", dll, procAddress, originalMethod, HookedMethod);

	LONG rBegin = DetourTransactionBegin();
	LONG rUpdate = (rBegin == NO_ERROR) ? DetourUpdateThread(GetCurrentThread()) : -1;
	LONG rAttach = (rUpdate == NO_ERROR) ? DetourAttach((PVOID*)&originalMethod, HookedMethod) : -1;
	LONG rCommit = (rAttach == NO_ERROR) ? DetourTransactionCommit() : DetourTransactionAbort();

	if (rBegin != NO_ERROR || rUpdate != NO_ERROR || rAttach != NO_ERROR || rCommit != NO_ERROR) {
		DBGLOG("HookDll: DETOURS FAILURE hooking %s (begin=%ld update=%ld attach=%ld commit=%ld)",
			procAddress, rBegin, rUpdate, rAttach, rCommit);
		ReportHookError(m_dataQueue, m_hEvent, id);
		return nullptr;
	}

	// originalMethod now points at the trampoline (call it to invoke the real function)
	DBGLOG("HookDll: SUCCESS hooking %s, trampoline=%p", procAddress, originalMethod);
	ReportHookSuccess(m_dataQueue, m_hEvent, id);
	return originalMethod;
}

void* BaseMethodHook::HookGame(char* procAddress, void* HookedMethod, DataQueue* m_dataQueue, HANDLE m_hEvent, int id) {
	return HookDll(L"Game.dll", procAddress, HookedMethod, m_dataQueue, m_hEvent, id);
}

void* BaseMethodHook::HookEngine(char* procAddress, void* HookedMethod, DataQueue* m_dataQueue, HANDLE m_hEvent, int id) {
	return HookDll(L"Engine.dll", procAddress, HookedMethod, m_dataQueue, m_hEvent, id);
}

void BaseMethodHook::Unhook(void* originalMethod, void* Method) {
	if (originalMethod == nullptr) {
		return;
	}
	LONG res1 = DetourTransactionBegin();
	LONG res2 = DetourUpdateThread(GetCurrentThread());
	LONG res3 = DetourDetach((PVOID*)&originalMethod, Method);
	LONG res4 = DetourTransactionCommit();
	if (res1 != NO_ERROR || res2 != NO_ERROR || res3 != NO_ERROR || res4 != NO_ERROR) {
		DBGLOG("Unhook: DETOURS failure (begin=%ld update=%ld detach=%ld commit=%ld)", res1, res2, res3, res4);
	}
}
