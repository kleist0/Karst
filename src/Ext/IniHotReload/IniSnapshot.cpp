#include <Ext/IniHotReload/IniSnapshot.h>

#include <cctype>

namespace Karst
{
	namespace Ext
	{
		namespace IniHotReload
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

				std::string Lowercase(const std::string& value)
				{
					std::string result = value;
					for (char& c : result)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					return result;
				}

				// A section's exact content, as one comparable string: the keys
				// in map order, each as "Key=Value" (separator included so that
				// key and value text cannot run together ambiguously).
				std::string Fingerprint(const IniSection& section)
				{
					std::string result;

					for (const auto& entry : section.Keys)
					{
						result += entry.first;
						result += '=';
						result += entry.second;
						result += '\n';
					}

					return result;
				}
			}

			void BuildSnapshot(const std::string& text, IniSnapshot& out)
			{
				out.Clear();

				std::string section;          // current section, lower case
				size_t position = 0;
				const size_t length = text.size();

				while (position <= length)
				{
					size_t newline = text.find('\n', position);
					if (newline == std::string::npos)
						newline = length;

					std::string line = Trim(text.substr(position, newline - position));
					position = newline + 1;

					if (line.empty() || line[0] == ';' || line[0] == '#')
					{
						if (position > length)
							break;
						continue;
					}

					// [Section]
					if (line.front() == '[')
					{
						const size_t close = line.find(']');
						if (close != std::string::npos)
						{
							section = Lowercase(Trim(line.substr(1, close - 1)));
							out.Sections[section];   // create even when empty
						}

						if (position > length)
							break;
						continue;
					}

					const size_t equals = line.find('=');
					if (equals != std::string::npos && !section.empty())
					{
						const std::string key = Lowercase(Trim(line.substr(0, equals)));
						const std::string value = Trim(line.substr(equals + 1));

						if (!key.empty())
						{
							// Later occurrences win, which is how the engine
							// treats a duplicate key as well.
							out.Sections[section].Keys[key] = value;
						}
					}

					if (position > length)
						break;
				}
			}

			size_t CollectChangedSections(const IniSnapshot& previous,
				const IniSnapshot& current,
				std::vector<ChangedSection>& out)
			{
				const size_t before = out.size();

				for (const auto& entry : current.Sections)
				{
					const std::string& name = entry.first;

					const auto old = previous.Sections.find(name);

					if (old == previous.Sections.end())
					{
						out.push_back({ name, SectionChange::Added });
						continue;
					}

					if (Fingerprint(old->second) != Fingerprint(entry.second))
					{
						out.push_back({ name, SectionChange::Changed });
						continue;
					}

					// Same values - but did the new read lose keys the old one
					// had? Then the section is only identical because part of
					// its data is missing from this read.
					for (const auto& key : old->second.Keys)
					{
						if (entry.second.Keys.find(key.first) == entry.second.Keys.end())
						{
							out.push_back({ name, SectionChange::LostKeys });
							break;
						}
					}
				}

				for (const auto& entry : previous.Sections)
				{
					if (current.Sections.find(entry.first) == current.Sections.end())
						out.push_back({ entry.first, SectionChange::Removed });
				}

				return out.size() - before;
			}

			const char* SectionChangeName(SectionChange kind)
			{
				switch (kind)
				{
				case SectionChange::Added:    return "added";
				case SectionChange::Changed:  return "changed";
				case SectionChange::Removed:  return "removed";
				case SectionChange::LostKeys: return "lost-keys";
				default:                      return "?";
				}
			}
		}
	}
}
