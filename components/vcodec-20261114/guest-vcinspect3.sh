set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
L=open('/work/root/private/var/db/nord2-vcodec/vcodecd.log','rb').read().decode('latin1').splitlines()
idx=[i for i,l in enumerate(L) if ' start dev=' in l]
for b in (154,159,232):
    s=idx[b]; e=idx[b+1] if b+1<len(idx) else len(L)
    print('=== BOOT',b, e-s)
    for l in L[s:e]:
        if 'n2a32_mem' in l or 'N2VHOST' in l: continue
        print('  ',l[:260])
PY
ls -la --time-style=full-iso /work/root/private/var/mobile/Media/DCIM/*/IMG_006* /work/root/private/var/mobile/Media/DCIM/*/IMG_007* 2>&1 | head
umount /work/root
echo INSPECT_DONE
