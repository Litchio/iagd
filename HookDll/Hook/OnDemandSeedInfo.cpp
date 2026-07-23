#include "stdafx.h"
#include <stdio.h>
#include <random>
#include <stdlib.h>
#include <fstream>
#include "MessageType.h"
#include "OnDemandSeedInfo.h"
#include "Exports.h"
#include <codecvt>
#include <filesystem>
#include "nlohmann/json.hpp"
#include "VTableDispatch.h"
#include "DebugLog.h"
#include "HookConfig.h"

#include "Logger.h"
std::wstring GetIagdFolder();

OnDemandSeedInfo::pItemEquipmentGetUIDisplayText OnDemandSeedInfo::fnItemEquipmentGetUIDisplayText;
OnDemandSeedInfo::pItemEquipmentGetUIDisplayText OnDemandSeedInfo::fnItemRelicGetUIDisplayText;
OnDemandSeedInfo* OnDemandSeedInfo::g_self;
std::mutex OnDemandSeedInfo::_mutex;
OnDemandSeedInfo::OnDemandSeedInfo() {}
OnDemandSeedInfo::OnDemandSeedInfo(DataQueue* dataQueue, HANDLE hEvent) {
	m_dataQueue = dataQueue;
	m_hEvent = hEvent;
	m_thread = nullptr;
	m_isActive = false;
	m_sleepMilliseconds = 0;
	dll_Engine_Render = nullptr;
	gameSetDifficultyRampMethod = nullptr;
	

	auto handle = GetProcAddressOrLogToFile(L"game.dll", "?GetUIDisplayText@ItemEquipment@GAME@@UEBAXPEBVCharacter@2@AEAV?$vector@UGameTextLine@GAME@@@mem@@_N@Z");
	if (handle == nullptr) LogToFile(LogLevel::FATAL, L"Error hooking GetUIDisplayText@ItemEquipment");
	fnItemEquipmentGetUIDisplayText = pItemEquipmentGetUIDisplayText(handle);


	auto handle2 = GetProcAddressOrLogToFile(L"game.dll", GET_ITEMARTIFACT_GETUIDISPLAYTEXT);
	if (handle2 == nullptr) LogToFile(LogLevel::FATAL, L"Error hooking GetUIDisplayText@ItemArtifact");
	fnItemRelicGetUIDisplayText = pItemEquipmentGetUIDisplayText(handle2);
}



void OnDemandSeedInfo::EnableHook() {
	g_self = this;

	if (!g_hookConfig.hookEngineRender) {
		DBGLOG("OnDemandSeedInfo::EnableHook - render/update hooks DISABLED by config");
		LogToFile(LogLevel::INFO, L"Engine render hook disabled by config");
		return;
	}

	gameSetDifficultyRampMethod = (OriginalEngineRenderMethodPtr)HookGame(
		"?SetDifficultyRamp@GameEngine@GAME@@QEAAXH@Z",
		HookedGameSetDifficultyRampMethod,
		m_dataQueue,
		m_hEvent,
		TYPE_GAMEENGINE_UPDATE
	);

	dll_Engine_Render = (Engine_Render)HookEngine(
		"?Render@Engine@GAME@@QEAAXXZ",
		Hooked_Engine_Render,
		m_dataQueue,
		m_hEvent,
		TYPE_GAMEENGINE_UPDATE
	);

	LogToFile(LogLevel::INFO, L"Seems we hooked it");
}
void OnDemandSeedInfo::DisableHook() {
	Stop();
	Unhook((PVOID*)&gameSetDifficultyRampMethod, HookedGameSetDifficultyRampMethod);
	Unhook((PVOID*)&dll_Engine_Render, Hooked_Engine_Render);
}

