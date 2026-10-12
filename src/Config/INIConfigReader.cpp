#include <Config/INIConfigReader.h>

#include <cctype>
#include <cstdlib>

namespace Karst
{
	namespace
	{
		bool IsSpace(char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n';
		}

		std::string Trim(const std::string& value)
		{
			size_t begin = 0;
			size_t end = value.size();

			while (begin < end && IsSpace(value[begin]))
				++begin;
			while (end > begin && IsSpace(value[end - 1]))
				--end;

			return value.substr(begin, end - begin);
		}

		// Strips a trailing comment introduced by ';' or '#', but only when the
		// marker is at the start of the value or preceded by whitespace. That
		// keeps values such as "C:\path#1" or a colour like "255;0;0" intact.
		std::string StripTrailingComment(const std::string& value)
		{
			for (size_t i = 0; i < value.size(); ++i)
			{
				const char c = value[i];
				if (c != ';' && c != '#')
					continue;

				if (i == 0 || IsSpace(value[i - 1]))
					return value.substr(0, i);
			}

			return value;
		}
	}

	std::string ToLower(const std::string& value)
	{
		std::string result = value;
		for (char& c : result)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

		return result;
	}

	bool ParseBool(const std::string& value, bool fallback)
	{
		const std::string lowered = ToLower(Trim(value));

		if (lowered == "1" || lowered == "yes" || lowered == "true" || lowered == "on")
			return true;
		if (lowered == "0" || lowered == "no" || lowered == "false" || lowered == "off")
			return false;

		return fallback;
	}

	bool INIConfigReader::LoadFile(const wchar_t* filePath)
	{
		this->Clear();

		if (!filePath || !*filePath)
			return false;

		this->Path = filePath;

		HANDLE file = ::CreateFileW(
			filePath,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (file == INVALID_HANDLE_VALUE)
			return false;

		std::string text;
		char buffer[4096];
		DWORD read = 0;
		bool ok = true;

		for (;;)
		{
			if (!::ReadFile(file, buffer, sizeof(buffer), &read, nullptr))
			{
				ok = false;
				break;
			}
			if (read == 0)
				break;

			text.append(buffer, read);
		}

		::CloseHandle(file);

		if (!ok)
			return false;

		// Strip a UTF-8 BOM if present.
		if (text.size() >= 3 &&
			static_cast<unsigned char>(text[0]) == 0xEF &&
			static_cast<unsigned char>(text[1]) == 0xBB &&
			static_cast<unsigned char>(text[2]) == 0xBF)
		{
			text.erase(0, 3);
		}

		// Keys that appear before any [Section] header belong to the implicit
		// "" section. Track the current section by index: the vector grows below,
		// so holding a pointer or reference into it would be unsafe.
		this->Sections.emplace_back();
		size_t current = 0;

		size_t pos = 0;
		while (pos <= text.size())
		{
			size_t newline = text.find('\n', pos);
			if (newline == std::string::npos)
				newline = text.size();

			std::string line = Trim(text.substr(pos, newline - pos));
			pos = newline + 1;

			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;

			if (line.front() == '[' && line.back() == ']')
			{
				const std::string name = ToLower(Trim(line.substr(1, line.size() - 2)));

				size_t index = this->Sections.size();
				for (size_t i = 0; i < this->Sections.size(); ++i)
				{
					if (this->Sections[i].Name == name)
					{
						index = i;
						break;
					}
				}

				if (index == this->Sections.size())
				{
					this->Sections.emplace_back();
					this->Sections.back().Name = name;
					index = this->Sections.size() - 1;
				}

				current = index;
				continue;
			}

			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;

			const std::string key = ToLower(Trim(line.substr(0, equals)));
			const std::string value = Trim(StripTrailingComment(line.substr(equals + 1)));

			if (key.empty())
				continue;

			// First occurrence wins; skip the rest.
			bool duplicate = false;
			for (const Entry& entry : this->Sections[current].Entries)
			{
				if (entry.Key == key)
				{
					duplicate = true;
					break;
				}
			}

			if (!duplicate)
				this->Sections[current].Entries.push_back({ key, value });
		}

		return true;
	}

	bool INIConfigReader::LoadFile(const char* filePath)
	{
		this->Clear();

		if (!filePath || !*filePath)
			return false;

		// Assume the caller hands us an ANSI path, which is what the game uses.
		wchar_t wide[MAX_PATH] = { 0 };
		if (::MultiByteToWideChar(CP_ACP, 0, filePath, -1, wide, MAX_PATH) == 0)
			return false;

		return this->LoadFile(wide);
	}

	void INIConfigReader::Clear()
	{
		this->Sections.clear();
		this->Path.clear();
	}

	INIConfigReader::Section* INIConfigReader::FindSection(const char* name)
	{
		if (!name)
			return nullptr;

		const std::string lowered = ToLower(name);

		for (Section& section : this->Sections)
		{
			if (section.Name == lowered)
				return &section;
		}

		return nullptr;
	}

	const INIConfigReader::Section* INIConfigReader::FindSection(const char* name) const
	{
		return const_cast<INIConfigReader*>(this)->FindSection(name);
	}

	bool INIConfigReader::HasSection(const char* section) const
	{
		return this->FindSection(section) != nullptr;
	}

	bool INIConfigReader::HasKey(const char* section, const char* key) const
	{
		const Section* found = this->FindSection(section);
		if (!found || !key)
			return false;

		const std::string lowered = ToLower(key);
		for (const Entry& entry : found->Entries)
		{
			if (entry.Key == lowered)
				return true;
		}

		return false;
	}

	std::string INIConfigReader::GetString(const char* section, const char* key, const char* fallback) const
	{
		const Section* found = this->FindSection(section);
		if (!found || !key)
			return fallback ? fallback : "";

		const std::string lowered = ToLower(key);
		for (const Entry& entry : found->Entries)
		{
			if (entry.Key == lowered)
				return entry.Value;
		}

		return fallback ? fallback : "";
	}

	int INIConfigReader::GetInt(const char* section, const char* key, int fallback) const
	{
		if (!this->HasKey(section, key))
			return fallback;

		const std::string value = this->GetString(section, key);
		char* end = nullptr;
		const long parsed = std::strtol(value.c_str(), &end, 0);

		if (end == value.c_str())
			return fallback;

		return static_cast<int>(parsed);
	}

	unsigned INIConfigReader::GetUnsigned(const char* section, const char* key, unsigned fallback) const
	{
		const int parsed = this->GetInt(section, key, static_cast<int>(fallback));
		return parsed < 0 ? fallback : static_cast<unsigned>(parsed);
	}

	bool INIConfigReader::GetBool(const char* section, const char* key, bool fallback) const
	{
		if (!this->HasKey(section, key))
			return fallback;

		return ParseBool(this->GetString(section, key), fallback);
	}
}
