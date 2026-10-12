#include <Config/KarstConfig.h>

#include <Version.h>

namespace Karst
{
	namespace
	{
		const char* const SECTION_MAIN = "Karst";

		const char* const KEY_ENABLED = "Enabled";
		const char* const KEY_LOG_LEVEL = "LogLevel";
		const char* const KEY_DEBUG_LOG = "DebugLog";
		const char* const KEY_ACTIVE_MESSAGE = "ShowActiveMessage";
		const char* const KEY_VALIDATION_DIALOG = "IniValidationDialog";
	}

	const char* Config::LogLevelName(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Off:   return "off";
		case LogLevel::Error: return "error";
		case LogLevel::Warn:  return "warn";
		case LogLevel::Info:  return "info";
		case LogLevel::Debug: return "debug";
		}

		return "info";
	}

	LogLevel Config::ParseLogLevel(const std::string& value, LogLevel fallback)
	{
		const std::string lowered = ToLower(value);

		if (lowered == "off" || lowered == "none" || lowered == "silent")
			return LogLevel::Off;
		if (lowered == "error" || lowered == "err")
			return LogLevel::Error;
		if (lowered == "warn" || lowered == "warning")
			return LogLevel::Warn;
		if (lowered == "info" || lowered == "information")
			return LogLevel::Info;
		if (lowered == "debug" || lowered == "trace" || lowered == "verbose")
			return LogLevel::Debug;

		return fallback;
	}

	void Config::Reset()
	{
		this->Data.Clear();
		this->FilePath.clear();

		this->IsLoaded = false;
		this->IsEnabled = true;
		this->IsDebugLog = false;
		this->IsActiveMessage = true;
		this->IsIniValidationDialog = true;
		this->Level = LogLevel::Info;
	}

	bool Config::LoadFrom(const wchar_t* filePath)
	{
		this->Reset();

		this->FilePath = filePath ? filePath : L"";
		this->IsLoaded = this->Data.LoadFile(filePath);

		if (!this->IsLoaded)
			return false;

		this->IsEnabled = this->Data.GetBool(SECTION_MAIN, KEY_ENABLED, true);
		this->IsDebugLog = this->Data.GetBool(SECTION_MAIN, KEY_DEBUG_LOG, false);
		this->IsActiveMessage = this->Data.GetBool(SECTION_MAIN, KEY_ACTIVE_MESSAGE, true);
		this->IsIniValidationDialog = this->Data.GetBool(SECTION_MAIN, KEY_VALIDATION_DIALOG, true);
		this->Level = ParseLogLevel(
			this->Data.GetString(SECTION_MAIN, KEY_LOG_LEVEL, "info"),
			LogLevel::Info);

		return this->IsEnabled;
	}

	bool Config::Load()
	{
		this->Reset();

		wchar_t gameDirectory[MAX_PATH] = { 0 };
		if (::GetModuleFileNameW(nullptr, gameDirectory, MAX_PATH) == 0)
			return false;

		// Trim the executable name to keep the directory.
		wchar_t* lastSeparator = nullptr;
		for (wchar_t* cursor = gameDirectory; *cursor; ++cursor)
		{
			if (*cursor == L'\\' || *cursor == L'/')
				lastSeparator = cursor;
		}

		if (!lastSeparator)
			return false;

		*(lastSeparator + 1) = L'\0';

		std::wstring path = gameDirectory;
		path += L"\\";

		// FileName() is plain ASCII, so widen it one character at a time.
		for (const char* c = FileName(); *c; ++c)
			path += static_cast<wchar_t>(static_cast<unsigned char>(*c));

		return this->LoadFrom(path.c_str());
	}
}
