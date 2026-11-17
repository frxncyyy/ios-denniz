#!/usr/bin/env python3
"""Read-only, bounded local HFSX audio prerequisite inspection; no mount/replay."""
import importlib.util,json,pathlib,stat,hashlib
ROOT=pathlib.Path(__file__).resolve().parents[5]
reader_path=ROOT/'research/nord2/wifi/claude-opus-security-failure/review-log-access/collect.py'
s=importlib.util.spec_from_file_location('bounded_audio_hfs',reader_path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
image=ROOT/'research/nord2/rootfs-vm/ios16-root-20H392-nord2-v1.img'
paths=['System/Library/Audio/Plug-Ins/HAL','Library/Audio/Plug-Ins/HAL','Library/Audio/Plug-Ins/HAL.disabled','Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin','Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin','Library/Audio/Tunings/D22','System/Library/Audio/Tunings/D22','System/Library/Audio/Plug-Ins','Library/Audio/Plug-Ins','Library/Audio/Tunings','System','System/Library','System/Library/Audio','Library','Library/Audio','Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin/Info.plist','Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin/VirtualAudio','System/Library/Audio/Plug-Ins/HAL.nord2-original','System/Library/Audio/Plug-Ins/HAL.nord2-stage','System/Library/Audio/Plug-Ins/HAL.nord2-journal','System/Library/Audio/Plug-Ins/HAL.nord2-commit','System/Library/Audio/Plug-Ins/HAL.nord2-rollback']
def main():
 with image.open('rb') as f:
  def fetch(off,n):f.seek(off);return f.read(n)
  r=m.Reader(fetch,image.stat().st_size)
  out=[]
  for path in paths:
   item={'path':path}
   try:
    rec=r.resolve(path)
    if rec is None:item['found']=False
    else:
     mode=m.h.U16(rec,42);item.update(found=True,cnid=m.h.U32(rec,8),uid=m.h.U32(rec,32),gid=m.h.U32(rec,36),mode=oct(mode),record_type=m.h.U16(rec,0),symlink=stat.S_ISLNK(mode))
     if stat.S_ISDIR(mode):
      children=r.children(path)
      item['children']=[x[0] if isinstance(x,tuple) else x for x in children]
   except Exception as e:item['refusal']=str(e)
   out.append(item)
  report={'image':str(image.relative_to(ROOT)),'image_bytes':image.stat().st_size,'reader':str(reader_path.relative_to(ROOT)),'reader_sha256':hashlib.sha256(reader_path.read_bytes()).hexdigest(),'dirty':r.dirty,'read_bytes':r.read_bytes,'paths':out}
  print(json.dumps(report,indent=2,sort_keys=True))

if __name__=='__main__':main()
