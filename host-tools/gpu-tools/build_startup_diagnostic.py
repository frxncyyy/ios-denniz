#!/usr/bin/env python3
"""Private offline build: bound early IOKit/structor diagnostics before USB.

Preserves the approved probe/kernel. Does not change hardware sequencing or
perform device operations. Exact source copies, compile logs and link receipt
are saved beside the diagnostic kernel.
"""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'gpu/build/kernel-native'
STAGE = ROOT / 'gpu/build/xnu-stage'
OUT = ROOT / 'gpu/build/startup-diagnostic'
OUT.mkdir(exist_ok=True)


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def replace_once(text, anchor, replacement):
    assert text.count(anchor) == 1, anchor
    return text.replace(anchor, replacement)


def member_hashes(path):
    result = {}
    with path.open('rb') as f:
        assert f.read(8) == b'!<arch>\n'
        while header := f.read(60):
            assert len(header) == 60 and header[58:] == b'`\n'
            size = int(header[48:58])
            name = header[:16].decode().strip()
            body = f.read(size)
            if size & 1:
                f.read(1)
            if name.startswith('#1/'):
                count = int(name[3:])
                name, body = body[:count].rstrip(b'\0').decode(), body[count:]
            name = name.rstrip('/')
            if not name or name.startswith('__.SYMDEF'):
                continue
            assert name not in result
            result[name] = hashlib.sha256(body).hexdigest()
    return result


src = STAGE / 'iokit/Kernel/IOStartIOKit.cpp'
text = src.read_text()
helper = '''
/* Bounded pre-USB trace: no IOKit allocation, locks, MMIO or USB calls. */
static void
nord2_iokit_boot_trace(const char *phase)
{
    printf("NORD2 IOKIT INIT: %s\\n", phase);
}

'''
text = replace_once(text, 'void\niokit_post_constructor_init(void)',
                    helper + 'void\niokit_post_constructor_init(void)')
for call in ['IOCPUInitialize();', 'IOPlatformActionsInitialize();',
             'root = IORegistryEntry::initialize();', 'IOService::initialize();',
             'IOCatalogue::initialize();', 'IOStatistics::initialize();',
             'OSKext::initialize();', 'IOUserClient::initialize();',
             'IOMemoryDescriptor::initialize();', 'IORootParent::initialize();',
             'IOReporter::initialize();', 'IOPMinformeeList::getSharedRecursiveLock();',
             'IOLibInit();', 'OSlibkernInit();', 'IOMachPortInitialize();',
             'interruptAccountingInit();', 'gRootNub = new IOPlatformExpertDevice;',
             'bool ok = gRootNub->init(dtTop);', 'gRootNub->attach(NULL);',
             'record_startup_extensions_function();', 'gRootNub->configureDefaults();']:
    label = call.replace('"', '')
    anchor = '\t' + call
    text = replace_once(text, anchor,
                        '\tnord2_iokit_boot_trace("before ' + label + '");\n' +
                        anchor + '\n\tnord2_iokit_boot_trace("after ' + label + '");')
io_source = OUT / src.name
io_source.write_text(text)

src = STAGE / 'libkern/c++/OSRuntime.cpp'
text = src.read_text()
text = replace_once(text, '\t\t\t\t(*structor)();', '''
                /* Only initial kernel constructors; cap persistent-ring use. */
                if (!gKernelCPPInitialized && !theKext && firstIndex < 256) {
                    printf("NORD2 CPP INIT: seg=%.16s index=%lu pc=0x%llx\\n",
                        segment->segname, firstIndex, (unsigned long long)value);
                }
                (*structor)();''')
text = replace_once(text, '\tresult = OSMetaClass::postModLoad(metaHandle);', '''
    if (!gKernelCPPInitialized && !theKext) printf("NORD2 CPP INIT: before postModLoad\\n");
    result = OSMetaClass::postModLoad(metaHandle);
    if (!gKernelCPPInitialized && !theKext) printf("NORD2 CPP INIT: after postModLoad result=%d\\n", result);''')
runtime_source = OUT / src.name
runtime_source.write_text(text)

receipt = {'purpose': 'diagnose pre-GPU PE_init_iokit stall; not a proven fix',
           'device_tested': False, 'sources': {}}
for source, original in [(io_source, STAGE/'iokit/Kernel/IOStartIOKit.cpp'),
                         (runtime_source, STAGE/'libkern/c++/OSRuntime.cpp')]:
    obj = OUT / (source.stem + '.cpo')
    command = ['python3', str(ROOT/'gpu/tools/xnu_staged_compile.py'), str(source),
               '--cxx', '--out', str(obj), '--extra=-I'+str(original.parent)]
    with (OUT/(source.stem+'-compile.log')).open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=log, check=True)
    receipt['sources'][source.name] = {'original': sha(original), 'instrumented': sha(source), 'object': sha(obj)}

ar = '$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/llvm-ar'
archive = OUT/'kernel.startup.a'
archive.write_bytes((BASE/'kernel.nord2.a').read_bytes())
members = subprocess.check_output([ar, 't', str(archive)], text=True).splitlines()
for name in ['IOStartIOKit.cpo', 'OSRuntime.cpo']:
    assert members.count(name) == 1, name
    subprocess.run([ar, 'r', str(archive), str(OUT/name)], check=True)
before, after = member_hashes(BASE/'kernel.nord2.a'), member_hashes(archive)
assert before.keys() == after.keys()
assert {n for n in before if before[n] != after[n]} == {'IOStartIOKit.cpo', 'OSRuntime.cpo'}
receipt['untouched_candidate_archive_members'] = len(before) - 2
output = OUT/'kernel.development.nord2-metal-startup'
command = json.loads((BASE/'candidate-link-command.json').read_text())
command = [str(archive) if arg == str(BASE/'kernel.nord2.a') else arg for arg in command]
command[command.index('-o')+1] = str(output)
with (OUT/'link.log').open('w') as log:
    subprocess.run(command, stdout=log, stderr=log, check=True)
receipt.update(kernel_sha256=sha(output), bytes=output.stat().st_size,
               original_kernel_sha256=sha(BASE/'kernel.development.nord2-metal'))
assert receipt['original_kernel_sha256'] == '8a0b8e7b3d600e44a9bacfa12980de746e0e55659f29d5e81e3f8a5c487df10f'
(OUT/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
(OUT/'link-command.json').write_text(json.dumps(command, indent=2)+'\n')
print(json.dumps(receipt, indent=2))
