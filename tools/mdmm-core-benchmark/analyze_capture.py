#!/usr/bin/env python3
"""Read an MD/MM JSONL capture; report inclusive wall timings, never CPU samples."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import statistics

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture', type=Path)
a = p.parse_args()
data = a.capture.read_bytes()
rows = [json.loads(line) for line in data.decode('utf-8-sig').splitlines() if line.strip()]
sessions = [r for r in rows if r.get('type') == 'session']
summaries = [r for r in rows if r.get('type') == 'summary']
callbacks = [r for r in rows if r.get('type') == 'callback']
if len(sessions) != 1 or not summaries:
    p.error('expected one session and at least one summary')
s, session = summaries[-1], sessions[0]
n = s['outerHostCallbackCount']
outer = s['outerHostCallbackNanoseconds']
if not n or not outer:
    p.error('empty capture')
complete = len(callbacks) == n and [r['index'] for r in callbacks] == list(range(1, n + 1))
consistency = {}
for field, total in [('durationNanoseconds', 'outerHostCallbackNanoseconds'),
                     ('synthNanoseconds', 'synthProcessNanoseconds'),
                     ('resamplerNanoseconds', 'resamplerNanoseconds'),
                     ('deviceNanoseconds', 'deviceProcessNanoseconds'),
                     ('lockWaitNanoseconds', 'synthProcessLockWaitNanoseconds')]:
    consistency[field] = sum(r[field] for r in callbacks) == s[total] if complete else None
settings = {k: sorted({r[k] for r in callbacks}) for k in
            ['frames', 'sampleRate', 'deviceSampleRate', 'resamplerMode',
             'dspClockPercent', 'outputBuses', 'outputChannels', 'offline', 'bypassed']}
if complete and not all(consistency.values()):
    p.error('complete trace disagrees with cumulative summary')
statistics_ms = {}
for field in ['durationNanoseconds', 'deviceNanoseconds', 'lockWaitNanoseconds']:
    values = sorted(r[field] / 1e6 for r in callbacks)
    if values:
        statistics_ms[field] = dict(mean=statistics.mean(values), p50=statistics.median(values),
                                   p99=values[int((len(values)-1)*.99)], maximum=values[-1])
result = dict(schema='mdmm-capture-analysis-v1', captureSha256=hashlib.sha256(data).hexdigest(),
              session=session, records=dict(collections.Counter(r.get('type') for r in rows)),
              captureSeconds=s['elapsedNanoseconds']/1e9, callbackCount=n,
              traceComplete=complete, traceSummaryConsistency=consistency,
              endReason=next((r.get('reason') for r in reversed(rows) if r.get('type') == 'end'), None),
              settings=settings, wallMilliseconds=statistics_ms,
              callbackTimeSplitPercent=dict(device=s['deviceProcessNanoseconds']/outer*100,
                  resamplerAndDispatchOverhead=(s['resamplerNanoseconds']-s['deviceProcessNanoseconds'])/outer*100,
                  remainder=(outer-s['resamplerNanoseconds'])/outer*100),
              estimatedOverruns=s['outerHostCallbackOverrunCount'],
              realtimeBudgetHistogram=s['realtimeBudgetHistogram'],
              jitCompilations=s['jitCompilationCount'],
              dualMachineCallbacks=s['deferredDualMachineCallbackCount'],
              droppedCallbacks=s['slowCallbacksDropped'], droppedEvents=s['timelineEventsDropped'],
              totalLockWaitMilliseconds=s['synthProcessLockWaitNanoseconds']/1e6,
              notes='Inclusive wall timings. Device share is not an MCU/DSP CPU attribution. Complete trace required for population percentiles.')
if complete:
    budget = sum(r['budgetNanoseconds'] for r in callbacks)
    result['nominalBudgetUsedPercent'] = outer / budget * 100
print(json.dumps(result, indent=2, ensure_ascii=False))