/*
* Continuously look for new files.
* This queues the data, avoiding IO operations during the game/render loops.
*/
void OnDemandSeedInfo::ThreadMain(void*) {

	LogToFile(LogLevel::INFO, L"Seed info thread started, sleeping for 6s..");
	try {

		// When spamming too much right as the game first loads, the game tends to crash
		// This is suboptimal, but it is what it is..
		g_self->m_sleepMilliseconds = 6000;

		LogToFile(LogLevel::INFO, L"Seed info thread ready, starting loop");
		while (g_self->m_isActive) {

			// The "m_sleepMilliseconds" is actually a counter read in the Update() method on a different thread. Letting us back off from doing too much in the update thread.
			while (g_self->m_sleepMilliseconds > 0) {
				//LogToFile(LogLevel::INFO, L"Sleeping for 100ms.. " + std::to_wstring(g_self->m_sleepMilliseconds) + L"ms remaining");
				Sleep(100);
				g_self->m_sleepMilliseconds -= 100;

				if (!g_self->m_isActive) {
					LogToFile(LogLevel::INFO, L"No longer running, cancelling sleep");
					return;
				}
			}

			g_self->Process();
		}
	}
	catch (std::exception& ex) {
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());
		LogToFile(LogLevel::FATAL, L"Error parsing in OndemandSeedInfo::ThreadMain.. " + wide);
	}
	catch (...) {
		LogToFile(LogLevel::FATAL, L"Error parsing in OndemandSeedInfo::ThreadMain.. (triple-dot)");
	}
}

/*
* Stop the running thread
*/
void OnDemandSeedInfo::Stop() {
	m_isActive = false;
	if (m_thread != NULL) {
		CloseHandle(m_thread);
		m_thread = NULL;
	}
}


/*
* Create the named pipe and start a thread to listen for events
*/
void OnDemandSeedInfo::Start() {
	g_self = this;

	LogToFile(LogLevel::INFO, L"Queuing thread start for seed info..");
	m_isActive = true;
	m_thread = (HANDLE)_beginthread(ThreadMain, NULL, 0);
}



ParsedSeedRequest* OnDemandSeedInfo::DeserializeReplicaCsv(std::vector<std::string> tokens) {
	if (tokens.size() != 12 && tokens.size() != 15) {
		LogToFile(LogLevel::WARNING, L"Error parsing CSV file, expected 12 or 15 tokens, got " + std::to_wstring(tokens.size()));
		return nullptr;
	}

	bool isNewDlc = tokens.size() == 15;

	// Value-initialize: without {} the scalar members (stackSize, id, owner, ...)
	// are left indeterminate, and this replica is fed straight into fnCreateItem.
	GAME::ItemReplicaInfo item{};

	// Match the game's own ItemReplicaInfo constructor, which defaults stackSize
	// to 1. A stackSize of 0 creates items whose GetStackSize() returns 0 (see
	// GAME::Deserialize in GrimTypes.cpp for the full story).
	item.stackSize = 1;

	ParsedSeedRequest* result = new ParsedSeedRequest();

	
	int idx = 0;
	int type = stoul(tokens.at(idx++));
	if (type == 1) {
		result->playerItemId = (unsigned int)stoul(tokens.at(idx++));
	}
	else if (type == 2) {
		result->buddyItemId = tokens.at(idx++);
	}
	else {
		return nullptr;
	}

	item.seed = (unsigned int)stoul(tokens.at(idx++));
	item.relicSeed = (unsigned int)stoul(tokens.at(idx++));
	item.enchantmentSeed = (unsigned int)stoul(tokens.at(idx++));

	if (isNewDlc) {
		auto rerollsUsed = (unsigned int)stoul(tokens.at(idx++));
	}

	item.baseRecord = tokens.at(idx++);
	item.prefixRecord = tokens.at(idx++);
	item.suffixRecord = tokens.at(idx++);

	item.modifierRecord = tokens.at(idx++);
	item.materiaRecord = tokens.at(idx++);
	item.enchantmentRecord = tokens.at(idx++);
	item.transmuteRecord = tokens.at(idx++);


		if (isNewDlc) {
		auto ascendantAffixNameRecord  = tokens.at(idx++);
		auto ascendantAffix2hNameRecord= tokens.at(idx++);
	}

	std::string s;
	for (auto it = tokens.begin(); it != tokens.end(); ++it) {
		s = s + *it + ";";
	}

	// Trim whitespace from string fields
	auto trim = [](std::string& str) {
		str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char c) { return !std::isspace(c); }));
		str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), str.end());
	};
	trim(item.baseRecord);
	trim(item.prefixRecord);
	trim(item.suffixRecord);
	trim(item.modifierRecord);
	trim(item.materiaRecord);
	trim(item.enchantmentRecord);
	trim(item.transmuteRecord);
	// TODO: !! Trip ascendant records

	result->itemReplicaInfo = item;


	// Relics have a different function it needs called
	result->isRelic = item.baseRecord.find("/gearrelic/") != std::string::npos;

	return result;
}

