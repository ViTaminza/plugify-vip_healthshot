// vip_healthshot unit tests — the engine-independent part of the plugin:
//   * the original module's round condition (round_min vs the pistol round and the played rounds),
//   * the healthshot top-up count (feature value minus the shots already carried),
//   * the plugin's own read-only KV1 reader (src/kv1.hpp) on the production vip_healthshot.ini and
//     on the grammar/error cases the file must not regress on,
//   * the map-gated schema-offset cache policy (src/schema_cache.hpp, PITFALLS C8): no lookup while
//     the map gate is closed, only successful offsets memoised, Clear() forgets them.
//
// No s2sdk, no vip bindings and no pisexcompat: every include is one of the plugin's own headers,
// so this target links against nothing engine-side. Run by ../../test.sh (inside the Steam Runtime
// image, cwd = build/tests).

#include "kv1.hpp"
#include "round_rule.hpp"
#include "schema_cache.hpp"

#include <cstdint>
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>

// CMake passes the absolute tests/data directory; the fallback keeps a plain `g++ ...` check working
// from the plugin directory.
#ifndef VIP_HEALTHSHOT_TEST_DATA_DIR
#define VIP_HEALTHSHOT_TEST_DATA_DIR "tests/data"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool actual, const std::string& what) {
	++g_checks;
	if (!actual) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEq(bool actual, bool expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %d, got %d)\n", what.c_str(), expected ? 1 : 0, actual ? 1 : 0);
	}
}

void CheckInt(long long actual, long long expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %lld, got %lld)\n", what.c_str(), expected, actual);
	}
}

void CheckStr(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected \"%s\", got \"%s\")\n", what.c_str(), std::string(expected).c_str(),
					std::string(actual).c_str());
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

void TestHealthshotsToGive() {
	using vip_healthshot::HealthshotsToGive;

	// The original's `for (int i = 0; i < iCount - iCurrent; ++i)`: an empty pouch gets the whole
	// feature value, a partly filled one only the difference.
	CheckInt(HealthshotsToGive(3, 0), 3, "empty pouch gets the feature value");
	CheckInt(HealthshotsToGive(3, 2), 1, "partly filled pouch gets the difference");
	CheckInt(HealthshotsToGive(3, 3), 0, "a full pouch gets nothing");
	CheckInt(HealthshotsToGive(3, 5), 0, "more shots than the feature allows are never taken away");
	CheckInt(HealthshotsToGive(0, 0), 0, "feature value 0 gives nothing");
	CheckInt(HealthshotsToGive(1, 0), 1, "single healthshot");
	CheckInt(HealthshotsToGive(-1, 0), 0, "an unavailable feature value gives nothing");
	CheckInt(HealthshotsToGive(-1, -1), 0, "unreadable ammo (-1) is the caller's skip case");
}

// The production config, byte for byte the file package.sh ships
// (ref/prod-27016/addons/configs/vip/vip_healthshot.ini), including its comment.
constexpr std::string_view kProductionIni =
	"\"VIP\"\n"
	"{\n"
	"    \"round_min\"     \"2\"     //Только со второго будут выданы шприцы\n"
	"}\n";

void TestKv1ProductionConfig() {
	using vip_healthshot::ParseKv1;

	const std::optional<vip_healthshot::KvNode> config = ParseKv1(kProductionIni);
	Check(config.has_value(), "the production vip_healthshot.ini parses");
	if (!config.has_value()) {
		return;
	}
	// A single "VIP" { ... } block is the file's root, as in Valve's loader.
	CheckStr(config->name, "VIP", "the root block is unwrapped");
	CheckInt(config->GetInt("round_min"), 2, "production round_min is 2");
	CheckStr(config->GetString("round_min"), "2", "the raw value is kept as a string");
	// Key lookup is case-insensitive, like Valve's KeyValues.
	Check(config->Find("ROUND_MIN") != nullptr, "key lookup is case-insensitive");
	Check(config->Find("round_min_extra") == nullptr, "a longer name is not the same key");
	// Missing keys keep the caller's default.
	CheckInt(config->GetInt("missing", 7), 7, "a missing key keeps the default");
	CheckStr(config->GetString("missing", "fallback"), "fallback", "a missing string keeps the default");
	CheckStr(config->GetString("missing"), "", "a missing string defaults to empty");

	// The same file read from tests/data, the way the plugin reads it from the configs dir.
	const std::string path = std::string(VIP_HEALTHSHOT_TEST_DATA_DIR) + "/vip_healthshot.ini";
	const std::optional<vip_healthshot::KvNode> fromFile = vip_healthshot::LoadKv1File(path);
	Check(fromFile.has_value(), "the shipped vip_healthshot.ini loads from disk (" + path + ")");
	if (fromFile.has_value()) {
		CheckInt(fromFile->GetInt("round_min", 0), 2, "the file on disk still says round_min 2");
	}
	Check(!vip_healthshot::LoadKv1File(std::string(VIP_HEALTHSHOT_TEST_DATA_DIR) + "/missing.ini").has_value(),
		  "a missing config file is rejected");
}

