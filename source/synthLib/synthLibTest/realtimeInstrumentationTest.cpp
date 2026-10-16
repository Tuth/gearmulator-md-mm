#include "synthLib/realtimeInstrumentation.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "synthLibRealtimeInstrumentationTest: " << _message << '\n';
		std::exit(1);
	}
}

int main()
{
	synthLib::RealtimeInstrumentation instrumentation;
	instrumentation.setEnabled(false);
	instrumentation.recordSynthProcess(100);
	instrumentation.recordSynthProcessLockWait(20);
	instrumentation.recordResampler(70, 60, 64, true);
	{
		synthLib::RealtimeInstrumentation::CallbackScope callback(
			instrumentation, 64, 48'000.0);
		synthLib::RealtimeInstrumentation::recordCurrentCallbackJitCompilation();
		synthLib::RealtimeInstrumentation::DeferredCandidateScope candidate(64);
	}
	require(instrumentation.snapshot().synthProcessCount == 0,
		"disabled instrumentation recorded work");
	require(instrumentation.snapshot().outerHostCallbackCount == 0
		&& instrumentation.snapshot().jitCompilationCount == 0
		&& instrumentation.snapshot().deferredCandidateAdvanceCount == 0,
		"disabled callback scopes recorded work");

	instrumentation.reset();
	instrumentation.setEnabled(true);
	synthLib::RealtimeInstrumentation::recordCurrentCallbackJitCompilation();
	require(instrumentation.snapshot().jitCompilationCount == 0,
		"JIT work outside an outer host callback was attributed");
	{
		synthLib::RealtimeInstrumentation::CallbackScope callback(
			instrumentation, 1, 1'000'000'000.0);
		callback.setActiveOutputLayout(3, 6);
		synthLib::RealtimeInstrumentation::recordCurrentCallbackJitCompilation();
		{
			synthLib::RealtimeInstrumentation::DeferredCandidateScope candidate(64);
			synthLib::RealtimeInstrumentation::recordCurrentCallbackJitCompilation();
			// An empty scope can span zero clock ticks on fast/coarse-clock hosts.
			// Give this timing-attribution fixture a known nonzero duration.
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	{
		synthLib::RealtimeInstrumentation::CallbackScope callback(
			instrumentation, 64, 48'000.0, true);
	}
	instrumentation.recordSynthProcess(100);
	instrumentation.recordSynthProcess(250);
	instrumentation.recordSynthProcessLockWait(20);
	instrumentation.recordSynthProcessLockWait(80);
	instrumentation.recordResampler(70, 60, 64, true);

	const auto snapshot = instrumentation.snapshot();
	require(snapshot.enabled && snapshot.outerHostCallbackCount == 2,
		"enabled callback was not counted");
	require(snapshot.outerHostCallbackOverrunCount >= 1
		&& snapshot.outerHostCallbackMaxOverrunNanoseconds > 0,
		"callback budget overrun was not counted");
	require(snapshot.jitCompilationCount == 2
		&& snapshot.liveJitCompilationCount == 1
		&& snapshot.deferredCandidateJitCompilationCount == 1
		&& snapshot.callbacksWithJitCompilation == 1,
		"callback JIT attribution is wrong");
	require(snapshot.deferredDualMachineCallbackCount == 1
		&& snapshot.deferredCandidateAdvanceCount == 1
		&& snapshot.deferredCandidateFrames == 64
		&& snapshot.deferredCandidateNanoseconds > 0,
		"deferred-candidate work was not attributed");
	require(snapshot.callbacksWithAuxOutputBuses == 1
		&& snapshot.maximumActiveOutputBuses == 3
		&& snapshot.maximumActiveOutputChannels == 6,
		"active output layout was not recorded");
	require(snapshot.bypassedCallbackCount == 1,
		"bypassed outer callback was not distinguished");
	require(snapshot.synthProcessCount == 2
		&& snapshot.synthProcessNanoseconds == 350
		&& snapshot.synthProcessMaxNanoseconds == 250,
		"plugin process totals are wrong");
	require(snapshot.synthProcessLockWaitNanoseconds == 100
		&& snapshot.synthProcessLockWaitMaxNanoseconds == 80,
		"lock-wait totals are wrong");
	require(snapshot.resamplerCallCount == 1
		&& snapshot.resamplerNanoseconds == 70
		&& snapshot.deviceProcessNanoseconds == 60
		&& snapshot.resamplerHostFrames == 64
		&& snapshot.resamplingActiveCallbackCount == 1,
		"resampler/device work totals are wrong");

	// Nested components partition callback wall time. Nested compilation batches
	// must not double-count children, and deferred work has its own bucket.
	using RI = synthLib::RealtimeInstrumentation;
	instrumentation.reset();
	{
		RI::CallbackScope callback(instrumentation, 64, 48000);
		RI::ComponentScope mcu(RI::Component::Mcu);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		{
			RI::ComponentScope dsp(RI::Component::Dsp1);
			const auto parent = RI::beginCurrentJitTiming();
			const auto child = RI::beginCurrentJitTiming();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			RI::endCurrentJitTiming(child);
			RI::endCurrentJitTiming(parent);
			RI::DeferredCandidateScope candidate(64);
			RI::ComponentScope dsp2(RI::Component::Dsp2);
			const auto deferred = RI::beginCurrentJitTiming();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			RI::endCurrentJitTiming(deferred);
		}
	}
	const auto timed = instrumentation.snapshot();
	require(timed.mcuSliceNanoseconds > 0 && timed.dsp1SliceNanoseconds > 0
		&& timed.dsp2SliceNanoseconds > 0, "nested component bucket missing");
	require(timed.mcuSliceNanoseconds + timed.dsp1SliceNanoseconds + timed.dsp2SliceNanoseconds
		<= timed.outerHostCallbackNanoseconds, "nested component time double-counted");
	require(timed.liveJitNanoseconds > 0 && timed.liveJitNanoseconds <= timed.dsp1SliceNanoseconds
		&& timed.deferredJitNanoseconds > 0 && timed.deferredJitNanoseconds <= timed.dsp2SliceNanoseconds
		&& timed.maxJitCompilationBatchNanoseconds <= std::max(timed.liveJitNanoseconds, timed.deferredJitNanoseconds),
		"JIT nesting or candidate timing attribution wrong");
	require(RI::beginCurrentJitTiming() == 0, "JIT timing outside callback was active");
	{
		RI::CallbackScope callback(instrumentation, 64, 48000);
		RI::ComponentScope scope(RI::Component::Dsp1);
		const auto jit = RI::beginCurrentJitTiming();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		RI::endCurrentJitTiming(jit);
	}
	const auto unsampled = instrumentation.snapshot();
	require(unsampled.componentSampledCallbackCount == 1
		&& unsampled.mcuSliceNanoseconds == timed.mcuSliceNanoseconds
		&& unsampled.dsp1SliceNanoseconds == timed.dsp1SliceNanoseconds
		&& unsampled.dsp2SliceNanoseconds == timed.dsp2SliceNanoseconds
		&& unsampled.liveJitNanoseconds > timed.liveJitNanoseconds,
		"component sampling skipped JIT timing or recorded an unsampled component");
	instrumentation.reset();
	require(instrumentation.snapshot().liveJitNanoseconds == 0
		&& instrumentation.snapshot().mcuSliceNanoseconds == 0,
		"reset did not clear new timings");
	require(instrumentation.snapshot().outerHostCallbackCount == 0,
		"reset did not clear counters");
	std::cout << "synthLib realtime instrumentation: PASS\n";
	return 0;
}
