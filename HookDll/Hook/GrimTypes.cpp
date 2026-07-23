#include "GrimTypes.h"
#include "Logger.h"
#include "DebugLog.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <windows.h>
#include <fstream>

namespace GAME {
	// Helper: strip \r, \n from a narrow string (defensive against game struct issues)
	static std::string sanitizeCsvField(const char* s) {
		std::string result(s);
		result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
		result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
		return result;
	}

	std::wstring Serialize(GAME::ItemReplicaInfo replica) {
		std::wstringstream stream;
		stream << sanitizeCsvField(replica.baseRecord.c_str()).c_str() << ";";
		stream << sanitizeCsvField(replica.prefixRecord.c_str()).c_str() << ";";
		stream << sanitizeCsvField(replica.suffixRecord.c_str()).c_str() << ";";
		stream << replica.seed << ";";
#ifdef PLAYTEST
		// Rerolls column (playtest offset 0x17c). 
		stream << replica.seedRerolls << ";";
#else
		stream << 0 << ";";
#endif
		stream << sanitizeCsvField(replica.modifierRecord.c_str()).c_str() << ";";
		stream << sanitizeCsvField(replica.materiaRecord.c_str()).c_str() << ";";
		stream << sanitizeCsvField(replica.relicBonus.c_str()).c_str() << ";";
		stream << replica.relicSeed << ";";
		stream << sanitizeCsvField(replica.enchantmentRecord.c_str()).c_str() << ";";
		stream << replica.enchantmentSeed << ";";
		stream << sanitizeCsvField(replica.transmuteRecord.c_str()).c_str() << ";";

#ifdef PLAYTEST
		stream << sanitizeCsvField(replica.ascendant1.c_str()).c_str() << ";";
		stream << sanitizeCsvField(replica.ascendant2.c_str()).c_str() << ";";
#else
		stream << "" << ";";
		stream << "" << ";";
#endif

#ifdef PLAYTEST
		// Affix rerolls column (playtest offset 0x180).
		stream << replica.affixRerolls;
#else
		stream << 0;
#endif

		return stream.str();
	}

	// https://stackoverflow.com/questions/1120140/how-can-i-read-and-parse-csv-files-in-c
	std::vector<std::string> GetNextLineAndSplitIntoTokens(std::istream& str) {
		std::vector<std::string>   result;
		std::string                line;
		std::getline(str, line);

		std::stringstream          lineStream(line);
		std::string                cell;

		while (std::getline(lineStream, cell, ';')) {
			result.push_back(cell);
		}

		// This checks for a trailing semicolon with no data after it.
		if (!lineStream && cell.empty()) {
			// If there was a trailing semicolon then add an empty element.
			result.push_back("");
		}
		return result;
	}

	GAME::ItemReplicaInfo* Deserialize(std::vector<std::string> tokens) {
		// 13 = legacy (no rerolls, no ascendants)
		// 14 = bugfix compat (rerolls present, ascendants split to next line)
		// 16 = rerolls + ascendants
		// 17 = current format (rerolls + ascendants + affixRerolls)
		if (tokens.size() != 13 && tokens.size() != 14 && tokens.size() != 16 && tokens.size() != 17) {
			LogToFile(LogLevel::WARNING, L"Error parsing CSV file, expected 13, 14, 16, or 17 tokens, got " + std::to_wstring(tokens.size()));
			return nullptr;
		}

		bool isNewDlc = tokens.size() >= 14;

		GAME::ItemReplicaInfo* item = new GAME::ItemReplicaInfo();

		// The game's own ItemReplicaInfo constructor defaults stackSize to 1 (verified
		// in the playtest 1.3 binary). Our zero-initialized struct left it at 0, which
		// creates items whose Item::GetStackSize() returns 0. The 1.3 crafting rework
		// counts owned recipe ingredients by summing GetStackSize() over the inventory,
		// so a stackSize=0 item is never recognized as a crafting ingredient (until an
		// inventor reroll re-creates it with a game-built replica). IA never loots
		// stackable items, so 1 is always the correct count for deposited items.
		item->stackSize = 1;

		int idx = 2; // 0: is the mod name, 1: is "isHardcore"
		item->baseRecord = tokens.at(idx++);
		item->prefixRecord = tokens.at(idx++);
		item->suffixRecord = tokens.at(idx++);
		item->seed = (unsigned int)stoul(tokens.at(idx++));
		if (isNewDlc) {
#ifdef PLAYTEST
			// See Serialize(): this column carries the reroll count (offset 0x17c).
			item->seedRerolls = (unsigned int)stoul(tokens.at(idx++));
#else
			auto unused = (unsigned int)stoul(tokens.at(idx++));
#endif
		}
		item->modifierRecord = tokens.at(idx++);
		item->materiaRecord = tokens.at(idx++);
		item->relicBonus = tokens.at(idx++);
		item->relicSeed = (unsigned int)stoul(tokens.at(idx++));
		item->enchantmentRecord = tokens.at(idx++);
		item->enchantmentSeed = (unsigned int)stoul(tokens.at(idx++));
		item->transmuteRecord = tokens.at(idx++);
		if (tokens.size() >= 16) {
#ifdef PLAYTEST
			item->ascendant1 = tokens.at(idx++);
			item->ascendant2 = tokens.at(idx++);
#else
			auto ascendant1 = tokens.at(idx++);
			auto ascendant2 = tokens.at(idx++);
#endif
		}
		if (tokens.size() == 17) {
#ifdef PLAYTEST
			// See Serialize(): this column carries the affix reroll count (offset 0x180).
			item->affixRerolls = (unsigned int)stoul(tokens.at(idx++));
#else
			auto unused = (unsigned int)stoul(tokens.at(idx++));
#endif
		}

		return item;
	}

