// vip_healthshot — Plugify port of the Pisex VIP module "VIP_Healthshot"
// (the original's vip_healthshot.cpp).
//
// Behaviour kept identical:
//   * config <configs>/vip_healthshot/vip_healthshot.ini, the VIP KeyValues file with
//     "round_min" (addons/configs/vip/vip_healthshot.ini of the Metamod plugin; package.sh ships
//     the same file to the plugin's own config directory).
//   * the feature is registered as "healthshot" (VIP_INT, TOGGLABLE).
//   * on VIP_OnPlayerSpawn (the original's OnPlayerSpawn callback) a VIP player gets
//     featureValue - currentHealthshotAmmo weapon_healthshot items, on the same round condition as
//     the original (round_min against the pistol round and the played rounds, see round_rule.hpp).
//   * the current count is the healthshot slot of the pawn's ammo array (ammo index 20), exactly
//     the m_iAmmo[20] the original read.
//   * the original refused to load on a missing config; Plugify must not fail hard, so the module
//     logs the error and stays disabled (no healthshots) instead.
//
// Pure s2sdk: no s2guard and no pisexcompat (docs/PLUGIN_STANDARD.md §7). Every safety rule the
// module used to get from pisexcompat lives in this plugin:
//   * PITFALLS B1/B2 - the map gate g_mapReady. No client, entity or schema call is made before
//     the first map or between OnMapEnd and the next OnMapStart: s2sdk dereferences null before
//     the first map and reads an emptied client list during map shutdown. The map listeners are
//     registered in OnPluginStart and removed in OnPluginEnd; readiness is latched by OnMapStart
//     and by the first OnPluginUpdate of a plugin loaded into a running map (unless an OnMapEnd
//     was seen since OnPluginStart), and dropped by OnMapEnd.
//   * PITFALLS C2 - IsConnected/IsAlive: slot 0..63, IsClientConnected, IsClientInGame,
//     !IsClientSourceTV, the controller entity present, IsClientAlive. Every call is guarded by
//     its __s2sdk_* binding too, so a plugin running without s2sdk fails soft instead of calling
//     through a null slot.
//   * PITFALLS C8 - SchemaOffset() answers -1 while the gate is closed (GetSchemaOffset before a
//     map exists segfaults inside the schema system) and caches only successful offsets
//     (schema_cache.hpp); the cache is cleared in OnPluginEnd, so a reload resolves the fields
//     again.
//
// Port notes:
//   * CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive -> IsAlive() below.
//   * CCSPlayer_ItemServices::GiveNamedItem("weapon_healthshot") -> s2sdk::GiveNamedItem(slot, ...).
//   * m_pWeaponServices->m_iAmmo[20] has no s2sdk accessor, so it is read through the schema
//     (CBasePlayerPawn::m_pWeaponServices + CPlayer_WeaponServices::m_iAmmo, the same fields the
//     Pisex SchemaEntity headers name). When the lookup fails, this spawn's handout is skipped -
//     what the original produced when its weapon-services pointer was null.
//   * pisex::KeyValues -> the plugin's own read-only KV1 reader (src/kv1.hpp), same grammar and
//     the same "a single root block is the file" rule, so the production vip_healthshot.ini is
//     read unchanged.

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include "kv1.hpp"
#include "round_rule.hpp"
#include "schema_cache.hpp"
#include "vip_api_compat.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace {

// The ported modules keep the original code's unqualified spellings (VIP_INT, TOGGLABLE, ...):
// the consumer wrapper declares them as plain enums in namespace vip.
using namespace vip;

IVIPApi* g_pVIPCore = &IVIPApi::Get();

constexpr int kMaxSlots = 64;
// Ammo slot of weapon_healthshot (AmmoIndex "AMMO_HEALTHSHOT" = 20 in the CS2 weapon scripts).
constexpr int kHealthshotAmmoIndex = 20;
constexpr const char* kItemName = "weapon_healthshot";
constexpr const char* kFeature = "healthshot";
// <Plugify configs dir>/vip_healthshot/vip_healthshot.ini (the pre-rewrite pisexcompat lookup
// resolved the bare file name to the same path).
constexpr const char* kConfigFile = "vip_healthshot/vip_healthshot.ini";

