// Checks the JSON reader the script loader is built on.
//
// A tolerant dialect (comments, trailing commas) is only useful if it is still
// strict about structure: a script that parses "almost right" would quietly do
// something other than what its author wrote. These cases pin both halves -
// what must be accepted, and what must be rejected with a useful message.
//
// No engine, no game. Build with tools\build_tools.bat:
//     check_json.exe

#include <Script/Json.h>

#include <cstdio>
#include <string>

using namespace Karst::Script;

static int Failures = 0;

static void Check(const char* what, bool condition)
{
	printf("  %-56s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++Failures;
}

// Parses and reports success.
static bool Accepts(const char* text)
{
	JsonValue value;
	std::string error;
	return ParseJson(text, value, error);
}

// Parses, expects failure, and reports the message.
static bool Rejects(const char* text, std::string& error)
{
	JsonValue value;
	error.clear();
	return !ParseJson(text, value, error);
}

int main()
{
	printf("json reader checks\n\n");

	// --- scalar types ------------------------------------------------------
	{
		JsonValue root;
		std::string error;

		const char* document =
			"{\n"
			"  \"name\": \"ironwall\",\n"      // string
			"  \"count\": 42,\n"               // int
			"  \"ratio\": -1.5e2,\n"           // float with exponent
			"  \"on\": true,\n"                // bool
			"  \"off\": false,\n"
			"  \"nothing\": null\n"            // null
			"}\n";

		printf("scalars:\n");
		Check("document parses", ParseJson(document, root, error));

		if (!error.empty())
			printf("     error: %s\n", error.c_str());

		Check("object type", root.IsObject());
		Check("string member", root.Member("name").AsString() == "ironwall");
		Check("integer member", root.Member("count").AsInt() == 42);
		Check("float member", root.Member("ratio").AsNumber() == -150.0);
		Check("true member", root.Member("on").AsBool() == true);
		Check("false member", root.Member("off").AsBool() == false);
		Check("null member", root.Member("nothing").IsNull());
		Check("absent member is null", root.Member("nope").IsNull());
		Check("HasMember true", root.HasMember("name"));
		Check("HasMember false", !root.HasMember("nope"));
	}

	// --- the tolerant dialect ---------------------------------------------
	{
		printf("\ntolerant dialect:\n");

		Check("line comments",
			Accepts("{ // a comment\n \"a\": 1\n}\n"));

		Check("block comments",
			Accepts("{ /* multi\n line */ \"a\": 1 }\n"));

		Check("trailing comma in object",
			Accepts("{ \"a\": 1, }\n"));

		Check("trailing comma in array",
			Accepts("[1, 2, 3, ]\n"));

		Check("nested trailing commas",
			Accepts("{ \"a\": [ { \"b\": 1, }, ], }\n"));

		Check("empty object", Accepts("{}\n"));
		Check("empty array", Accepts("[]\n"));
		Check("bare string document", Accepts("\"hello\"\n"));
		Check("bare number document", Accepts("7\n"));
	}

	// --- arrays and nesting ------------------------------------------------
	{
		JsonValue root;
		std::string error;

		const char* document =
			"{\n"
			"  \"then\": [\n"
			"    { \"action\": \"Heal\", \"amount\": 500 },\n"
			"    { \"action\": \"Message\", \"text\": \"hi\" }\n"
			"  ]\n"
			"}\n";

		printf("\narrays:\n");
		Check("parses", ParseJson(document, root, error));

		const JsonValue& list = root.Member("then");
		Check("is an array", list.IsArray());
		Check("has two entries", list.AsArray().size() == 2);
		Check("first entry action", list.At(0).Member("action").AsString() == "Heal");
		Check("first entry amount", list.At(0).Member("amount").AsInt() == 500);
		Check("second entry text",
			list.At(1).Member("text").AsString() == "hi");
		Check("out of range yields null", list.At(99).IsNull());
	}

	// --- escapes -----------------------------------------------------------
	{
		JsonValue root;
		std::string error;

		Check("escapes parse",
			ParseJson("{ \"s\": \"a\\\"b\\\\c\\nd\\te\" }", root, error));

		Check("escaped quote and backslash",
			root.Member("s").AsString() == "a\"b\\c\nd\te");
	}

	// --- unicode -----------------------------------------------------------
	{
		JsonValue root;
		std::string error;

		// \u4e2d\u6587 = 中文 (UTF-8: E4 B8 AD E6 96 87)
		Check("\\u escapes parse",
			ParseJson("{ \"s\": \"\\u4e2d\\u6587\" }", root, error));

		Check("two CJK characters decoded",
			root.Member("s").AsString() == "\xE4\xB8\xAD\xE6\x96\x87");

		// Raw UTF-8 passes through untouched.
		Check("raw UTF-8 survives",
			ParseJson("{ \"s\": \"\xE4\xB8\xAD\xE6\x96\x87\" }", root, error) &&
			root.Member("s").AsString() == "\xE4\xB8\xAD\xE6\x96\x87");
	}

	// --- diagnostics: what must be rejected --------------------------------
	{
		printf("\nrejects malformed input:\n");

		std::string error;

		Check("missing value", Rejects("{ \"a\": }", error));
		printf("     -> %s\n", error.c_str());

		Check("unterminated string", Rejects("{ \"a\": \"x }", error));
		printf("     -> %s\n", error.c_str());

		Check("missing colon", Rejects("{ \"a\" 1 }", error));
		printf("     -> %s\n", error.c_str());

		Check("missing comma", Rejects("{ \"a\": 1 \"b\": 2 }", error));
		printf("     -> %s\n", error.c_str());

		Check("unquoted member name", Rejects("{ a: 1 }", error));
		printf("     -> %s\n", error.c_str());

		Check("trailing text", Rejects("{ \"a\": 1 } junk", error));
		printf("     -> %s\n", error.c_str());

		Check("empty document", Rejects("", error));
		printf("     -> %s\n", error.c_str());

		Check("unclosed brace", Rejects("{ \"a\": 1", error));
		printf("     -> %s\n", error.c_str());
	}

	// --- wrong-type access falls back, never crashes ------------------------
	{
		JsonValue root;
		std::string error;
		ParseJson("{ \"s\": \"text\", \"n\": 5 }", root, error);

		printf("\nwrong-type access:\n");
		Check("string read as int -> fallback",
			root.Member("s").AsInt(-1) == -1);
		Check("number read as string -> fallback",
			root.Member("n").AsString("<none>") == "<none>");
		Check("object read as array -> empty",
			root.Member("n").AsArray().empty());
		Check("chained lookup on a scalar is safe",
			root.Member("n").Member("deeper").IsNull());
	}

	printf("\n%s (%d failure(s))\n",
		Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", Failures);

	return Failures == 0 ? 0 : 1;
}