	/// <summary>
	/// Helper method for converting gameTextLine to a CSV string.
	/// </summary>
	/// <param name="gameTextLines"></param>
	/// <returns></returns>
	std::wstring GameTextLineToString(std::vector<GameTextLine>& gameTextLines) {
		std::wstringstream stream;
		GAME::ItemReplicaInfo replica;

		for (auto& it : gameTextLines) {
			stream << it.textClass << ";" << it.text.c_str() << "\n";
		}

		std::wstring str = stream.str();
		return str;
	}



}

/// <summary>
/// Fetches the static pointer to GAME::GameEngine (not a method call)
/// </summary>
/// <returns></returns>
GAME::GameEngine* fnGetGameEngine() {
	void* addr = GetProcAddressOrLogToFile(L"game.dll", "?gGameEngine@GAME@@3PEAVGameEngine@1@EA");
	if (addr == nullptr) {
		// game.dll not loaded yet (or export missing) - MUST NOT dereference.
		return nullptr;
	}
	auto gameEngine = (GAME::GameEngine*)*(DWORD_PTR*)addr;
	if (gameEngine == nullptr) {
		LogToFile(LogLevel::WARNING, "Got game engine nullptr, beware if a crash follows this.");
	}
	return gameEngine;
}

/// <summary>
/// Fetches the static pointer to GAME::Engine (not a method call)
/// </summary>
/// <returns></returns>
GAME::Engine* fnGetEngine(bool skipLog) {
	void* addr = GetProcAddressOrLogToFile(L"engine.dll", "?gEngine@GAME@@3PEAVEngine@1@EA", skipLog);
	if (addr == nullptr) {
		// engine.dll not loaded yet (or export missing) - MUST NOT dereference.
		return nullptr;
	}
	auto engine = (GAME::Engine*)*(DWORD_PTR*)addr;
	if (engine == nullptr) {
		LogToFile(LogLevel::WARNING, "Got engine nullptr, beware if a crash follows this.");
	}
	return engine;
}

bool fnGetHardcore(GAME::GameInfo* gameInfo, bool skipLog) {
	pGetHardcore f = pGetHardcore(GetProcAddressOrLogToFile(L"engine.dll", "?GetHardcore@GameInfo@GAME@@QEBA_NXZ", skipLog));
	if (f == nullptr) {
		LogToFile(LogLevel::WARNING, "fnGetHardcore: export unresolved, defaulting to false");
		return false;
	}
	return f(gameInfo);

}

typedef std::basic_string<char, std::char_traits<char>, std::allocator<char> > const& Fancystring;

void* GetProcAddressOrLogToFile(const wchar_t* dll, char* procAddress, bool skipLog) {
	HMODULE hModule = ::GetModuleHandle(dll);
	void* originalMethod = hModule ? GetProcAddress(hModule, procAddress) : NULL;
	if (originalMethod == NULL) {
		LogToFile(LogLevel::FATAL, std::string("Error finding export from DLL: ") + std::string(procAddress));
		DBGLOG("GetProcAddress FAILED: module=%ls proc=%s (module handle=%p)", dll, procAddress, (void*)hModule);
	}
	else if (!skipLog) {
		LogToFile(LogLevel::INFO, std::string("Successfully found DLL export: ") + std::string(procAddress));
	}

	return originalMethod;
}


