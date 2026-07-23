#include "stdafx.h"
#include "HookConfig.h"
#include "DebugLog.h"
#include "nlohmann/json.hpp"
#include <windows.h>
#include <fstream>

HookConfig g_hookConfig;

static std::string GetSelfDllDirectory() {
	HMODULE self = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&GetSelfDllDirectory, &self) || self == NULL) {
		return "";
	}
	char modPath[MAX_PATH];
	DWORD len = GetModuleFileNameA(self, modPath, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) {
		return "";
	}
	for (DWORD i = len; i > 0; --i) {
		if (modPath[i - 1] == '\\' || modPath[i - 1] == '/') {
			modPath[i] = 0;
			break;
		}
	}
	return std::string(modPath);
}

void LoadHookConfig() {
	std::string configPath = GetSelfDllDirectory() + "dinput8_config.json";
	DBGLOG("LoadHookConfig: looking for %s", configPath.c_str());

	std::ifstream file(configPath);
	if (!file.is_open()) {
		DBGLOG("LoadHookConfig: no config file, using defaults (all hooks enabled)");
		return;
	}

	try {
		nlohmann::json root;
		file >> root;

		auto readBool = [&root](const char* key, bool& target) {
			if (root.contains(key) && root[key].is_boolean()) {
				target = root[key].get<bool>();
			}
		};

		readBool("passthroughOnly", g_hookConfig.passthroughOnly);
		readBool("hookInstaloot", g_hookConfig.hookInstaloot);
		readBool("hookGameEngineUpdate", g_hookConfig.hookGameEngineUpdate);
		readBool("hookEngineRender", g_hookConfig.hookEngineRender);
		readBool("startSeedInfoThread", g_hookConfig.startSeedInfoThread);
		readBool("startTcpClient", g_hookConfig.startTcpClient);
	}
	catch (...) {
		DBGLOG("LoadHookConfig: JSON parse error in %s - using defaults", configPath.c_str());
	}

	DBGLOG("LoadHookConfig: passthroughOnly=%d instaloot=%d update=%d render=%d seedThread=%d tcp=%d",
		(int)g_hookConfig.passthroughOnly,
		(int)g_hookConfig.hookInstaloot,
		(int)g_hookConfig.hookGameEngineUpdate,
		(int)g_hookConfig.hookEngineRender,
		(int)g_hookConfig.startSeedInfoThread,
		(int)g_hookConfig.startTcpClient);
}