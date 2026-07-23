/*
 * HookConfig.h - Runtime hook-selection matrix for bisecting the black screen.
 *
 * Reads dinput8_config.json located next to dinput8.dll (game x64 folder).
 * Missing file or missing keys fall back to the defaults below, which
 * preserve the original behavior (everything enabled).
 *
 * Example dinput8_config.json:
 * {
 *   "passthroughOnly": false,
 *   "hookInstaloot": true,
 *   "hookGameEngineUpdate": true,
 *   "hookEngineRender": true,
 *   "startSeedInfoThread": true,
 *   "startTcpClient": true
 * }
 */
#pragma once

struct HookConfig {
	// Test 0: do absolutely nothing except pin the DLL and forward
	// DirectInput8Create. If the game still black-screens in this mode,
	// the problem is the proxy/export forwarding itself, not the hooks.
	bool passthroughOnly = false;

	// InventorySack::AddItem x2, SetTransferOpen, GameInfo ctor, GetPrivateStash
	bool hookInstaloot = true;

	// GameEngine::Update (the main game loop tick)
	bool hookGameEngineUpdate = true;

	// Engine::Render (the render loop) + GameEngine::SetDifficultyRamp
	bool hookEngineRender = true;

	// OnDemandSeedInfo background thread (file watcher for item deposits)
	bool startSeedInfoThread = true;

	// TCP client connection to the Rust backend (127.0.0.1:1337)
	bool startTcpClient = true;
};

extern HookConfig g_hookConfig;

/// Loads dinput8_config.json from the DLL's directory. Safe to call from
/// DllMain (plain file read, no COM, no shell APIs).
void LoadHookConfig();