// ============================================================================
// Game API pointers - all start NULL, resolved lazily by ResolveGameApi().
// The old code resolved these in global initializers at CRT init time, which
// produced NULL pointers (and NULL derefs) whenever Proton loaded this proxy
// before game.dll/engine.dll.
// ============================================================================
ItemGetItemReplicaInfo fnItemGetItemReplicaInfo = nullptr;
pCreateItem fnCreateItem = nullptr;
pGetObjectManager fnGetObjectManager = nullptr;
pDestroyObjectEx fnDestroyObjectEx = nullptr;
pGetMainPlayer fnGetMainPlayer = nullptr;
pGetModNameArg fnGetModNameArg = nullptr;
pGetGameInfoMode fnGetGameInfoMode = nullptr;
pGetGameInfo fnGetGameInfo = nullptr;
pPlayDropSound fnPlayDropSound = nullptr;
pShowCinematicText fnShowCinematicText = nullptr;
pGetPlayerTransfer fnGetPlayerTransfer = nullptr;
IsGameLoadingPtr IsGameLoading = nullptr;
IsGameLoadingPtr IsGameEngineOnline = nullptr;
IsGameWaitingPtr IsGameWaiting = nullptr;
SortInventorySackPtr SortInventorySack = nullptr;

static bool g_gameApiResolved = false;

bool IsGameApiResolved() {
	return g_gameApiResolved;
}

bool ResolveGameApi() {
	if (g_gameApiResolved) {
		return true;
	}

	// GetModuleHandle does NOT load modules - if the game DLLs are not mapped
	// yet there is nothing to do. The caller (deferred init thread) retries.
	if (::GetModuleHandleW(L"game.dll") == NULL || ::GetModuleHandleW(L"engine.dll") == NULL) {
		DBGLOG("ResolveGameApi: game.dll=%p engine.dll=%p - not both loaded yet",
			(void*)::GetModuleHandleW(L"game.dll"), (void*)::GetModuleHandleW(L"engine.dll"));
		return false;
	}

	int failures = 0;
	auto resolve = [&failures](const wchar_t* dll, char* name) -> void* {
		void* p = GetProcAddressOrLogToFile(dll, name, true);
		if (p == nullptr) {
			failures++;
			DBGLOG("ResolveGameApi: FAILED to resolve %s from %ls", name, dll);
		}
		return p;
	};

	fnItemGetItemReplicaInfo = ItemGetItemReplicaInfo(resolve(L"game.dll", GET_ITEM_REPLICAINFO));
	fnCreateItem = pCreateItem(resolve(L"game.dll", "?CreateItem@Item@GAME@@SAPEAV12@AEBUItemReplicaInfo@2@@Z"));
	fnGetObjectManager = pGetObjectManager(resolve(L"engine.dll", "?Get@?$Singleton@VObjectManager@GAME@@@GAME@@SAPEAVObjectManager@2@XZ"));
	fnDestroyObjectEx = pDestroyObjectEx(resolve(L"engine.dll", "?DestroyObjectEx@ObjectManager@GAME@@QEAAXPEAVObject@2@PEBDH@Z"));
	fnGetMainPlayer = pGetMainPlayer(resolve(L"game.dll", "?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ"));
	fnGetModNameArg = pGetModNameArg(resolve(L"engine.dll", "?GetModName@GameInfo@GAME@@QEAAXAEAV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@@Z"));
	fnGetGameInfoMode = pGetGameInfoMode(resolve(L"engine.dll", "?GetMode@GameInfo@GAME@@QEBAIXZ"));
	fnGetGameInfo = pGetGameInfo(resolve(L"engine.dll", "?GetGameInfo@Engine@GAME@@QEAAPEAVGameInfo@2@XZ"));
	fnPlayDropSound = pPlayDropSound(resolve(L"game.dll", "?PlayDropSound@Item@GAME@@UEAAXXZ"));
	fnShowCinematicText = pShowCinematicText(resolve(L"engine.dll", "?ShowCinematicText@Engine@GAME@@QEAAXAEBV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@0W4CinematicTextType@2@AEBVColor@2@_N@Z"));
	fnGetPlayerTransfer = pGetPlayerTransfer(resolve(L"game.dll", "?GetPlayerTransfer@GameEngine@GAME@@QEAAAEAV?$vector@PEAVInventorySack@GAME@@@mem@@XZ"));
	IsGameLoading = IsGameLoadingPtr(resolve(L"game.dll", "?IsGameLoading@GameEngine@GAME@@QEBA_NXZ"));
	IsGameEngineOnline = IsGameLoadingPtr(resolve(L"game.dll", "?IsGameEngineOnline@GameEngine@GAME@@QEBA_NXZ"));
	IsGameWaiting = IsGameWaitingPtr(resolve(L"game.dll", "?IsGameWaiting@GameEngine@GAME@@QEAA_N_N@Z"));
	SortInventorySack = SortInventorySackPtr(resolve(L"game.dll", "?Sort@InventorySack@GAME@@QEAA_NI@Z"));

	g_gameApiResolved = (failures == 0);
	DBGLOG("ResolveGameApi: %s (%d failures)", g_gameApiResolved ? "SUCCESS" : "INCOMPLETE", failures);
	if (!g_gameApiResolved) {
		LogToFile(LogLevel::FATAL, "ResolveGameApi: " + std::to_string(failures) + " exports failed to resolve - wrong game version?");
	}
	return g_gameApiResolved;
}
