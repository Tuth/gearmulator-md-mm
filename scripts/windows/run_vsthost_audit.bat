@echo off
rem Gearmulator MD/MM - fresh VSTHost measurement launcher.
rem
rem Purpose: start VSTHost with the MD/MM per-thread CPU audit probe enabled
rem WITHOUT setting any system or user environment variable. The env vars
rem below exist only inside the VSTHost process started by this script.
rem
rem Usage:
rem   Run this script (double-click or from a shell), then load the plugin
rem   and start your measurement. Close VSTHost when done.
rem
rem Measurement rules recorded in STATE v41:
rem   - ALWAYS start VSTHost fresh for a measurement run; never load test
rem     plugins into an hours-old session (aged sessions inflate CPU readings).
rem   - Reports appear in DebugView as lines prefixed with " [MDMM-TA] ".
rem   - Capture ~30 s foreground, then ~30 s backgrounded, and compare the
rem     per-thread CPU% rows between the two phases.
rem   - The probe exists only in DIAGNOSTICS builds (-Dgearmulator_DIAGNOSTICS=ON,
rem     build dir temp/cmake_win64_diag, products root bin/plugins-diag). A clean
rem     release plugin contains no probe and never prints MDMM-TA lines.

setlocal

rem Per-process env only - nothing persistent is touched.
set "GEARMULATOR_MDMM_THREAD_AUDIT=1"
set "GEARMULATOR_RT_INSTRUMENTATION=1"

start "" "C:\VHost\vsthost.exe" %*

endlocal