void LogInfo(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Info, plg::source_location::current());
	}
}

void LogError(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Error, plg::source_location::current());
	}
}

// --- PITFALLS B1/B2: the map gate -------------------------------------------------
//
// The state machine pisexcompat used to own (pisex::Init/Poll/Shutdown); a pure-s2sdk plugin keeps
// it in its own source. g_mapReady is the single flag every other part of the plugin asks before
// it touches s2sdk.
bool g_mapReady = false;               // a map is up: every client/entity/schema call is allowed
bool g_mapStartSeen = false;           // OnMapStart arrived since OnPluginStart / the last OnMapEnd
bool g_mapEndSeen = false;             // OnMapEnd arrived since OnPluginStart
bool g_mapListenersRegistered = false; // OnMapStart/OnMapEnd are registered with s2sdk

// s2sdk listeners are plain function pointers called on the game thread. They must not throw and
// must not touch a client: OnMapEnd runs while the client list is being torn down (PITFALLS B2).

void OnMapStartListener() {
	// OnMapStart is the map's first live frame; the gate is open for everything that follows.
	// (A plugin loaded into a running map never sees this callback - PollMapGate() covers it.)
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
}

void OnMapEndListener() {
	// From here until the next OnMapStart every s2sdk client/entity/schema call can crash
	// (PITFALLS B2): only state changes, no client call.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

// Registers the map listeners and closes the gate. Touches nothing else, so it is safe in
// OnPluginStart (PITFALLS B1). Without the listeners nothing could ever close the gate again, so a
// missing s2sdk binding keeps it shut for good (fail soft, never Failed - PITFALLS A1).
void InitMapGate() {
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	if (g_mapListenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register != nullptr && __s2sdk_OnMapEnd_Register != nullptr) {
		s2sdk::OnMapStart_Register(&OnMapStartListener);
		s2sdk::OnMapEnd_Register(&OnMapEndListener);
		g_mapListenersRegistered = true;
	} else {
		LogError("vip_healthshot: the s2sdk map listeners are unavailable - client calls stay disabled");
	}
}

// Per-frame pump (OnPluginUpdate), the replacement of pisex::Poll: a plugin loaded into a running
// map never saw an OnMapStart, so its first update is the proof of a live map (the host pumps
// updates from the server frame). After an OnMapEnd only a new OnMapStart opens the gate again:
// the host keeps pumping updates through a changelevel, so `!g_mapEndSeen` must not be enough once
// one was seen.
void PollMapGate() {
	if (g_mapListenersRegistered && !g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
	}
}

// Removes the map listeners and closes the gate (PITFALLS A7): `plg reload` must leave no pointer
// of this library behind.
void ShutdownMapGate() {
	if (g_mapListenersRegistered) {
		g_mapListenersRegistered = false;
		if (__s2sdk_OnMapStart_Unregister != nullptr) {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		}
		if (__s2sdk_OnMapEnd_Unregister != nullptr) {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		}
	}
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
}

// --- PITFALLS C8: the schema offsets ------------------------------------------------

vip_healthshot::SchemaOffsetCache g_schemaOffsets;

int32_t LookupSchemaOffset(const char* szClassName, const char* szFieldName) {
	if (__s2sdk_GetSchemaOffset == nullptr) {
		return -1;
	}
	try {
		return s2sdk::GetSchemaOffset(plg::string(szClassName), plg::string(szFieldName)); // -1: not found
	} catch (...) {
		return -1;
	}
}

// The map-gated replacement of pisex::SchemaOffset (PITFALLS C8): -1 while the gate is closed and
// in the window between OnMapEnd and the next OnMapStart; only offsets >= 0 are cached by
// schema_cache.hpp; OnPluginEnd clears the cache.
int32_t SchemaOffset(const char* szClassName, const char* szFieldName) {
	try {
		return g_schemaOffsets.Offset(g_mapReady, szClassName, szFieldName, &LookupSchemaOffset);
	} catch (...) {
		return -1;
	}
}

// --- PITFALLS C2: the client guards -------------------------------------------------

// A connected slot while a map is up. The controller/pawn checks come on top in IsAlive().
bool IsConnected(int iSlot) {
	if (!g_mapReady || iSlot < 0 || iSlot >= kMaxSlots || __s2sdk_IsClientConnected == nullptr) {
		return false;
	}
	try {
		return s2sdk::IsClientConnected(iSlot);
	} catch (...) {
		return false;
	}
}

// The original's CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive chain: a connected,
// in-game, non-SourceTV client whose controller entity exists and whose pawn is alive.
bool IsAlive(int iSlot) {
	if (!IsConnected(iSlot) || __s2sdk_IsClientInGame == nullptr || __s2sdk_IsClientSourceTV == nullptr ||
		__s2sdk_PlayerSlotToEntPointer == nullptr || __s2sdk_IsClientAlive == nullptr) {
		return false;
	}
	try {
		if (!s2sdk::IsClientInGame(iSlot) || s2sdk::IsClientSourceTV(iSlot)) {
			return false;
		}
		// The controller entity: without it s2sdk's pawn accessors would warn or read a stale
		// client record (PITFALLS C2).
		if (s2sdk::PlayerSlotToEntPointer(iSlot) == nullptr) {
			return false;
		}
		return s2sdk::IsClientAlive(iSlot);
	} catch (...) {
		return false;
	}
}

// m_iAmmo[ammoIndex] of the player's weapon services, -1 when it cannot be read. Only called for a
// slot that passed IsAlive(), which is what makes the s2sdk::GetClientPawn call warning-free; the
// IsConnected() guard keeps a future call site from poking an empty slot.
int GetClientAmmo(int iSlot, int iAmmoIndex) {
	if (__s2sdk_GetClientPawn == nullptr || __s2sdk_GetEntData2 == nullptr || !IsConnected(iSlot)) {
		return -1;
	}
	const int32_t iServicesOffset = SchemaOffset("CBasePlayerPawn", "m_pWeaponServices");
	const int32_t iAmmoOffset = SchemaOffset("CPlayer_WeaponServices", "m_iAmmo");
	if (iServicesOffset < 0 || iAmmoOffset < 0) {
		return -1;
	}
	try {
		void* pPawn = s2sdk::GetClientPawn(iSlot);
		if (pPawn == nullptr) {
			return -1;
		}
		// m_pWeaponServices is a pointer field: its value is the component address.
		void* pServices = *reinterpret_cast<void**>(static_cast<char*>(pPawn) + iServicesOffset);
		if (pServices == nullptr) {
			return -1;
		}
		// m_iAmmo is a uint16_t array; element stride 2.
		return static_cast<int>(s2sdk::GetEntData2(pServices, iAmmoOffset + iAmmoIndex * 2, 2));
	} catch (...) {
		return -1;
	}
}

// <Plugify configs dir>/<relative>; "" when the host does not provide the configs dir.
std::string ConfigPath(std::string_view relative) {
	if (!plg::GetConfigsDir) {
		return {};
	}
	std::string path(plg::GetConfigsDir());
	if (path.empty()) {
		return {};
	}
	if (path.back() != '/') {
		path.push_back('/');
	}
	path.append(relative);
	return path;
}

class HealthshotPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

private:
	// The original loaded the file in Load() and refused to start without it; the port logs one
	// error and stays disabled (fail soft, PITFALLS A1).
	void LoadConfig();
	// VIP_OnPlayerSpawn of the original.
	void OnPlayerSpawn(int iSlot, bool bIsVIP);

	bool m_loaded = false;
	int m_roundMin = 0;
};

