////////////////////////////////////////////////////////////////////////
//
// This file is part of sc4-single-cpu, a DLL Plugin for SimCity 4
// that automatically configures the game to use a single CPU.
//
// Copyright (c) 2024 Nicholas Hayes
//
// This file is licensed under terms of the MIT License.
// See LICENSE.txt for more information.
//
////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "version.h"
#include "cIGZCmdLine.h"
#include "cIGZFrameWork.h"
#include "cRZBaseString.h"
#include "cRZCOMDllDirector.h"
#include <string>

#include <Windows.h>
#include "wil/resource.h"
#include "wil/result.h"
#include "wil/win32_helpers.h"

static constexpr uint32_t kSC4SingleCPUDllDirector = 0x83868AE0;

static constexpr std::string_view PluginLogFileName = "SC4SingleCPU.log";

namespace
{
	std::filesystem::path GetDllFolderPath()
	{
		wil::unique_cotaskmem_string modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());

		std::filesystem::path temp(modulePath.get());

		return temp.parent_path();
	}

	DWORD_PTR GetLowestSetBitMask(DWORD_PTR value)
	{
		// Adapted from https://stackoverflow.com/a/12250963
		//
		// This relies on the fact that C++ integers are "two's complement".
		// For example: 15 (00001111) & -15 (11110001) would return 1 (00000001).
		return static_cast<DWORD_PTR>(static_cast<uintptr_t>(value) & -static_cast<intptr_t>(value));
	}

	void ConfigureForSingleCPU()
	{
		Logger& logger = Logger::GetInstance();

		try
		{
			HANDLE hProcess = GetCurrentProcess();

			DWORD_PTR processAffinityMask = 0;
			DWORD_PTR systemAffinityMask = 0;

			THROW_IF_WIN32_BOOL_FALSE(GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask));

			// SetProcessAffinityMask takes a bit mask that specifies which physical/logical CPU cores
			// are used for the process, we select the first core that is enabled in the system mask.
			//
			// We do this instead of hard-coding a value of 1 to handle the case where the system mask
			// doesn't have the first logical processor enabled.
			const DWORD_PTR firstLogicalProcessor = GetLowestSetBitMask(systemAffinityMask);

			THROW_IF_WIN32_BOOL_FALSE(SetProcessAffinityMask(hProcess, firstLogicalProcessor));
			logger.WriteLine(
				LogLevel::Info,
				"Configured the game to use 1 CPU core.");
		}
		catch (const wil::ResultException& e)
		{
			logger.WriteLineFormatted(
				LogLevel::Error,
				"An OS error occurred when configuring the game to use 1 CPU: %s.",
				e.what());
		}
	}
}

class SingleCPUDllDirector : public cRZCOMDllDirector
{
public:

	SingleCPUDllDirector()
	{
		std::filesystem::path dllFolderPath = GetDllFolderPath();

		std::filesystem::path logFilePath = dllFolderPath;
		logFilePath /= PluginLogFileName;

		Logger& logger = Logger::GetInstance();
		logger.Init(logFilePath, LogLevel::Error);
		logger.WriteLogFileHeader("SC4SingleCPU v" PLUGIN_VERSION_STR);
	}

	uint32_t GetDirectorID() const
	{
		return kSC4SingleCPUDllDirector;
	}

	bool OnStart(cIGZCOM* pCOM)
	{
		cIGZFrameWork* const pFramework = RZGetFramework();

		const cIGZCmdLine* pCmdLine = pFramework->CommandLine();

		cRZBaseString value;
		if (pCmdLine->IsSwitchPresent(cRZBaseString("CPUCount"), value, true))
		{
			// We don't second guess the user if they explicitly set the CPUCount argument.
			// SC4 processes its command line arguments before DLLs are loaded, so whatever value
			// the user requested will have already been applied.
			Logger::GetInstance().WriteLineFormatted(
				LogLevel::Info,
				"Skipped because the command line contains -CPUCount:%s.",
				value.ToChar());
		}
		else
		{
			ConfigureForSingleCPU();
		}

		return true;
	}
};

cRZCOMDllDirector* RZGetCOMDllDirector() {
	static SingleCPUDllDirector sDirector;
	return &sDirector;
}