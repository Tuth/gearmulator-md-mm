#include "winThreadAudit.h"

#ifdef _WIN32

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "dsp56kBase/threadtools.h"

namespace
{
	constexpr int g_reportIntervalMs = 2000;

	bool auditEnabled()
	{
		const auto* v = getenv("GEARMULATOR_MDMM_THREAD_AUDIT");
		return v != nullptr && strcmp(v, "0") != 0;
	}

	std::string narrow(const wchar_t* _wide)
	{
		if(_wide == nullptr)
			return {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, _wide, -1, nullptr, 0,
			nullptr, nullptr);
		if(len <= 0)
			return {};
		std::string result(static_cast<size_t>(len - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, _wide, -1, result.data(),
			static_cast<int>(result.size()) + 1, nullptr, nullptr);
		return result;
	}

	void auditLog(const std::string& _line)
	{
		OutputDebugStringA((" [MDMM-TA] " + _line + "\n").c_str());
	}
}

namespace pluginLib
{
	WinThreadAudit& WinThreadAudit::getInstance()
	{
		static WinThreadAudit instance;
		return instance;
	}

	void WinThreadAudit::start()
	{
		if(!auditEnabled())
			return;
		const auto generation = m_generation.load() + 1;
		if(m_running.exchange(true))
			return;
		m_generation.store(generation);

		std::thread([this, generation]
		{
			dsp56k::ThreadTools::setCurrentThreadName("MDMM-ThreadAudit");
			samplerLoop(generation);
		}).detach();
	}

	void WinThreadAudit::stop()
	{
		m_generation.fetch_add(1);
	}

	WinThreadAudit::~WinThreadAudit()
	{
		m_generation.fetch_add(1);
	}

	void WinThreadAudit::samplerLoop(const long _generation)
	{
		auditLog("thread audit started");

		std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> lastCpu;
		std::vector<std::pair<std::string, double>> rows;
		auto lastSnapshot = std::chrono::steady_clock::now();

		for(;;)
		{
			if(_generation != m_generation.load())
			{
				auditLog("thread audit stopped");
				return;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(g_reportIntervalMs));

			if(_generation != m_generation.load())
			{
				auditLog("thread audit stopped");
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			const auto wall = std::chrono::duration<double>(now - lastSnapshot).count();
			lastSnapshot = now;
			if(wall <= 0.0)
				continue;

			rows.clear();

			const auto pid = GetCurrentProcessId();

			auto* const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if(snapshot == INVALID_HANDLE_VALUE)
				continue;

			THREADENTRY32 entry;
			entry.dwSize = sizeof(entry);
			auto total = 0.0;

			if(Thread32First(snapshot, &entry))
			{
				do
				{
					if(entry.th32OwnerProcessID != pid)
						continue;

					auto* const hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION,
						FALSE, entry.th32ThreadID);
					if(hThread == nullptr)
						continue;

					FILETIME ftCreation, ftExit, ftKernel, ftUser;
					uint64_t kernel = 0, user = 0;
					std::string name;

					if(GetThreadTimes(hThread, &ftCreation, &ftExit, &ftKernel, &ftUser))
					{
						kernel = (static_cast<uint64_t>(ftKernel.dwHighDateTime) << 32)
							| ftKernel.dwLowDateTime;
						user = (static_cast<uint64_t>(ftUser.dwHighDateTime) << 32)
							| ftUser.dwLowDateTime;
					}

					wchar_t* desc = nullptr;
					if(SUCCEEDED(GetThreadDescription(hThread, &desc)) && desc != nullptr)
					{
						name = narrow(desc);
						LocalFree(desc);
					}

					CloseHandle(hThread);

					auto& prev = lastCpu[entry.th32ThreadID];
					const auto dKernel = kernel >= prev.first ? kernel - prev.first : 0;
					const auto dUser = user >= prev.second ? user - prev.second : 0;
					prev = {kernel, user};

					const auto usedCpu = static_cast<double>(dKernel + dUser)
						/ 10000000.0 / wall * 100.0;
					total += usedCpu;

					if(usedCpu >= 0.05 || !name.empty())
						rows.push_back({name.empty()
							? ("tid " + std::to_string(entry.th32ThreadID))
							: name, usedCpu});
				}
				while(Thread32Next(snapshot, &entry));
			}

			CloseHandle(snapshot);

			std::sort(rows.begin(), rows.end(), [](const auto& _a, const auto& _b)
			{
				return _a.second > _b.second;
			});

			auditLog("report total=" + std::to_string(total) + "% threads="
				+ std::to_string(rows.size()));

			for(size_t i = 0; i < rows.size() && i < 15; ++i)
			{
				char buf[128];
				snprintf(buf, sizeof(buf), "%6.2f%%  %s",
					rows[i].second, rows[i].first.c_str());
				auditLog(buf);
			}
		}
	}
}

#else

namespace pluginLib
{
	WinThreadAudit& WinThreadAudit::getInstance()
	{
		static WinThreadAudit instance;
		return instance;
	}
	void WinThreadAudit::start() {}
	void WinThreadAudit::stop() {}
}

#endif
