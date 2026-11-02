#pragma once
// The round condition of the original VIP_Healthshot module: round_min 1 counts from the first
// non-pistol round, a larger round_min from that many played rounds on but never on a pistol
// round, and 0 always applies.
//
// Engine-free so the module test can drive it (tests/vip_healthshot_tests.cpp).

namespace vip_healthshot {

inline bool ShouldGiveOnRound(int roundMin, bool pistolRound, int totalRounds) {
	return (roundMin == 1 && !pistolRound) || (roundMin <= totalRounds && !pistolRound) || (roundMin == 0);
}

// How many weapon_healthshot items the spawn handler hands out: the original's
//   for (int i = 0; i < iCount - iCurrent; ++i) GiveNamedItem("weapon_healthshot");
// i.e. the missing part of the feature value, and never a removal when the player already carries
// more shots than the feature allows. `currentAmmo < 0` (unreadable ammo) is the caller's "skip"
// case and yields 0 here too.
inline int HealthshotsToGive(int featureCount, int currentAmmo) {
	const int iMissing = featureCount - currentAmmo;
	return iMissing > 0 ? iMissing : 0;
}

} // namespace vip_healthshot
