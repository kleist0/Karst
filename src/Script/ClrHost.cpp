#include "ClrHost.h"

#include <Karst.h>
#include <Core/Log.h>

#include <windows.h>
#include <string>
#include <cstdio>

// The .NET runtime hosting API is declared in metahost.h; CLRCreateInstance is
// exported by mscoree.lib (Windows Kits\NETFXSDK\<ver>\Lib\um\x86).
#include <metahost.h>

// Type library of the managed runtime, which is what provides the _AppDomain /
// _Assembly / _Type / _MethodInfo interfaces used below.
//
// raw_interfaces_only keeps the generated wrappers as plain HRESULT-returning
// methods, which suits a build that does not use C++ exceptions
// (HAS_EXCEPTIONS=0). The #import also emits a warning about the
// System.EnterpriseServices namespace, which is silenceable but harmless.
#import <mscorlib.tlb> raw_interfaces_only \
	rename("or", "or_")

#include <mscorlib.tlh>

namespace
{
	ICLRMetaHost*      g_metaHost = nullptr;
	ICLRRuntimeInfo*   g_runtimeInfo = nullptr;
	ICorRuntimeHost*   g_corHost = nullptr;

	bool g_clrStarted = false;
	bool g_scriptsLoaded = false;

	// Cached so the per-frame driver does not pay a lookup every frame.
	// DoTick is a STATIC method, so it is invoked with an empty instance.
	mscorlib::_MethodInfo* g_doTick = nullptr;

	std::wstring g_assemblyDirectory;

	// Logs a failed HRESULT and stops the chain. Returns false so a caller can
	// simply `if (!Step(...)) return false;`.
	// Writes the script compiler's configuration if it is not there yet.
	//
	// WHY KARST WRITES IT
	//
	// The host's Patcher.Init throws FileNotFoundException when
	// compiler.config.json is missing, which aborts the whole script system
	// (observed: Program::Activate failed with 0x80131604). Requiring the user to
	// hand-place a file whose contents are fixed would be a pointless way to
	// fail, so the default is created on first run.
	//
	// The reference list uses the KARST assembly names: the samples ship
	// "PatcherYRpp.dll"/"Extension.dll", and compiling a script against names
	// that do not exist would fail later with a confusing error.
	void EnsureCompilerConfig(const std::wstring& directory)
	{
		const std::wstring path = directory + L"\\compiler.config.json";

		if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
			return;

		static const char kDefaultConfig[] =
			"{\n"
			"    \"references\" : [\n"
			"        \"System.dll\",\n"
			"        \"System.Core.dll\",\n"
			"        \"System.Data.dll\",\n"
			"        \"KarstPatcher.dll\",\n"
			"        \"KarstYRpp.dll\",\n"
			"        \"KarstExtension.dll\"\n"
			"    ],\n"
			"    \"preprocessor_symbols\" : [\n"
			"        \"REALTIME_INI\"\n"
			"    ],\n"
			"    \"show_hidden\" : false,\n"
			"    \"load_temp_file_in_memory\" : false,\n"
			"    \"emit_pdb\" : true,\n"
			"    \"force_compile\" : false,\n"
			"    \"pack_assembly\" : true\n"
			"}\n";

		FILE* file = nullptr;
		if (::fopen_s(&file, std::string(path.begin(), path.end()).c_str(), "wb") != 0
			|| !file)
		{
			Karst::LogInfo(L"ScriptHost: could not create '%ls'.", path.c_str());
			return;
		}

		::fwrite(kDefaultConfig, 1, sizeof(kDefaultConfig) - 1, file);
		::fclose(file);

		Karst::LogInfo(L"ScriptHost: created a default '%ls'.", path.c_str());
	}

	// Removes the Zone.Identifier alternate data stream - the "Mark of the Web" -
	// from one file.
	//
	// WHY THIS EXISTS
	//
	// Karst ships as a folder people copy around, very often out of a zip or rar
	// they downloaded. Windows marks those files as coming from the Internet, and
	// the CLR then REFUSES to load them:
	//
	//     _AppDomain::Load_2("KarstPatcher")  ->  0x80131515 (COR_E_FILELOAD)
	//
	// The only visible symptom is one line in karst.log:
	//
	//     ScriptHost: _Assembly::CreateInstance(DynamicPatcher.Program) failed
	//     (HRESULT 0x80131604)
	//
	// ...with NO patcher.log at all, because Patcher.Init throws while opening
	// that log file, before its own try block. Measured twice on 2026-10-12: the
	// very same files load fine the moment the mark is removed.
	//
	// Telling every user to "right-click, Properties, Unblock" is not a fix - it
	// is a support burden that fails silently when skipped. Deleting the stream
	// is harmless (it is Windows metadata, not file content) and this only ever
	// touches DLLs inside our own KarstPatcher directory.
	void UnblockFile(const std::wstring& path)
	{
		const std::wstring stream = path + L":Zone.Identifier";

		if (::DeleteFileW(stream.c_str()))
		{
			Karst::LogInfo(L"ScriptHost: removed the 'downloaded from the Internet' "
				L"mark from '%ls'.", path.c_str());
		}
	}

