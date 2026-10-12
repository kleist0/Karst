#pragma once

#include <Config/INIConfigReader.h>

#include <string>

// Typed view over karst.ini.
//
// The file is expected next to gamemd.exe and is entirely optional: when it is
// missing or a key is absent, the defaults below apply. A key that is present
// but invalid (e.g. LogLevel=banana) also falls back to the default, so a typo
// can never produce undefined behaviour.
//
//   [Karst]
//   Enabled=yes          ; master switch, "no" disables the extension
//   LogLevel=info        ; off | error | warn | info | debug
//   DebugLog=no          ; write a timestamped karst.log next to the game
//   ShowActiveMessage=yes ; post an on-screen banner once the game is running
//
//   [Features]
//   ; Add your own feature switches here.

namespace Karst
{
	enum class LogLevel
	{
		Off = 0,
		Error,
		Warn,
		Info,
		Debug,
	};

	class Config
	{
	public:
		// Name of the file looked up next to the game executable.
		static const char* FileName() { return "karst.ini"; }

		Config() = default;

		// Load <game directory>\karst.ini. Returns false when the file is absent,
		// unreadable, or sets Enabled=no. The Config still holds valid defaults
		// in every failure case.
		bool Load();

		// Load an explicit path (used by Load() and by tests/tools).
		bool LoadFrom(const wchar_t* filePath);

		// Load the defaults and forget everything read from disk.
		void Reset();

		bool            Enabled() const { return this->IsEnabled; }
		LogLevel        GetLogLevel() const { return this->Level; }
		bool            DebugLog() const { return this->IsDebugLog; }
		bool            ShowActiveMessage() const { return this->IsActiveMessage; }
		bool            ShowIniValidationDialog() const { return this->IsIniValidationDialog; }
		const std::wstring& Path() const { return this->FilePath; }

		// True when the file was found and parsed.
		bool Loaded() const { return this->IsLoaded; }
		bool HasSection(const char* section) const { return this->Data.HasSection(section); }
		bool HasKey(const char* section, const char* key) const { return this->Data.HasKey(section, key); }
		std::string GetRaw(const char* section, const char* key, const char* fallback = "") const
		{
			return this->Data.GetString(section, key, fallback);
		}

		// Escape hatch for features that want to read their own keys, e.g.
		//   config.Reader().GetInt("MyFeature", "SomeKey", 5)
		const INIConfigReader& Reader() const { return this->Data; }

		static const char* LogLevelName(LogLevel level);
		static LogLevel ParseLogLevel(const std::string& value, LogLevel fallback);

	private:
		INIConfigReader Data;
		std::wstring    FilePath;

		bool     IsLoaded = false;
		bool     IsEnabled = true;
		bool     IsDebugLog = false;
		bool     IsActiveMessage = true;
		bool     IsIniValidationDialog = true;
		LogLevel Level = LogLevel::Info;
	};
}