/// <summary>
/// Read a .CSV file into a GAME::ItemReplicaInfo object
/// </summary>
/// <param name="filename">A valid CSV file</param>
/// <returns></returns>
ParsedSeedRequest* OnDemandSeedInfo::ReadReplicaInfo(const std::wstring& filename) {
	try {
		std::ifstream file(filename.c_str());
		return DeserializeReplicaCsv(GAME::GetNextLineAndSplitIntoTokens(file));
	}
	catch (std::exception& ex) {
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());

		LogToFile(LogLevel::FATAL, L"ERROR Creating ReplicaItem.." + wide);

	}
	catch (...) {
		LogToFile(LogLevel::FATAL, L"ERROR Creating ReplicaItem.. (triple-dot)");
	}

	return nullptr;
}

/// <summary>
/// We look for CSV files that the IA client has written to a specific folder, when wanting to move items back into the game.
/// </summary>
/// <param name="modName"></param>
/// <param name="isHardcore"></param>
/// <returns></returns>
std::wstring GetFolderToReadFrom(std::wstring modName, bool isHardcore) {
	std::wstring folder;
	if (modName.empty()) {
		folder = GetIagdFolder() + L"replica\\from_ia";
	}
	else {
		folder = GetIagdFolder() + L"replica\\from_ia\\" + modName;
	}

	if (!std::filesystem::is_directory(folder)) {
		std::filesystem::create_directories(folder);
	}

	return folder;
}

std::wstring OnDemandSeedInfo::GetModName(GAME::GameInfo* gameInfo) {
	std::wstring modName;
	if (fnGetGameInfoMode(gameInfo) != 1) { // Skip mod name if we're in Crucible, we don't treat that as a mod.
		fnGetModNameArg(gameInfo, &modName);
		modName.erase(std::remove(modName.begin(), modName.end(), '\r'), modName.end());
		modName.erase(std::remove(modName.begin(), modName.end(), '\n'), modName.end());
	}

	return modName;
}

/*
* Process a single request on the named pipe
*/
void OnDemandSeedInfo::Process() {
	try {
		std::filesystem::create_directories(GetIagdFolder() + L"replica\\to_ia\\");

		while (m_isActive) {
			Sleep(500);

			auto engine = fnGetEngine(true);
			if (engine == nullptr) {
				LogToFile(LogLevel::INFO, L"Debug: NoEngine");
				continue;
			}

			GAME::GameInfo* gameInfo = fnGetGameInfo(engine);
			if (gameInfo == nullptr) {
				LogToFile(LogLevel::INFO, L"GameInfo is null, aborting..");
				continue;
			}

			std::wstring folder = GetFolderToReadFrom(GetModName(gameInfo), fnGetHardcore(gameInfo, true));

			for (auto& entry : std::filesystem::directory_iterator(folder)) {
				auto filename = std::wstring(entry.path().c_str());

				if (filename.size() >= 4 && filename.substr(filename.size() - 4) == L".csv") {
					LogToFile(LogLevel::INFO, std::wstring(L"Queued file: ") + std::wstring(entry.path().c_str()));

					ParsedSeedRequest* obj = ReadReplicaInfo(entry.path().c_str());
					if (obj == nullptr) {
						LogToFile(LogLevel::WARNING, std::wstring(L"Ignoring file, got nullptr when deserializing: ") + std::wstring(entry.path().c_str()));
					}
					else {
						ParsedSeedRequestPtr abc(obj);
						m_itemQueue.push(abc);
					}
				}
				else {
					LogToFile(LogLevel::INFO, std::wstring(L"Ignoring file: ") + std::wstring(entry.path().c_str()));
				}

				DeleteFile(filename.c_str());

				if (m_itemQueue.size() > 20) {
					Sleep(1);
				}
			}
		}
	}
	catch (std::exception& ex) {
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());
		LogToFile(LogLevel::FATAL, L"Error parsing in OnDemandSeedInfo::ThreadMain.. " + wide);
	}
	catch (...) {
		LogToFile(LogLevel::FATAL, L"Error parsing in OnDemandSeedInfo::ThreadMain.. (triple-dot)");
	}
	LogToFile(LogLevel::INFO, L"Stopping deposit listener..");
}

std::string toString(std::wstring s) {
	using convert_type = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_type, wchar_t> converter;
	return converter.to_bytes(s);
}