	// Unblocks every DLL in the assembly directory.
	void UnblockAssemblies(const std::wstring& directory)
	{
		const std::wstring pattern = directory + L"\\*.dll";

		WIN32_FIND_DATAW data = {};
		HANDLE find = ::FindFirstFileW(pattern.c_str(), &data);

		if (find == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				UnblockFile(directory + L"\\" + data.cFileName);
		} while (::FindNextFileW(find, &data));

		::FindClose(find);
	}

	bool Failed(const char* what, HRESULT hr)
	{
		Karst::LogInfo(L"ScriptHost: %hs failed (HRESULT 0x%08X).",
			what, static_cast<unsigned>(hr));
		return false;
	}
}

namespace Karst
{
	namespace Script
	{
		bool InitClr(const wchar_t* gameDirectory)
		{
			if (g_clrStarted)
				return true;

			if (!gameDirectory || !*gameDirectory)
			{
				Karst::LogInfo(L"ScriptHost: no game directory given.");
				return false;
			}

			g_assemblyDirectory = gameDirectory;
			if (g_assemblyDirectory.back() != L'\\' && g_assemblyDirectory.back() != L'/')
				g_assemblyDirectory.push_back(L'\\');
			g_assemblyDirectory += L"KarstPatcher";

			HRESULT hr = ::CLRCreateInstance(CLSID_CLRMetaHost, IID_PPV_ARGS(&g_metaHost));
			if (FAILED(hr))
				return Failed("CLRCreateInstance(CLRMetaHost)", hr);

			// v4.0.30319 is the CLR of .NET Framework 4.x, which the three
			// assemblies target (they are v4.8 builds).
			hr = g_metaHost->GetRuntime(L"v4.0.30319", IID_PPV_ARGS(&g_runtimeInfo));
			if (FAILED(hr))
				return Failed("ICLRMetaHost::GetRuntime(v4.0.30319)", hr);

			BOOL loadable = FALSE;
			hr = g_runtimeInfo->IsLoadable(&loadable);
			if (FAILED(hr))
				return Failed("ICLRRuntimeInfo::IsLoadable", hr);

			if (!loadable)
			{
				Karst::LogInfo(L"ScriptHost: the v4.0.30319 runtime reports itself "
					L"as not loadable.");
				return false;
			}

			// CorRuntimeHost (not CLRRuntimeHost): the legacy AppDomain API is
			// the one the reference host uses, and it is present on every 4.x
			// runtime.
			hr = g_runtimeInfo->GetInterface(CLSID_CorRuntimeHost, IID_PPV_ARGS(&g_corHost));
			if (FAILED(hr))
				return Failed("ICLRRuntimeInfo::GetInterface(CorRuntimeHost)", hr);

			hr = g_corHost->Start();
			if (FAILED(hr))
				return Failed("ICorRuntimeHost::Start", hr);

			g_clrStarted = true;

			Karst::LogInfo(L"ScriptHost: the .NET runtime is up; assemblies are "
				L"expected in '%ls'.", g_assemblyDirectory.c_str());

			return true;
		}