void TestKv1Grammar() {
	using vip_healthshot::ParseKv1;

	// Unquoted tokens, outside and inside a block.
	const std::optional<vip_healthshot::KvNode> unquoted = ParseKv1("VIP { round_min 2 }");
	Check(unquoted.has_value(), "unquoted tokens parse");
	if (unquoted.has_value()) {
		CheckInt(unquoted->GetInt("round_min"), 2, "unquoted value is read");
	}

	// A flat file (no wrapping root block) keeps every key at the top level.
	const std::optional<vip_healthshot::KvNode> flat = ParseKv1("\"round_min\" \"3\"");
	Check(flat.has_value(), "a flat file parses");
	if (flat.has_value()) {
		CheckInt(flat->GetInt("round_min"), 3, "a flat file keeps its top-level keys");
	}

	// Comments: // to the end of the line and /* */ anywhere, including braces and quotes inside.
	const std::optional<vip_healthshot::KvNode> comments = ParseKv1(R"(
// "VIP" { "round_min" "9" } in a line comment
"VIP"
{
    /* "round_min" "9" // a block comment */
    "round_min" "4" // the real value
}
)");
	Check(comments.has_value(), "comments parse");
	if (comments.has_value()) {
		CheckInt(comments->GetInt("round_min"), 4, "commented-out keys are ignored");
	}

	// #base / #include / #define lines are skipped (no inclusion).
	const std::optional<vip_healthshot::KvNode> base =
		ParseKv1("#base \"other.ini\"\n\"VIP\" { \"round_min\" \"5\" }\n");
	Check(base.has_value(), "#base lines are skipped");
	if (base.has_value()) {
		CheckInt(base->GetInt("round_min"), 5, "the keys after a #base line are read");
	}

	// Escapes inside quotes, the same set pisexcompat accepted.
	const std::optional<vip_healthshot::KvNode> escapes = ParseKv1(R"("VIP" { "s" "a\"b\\c\td" })");
	Check(escapes.has_value(), "an escaped string parses");
	if (escapes.has_value()) {
		CheckStr(escapes->GetString("s"), "a\"b\\c\td", "escapes are unescaped");
	}

	// CRLF line endings (the file may be edited on Windows).
	const std::optional<vip_healthshot::KvNode> crlf =
		ParseKv1("\"VIP\"\r\n{\r\n    \"round_min\"    \"6\"\r\n}\r\n");
	Check(crlf.has_value(), "CRLF parses");
	if (crlf.has_value()) {
		CheckInt(crlf->GetInt("round_min"), 6, "CRLF round_min is read");
	}

	// Valve's atoi fallback for values that are not plain integers.
	const std::optional<vip_healthshot::KvNode> atoi =
		ParseKv1("\"VIP\" { \"a\" \"2x\" \"b\" \"abc\" \"c\" \"\" }");
	Check(atoi.has_value(), "a non-numeric value parses");
	if (atoi.has_value()) {
		CheckInt(atoi->GetInt("a"), 2, "\"2x\" reads as 2 (Valve atoi)");
		CheckInt(atoi->GetInt("b", 7), 0, "\"abc\" reads as 0 (Valve atoi)");
		CheckInt(atoi->GetInt("c", 7), 7, "an empty value keeps the default");
	}

	// The first of two equally named keys wins, as in Valve's FindKey.
	const std::optional<vip_healthshot::KvNode> duplicate = ParseKv1("\"VIP\" { \"round_min\" \"1\" \"round_min\" \"2\" }");
	Check(duplicate.has_value(), "duplicate keys parse");
	if (duplicate.has_value()) {
		CheckInt(duplicate->GetInt("round_min"), 1, "the first duplicate key wins");
	}

	// Malformed files are rejected as a whole instead of yielding a half-read tree.
	Check(!ParseKv1("\"VIP\" { \"round_min\" \"2\"").has_value(), "a missing '}' is invalid");
	Check(!ParseKv1("\"VIP\" { \"round_min\" \"2 }").has_value(), "an unterminated string is invalid");
	Check(!ParseKv1("\"VIP\" }\n").has_value(), "a stray top-level '}' is invalid");
}

