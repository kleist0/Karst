// Checks the section-diff logic that decides what a hot reload refeeds.
//
// This is the piece that replaced "refeed all 402 building types" with "refeed
// only the sections that changed", so it is worth testing on its own: a diff
// that misses a change means an edit silently does nothing, and a diff that
// invents changes means the reload does more work than it should.
//
// Pure logic - no game, no engine. Build (32-bit):
//     cl /nologo /EHsc /I..\src /Fe:check_snapshot.exe check_snapshot.cpp ^
//        ..\src\Ext\IniHotReload\IniSnapshot.cpp

#include <Ext/IniHotReload/IniSnapshot.h>

#include <cstdio>
#include <string>

using namespace Karst::Ext::IniHotReload;

static int Failures = 0;

static void Check(const char* what, bool condition)
{
	printf("  %-58s %s\n", what, condition ? "ok" : "FAILED");
	if (!condition)
		++Failures;
}

// True when `name` is among the changed sections.
static bool Has(const std::vector<ChangedSection>& list, const char* name,
	SectionChange kind)
{
	for (const ChangedSection& section : list)
	{
		if (section.Name == name && section.Kind == kind)
			return true;
	}
	return false;
}

static size_t Diff(const char* before, const char* after,
	std::vector<ChangedSection>& out)
{
	IniSnapshot a;
	IniSnapshot b;
	BuildSnapshot(before, a);
	BuildSnapshot(after, b);
	out.clear();
	return CollectChangedSections(a, b, out);
}

int main()
{
	printf("section diff checks\n\n");

	// --- parsing -----------------------------------------------------------
	{
		IniSnapshot snapshot;
		BuildSnapshot(
			"; comment\n"
			"\n"
			"[General]\n"
			"Speed=1\n"
			"Firestorm=no\n"
			"\n"
			"[GAPOWR]\n"
			"Cost=800\n"
			"Name=Allied Power Plant\n"
			"Empty=\n",
			snapshot);

		printf("parsing:\n");
		Check("two sections parsed", snapshot.Sections.size() == 2);
		Check("[general] is stored lower case",
			snapshot.Sections.count("general") == 1);
		Check("[GAPOWR] is stored lower case",
			snapshot.Sections.count("gapowr") == 1);
		Check("keys are counted", snapshot.Sections["gapowr"].Keys.size() == 3);
		Check("values are kept",
			snapshot.Sections["gapowr"].Keys["cost"] == "800");
		Check("an empty value still counts as a key",
			snapshot.Sections["gapowr"].Keys.count("empty") == 1);
		Check("comments are ignored",
			snapshot.Sections.count("; comment") == 0);
	}

	// --- no change ---------------------------------------------------------
	{
		std::vector<ChangedSection> changed;
		const size_t n = Diff("[A]\nX=1\n", "[A]\nX=1\n", changed);
		printf("\nunchanged file:\n");
		Check("nothing is reported as changed", n == 0);
	}

	// --- one value changed -------------------------------------------------
	{
		std::vector<ChangedSection> changed;
		const size_t n = Diff(
			"[General]\nSpeed=1\n[GAPOWR]\nCost=800\n",
			"[General]\nSpeed=1\n[GAPOWR]\nCost=9999\n",
			changed);

		printf("\none building edited:\n");
		Check("exactly one section is reported", n == 1);
		Check("it is [gapowr], as a change", Has(changed, "gapowr", SectionChange::Changed));
		Check("[general] is NOT reported", !Has(changed, "general", SectionChange::Changed));
	}

	// --- key added / removed within a section ------------------------------
	{
		std::vector<ChangedSection> changed;
		size_t n = Diff("[A]\nX=1\n", "[A]\nX=1\nY=2\n", changed);
		Check("adding a key counts as a change", n == 1);

		n = Diff("[A]\nX=1\nY=2\n", "[A]\nX=1\n", changed);
		Check("removing a key counts as a change", n == 1);

		n = Diff("[A]\nX=1\n", "[A]\nX=1\n[B]\nZ=1\n", changed);
		Check("a new section is reported as added",
			n == 1 && Has(changed, "b", SectionChange::Added));

		n = Diff("[A]\nX=1\n[B]\nZ=1\n", "[A]\nX=1\n", changed);
		Check("a removed section is reported",
			n == 1 && Has(changed, "b", SectionChange::Removed));
	}

	// --- many sections edited ----------------------------------------------
	{
		std::string before;
		std::string after;

		for (int i = 0; i < 50; ++i)
		{
			char buffer[64] = { 0 };
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"[SEC%02d]\nValue=%d\n", i, i);
			before += buffer;

			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"[SEC%02d]\nValue=%d\n", i, i == 7 ? 999 : i);
			after += buffer;
		}

		std::vector<ChangedSection> changed;
		const size_t n = Diff(before.c_str(), after.c_str(), changed);

		printf("\n50 sections, one edited:\n");
		Check("exactly one of 50 is reported", n == 1);
		Check("it is the one that changed",
			n == 1 && Has(changed, "sec07", SectionChange::Changed));
	}

	// --- lost keys (multi-file setups) -------------------------------------
	{
		// The new read is missing a key the old one had, while the remaining
		// values are identical: that has to be flagged, not treated as unchanged.
		std::vector<ChangedSection> changed;
		const size_t n = Diff("[A]\nX=1\nY=2\n", "[A]\nX=1\n", changed);

		printf("\npartial reread:\n");
		Check("a section that lost keys is reported", n == 1);
	}

	// --- the real shape: one edit in a big file ----------------------------
	{
		std::string before;
		std::string after;

		for (int i = 0; i < 2000; ++i)
		{
			char buffer[96] = { 0 };
			_snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
				"[TYPE%04d]\nCost=%d\nName=Type %d\n", i, i * 10, i);
			before += buffer;
			after += buffer;
		}

		const std::string needle = "[TYPE1234]\nCost=12340\n";
		const size_t at = after.find(needle);
		after.replace(at, needle.size(), "[TYPE1234]\nCost=1\n");

		std::vector<ChangedSection> changed;
		const size_t n = Diff(before.c_str(), after.c_str(), changed);

		printf("\n2000 sections, one edited:\n");
		Check("exactly one is reported", n == 1);
		Check("it is [type1234]",
			n == 1 && Has(changed, "type1234", SectionChange::Changed));
	}

	printf("\n%s (%d failure(s))\n",
		Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", Failures);

	return Failures == 0 ? 0 : 1;
}