nlohmann::json toJson(ParsedSeedRequest obj, std::vector<GAME::GameTextLine>& gameTextLines) {
	nlohmann::json root;
	root["playerItemId"] = obj.playerItemId;
	root["buddyItemId"] = obj.buddyItemId;

	// Completely redundant information, might help others to use this DLL.
	nlohmann::json replica;
	replica["baseRecord"] = obj.itemReplicaInfo.baseRecord;
	replica["prefixRecord"] = obj.itemReplicaInfo.prefixRecord;
	replica["suffixRecord"] = obj.itemReplicaInfo.suffixRecord;
	replica["modifierRecord"] = obj.itemReplicaInfo.modifierRecord;
	replica["transmuteRecord"] = obj.itemReplicaInfo.transmuteRecord;
	replica["seed"] = obj.itemReplicaInfo.seed;
	replica["materiaRecord"] = obj.itemReplicaInfo.materiaRecord;
	replica["relicBonus"] = obj.itemReplicaInfo.relicBonus;
	replica["relicSeed"] = obj.itemReplicaInfo.relicSeed;
	replica["enchantmentRecord"] = obj.itemReplicaInfo.enchantmentRecord;
	replica["enchantmentSeed"] = obj.itemReplicaInfo.enchantmentSeed;
	root["replica"] = replica;
	// TODO: ascendant changes here, not critical, not used by IAGD

	nlohmann::json stats = nlohmann::json::array();
	for (auto& it : gameTextLines) {
		nlohmann::json stat;
		stat["text"] = toString(it.text);
		stat["type"] = it.textClass;
		stats.push_back(stat);
	}
	root["stats"] = stats;

	return root;
}

// TODO: Either rename, or make this method do less. Probably the latter
nlohmann::json OnDemandSeedInfo::GetItemInfo(ParsedSeedRequest obj) {
	// Check for access to Game.dll
	if (GetModuleHandleA("Game.dll")) {
		GAME::ItemReplicaInfo replica = obj.itemReplicaInfo;
		GAME::Item* newItem = fnCreateItem(&replica);
		if (newItem) {
			std::vector<GAME::GameTextLine> gameTextLines = {};

			auto gameEngine = fnGetGameEngine();
			if (gameEngine == nullptr) {
				LogToFile(LogLevel::INFO, "No game engine, skipping item stat generation");
				DataItemPtr item(new DataItem(TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOGAME, 0, nullptr));
				m_dataQueue->push(item);
				SetEvent(m_hEvent);
				return nlohmann::json{};
			}

			GAME::Character* character = (GAME::Character*)fnGetMainPlayer(gameEngine);
			if (character == nullptr) {
				LogToFile(LogLevel::INFO, "No character found, skipping item stat generation");
				DataItemPtr item(new DataItem(TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOGAME, 0, nullptr));
				m_dataQueue->push(item);
				SetEvent(m_hEvent);
				return nlohmann::json{};
			}

			if (obj.isRelic) {
				fnItemRelicGetUIDisplayText((GAME::ItemEquipment*)newItem, character, &gameTextLines, true);
			}
			else {
				fnItemEquipmentGetUIDisplayText((GAME::ItemEquipment*)newItem, character, &gameTextLines, true);
			}

			fnDestroyObjectEx(fnGetObjectManager(), (GAME::Object*)newItem, nullptr, 0);

			LogToFile(LogLevel::INFO, L"Generating json..");

			return toJson(obj, gameTextLines);
		}
		else {
			std::string str = obj.itemReplicaInfo.baseRecord;
			DataItemPtr item(new DataItem(TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOITEM, str.size(), (char*)str.c_str()));
			m_dataQueue->push(item);
			SetEvent(m_hEvent);
		}
	}
	else {
		DataItemPtr item(new DataItem(TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOGAME, 0, nullptr));
		m_dataQueue->push(item);
		SetEvent(m_hEvent);
	}

	return nlohmann::json{};
}


