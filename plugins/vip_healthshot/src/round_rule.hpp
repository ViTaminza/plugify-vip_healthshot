#pragma once
// The round condition of the original VIP_Healthshot (and VIP_Items) module: round_min 1 counts
// from the first non-pistol round, a larger round_min from that many played rounds on but never
// on a pistol round, and 0 always applies.
//
// Engine-free so the module test can drive it (tests/vip_healthshot_tests.cpp).

namespace vip_healthshot {

inline bool ShouldGiveOnRound(int roundMin, bool pistolRound, int totalRounds) {
	return (roundMin == 1 && !pistolRound) || (roundMin <= totalRounds && !pistolRound) || (roundMin == 0);
}

} // namespace vip_healthshot
