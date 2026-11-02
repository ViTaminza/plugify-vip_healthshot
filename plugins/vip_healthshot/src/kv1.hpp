// vip_healthshot — a read-only Valve KeyValues (KV1) reader for the plugin's config.
//
// The module is pure s2sdk and therefore self-contained (docs/PLUGIN_STANDARD.md §7): the
// pisexcompat KeyValues the port used is gone, and this header replaces exactly the part of it the
// plugin needs.
//
// The production config is the original Metamod file, unchanged
// (ref/prod-27016/addons/configs/vip/vip_healthshot.ini):
//
//   "VIP"
//   {
//       "round_min"     "2"     //Только со второго будут выданы шприцы
//   }
//
// The grammar is the one pisexcompat's KeyValues accepted (the same code the earlier wave of
// plugins now ships in their own src/, PLUGIN_STANDARD §7):
//   * quoted ("key" "value") and unquoted tokens, nested "key" { ... } blocks, a key may carry a
//     value and a block,
//   * // and /* */ comments, escapes \" \\ \n \r \t inside quotes,
//   * #base / #include / #define lines are skipped (no inclusion),
//   * an unterminated string, a missing '}' or a stray top-level '}' makes the file invalid
//     (nullopt; the caller keeps its default instead of a half-read tree).
// Like Valve's loader, a file that is a single "Root" { ... } block is returned as that block, so
// `ParseKv1(text)->GetInt("round_min")` reads the key inside "VIP" { ... }. Key lookup is
// case-insensitive.
//
// Header-only (inline): included by src/plugin.cpp and by tests/vip_healthshot_tests.cpp, so the
// test target needs no engine, no bindings and no pisexcompat.

#pragma once

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vip_healthshot {

struct KvNode {
	std::string name;
	std::string value;
	bool hasValue = false;
	std::vector<KvNode> children;

	// First child with that name (case-insensitive), or nullptr.
	const KvNode* Find(std::string_view key) const;
	// The child's value; `def` when the child is missing or has no value.
	std::string_view GetString(std::string_view key, std::string_view def = {}) const;
	// The child's value as an int, with Valve's atoi fallback ("5x" -> 5, "abc" -> 0); `def` when
	// the child is missing or its value is empty.
	int GetInt(std::string_view key, int def = 0) const;
};

namespace detail {

inline char Lower(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline bool EqualNoCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (Lower(a[i]) != Lower(b[i])) {
			return false;
		}
	}
	return true;
}

inline std::string_view Trim(std::string_view text) {
	std::size_t begin = 0;
	std::size_t end = text.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
		++begin;
	}
	while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(begin, end - begin);
}

// The tokenizer and grammar of pisexcompat's KeyValues, building a KvNode tree instead:
//
//   file  := node* EOF
//   node  := token [ token ] [ '{' node* '}' ]
//   token := '"' escaped '"' | raw
class Parser {
public:
	explicit Parser(std::string_view text) : m_text(text) {}

	bool Parse(KvNode& root) {
		ParseNodes(root, false);
		return !m_failed;
	}

private:
	void SkipSpace() {
		while (m_pos < m_text.size()) {
			const char c = m_text[m_pos];
			if (std::isspace(static_cast<unsigned char>(c))) {
				++m_pos;
				continue;
			}
			if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
				m_pos += 2;
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
					++m_pos;
				}
				continue;
			}
			if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*') {
				m_pos += 2;
				while (m_pos + 1 < m_text.size() && !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/')) {
					++m_pos;
				}
				m_pos = (m_pos + 1 < m_text.size()) ? m_pos + 2 : m_text.size();
				continue;
			}
			break;
		}
	}

	// One token; false at the end of the input or at a '}'.
	bool ReadToken(std::string& out) {
		SkipSpace();
		if (m_pos >= m_text.size() || m_text[m_pos] == '}') {
			return false;
		}
		out.clear();
		if (m_text[m_pos] == '"') {
			++m_pos;
			while (m_pos < m_text.size()) {
				const char c = m_text[m_pos++];
				if (c == '\\' && m_pos < m_text.size()) {
					const char esc = m_text[m_pos++];
					switch (esc) {
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					default:
						out.push_back('\\');
						out.push_back(esc);
						break;
					}
					continue;
				}
				if (c == '"') {
					return true;
				}
				out.push_back(c);
			}
			m_failed = true; // unterminated string
			return true;
		}
		while (m_pos < m_text.size()) {
			const char c = m_text[m_pos];
			if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '"') {
				break;
			}
			if (c == '/' && m_pos + 1 < m_text.size() &&
				(m_text[m_pos + 1] == '/' || m_text[m_pos + 1] == '*')) {
				break;
			}
			out.push_back(c);
			++m_pos;
		}
		return true;
	}

	bool PeekIs(char c) {
		SkipSpace();
		return m_pos < m_text.size() && m_text[m_pos] == c;
	}

	void ParseNodes(KvNode& parent, bool insideBlock) {
		while (true) {
			SkipSpace();
			if (m_pos >= m_text.size()) {
				if (insideBlock) {
					m_failed = true; // missing '}'
				}
				return;
			}
			if (m_text[m_pos] == '}') {
				if (!insideBlock) {
					m_failed = true; // stray '}'
				}
				++m_pos;
				return;
			}
			std::string key;
			if (!ReadToken(key)) {
				return;
			}
			if (!key.empty() && key[0] == '#') {
				// #base / #include / #define: no inclusion, skip the line.
				while (m_pos < m_text.size() && m_text[m_pos] != '\n') {
					++m_pos;
				}
				continue;
			}
			parent.children.push_back(KvNode{});
			// Filled through an index: the recursion below may reallocate parent.children.
			const std::size_t index = parent.children.size() - 1;
			parent.children[index].name = std::move(key);
			if (!PeekIs('{')) {
				std::string value;
				if (ReadToken(value)) {
					parent.children[index].value = std::move(value);
					parent.children[index].hasValue = true;
				}
			}
			if (PeekIs('{')) {
				++m_pos;
				KvNode child = std::move(parent.children[index]);
				ParseNodes(child, true);
				parent.children[index] = std::move(child);
			}
		}
	}

	std::string_view m_text;
	std::size_t m_pos = 0;
	bool m_failed = false;
};

} // namespace detail

inline const KvNode* KvNode::Find(std::string_view key) const {
	if (key.empty()) {
		return nullptr;
	}
	for (const KvNode& child : children) {
		if (detail::EqualNoCase(child.name, key)) {
			return &child;
		}
	}
	return nullptr;
}

inline std::string_view KvNode::GetString(std::string_view key, std::string_view def) const {
	const KvNode* found = Find(key);
	if (found == nullptr || !found->hasValue) {
		return def;
	}
	return found->value;
}

inline int KvNode::GetInt(std::string_view key, int def) const {
	const std::string_view text = detail::Trim(GetString(key));
	if (text.empty()) {
		return def;
	}
	int number = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
	if (result.ec != std::errc{}) {
		return std::atoi(std::string(text).c_str()); // Valve's atoi semantics
	}
	return number;
}

inline std::optional<KvNode> ParseKv1(std::string_view text) {
	KvNode root;
	detail::Parser parser(text);
	if (!parser.Parse(root)) {
		return std::nullopt;
	}
	// A single top-level block is the file's root ("VIP" { ... }), as in Valve's loader.
	if (root.children.size() == 1 && !root.children.front().children.empty()) {
		KvNode top = std::move(root.children.front());
		return top;
	}
	return root;
}

inline std::optional<KvNode> LoadKv1File(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::ostringstream content;
	content << file.rdbuf();
	return ParseKv1(content.str());
}

} // namespace vip_healthshot
