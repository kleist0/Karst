#include <Script/Json.h>

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Karst
{
	namespace Script
	{
		namespace
		{
			const JsonValue& NullValue()
			{
				static const JsonValue null;
				return null;
			}
		}

		// --- JsonValue access -------------------------------------------------

		bool JsonValue::AsBool(bool fallback) const
		{
			return this->Kind == Type::Bool ? this->BoolValue : fallback;
		}

		double JsonValue::AsNumber(double fallback) const
		{
			return this->Kind == Type::Number ? this->NumberValue : fallback;
		}

		int JsonValue::AsInt(int fallback) const
		{
			return this->Kind == Type::Number
				? static_cast<int>(this->NumberValue) : fallback;
		}

		std::string JsonValue::AsString(const char* fallback) const
		{
			return this->Kind == Type::String
				? this->StringValue : std::string(fallback ? fallback : "");
		}

		const std::vector<JsonValue>& JsonValue::AsArray() const
		{
			static const std::vector<JsonValue> empty;
			return this->Kind == Type::Array ? this->ArrayValue : empty;
		}

		const std::map<std::string, JsonValue>& JsonValue::AsObject() const
		{
			static const std::map<std::string, JsonValue> empty;
			return this->Kind == Type::Object ? this->ObjectValue : empty;
		}

		const JsonValue& JsonValue::Member(const char* name) const
		{
			if (this->Kind != Type::Object || !name)
				return NullValue();

			const auto found = this->ObjectValue.find(name);
			return found == this->ObjectValue.end() ? NullValue() : found->second;
		}

		bool JsonValue::HasMember(const char* name) const
		{
			if (this->Kind != Type::Object || !name)
				return false;

			return this->ObjectValue.find(name) != this->ObjectValue.end();
		}

		const JsonValue& JsonValue::At(size_t index) const
		{
			if (this->Kind != Type::Array || index >= this->ArrayValue.size())
				return NullValue();

			return this->ArrayValue[index];
		}

		const JsonValue& JsonValue::Null()
		{
			return NullValue();
		}

		JsonValue JsonValue::MakeNull()
		{
			JsonValue value;
			value.Kind = Type::Null;
			return value;
		}

		JsonValue JsonValue::MakeBool(bool input)
		{
			JsonValue value;
			value.Kind = Type::Bool;
			value.BoolValue = input;
			return value;
		}

		JsonValue JsonValue::MakeNumber(double input)
		{
			JsonValue value;
			value.Kind = Type::Number;
			value.NumberValue = input;
			return value;
		}

		JsonValue JsonValue::MakeString(const std::string& input)
		{
			JsonValue value;
			value.Kind = Type::String;
			value.StringValue = input;
			return value;
		}

		JsonValue JsonValue::MakeArray(std::vector<JsonValue> items)
		{
			JsonValue value;
			value.Kind = Type::Array;
			value.ArrayValue = std::move(items);
			return value;
		}

		JsonValue JsonValue::MakeObject(std::map<std::string, JsonValue> members)
		{
			JsonValue value;
			value.Kind = Type::Object;
			value.ObjectValue = std::move(members);
			return value;
		}

		// --- parser -----------------------------------------------------------

		class JsonParser
		{
		public:
			JsonParser(const std::string& text, std::string& error)
				: Text(text), Error(error)
			{
			}

			bool Parse(JsonValue& out)
			{
				this->SkipWhitespace();

				if (!this->ParseValue(out))
					return false;

				this->SkipWhitespace();

				if (this->Position != this->Text.size())
				{
					this->Fail("unexpected trailing text");
					return false;
				}

				return true;
			}

		private:
			const std::string& Text;
			std::string&       Error;
			size_t             Position = 0;
			int                Line = 1;

			void Fail(const char* message)
			{
				char buffer[512] = { 0 };
				_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "line %d: %s",
					this->Line, message);

				if (this->Error.empty())
					this->Error = buffer;
			}

			bool AtEnd() const { return this->Position >= this->Text.size(); }

			char Peek() const { return this->AtEnd() ? '\0' : this->Text[this->Position]; }

			char Next()
			{
				const char c = this->Peek();
				if (!this->AtEnd())
					++this->Position;
				if (c == '\n')
					++this->Line;
				return c;
			}

			bool Match(char expected)
			{
				if (this->Peek() != expected)
					return false;

				this->Next();
				return true;
			}

			void SkipWhitespace()
			{
				for (;;)
				{
					const char c = this->Peek();

					if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
					{
						this->Next();
						continue;
					}

					// // line comment
					if (c == '/' && this->Position + 1 < this->Text.size() &&
						this->Text[this->Position + 1] == '/')
					{
						while (!this->AtEnd() && this->Peek() != '\n')
							this->Next();
						continue;
					}

					// /* block comment */
					if (c == '/' && this->Position + 1 < this->Text.size() &&
						this->Text[this->Position + 1] == '*')
					{
						this->Next();
						this->Next();

						while (!this->AtEnd())
						{
							if (this->Peek() == '*' &&
								this->Position + 1 < this->Text.size() &&
								this->Text[this->Position + 1] == '/')
							{
								this->Next();
								this->Next();
								break;
							}

							this->Next();
						}

						continue;
					}

					return;
				}
			}

			bool ParseValue(JsonValue& out)
			{
				this->SkipWhitespace();

				if (this->AtEnd())
				{
					this->Fail("unexpected end of file");
					return false;
				}

				const int startLine = this->Line;
				const char c = this->Peek();

				bool ok = false;

				switch (c)
				{
				case '{': ok = this->ParseObject(out); break;
				case '[': ok = this->ParseArray(out); break;
				case '"': ok = this->ParseString(out); break;
				case 't': ok = this->ParseLiteral("true", JsonValue::MakeBool(true), out); break;
				case 'f': ok = this->ParseLiteral("false", JsonValue::MakeBool(false), out); break;
				case 'n': ok = this->ParseLiteral("null", JsonValue::MakeNull(), out); break;
				default:  ok = this->ParseNumber(out); break;
				}

				if (ok)
					out.SetLine(startLine);

				return ok;
			}

			bool ParseLiteral(const char* literal, const JsonValue& value, JsonValue& out)
			{
				const size_t length = ::strlen(literal);

				if (this->Text.compare(this->Position, length, literal) != 0)
				{
					this->Fail("invalid literal");
					return false;
				}

				this->Position += length;
				out = value;
				return true;
			}

			bool ParseNumber(JsonValue& out)
			{
				const size_t begin = this->Position;

				if (this->Peek() == '-' || this->Peek() == '+')
					this->Next();

				bool anyDigit = false;

				while (!this->AtEnd())
				{
					const char c = this->Peek();

					if ((c >= '0' && c <= '9'))
					{
						anyDigit = true;
						this->Next();
						continue;
					}

					if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
					{
						this->Next();
						continue;
					}

					break;
				}

				if (!anyDigit)
				{
					this->Fail("expected a value");
					return false;
				}

				const std::string token = this->Text.substr(begin, this->Position - begin);
				out = JsonValue::MakeNumber(::strtod(token.c_str(), nullptr));
				return true;
			}

			bool ParseString(JsonValue& out)
			{
				if (!this->Match('"'))
				{
					this->Fail("expected a string");
					return false;
				}

				std::string result;

				for (;;)
				{
					if (this->AtEnd())
					{
						this->Fail("unterminated string");
						return false;
					}

					const char c = this->Next();

					if (c == '"')
						break;

					if (c != '\\')
					{
						result.push_back(c);
						continue;
					}

					if (this->AtEnd())
					{
						this->Fail("unterminated escape");
						return false;
					}

					const char escape = this->Next();

					switch (escape)
					{
					case '"':  result.push_back('"');  break;
					case '\\': result.push_back('\\'); break;
					case '/':  result.push_back('/');  break;
					case 'b':  result.push_back('\b'); break;
					case 'f':  result.push_back('\f'); break;
					case 'n':  result.push_back('\n'); break;
					case 'r':  result.push_back('\r'); break;
					case 't':  result.push_back('\t'); break;
					case 'u':
					{
						// Decoded to UTF-8. Surrogate pairs are handled because a
						// script may well contain an emoji or a CJK name.
						unsigned code = 0;

						for (int i = 0; i < 4; ++i)
						{
							if (this->AtEnd())
							{
								this->Fail("truncated \\u escape");
								return false;
							}

							const char h = this->Next();
							code <<= 4;

							if (h >= '0' && h <= '9')      code |= static_cast<unsigned>(h - '0');
							else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
							else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
							else
							{
								this->Fail("invalid \\u escape");
								return false;
							}
						}

						unsigned full = code;

						if (code >= 0xD800 && code <= 0xDBFF &&
							this->Text.compare(this->Position, 2, "\\u") == 0)
						{
							this->Position += 2;
							unsigned low = 0;

							for (int i = 0; i < 4; ++i)
							{
								const char h = this->Next();
								low <<= 4;

								if (h >= '0' && h <= '9')      low |= static_cast<unsigned>(h - '0');
								else if (h >= 'a' && h <= 'f') low |= static_cast<unsigned>(h - 'a' + 10);
								else if (h >= 'A' && h <= 'F') low |= static_cast<unsigned>(h - 'A' + 10);
							}

							if (low >= 0xDC00 && low <= 0xDFFF)
								full = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						}

						if (full < 0x80)
						{
							result.push_back(static_cast<char>(full));
						}
						else if (full < 0x800)
						{
							result.push_back(static_cast<char>(0xC0 | (full >> 6)));
							result.push_back(static_cast<char>(0x80 | (full & 0x3F)));
						}
						else if (full < 0x10000)
						{
							result.push_back(static_cast<char>(0xE0 | (full >> 12)));
							result.push_back(static_cast<char>(0x80 | ((full >> 6) & 0x3F)));
							result.push_back(static_cast<char>(0x80 | (full & 0x3F)));
						}
						else
						{
							result.push_back(static_cast<char>(0xF0 | (full >> 18)));
							result.push_back(static_cast<char>(0x80 | ((full >> 12) & 0x3F)));
							result.push_back(static_cast<char>(0x80 | ((full >> 6) & 0x3F)));
							result.push_back(static_cast<char>(0x80 | (full & 0x3F)));
						}

						break;
					}

					default:
						this->Fail("unknown escape sequence");
						return false;
					}
				}

				out = JsonValue::MakeString(result);
				return true;
			}

			bool ParseArray(JsonValue& out)
			{
				if (!this->Match('['))
				{
					this->Fail("expected '['");
					return false;
				}

				std::vector<JsonValue> items;

				this->SkipWhitespace();

				if (this->Match(']'))
				{
					out = JsonValue::MakeArray(std::move(items));
					return true;
				}

				for (;;)
				{
					JsonValue item;

					if (!this->ParseValue(item))
						return false;

					items.push_back(std::move(item));

					this->SkipWhitespace();

					if (this->Match(']'))
						break;

					if (!this->Match(','))
					{
						this->Fail("expected ',' or ']' in array");
						return false;
					}

					this->SkipWhitespace();

					// Trailing comma: allow the closing bracket right after it.
					if (this->Match(']'))
						break;
				}

				out = JsonValue::MakeArray(std::move(items));
				return true;
			}

			bool ParseObject(JsonValue& out)
			{
				if (!this->Match('{'))
				{
					this->Fail("expected '{'");
					return false;
				}

				std::map<std::string, JsonValue> members;

				this->SkipWhitespace();

				if (this->Match('}'))
				{
					out = JsonValue::MakeObject(std::move(members));
					return true;
				}

				for (;;)
				{
					this->SkipWhitespace();

					JsonValue key;

					if (this->Peek() != '"')
					{
						this->Fail("expected a quoted member name");
						return false;
					}

					if (!this->ParseString(key))
						return false;

					this->SkipWhitespace();

					if (!this->Match(':'))
					{
						this->Fail("expected ':' after a member name");
						return false;
					}

					JsonValue value;

					if (!this->ParseValue(value))
						return false;

					members[key.AsString()] = std::move(value);

					this->SkipWhitespace();

					if (this->Match('}'))
						break;

					if (!this->Match(','))
					{
						this->Fail("expected ',' or '}' in object");
						return false;
					}

					this->SkipWhitespace();

					// Trailing comma.
					if (this->Match('}'))
						break;
				}

				out = JsonValue::MakeObject(std::move(members));
				return true;
			}
		};

		bool ParseJson(const std::string& text, JsonValue& out, std::string& error)
		{
			error.clear();

			JsonParser parser(text, error);
			return parser.Parse(out);
		}

		bool ReadTextFile(const wchar_t* path, std::string& out)
		{
			out.clear();

			if (!path || !*path)
				return false;

			HANDLE file = ::CreateFileW(path, GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, nullptr);

			if (file == INVALID_HANDLE_VALUE)
				return false;

			char buffer[64 * 1024];
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

				out.append(buffer, read);
			}

			::CloseHandle(file);

			if (!ok)
				return false;

			// Strip a UTF-8 BOM: an editor on Windows may well add one, and the
			// parser would otherwise report a stray character at line 1.
			if (out.size() >= 3 &&
				static_cast<unsigned char>(out[0]) == 0xEF &&
				static_cast<unsigned char>(out[1]) == 0xBB &&
				static_cast<unsigned char>(out[2]) == 0xBF)
			{
				out.erase(0, 3);
			}

			return !out.empty();
		}
	}
}
