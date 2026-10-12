#pragma once

#include <Windows.h>
#include <string>
#include <vector>

// A small, dependency-free INI reader.
//
// Supported syntax (deliberately close to what YR itself accepts):
//
//   ; comment            - comment lines start with ';' or '#'
//   [Section]            - section header, case-insensitive
//   Key=Value            - key/value pair, keys are case-insensitive
//   Key = Value          - surrounding whitespace is trimmed
//   Key=                 - empty value
//
// Duplicate keys keep the first occurrence, so a default file can be
// overridden by simply appending nothing.
//
// Keys read before any section header land in the implicit "" section.

namespace Karst
{
	class INIConfigReader
	{
	public:
		INIConfigReader() = default;
		~INIConfigReader() = default;

		INIConfigReader(const INIConfigReader&) = delete;
		INIConfigReader& operator=(const INIConfigReader&) = delete;

		// Replace all loaded data. `filePath` is only remembered for diagnostics.
		bool LoadFile(const wchar_t* filePath);
		bool LoadFile(const char* filePath);

		// Drop everything.
		void Clear();

		// True when `section` exists (even if it holds no keys).
		bool HasSection(const char* section) const;

		// True when `section`/`key` is present, even with an empty value.
		bool HasKey(const char* section, const char* key) const;

		// Returns `fallback` when the key is missing.
		std::string GetString(const char* section, const char* key, const char* fallback = "") const;

		// Integer parsing accepts decimal, hex (0x..), and a leading sign.
		int         GetInt(const char* section, const char* key, int fallback = 0) const;
		unsigned    GetUnsigned(const char* section, const char* key, unsigned fallback = 0) const;

		// Boolean parsing accepts 1/0, yes/no, true/false, on/off (case-insensitive).
		bool GetBool(const char* section, const char* key, bool fallback = false) const;

		const wchar_t* GetPath() const { return this->Path.c_str(); }

	private:
		std::wstring Path;

		// std::string keys with _stricmp-style comparison: the reader lowercases
		// section/key names on insert and on lookup.
		struct Entry
		{
			std::string Key;
			std::string Value;
		};

		struct Section
		{
			std::string Name;
			std::vector<Entry> Entries;
		};

		std::vector<Section> Sections;

		Section* FindSection(const char* name);
		const Section* FindSection(const char* name) const;
	};

	// Case-insensitive helpers used by the reader and by callers that want the
	// same rules without pulling in the whole reader.
	std::string ToLower(const std::string& value);
	bool ParseBool(const std::string& value, bool fallback);
}
