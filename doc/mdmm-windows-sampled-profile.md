# Next Windows measurement: sampled CPU stacks

Use the known clean Windows Release (snapshot `30ddd9e1`), with its matching
DLL/PDB files. Stop the plugin's Performance capture and leave optional probes
disabled. This task needs no new diagnostic emulator build.

Open an administrator PowerShell in an output directory and start the built-in
CPU profile:

```powershell
wpr -start CPU -filemode
```

Only proceed if that command succeeds. If another recording is already active,
leave it intact rather than cancelling it. Run the same MM kit/pattern for about
30 seconds, then stop this successful recording:

```powershell
wpr -stop mdmm-cpu.etl
```

Keep sample rate, host block size, connected outputs and emulated clock unchanged.
Record host/process name, plugin revision and the approximate active interval.
Do not publish the raw ETL in the source repository.

Open the ETL in Windows Performance Analyzer and load the exact DLL's matching
PDB symbols. In CPU Usage (Sampled), select the steady interval and filter to the
plugin host process. Group by Process, Thread ID, Stack; inspect Module, Function,
Address, Weight and Count as well. Provide the dominant native stacks and their
sample weights/counts, plus the total selected process/thread weight. Keep
unresolved/JIT addresses as a separate group; missing symbols are not zero cost.
Generated JIT memory may not have named symbols or unwind information in this
trace. Native PDBs alone do not resolve all generated DSP code. This recording
therefore helps distinguish native hot paths from anonymous execution, without
promising complete JIT block attribution.

Microsoft references:
- https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options
- https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr
