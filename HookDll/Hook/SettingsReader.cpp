#include "SettingsReader.h"
#include "nlohmann/json.hpp"
#include "Logger.h"
#include <fstream>
#include <codecvt>

std::wstring GetIagdFolder();

// Helper: load and parse settings.json, returns empty object on failure
static nlohmann::json LoadSettingsJson() {
	std::wstring settingsJson = GetIagdFolder() + L"settings.json";
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
	std::ifstream file(conv.to_bytes(settingsJson));
	if (!file.is_open()) {
		return nlohmann::json{};
	}
	try {
		nlohmann::json root;
		file >> root;
		return root;
	}
	catch (...) {
		return nlohmann::json{};
	}
}

int SettingsReader::GetStashTabToLootFrom() {
	auto root = LoadSettingsJson();
	if (!root.contains("local") || !root["local"].contains("stashToLootFrom")) {
		LogToFile(LogLevel::WARNING, L"No \"loot from\" configuration found, defaulting to last stash tab");
		return 0;
	}
	const int stashToLootFrom = root["local"]["stashToLootFrom"].get<int>();
	if (stashToLootFrom == 0) {
		LogToFile(LogLevel::INFO, L"Configured to loot from last stash tab");
	} else {
		LogToFile(LogLevel::INFO, L"Configured to loot from tab: " + std::to_wstring(stashToLootFrom));
	}
	return stashToLootFrom;
}

int SettingsReader::GetStashTabToDepositTo() {
	auto root = LoadSettingsJson();
	if (!root.contains("local") || !root["local"].contains("stashToDepositTo")) {
		LogToFile(LogLevel::WARNING, L"No \"deposit to\" configuration found, defaulting to second-to-last stash tab");
		return 0;
	}
	const int stashToDepositTo = root["local"]["stashToDepositTo"].get<int>();
	if (stashToDepositTo == 0) {
		LogToFile(LogLevel::INFO, L"Configured to deposit to last stash tab");
	} else {
		LogToFile(LogLevel::INFO, L"Configured to deposit to tab: " + std::to_wstring(stashToDepositTo));
	}
	return stashToDepositTo;
}

bool SettingsReader::GetIsGrimDawnParsed() {
	auto root = LoadSettingsJson();
	if (!root.contains("local") || !root["local"].contains("isGrimDawnParsed")) {
		LogToFile(LogLevel::WARNING, L"GrimDawnParsed: No configuration found, defaulting to NOT parsed");
		return false;
	}
	const bool isGdParsed = root["local"]["isGrimDawnParsed"].get<bool>();
	LogToFile(LogLevel::INFO, std::wstring(L"Grim Dawn parsed: ") + (isGdParsed ? L"True" : L"False"));
	return isGdParsed;
}

bool SettingsReader::GetIsRunningInWine() {
	auto root = LoadSettingsJson();
	if (!root.contains("persistent") || !root["persistent"].contains("isRunningInWine")) {
		LogToFile(LogLevel::WARNING, L"RunningInWine: No configuration found, defaulting to false");
		return false;
	}
	const bool isWine = root["persistent"]["isRunningInWine"].get<bool>();
	LogToFile(LogLevel::INFO, std::wstring(L"Running in Wine: ") + (isWine ? L"True" : L"False"));
	return isWine;
}
