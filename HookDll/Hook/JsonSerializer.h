#pragma once

#include "MessageType.h"
#include <string>
#include <sstream>
#include <ctime>

/**
 * JSON serialization helper for hook messages
 * Converts binary message types to JSON format compatible with Rust backend
 */
class JsonSerializer {
public:
	/**
	 * Serialize a hook message to JSON string
	 * format: {"type": "TYPE_NAME", "timestamp": timestamp_ms, "data": {...}}
	 */
	static std::string MessageToJson(MessageType messageType, const char* data, unsigned int dataSize) {
		std::stringstream ss;
		ss << "{";
		ss << "\"type\":\"" << MessageTypeToString(messageType) << "\",";
		
		// Add timestamp in milliseconds
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
		ss << "\"timestamp\":" << millis << ",";
		
		// Add raw data as base64 or hex encoded (optional for basic messages)
		ss << "\"dataLength\":" << dataSize;
		
		// For now, skip binary data encoding - Rust backend will handle type-specific parsing
		ss << "}";
		
		return ss.str();
	}

	/**
	 * Convert MessageType enum to string name
	 */
	static std::string MessageTypeToString(MessageType type) {
		switch (type) {
		case TYPE_REPORT_WORKER_THREAD_LAUNCHED:
			return "TYPE_REPORT_WORKER_THREAD_LAUNCHED";
		case TYPE_CloudGetNumFiles:
			return "TYPE_CloudGetNumFiles";
		case TYPE_CloudRead:
			return "TYPE_CloudRead";
		case TYPE_CloudWrite:
			return "TYPE_CloudWrite";
		case TYPE_GameInfo_IsHardcore:
			return "TYPE_GameInfo_IsHardcore";
		case TYPE_GameInfo_SetModName:
			return "TYPE_GameInfo_SetModName";
		case TYPE_Stash_Item_BasicInfo:
			return "TYPE_Stash_Item_BasicInfo";
		case TYPE_ERROR_HOOKING_GENERIC:
			return "TYPE_ERROR_HOOKING_GENERIC";
		case TYPE_GameInfo_IsHardcore_via_init:
			return "TYPE_GameInfo_IsHardcore_via_init";
		case TYPE_SUCCESS_HOOKING_GENERIC:
			return "TYPE_SUCCESS_HOOKING_GENERIC";
		case TYPE_ITEMSEEDDATA_PLAYERID:
			return "TYPE_ITEMSEEDDATA_PLAYERID";
		case TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOGAME:
			return "TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOGAME";
		case TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOITEM:
			return "TYPE_ITEMSEEDDATA_PLAYERID_ERR_NOITEM";
		case TYPE_GAMEENGINE_UPDATE:
			return "TYPE_GAMEENGINE_UPDATE";
		case TYPE_INJECTION_CANCELLED:
			return "TYPE_INJECTION_CANCELLED";
		default:
			return "TYPE_UNKNOWN";
		}
	}

private:
	JsonSerializer() = delete;
};