HealthshotPlugin g_healthshotPlugin;

plg::PluginResult HealthshotPlugin::OnPluginStart() {
	try {
		// The map gate first: nothing below may assume a map (PITFALLS B1).
		InitMapGate();

		// Reads a file only, no s2sdk call: safe even with the gate closed.
		LoadConfig();

		// The original registered the spawn callback and the feature in AllPluginsLoaded. The
		// feature has no callbacks, so its core entry holds no pointer into this library.
		g_pVIPCore->VIP_OnPlayerSpawn([this](int iSlot, int /*iTeam*/, bool bIsVIP) {
			// The callback runs on the game thread from the vip plugin: no exception may leave it.
			try {
				OnPlayerSpawn(iSlot, bIsVIP);
			} catch (...) {
			}
		});
		g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_INT, TOGGLABLE);

		LogInfo("vip_healthshot: ready");
	} catch (const std::exception& ex) {
		LogError(std::string("vip_healthshot: OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogError("vip_healthshot: OnPluginStart failed with an unknown exception");
	}
	return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
}

plg::PluginResult HealthshotPlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	try {
		PollMapGate();
	} catch (const std::exception& ex) {
		LogError(std::string("vip_healthshot: OnPluginUpdate failed: ") + ex.what());
	} catch (...) {
		LogError("vip_healthshot: OnPluginUpdate failed with an unknown exception");
	}
	return {};
}

