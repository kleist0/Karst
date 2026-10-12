#pragma once

#include <map>
#include <string>
#include <vector>

// A small JSON reader for the script files.
//
// WHY NOT A LIBRARY
//
// The scripts have to be readable and writable by hand and by an HTML editor, so
// the dialect is deliberately forgiving:
//
//   * `//` and `/* */` comments are allowed (the file extension is .jsonc)
//   * a trailing comma is allowed in arrays and objects
//   * everything is treated as UTF-8 text; escapes are decoded
//
// That dialect is why a stock parser is not used: comments and trailing commas
// are exactly what people write first, and rejecting them would make the editor
// fight the format.
//
// The parser is strict about structure - a malformed file produces an error with
// the line and column, not a half-loaded script. A script that cannot be trusted
// is worse than no script, because it silently does something else than asked.

namespace Karst
{
	namespace Script
	{
		class JsonValue
		{
		public:
			enum class Type
			{
				Null,
				Bool,
				Number,
				String,
				Array,
				Object,
			};

			JsonValue() = default;

			Type GetType() const { return this->Kind; }

			bool IsNull()   const { return this->Kind == Type::Null; }
			bool IsBool()   const { return this->Kind == Type::Bool; }
			bool IsNumber() const { return this->Kind == Type::Number; }
			bool IsString() const { return this->Kind == Type::String; }
			bool IsArray()  const { return this->Kind == Type::Array; }
			bool IsObject() const { return this->Kind == Type::Object; }

			// Scalar access. `fallback` is returned when the type does not match,
			// so a script with a typo reads as "the value I asked for is absent"
			// instead of producing garbage.
			bool        AsBool(bool fallback = false) const;
			double      AsNumber(double fallback = 0.0) const;
			int         AsInt(int fallback = 0) const;
			std::string AsString(const char* fallback = "") const;

			// Containers. A wrong type yields an empty container.
			const std::vector<JsonValue>& AsArray() const;
			const std::map<std::string, JsonValue>& AsObject() const;

			// Object lookup. `Member` returns null when absent, so chains like
			// value["a"]["b"] are safe to write.
			const JsonValue& Member(const char* name) const;
			bool HasMember(const char* name) const;

			// Array lookup; null when out of range.
			const JsonValue& At(size_t index) const;

			// Diagnostics.
			int GetLine() const { return this->Line; }
			void SetLine(int line) { this->Line = line; }

			// --- construction (used by the parser and by tests) --------------
			static JsonValue MakeNull();
			static JsonValue MakeBool(bool value);
			static JsonValue MakeNumber(double value);
			static JsonValue MakeString(const std::string& value);
			static JsonValue MakeArray(std::vector<JsonValue> items);
			static JsonValue MakeObject(std::map<std::string, JsonValue> members);

			static const JsonValue& Null();

		private:
			Type Kind = Type::Null;

			bool                      BoolValue = false;
			double                    NumberValue = 0.0;
			std::string               StringValue;
			std::vector<JsonValue>    ArrayValue;
			std::map<std::string, JsonValue> ObjectValue;

			// 1-based line the value started on, for error messages.
			int Line = 0;
		};

		// Parses `text`. On failure returns false and fills `error` with a
		// "line N: message" description.
		bool ParseJson(const std::string& text, JsonValue& out, std::string& error);

		// Reads a file as text (UTF-8, BOM tolerated). Returns false when the file
		// cannot be read or is empty.
		bool ReadTextFile(const wchar_t* path, std::string& out);
	}
}
