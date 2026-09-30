#pragma once

#include <atomic>
#include <string>

namespace pluginLib
{
	// Opt-in Windows-only per-thread CPU attribution probe (default OFF).
	// Enable with GEARMULATOR_MDMM_THREAD_AUDIT=1 in the host process.
	// Reports every thread of the current process (id, name, user+kernel CPU %)
	// via OutputDebugStringA/printf so DebugView or a console host can read it.
	// Purpose: attribute the known "backgrounded host adds ~10% CPU" issue to a
	// concrete thread (JUCE message thread timers, RmlUI-Renderer GL thread,
	// audio callback thread, MidiOutputSender, DSP threads, ...).
	class WinThreadAudit
	{
	public:
		static WinThreadAudit& getInstance();

		// No-op unless GEARMULATOR_MDMM_THREAD_AUDIT is set to a value != "0".
		void start();
		void stop();

	private:
		WinThreadAudit() = default;
		~WinThreadAudit();

		WinThreadAudit(const WinThreadAudit&) = delete;
		WinThreadAudit& operator=(const WinThreadAudit&) = delete;

		void samplerLoop(long _generation);

		std::atomic<bool> m_running{false};
		std::atomic<long> m_generation{0};
		std::string m_threadName;	// name of the native sampler thread
	};
}
