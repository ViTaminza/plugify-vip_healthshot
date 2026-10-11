// vip_healthshot — Plugify port of the Pisex VIP module "VIP_Healthshot"
// (the original's vip_healthshot.cpp).
//
// Behaviour kept identical:
//   * config <configs>/vip_healthshot/vip_healthshot.ini, the VIP KeyValues file with
//     "round_min" (addons/configs/vip/vip_healthshot.ini of the Metamod plugin).
//   * the feature is registered as "healthshot" (VIP_INT, TOGGLABLE).
//   * on VIP_OnPlayerSpawn (the original's OnPlayerSpawn callback) a VIP player gets
//     featureValue - currentHealthshotAmmo weapon_healthshot items, on the same round
//     condition as the original (round_min against the pistol round and the played rounds, see
//     round_rule.hpp).
//   * the current count is the healthshot slot of the pawn's ammo array (ammo index 20),
//     exactly the m_iAmmo[20] the original read.
//
// Port notes:
//   * CCSPlayerController::FromSlot + m_hPlayerPawn + IsAlive -> pisex::IsAlive().
//   * CCSPlayer_ItemServices::GiveNamedItem("weapon_healthshot") -> s2sdk::GiveNamedItem(slot, ...).
//   * m_pWeaponServices->m_iAmmo[20] has no s2sdk accessor, so it is read through the schema
//     (CBasePlayerPawn::m_pWeaponServices + CPlayer_WeaponServices::m_iAmmo, the same fields the
//     Pisex SchemaEntity headers name). When the lookup fails, the item count is treated as 0 -
//     the same result the original produced through a null weapon-services pointer.
//   * The original refused to load on a missing config; Plugify must not fail hard, so the module
//     logs the error and stays disabled (no healthshots) instead.

#include <pisex/pisex.hpp>
#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include "round_rule.hpp"
#include "vip_api_compat.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

// The ported modules keep the original code's unqualified spellings (VIP_INT, TOGGLABLE,
// DISABLED, ...): the consumer wrapper declares them as plain enums in namespace vip.
using namespace vip;

IVIPApi* g_pVIPCore = &IVIPApi::Get();

constexpr int kMaxSlots = 64;
// Ammo slot of weapon_healthshot (AmmoIndex "AMMO_HEALTHSHOT" = 20 in the CS2 weapon scripts).
constexpr int kHealthshotAmmoIndex = 20;
constexpr const char* kItemName = "weapon_healthshot";
constexpr const char* kFeature = "healthshot";
constexpr const char* kConfigFile = "vip_healthshot.ini";

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

// m_iAmmo[ammoIndex] of the player's weapon services, -1 when it cannot be read.
int GetClientAmmo(int iSlot, int iAmmoIndex) {
	// s2sdk's GetClientPawn warns "invalid player slot: N" (to the server console) for every call
	// on a slot without a controller, so an empty slot is filtered out here.
	if (!__s2sdk_GetClientPawn || !__s2sdk_GetEntData2 || iSlot < 0 || iSlot >= kMaxSlots ||
		!pisex::IsConnected(iSlot)) {
		return -1;
	}
	try {
		void* pawn = s2sdk::GetClientPawn(iSlot);
		if (!pawn) {
			return -1;
		}
		const int servicesOffset = pisex::SchemaOffset("CBasePlayerPawn", "m_pWeaponServices");
		const int ammoOffset = pisex::SchemaOffset("CPlayer_WeaponServices", "m_iAmmo");
		if (servicesOffset < 0 || ammoOffset < 0) {
			return -1;
		}
		void* services = *reinterpret_cast<void**>(static_cast<char*>(pawn) + servicesOffset);
		if (!services) {
			return -1;
		}
		// m_iAmmo is a uint16_t array; element stride 2.
		return static_cast<int>(s2sdk::GetEntData2(services, ammoOffset + iAmmoIndex * 2, 2));
	} catch (...) {
		return -1;
	}
}

class HealthshotPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

private:
	void LoadConfig();
	void OnPlayerSpawn(int iSlot, bool bIsVIP);

	bool m_loaded = false;
	int m_roundMin = 0;
};

HealthshotPlugin g_healthshotPlugin;

plg::PluginResult HealthshotPlugin::OnPluginStart() {
	try {
		// Never fatal: only logs when the s2sdk bindings are missing.
		pisex::Init("vip_healthshot");

		LoadConfig();

		g_pVIPCore->VIP_OnPlayerSpawn([this](int iSlot, int /*iTeam*/, bool bIsVIP) {
			OnPlayerSpawn(iSlot, bIsVIP);
		});
		g_pVIPCore->VIP_RegisterFeature(kFeature, VIP_INT, TOGGLABLE);

		LogInfo("vip_healthshot: ready");
	} catch (const std::exception& ex) {
		LogError(std::string("vip_healthshot: OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogError("vip_healthshot: OnPluginStart failed with an unknown exception");
	}
	return {};
}

plg::PluginResult HealthshotPlugin::OnPluginUpdate(std::chrono::milliseconds /*dt*/) {
	pisex::Poll();
	return {};
}

plg::PluginResult HealthshotPlugin::OnPluginEnd() {
	// Drops the VIP core callbacks (the feature itself has no callbacks, so its core entry holds no
	// pointer into this library).
	g_pVIPCore->VIP_OnPlayerSpawnUnregister();
	pisex::Shutdown();
	return {};
}

void HealthshotPlugin::LoadConfig() {
	m_loaded = false;
	pisex::KeyValues kv("VIP");
	if (!kv.LoadFromFile(kConfigFile)) {
		LogError(std::string("vip_healthshot: failed to load ") + kConfigFile +
				 " - the module stays disabled (no healthshots)");
		return;
	}
	m_roundMin = kv.GetInt("round_min");
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
	if (!pisex::IsAlive(iSlot)) {
		return;
	}
	if (!__s2sdk_GiveNamedItem) {
		return;
	}
	const int iCount = g_pVIPCore->VIP_GetClientFeatureInt(iSlot, kFeature);
	const int iCurrent = GetClientAmmo(iSlot, kHealthshotAmmoIndex);
	if (iCurrent < 0) {
		return; // no weapon services (or no schema): the original returned here too
	}
	for (int i = 0; i < iCount - iCurrent; ++i) {
		s2sdk::GiveNamedItem(iSlot, plg::string(kItemName));
	}
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_healthshotPlugin)
