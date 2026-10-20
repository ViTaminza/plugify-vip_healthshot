// vip_healthshot unit tests — the engine-independent part: the original module's round condition
// (round_min vs the pistol round and the number of played rounds).
//
// Run by ../../test.sh (inside the Steam Runtime image, cwd = build/tests).

#include "round_rule.hpp"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void CheckEq(bool actual, bool expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected ? 1 : 0, actual ? 1 : 0);
	}
}

void TestRoundRule() {
	using vip_healthshot::ShouldGiveOnRound;

	// round_min 0 = every round.
	CheckEq(ShouldGiveOnRound(0, false, 0), true, "round_min 0, first round");
	CheckEq(ShouldGiveOnRound(0, true, 3), true, "round_min 0, pistol round");

	// round_min 1 = from the first round on, but never on a pistol round.
	CheckEq(ShouldGiveOnRound(1, false, 0), true, "round_min 1, not a pistol round");
	CheckEq(ShouldGiveOnRound(1, true, 0), false, "round_min 1, pistol round");
	CheckEq(ShouldGiveOnRound(1, false, 7), true, "round_min 1, later round");

	// round_min 2 = from the second played round on, never on a pistol round.
	CheckEq(ShouldGiveOnRound(2, false, 1), false, "round_min 2, only 1 played");
	CheckEq(ShouldGiveOnRound(2, false, 2), true, "round_min 2, 2 played");
	CheckEq(ShouldGiveOnRound(2, true, 5), false, "round_min 2, pistol round after switch");
	CheckEq(ShouldGiveOnRound(2, false, 12), true, "round_min 2, later round");

	// The shipped vip_healthshot.ini uses round_min 2.
	CheckEq(ShouldGiveOnRound(2, false, 0), false, "production round_min 2, warmup round");
}

} // namespace

int main() {
	std::printf("vip_healthshot tests\n");
	TestRoundRule();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