std::wstring randomFilename32() {
	std::wstring str(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	std::random_device rd;
	std::mt19937 generator(rd());

	std::shuffle(str.begin(), str.end(), generator);

	return str.substr(0, 32);    // assumes 32 < number of characters in str         
}

void* __fastcall OnDemandSeedInfo::HookedGameSetDifficultyRampMethod(void* This, int v) {
	static bool firstCallLogged = false;
	if (!firstCallLogged) {
		firstCallLogged = true;
		DBGLOG("HookedGameSetDifficultyRampMethod: FIRST CALL - hook is live");
	}
	if (g_self == nullptr || g_self->gameSetDifficultyRampMethod == nullptr) {
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(g_self->_mutex);

	LogToFile(LogLevel::INFO, L"The pesky SetDifficultyRamp@GameEngine is being called");
	auto result = g_self->gameSetDifficultyRampMethod(This, v);
	return result;
}

void* __fastcall OnDemandSeedInfo::Hooked_Engine_Render(void* This) {
	static bool firstCallLogged = false;
	if (!firstCallLogged) {
		firstCallLogged = true;
		DBGLOG("Hooked_Engine_Render: FIRST CALL - render hook is live");
	}
	if (This == nullptr) {
		LogToFile(LogLevel::WARNING, L"Render@Engine called with 'This' being null");
	}
	if (g_self == nullptr) {
		LogToFile(LogLevel::FATAL, L"Render@Engine called with 'g_self' being null");
	}

	// Never call game APIs that failed to resolve; just render and get out.
	if (!IsGameApiResolved() || IsGameLoading == nullptr || IsGameEngineOnline == nullptr ||
		g_self == nullptr || g_self->dll_Engine_Render == nullptr) {
		if (g_self != nullptr && g_self->dll_Engine_Render != nullptr) {
			return g_self->dll_Engine_Render(This);
		}
		return nullptr;
	}

	if (g_self->m_sleepMilliseconds <= 0) {
		try {
			// Only start processing items if the game is running.
			// Attempting to create items with a set bonus from the main menu may crash the game.
			// Items with skills may also end up with missing info if created from the main menu.

			auto gameEngine = fnGetGameEngine();
			if (gameEngine != nullptr && !IsGameLoading(gameEngine) && IsGameEngineOnline(gameEngine)) {

				// Process the queue
				int num = 0;

				nlohmann::json result = nlohmann::json::array();

				std::unique_lock<std::mutex> lock(g_self->_mutex, std::try_to_lock);
				// No point arguing over a lock. We can parse this later.
				if (!lock.owns_lock()) {
					return g_self->dll_Engine_Render(This);
				}
				while (!g_self->m_itemQueue.empty() && num++ < 100 && !IsGameLoading(gameEngine) && IsGameEngineOnline(gameEngine)) {
					LogToFile(LogLevel::INFO, L"Processing..");
					ParsedSeedRequestPtr ptr = g_self->m_itemQueue.pop();
					if (ptr == nullptr) {
						return g_self->dll_Engine_Render(This);
					}
					ParsedSeedRequest obj = *ptr.get();

					LogToFile(LogLevel::INFO, L"Fetching items stats for " + GAME::Serialize(obj.itemReplicaInfo));
					nlohmann::json json = g_self->GetItemInfo(obj);
					if (!json.is_null() && !json.empty()) {
						result.push_back(json);
					}
				}

				if (!result.empty()) {
					// Write json array
					std::wstring fullPath = GetIagdFolder() + L"replica\\to_ia\\" + randomFilename32();

					std::string jsonStr = result.dump();

					std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
					std::wofstream stream;
					stream.open(fullPath.c_str());
					stream << jsonStr.c_str();
					stream.flush();
					stream.close();
					LogToFile(LogLevel::INFO, L"Wrote items stats to " + fullPath);

					// Now that we're done writing we can move it and give it the .json suffix, that way IA isn't trying to read it while we're writing
					MoveFile(fullPath.c_str(), (fullPath + L".json").c_str());
				}
			}
			else {
				if (gameEngine != nullptr && IsGameLoading(gameEngine)) {
					LogToFile(LogLevel::INFO, "Game is loading, real stat generation not available.");
				}
				if (gameEngine == nullptr || !IsGameEngineOnline(gameEngine)) {
					LogToFile(LogLevel::INFO, "///Game engine is not online, real stat generation not available.");
				}
				g_self->m_sleepMilliseconds = 12000;
			}
		}
		catch (std::exception& ex) {
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			std::wstring wide = converter.from_bytes(ex.what());
			LogToFile(LogLevel::FATAL, L"Error parsing on-demand item stats.. " + wide);
		}
		catch (...) {
			LogToFile(LogLevel::FATAL, L"Error parsing on-demand item stats.. (triple-dot)");
		}

	}

	void* r = g_self->dll_Engine_Render(This);
	return r;
}