plg::PluginResult HealthshotPlugin::OnPluginEnd() {
	try {
		// Drops the VIP core callbacks (the feature itself has no callbacks, so its core entry
		// holds no pointer into this library).
		g_pVIPCore->VIP_OnPlayerSpawnUnregister();
	} catch (...) {
		// The core may already be gone during a full unload; the library must still drop its own
		// s2sdk state below (PITFALLS A7).
	}
	// PITFALLS A7: `plg reload` must leave nothing behind that points into this library, and no
	// stale schema offset may survive the unload (C8).
	ShutdownMapGate();
	g_schemaOffsets.Clear();
	return {};
}

void HealthshotPlugin::LoadConfig() {
	m_loaded = false;
	const std::string path = ConfigPath(kConfigFile);
	if (path.empty()) {
		LogError("vip_healthshot: the Plugify configs directory is unavailable - the module stays "
				 "disabled (no healthshots)");
		return;
	}
	const std::optional<vip_healthshot::KvNode> config = vip_healthshot::LoadKv1File(path);
	if (!config) {
		LogError(std::string("vip_healthshot: failed to load ") + path +
				 " - the module stays disabled (no healthshots)");
		return;
	}
	// The original's KeyValues::GetInt("round_min"): 0 when the key or its value is missing.
	m_roundMin = config->GetInt("round_min");
	m_loaded = true;
}

void HealthshotPlugin::OnPlayerSpawn(int iSlot, bool bIsVIP) {
	if (!m_loaded || !bIsVIP || iSlot < 0 || iSlot >= kMaxSlots) {
		return;
	}
	const bool bPistolRound = g_pVIPCore->VIP_PistolRound();
	const int iTotalRounds = g_pVIPCore->VIP_GetTotalRounds();
	if (!vip_healthshot::ShouldGiveOnRound(m_roundMin, bPistolRound, iTotalRounds)) {
		return;
	}
	if (!IsAlive(iSlot)) {
		return;
	}
	if (__s2sdk_GiveNamedItem == nullptr) {
		return;
	}
	const int iCount = g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kFeature);
	const int iCurrent = GetClientAmmo(iSlot, kHealthshotAmmoIndex);
	if (iCurrent < 0) {
		return; // no weapon services (or no schema): the original returned here too
	}
	const int iToGive = vip_healthshot::HealthshotsToGive(iCount, iCurrent);
	for (int i = 0; i < iToGive; ++i) {
		try {
			s2sdk::GiveNamedItem(iSlot, plg::string(kItemName));
		} catch (...) {
			return; // a throwing s2sdk call ends this spawn's handout, never the callback
		}
	}
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_healthshotPlugin)
