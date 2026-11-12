#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# decrun: host reference for n2vt_decrun.  For each clip, ffmpeg decodes every picture and md5s its luma plane (extractplanes=y:
# the decoded Y bytes, width bytes per row, cropped size, display order; dr2 fix: '-pix_fmt gray' went through swscale, which
# expands video range to full range and changed every hash -- checked: extractplanes=y equals the dprobe fnv_y of vc7-dec4k2) -- the same bytes n2vt_decrun hashes from VT's 420v plane 0.
# Writes JSON {clip: {w, h, frames, md5: [...], md5first, md5all, md5set}} (md5all/md5set exactly as in n2vt_decrun.c).
# usage: expected_md5.py OUT.json CLIP...
import hashlib, json, os, subprocess, sys

def ref(path):
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:v:0', '-vf', 'extractplanes=y', '-f', 'framemd5', '-'],
                         capture_output=True, text=True, check=True).stdout
    md5 = [l.split(',')[-1].strip() for l in out.splitlines() if l and not l.startswith('#')]
    wh = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height', '-of', 'csv=p=0', path],
                        capture_output=True, text=True, check=True).stdout.strip().split(',')
    dig = lambda l: hashlib.md5(''.join(m + '\n' for m in l).encode()).hexdigest()
    return {'w': int(wh[0]), 'h': int(wh[1]), 'frames': len(md5), 'md5': md5, 'md5first': md5[0] if md5 else None,
            'md5all': dig(md5), 'md5set': dig(sorted(md5))}

if __name__ == '__main__':
    if len(sys.argv) < 3: sys.exit(__doc__ or 'usage: expected_md5.py OUT.json CLIP...')
    res = {os.path.basename(c): ref(c) for c in sys.argv[2:]}
    json.dump(res, open(sys.argv[1], 'w'), indent=1)
    for k, v in res.items(): print(k, '%dx%d' % (v['w'], v['h']), 'frames', v['frames'], 'md5first', v['md5first'], 'md5all', v['md5all'], 'md5set', v['md5set'])
