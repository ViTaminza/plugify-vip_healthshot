#pragma once
// The round condition of the original VIP_Healthshot (and VIP_Items) module:
//
//   (iRoundMin == 1 && !pistolRound) || (iRoundMin <= totalRounds && !pistolRound) || iRoundMin == 0
//
// Engine-free so the module test can drive it (tests/vip_healthshot_tests.cpp).

namespace vip_healthshot {

inline bool ShouldGiveOnRound(int roundMin, bool pistolRound, int totalRounds) {
	return (roundMin == 1 && !pistolRound) || (roundMin <= totalRounds && !pistolRound) || (roundMin == 0);
}

} // namespace vip_healthshot
