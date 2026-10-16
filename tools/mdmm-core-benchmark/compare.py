#!/usr/bin/env python3
"""Alternate preserved control/candidate executables; verify full PCM and clocks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('control', type=Path)
p.add_argument('candidate', type=Path)
p.add_argument('model', choices=['md', 'mm'])
p.add_argument('firmware', type=Path)
p.add_argument('output', type=Path, help='new directory for private logs and JSON')
p.add_argument('--seconds', type=int, default=3)
p.add_argument('--repeats', type=int, default=3)
p.add_argument('--cpu', type=int, help='Linux CPU affinity for the child process')
p.add_argument('--control-env', action='append', default=[], metavar='NAME=VALUE')
p.add_argument('--candidate-env', action='append', default=[], metavar='NAME=VALUE')
a = p.parse_args()
def parse_environment(items):
    result = {}
    for item in items:
        name, separator, value = item.partition('=')
        if not separator or not name:
            p.error('environment overrides must use NAME=VALUE')
        result[name] = value
    return result
environment = {'control': parse_environment(a.control_env),
               'candidate': parse_environment(a.candidate_env)}
if not (1 <= a.seconds <= 120 and 1 <= a.repeats <= 20):
    p.error('seconds must be 1..120 and repeats 1..20')
if a.cpu is not None and (not hasattr(os, 'sched_setaffinity') or a.cpu not in os.sched_getaffinity(0)):
    p.error('requested CPU is not available for affinity')
a.output.mkdir(parents=True, exist_ok=False)
control, candidate, firmware = (x.resolve(strict=True) for x in (a.control, a.candidate, a.firmware))
reference, records = None, []
keys = ['model', 'phase', 'bootFrames', 'frames', 'pcmFnv64',
        'dsp1Cycles', 'dsp2Cycles', 'dsp1Pc', 'dsp2Pc']
summary = {
    'controlSha256': hashlib.sha256(control.read_bytes()).hexdigest(),
    'candidateSha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
    'firmwareSha1': hashlib.sha1(firmware.read_bytes()).hexdigest(),
    'model': a.model, 'secondsPerPhase': a.seconds, 'repeats': a.repeats,
    'cpuAffinity': a.cpu, 'runs': records,
    'environmentOverrides': environment,
}
for repeat in range(a.repeats):
    order = [('control', control), ('candidate', candidate)]
    if repeat % 2:
        order.reverse()
    for label, executable in order:
        name = f'{repeat + 1}-{label}'
        with tempfile.TemporaryDirectory(prefix='pcm-', dir=a.output) as temp:
            pcm = Path(temp).resolve() / 'render.f32'
            cmd = [str(executable), a.model, str(firmware), str(a.seconds), str(pcm)]
            with (a.output / (name + '.log')).open('w') as log:
                subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True,
                               env={**os.environ, **environment[label]},
                               preexec_fn=(lambda: os.sched_setaffinity(0, {a.cpu})) if a.cpu is not None else None)
            phases = [json.loads(line) for line in (a.output / (name + '.log')).read_text().splitlines()
                      if line.startswith('{')]
            if len(phases) != 3 or [x['phase'] for x in phases] != [0, 1, 2]:
                raise RuntimeError(f'{name}: expected exactly three phase records')
            digest = hashlib.sha256(pcm.read_bytes()).hexdigest()
            identity = (digest, [[x[k] for k in keys] for x in phases])
            if reference is None:
                reference = identity
            if identity != reference:
                raise RuntimeError(f'{name}: PCM or DSP/boot boundary differs from control')
            record = dict(run=name, kind=label, pcmSha256=digest, phases=phases)
            records.append(record)
            (a.output / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(name, 'PCM/clocks exact;', 'render CPU seconds:',
                  round(sum(x['renderCpuSeconds'] for x in phases), 6), flush=True)
summary['phases'] = []
for phase in range(3):
    control_times = [r['phases'][phase]['renderCpuSeconds'] for r in records if r['kind'] == 'control']
    candidate_times = [r['phases'][phase]['renderCpuSeconds'] for r in records if r['kind'] == 'candidate']
    c, n = statistics.median(control_times), statistics.median(candidate_times)
    summary['phases'].append(dict(phase=phase, controlMedianCpuSeconds=c,
                                 candidateMedianCpuSeconds=n, cpuReductionPercent=100 * (1 - n / c)))
c = statistics.median(sum(x['renderCpuSeconds'] for x in r['phases']) for r in records if r['kind'] == 'control')
n = statistics.median(sum(x['renderCpuSeconds'] for x in r['phases']) for r in records if r['kind'] == 'candidate')
summary['total'] = dict(controlMedianCpuSeconds=c, candidateMedianCpuSeconds=n,
                        cpuReductionPercent=100 * (1 - n / c))
(a.output / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(dict(phases=summary['phases'], total=summary['total']), indent=2), flush=True)