// A fake engine lookup for the schema-cache policy test; every call is counted.
int g_lookups = 0;

int32_t FakeLookup(const char* szClassName, const char* szFieldName) {
	++g_lookups;
	if (std::string_view(szClassName) == "CBasePlayerPawn" &&
		std::string_view(szFieldName) == "m_pWeaponServices") {
		return 0x1234;
	}
	return -1; // the field is unknown to the fake schema
}

void TestSchemaOffsetCache() {
	vip_healthshot::SchemaOffsetCache cache;
	g_lookups = 0;

	// PITFALLS C8: while the map gate is closed GetSchemaOffset must not be called at all.
	CheckInt(cache.Offset(false, "CBasePlayerPawn", "m_pWeaponServices", &FakeLookup), -1,
			 "a closed gate answers -1");
	CheckInt(g_lookups, 0, "a closed gate never reaches the engine lookup");
	CheckInt(static_cast<long long>(cache.CachedCount()), 0, "a closed gate caches nothing");

	// An open gate resolves the field once and memoises the successful offset.
	CheckInt(cache.Offset(true, "CBasePlayerPawn", "m_pWeaponServices", &FakeLookup), 0x1234,
			 "an open gate resolves the field");
	CheckInt(g_lookups, 1, "the first open-gate lookup runs once");
	CheckInt(cache.Offset(true, "CBasePlayerPawn", "m_pWeaponServices", &FakeLookup), 0x1234,
			 "a cached offset is handed out again");
	CheckInt(g_lookups, 1, "a cached offset is not resolved again");

	// A failed lookup is not cached: the field is retried on the next call instead of being hidden
	// for the whole session.
	CheckInt(cache.Offset(true, "CPlayer_WeaponServices", "m_iAmmo", &FakeLookup), -1,
			 "an unknown field answers -1");
	CheckInt(g_lookups, 2, "an unknown field is looked up");
	CheckInt(cache.Offset(true, "CPlayer_WeaponServices", "m_iAmmo", &FakeLookup), -1,
			 "an unknown field answers -1 again");
	CheckInt(g_lookups, 3, "an unknown field is retried");
	CheckInt(static_cast<long long>(cache.CachedCount()), 1, "only the successful offset is remembered");

	// A closed gate does not even hand out a remembered offset (the caller uses -1 as "no schema
	// read in this window").
	CheckInt(cache.Offset(false, "CBasePlayerPawn", "m_pWeaponServices", &FakeLookup), -1,
			 "a closed gate hides the cached offset");
	CheckInt(g_lookups, 3, "a closed gate does not call the lookup even on a cache hit");

	// Clear() (called from OnPluginEnd) forgets everything, so a reload resolves the fields again.
	cache.Clear();
	CheckInt(static_cast<long long>(cache.CachedCount()), 0, "Clear() forgets every offset");
	CheckInt(cache.Offset(true, "CBasePlayerPawn", "m_pWeaponServices", &FakeLookup), 0x1234,
			 "the field resolves again after Clear()");
	CheckInt(g_lookups, 4, "a reload resolves the field again");

	// Defensive argument checks: nothing reaches the engine.
	CheckInt(cache.Offset(true, nullptr, "m_pWeaponServices", &FakeLookup), -1, "a null class name answers -1");
	CheckInt(cache.Offset(true, "CBasePlayerPawn", nullptr, &FakeLookup), -1, "a null field name answers -1");
	CheckInt(cache.Offset(true, "", "m_iAmmo", &FakeLookup), -1, "an empty class name answers -1");
	CheckInt(cache.Offset(true, "CBasePlayerPawn", "m_pWeaponServices", nullptr), -1, "a null lookup answers -1");
	CheckInt(g_lookups, 4, "defensive calls never reach the engine");
}

} // namespace

int main() {
	std::printf("vip_healthshot tests\n");
	TestRoundRule();
	TestHealthshotsToGive();
	TestKv1ProductionConfig();
	TestKv1Grammar();
	TestSchemaOffsetCache();
	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