		bool LoadScriptSystem()
		{
			if (!g_clrStarted)
			{
				Karst::LogInfo(L"ScriptHost: LoadScriptSystem called before InitClr.");
				return false;
			}

			// Let the CLR find KarstPatcher.dll and, through it, KarstYRpp.dll and
			// KarstExtension.dll. The runtime resolves a dependency next to the
			// assembly that needs it, so one directory entry is enough.
			{
				const std::wstring full = g_assemblyDirectory + L"\\KarstPatcher.dll";
				if (::GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES)
				{
					Karst::LogInfo(L"ScriptHost: '%ls' is missing - scripts are off.",
						full.c_str());
					return false;
				}
			}

			// The host reads this during its own initialisation and throws if it
			// is absent, so it has to exist before the assembly is activated.
			EnsureCompilerConfig(g_assemblyDirectory);

			// And it must happen BEFORE the CLR touches any assembly, because a
			// file carrying the "downloaded from the Internet" mark cannot be
			// loaded at all - see UnblockFile for the measurement.
			UnblockAssemblies(g_assemblyDirectory);

			IUnknown* domainThunk = nullptr;
			HRESULT hr = g_corHost->GetDefaultDomain(&domainThunk);
			if (FAILED(hr))
				return Failed("ICorRuntimeHost::GetDefaultDomain", hr);

			mscorlib::_AppDomain* domain = nullptr;
			hr = domainThunk->QueryInterface(__uuidof(mscorlib::_AppDomain),
				reinterpret_cast<void**>(&domain));
			domainThunk->Release();

			if (FAILED(hr) || !domain)
				return Failed("QueryInterface(_AppDomain)", hr);

			// Load_2 resolves by assembly DISPLAY NAME, not by file path.
			//
			// Measured: passing the absolute path
			// "E:\...\KarstPatcher\KarstPatcher.dll" fails with 0x80131047
			// (COR_E_ASSEMBLYEXPECTED - "the file is not a valid assembly"),
			// even though the file IS a valid assembly (verified with
			// AssemblyName.GetAssemblyName). The runtime parses the argument as a
			// display name, and a path is not one.
			//
			// So the current directory is moved to the assembly folder first
			// (which also gives the CLR its probing base for KarstYRpp.dll and
			// KarstExtension.dll) and the assembly is requested by simple name -
			// what the reference host does.
			::SetCurrentDirectoryW((g_assemblyDirectory + L"\\").c_str());

			mscorlib::_Assembly* assembly = nullptr;
			hr = domain->Load_2(::SysAllocString(L"KarstPatcher"), &assembly);
			if (FAILED(hr) || !assembly)
				return Failed("_AppDomain::Load_2(KarstPatcher)", hr);

			const wchar_t* typeName = L"DynamicPatcher.Program";

			_variant_t instance;
			hr = assembly->CreateInstance(::SysAllocString(typeName), instance.GetAddress());
			if (FAILED(hr))
				return Failed("_Assembly::CreateInstance(DynamicPatcher.Program)", hr);

			mscorlib::_Object* object = nullptr;
			hr = instance.punkVal
				? instance.punkVal->QueryInterface(__uuidof(mscorlib::_Object),
					reinterpret_cast<void**>(&object))
				: E_POINTER;
			if (FAILED(hr) || !object)
				return Failed("QueryInterface(_Object)", hr);

			mscorlib::_Type* type = nullptr;
			hr = object->GetType(&type);
			if (FAILED(hr) || !type)
				return Failed("_Object::GetType", hr);

			const wchar_t* methodName = L"Activate";

			mscorlib::_MethodInfo* method = nullptr;
			hr = type->GetMethod_6(::SysAllocString(methodName), &method);
			if (FAILED(hr) || !method)
				return Failed("_Type::GetMethod_6(Activate)", hr);

			_variant_t self;
			self.vt = VT_DISPATCH;
			self.pdispVal = static_cast<IDispatch*>(object);

			_variant_t result;
			hr = method->Invoke_3(self, nullptr, result.GetAddress());
			if (FAILED(hr))
				return Failed("Program::Activate()", hr);

			// Cache the per-frame entry while the type is in hand.
			hr = type->GetMethod_6(::SysAllocString(L"DoTick"), &g_doTick);
			if (FAILED(hr) || !g_doTick)
			{
				Karst::LogInfo(L"ScriptHost: Program.DoTick not found - scripts "
					L"will load but never run.");
			}

			g_scriptsLoaded = true;

			Karst::LogInfo(L"ScriptHost: the script system is loaded and active.");

			return true;
		}

		bool ScriptSystemLoaded()
		{
			return g_scriptsLoaded;
		}

		void TickScripts()
		{
			if (!g_scriptsLoaded || !g_doTick)
				return;

			// Static method: empty instance, no arguments. Program.DoTick wraps the
			// call in try/catch on the managed side, so a faulting script cannot
			// unwind into the engine's main loop.
			_variant_t result;
			g_doTick->Invoke_3(_variant_t(), nullptr, result.GetAddress());
		}
	}
}
