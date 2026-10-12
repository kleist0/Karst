// Offset cross-check.
//
// The runtime hooks read a handful of members by fixed offset. Those offsets
// were derived by disassembling gamemd.exe, so they need to agree with the
// class layouts YRpp describes - if they do not, one of the two is wrong and the
// hook would read the wrong field.
//
// MSVC can print the layout it actually computes for a class. Compile this file
// with the layout report and compare the numbers with src\Ext\EngineLayout.h:
//
//   cl /c /d1reportSingleClassLayoutTechnoClass /d1reportSingleClassLayoutHouseClass ^
//      /I YRpp /I src offset_check.cpp
//
// The program itself only needs to instantiate nothing; the diagnostics are the
// output. It is never linked into the DLL.

#include <TechnoClass.h>
#include <HouseClass.h>
#include <BuildingClass.h>
#include <HouseTypeClass.h>

// Keep the translation unit meaningful without emitting any code: the class
// layout report is produced while compiling, before code generation.
namespace
{
	void Anchor()
	{
		// Referencing the types is enough to make the compiler instantiate the
		// layout reports for them.
		(void)sizeof(TechnoClass);
		(void)sizeof(HouseClass);
		(void)sizeof(BuildingClass);
		(void)sizeof(HouseTypeClass);
	}
}
