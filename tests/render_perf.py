"""Measure host CPU use while idle and native command response latency."""
import argparse
import json
import statistics
import re
import time
from qemu_support import Machine, ROOT

parser = argparse.ArgumentParser()
parser.add_argument('--output', default='build/performance.json')
args = parser.parse_args()
machine = Machine('performance')
try:
    time.sleep(.3)
    begin = machine.cpu_seconds()
    wall_begin = time.monotonic()
    time.sleep(2)
    idle_cpu = (machine.cpu_seconds()-begin)/(time.monotonic()-wall_begin)*100
    machine.key('f2')
    latencies = []
    for i in range(12):
        marker=f'echo latency{i}'
        machine.type(marker, enter=False)
        offset=machine.log.stat().st_size
        before=time.monotonic()
        machine.key('ret', delay=0)
        machine.wait('CMD: '+marker, start=offset)
        latencies.append((time.monotonic()-before)*1000)
        time.sleep(.03)
    offset=machine.log.stat().st_size
    machine.type('perf');machine.wait('RENDER frames=',start=offset)
    match=list(re.finditer(r'gear_frames=(\d+)',machine.log.read_text()))[-1]
    frames_before=int(match[1]);cpu_before=machine.cpu_seconds();before=time.monotonic()
    machine.key('f5');time.sleep(2);machine.key('f2')
    elapsed=time.monotonic()-before;active_cpu=(machine.cpu_seconds()-cpu_before)/elapsed*100
    offset=machine.log.stat().st_size
    machine.type('perf');machine.wait('RENDER frames=',start=offset)
    frames_after=int(list(re.finditer(r'gear_frames=(\d+)',machine.log.read_text()))[-1][1])
    result={'gear_frames_per_second':round((frames_after-frames_before)/elapsed,2),
            'active_gears_qemu_host_cpu_percent':round(active_cpu,2),
            'idle_qemu_host_cpu_percent' :round(idle_cpu,2),
            'command_response_median_ms':round(statistics.median(latencies),3),
            'command_response_max_ms':round(max(latencies),3),
            'response_samples_ms':[round(x,3) for x in latencies],
            'environment':'QEMU TCG, q35, 512 MiB, headless, default GOP'}
    (ROOT/args.output).write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
finally:
    machine.close()
