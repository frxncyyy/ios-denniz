#!/usr/bin/env python3
"""camprev-20261114: what a capture says about the camera preview's cost (Magnifier / Camera).
usage: camprev_check.py xnu-live.log
Prints: the bundle's 'preview: a video data output of AxB made CxD' lines (camprev active), every PrimaryScaler / StillImage pool size,
the PrimaryScaler 5 s summaries (pictures/s, made in, GPU wait), the camera ISP lines (output size, places), VideoToolbox's
VTMTSComputeFunction dispatches by grid (count, mean/max ms), mediaserverd's slow compute commits, the core's frames read per second.
Verdict CAMPREV_ACTIVE when at least one shrink line is there and no 4096x3072 PrimaryScaler pool follows it."""
import re, sys, collections

def main(path):
    shrink, pools, prim, isp, cores, slow = [], [], [], [], [], []
    disp = collections.defaultdict(list)
    for line in open(path, 'rb'):
        s = line.decode('utf-8', 'replace')
        if 'preview: a video data output of' in s:
            shrink.append(s.strip()[s.find('preview:'):][:160])
        m = re.search(r'(PrimaryScaler|SecondaryScaler|StillImage): the graph\'s own buffer pool: (\d+x\d+)', s)
        if m:
            pools.append((m.group(1), m.group(2), len(shrink)))
        m = re.search(r'PrimaryScaler of \w+ Camera: 5\.\d s: (\d+) pictures given \(([\d.]+) a second\), (\d+x\d+) \S+, made in ([\d.]+) ms', s)
        if m:
            prim.append((m.group(3), float(m.group(2)), float(m.group(4))))
        m = re.search(r'camera ISP: [\d.]+ s of (\d+x\d+) from \d+x\d+ \(([^;]*); the ISP pass over (\d+x\d+) places\): (\d+) renders', s)
        if m:
            isp.append((m.group(1), m.group(3), int(m.group(4))))
        m = re.search(r'dispatch VTMTSComputeFunction (\d+x\d+x\d+) x \S+ ([\d.]+) ms', s)
        if m:
            disp[m.group(1)].append(float(m.group(2)))
        m = re.search(r'<mediaserverd> slow: ([\d.]+) ms commit .*\(compute\)', s)
        if m:
            slow.append(float(m.group(1)))
        m = re.search(r'core: node \d camera \d: [\d.]+ s: (\d+) frames read \(([\d.]+) a second\) of (\d+) the kernel made \(([\d.]+) a second', s)
        if m:
            cores.append((float(m.group(2)), float(m.group(4))))
    print('shrink lines (camprev):', len(shrink))
    for x in shrink[:6]:
        print('  ', x)
    print('pools (output, size, after N shrink lines):', pools[:20])
    if prim:
        by = collections.defaultdict(list)
        for size, rate, made in prim:
            by[size].append((rate, made))
        for size, v in by.items():
            print('PrimaryScaler %s: %d summaries, %.1f pictures/s, made in %.1f ms (means)' % (size, len(v), sum(a for a, _ in v) / len(v), sum(b for _, b in v) / len(v)))
    for size, places, n in isp[:12]:
        print('camera ISP: output %s, pass over %s places, %d renders' % (size, places, n))
    for grid, v in sorted(disp.items(), key=lambda kv: -len(kv[1])):
        print('VTMTSComputeFunction %s: %d dispatches, mean %.1f ms, max %.1f ms' % (grid, len(v), sum(v) / len(v), max(v)))
    if slow:
        print('mediaserverd slow compute commits: %d, mean %.1f ms, max %.1f ms' % (len(slow), sum(slow) / len(slow), max(slow)))
    if cores:
        print('core: frames read %.1f/s of %.1f/s made (means of %d windows)' % (sum(a for a, _ in cores) / len(cores), sum(b for _, b in cores) / len(cores), len(cores)))
    full_after = [p for p in pools if p[0] == 'PrimaryScaler' and p[1] in ('4096x3072', '4032x3024') and p[2] > 0]
    print('CAMPREV_ACTIVE' if shrink and not full_after else 'CAMPREV_NOT_SEEN' if not shrink else 'CAMPREV_FULL_POOL_AFTER_SHRINK')

if __name__ == '__main__':
    main(sys.argv[1])
