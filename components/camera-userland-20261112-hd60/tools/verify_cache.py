#!/usr/bin/env python3
"""
Checks, against the LOCAL iOS 16.7 dyld shared cache (private, never copied), every assumption HakiOS's camera code (written for 16.5) makes about
CMCapture and every one this port adds. Prints PASS/FAIL lines; exit status 1 if any FAIL. See setup_cache_work.sh for the one-time preparation of
$RWCAM_WORK (the cache's LINKEDIT subcaches are APFS-compressed, LZBITMAP, and hold the export tables).
usage: RWCAM_WORK=<dir> verify_cache.py
"""
import os, re, struct, sys, base64, hashlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cachetools'))
import dsc2, exports, d, allsyms

fails = 0
def ok(cond, what, extra=''):
    global fails
    print('%s  %s%s' % ('PASS' if cond else 'FAIL', what, ('   [' + extra + ']') if extra else ''))
    fails += 0 if cond else 1

P = d.P
def cfs(addr):                    # a CFString object's text
    return dsc2.cs(P(d.q(addr + 16)))
def const_string(sym_addr):       # exported `NSString * const` -> its text
    return cfs(P(d.q(sym_addr)))

ex = exports.build_cache()
by_name = {}
for path, (base, L) in ex.items():
    for addr, n in L:
        nm = dsc2.rd(exports.NAMES + (n & 0x0fffffff), 120).split(b'\0')[0].decode('latin1')
        by_name.setdefault(nm, []).append((path, addr))
def exp(name, lib=None):
    for p, a in by_name.get('_' + name, []):
        if lib is None or p.endswith(lib):
            return a
CMC = '/CMCapture.framework/CMCapture'

print('== 1. what the cache is')
ok(dsc2.M[:16].startswith(b'dyld_v1'), 'dyld shared cache arm64', 'images: %d' % len(dsc2.images))
ok(any(p.endswith(CMC) for a, p in dsc2.images), 'CMCapture is in the cache')

print('== 2. the Objective-C methods that are replaced / called (found by name, no address compared)')
meth = {n: a for a, n in d.M}
for n in ('+[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor]', '-[BWFigCaptureDeviceVendor initWithDeviceCreateFunction:]',
          '+[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:]',
          '-[BWFigCaptureDeviceVendor copyStreamsWithPositions:deviceTypes:forDevice:error:]', '-[BWFigCaptureDevice copyStreamsWithPortTypes:error:]',
          '-[BWFigCaptureDevice requestControlOfStreams:]', '-[BWFigCaptureDevice relinquishControlOfStreams:]',
          '-[BWFigCaptureDevice copyISPProcessingSessionWithType:error:]', '-[BWFigCaptureStream start]', '-[BWFigCaptureStream stop]',
          '-[BWFigCaptureStream addAttachmentsToSampleBuffer:options:]', '-[BWFigCaptureISPProcessingSession prepareWithHandler:]',
          '+[FigCaptureCameraParameters sensorIDStringFromModuleInfo:]', '-[FigCaptureCameraParameters sensorIDDictionaryForPortType:sensorIDString:]'):
    ok(n in meth, n, hex(meth[n]) if n in meth else '')
# HakiOS compared the unslid addresses of 16.5 (20F66): they are different methods here
for a, what in ((0x1a1ef0d04, 'sharedCaptureDeviceVendor'), (0x1a2115c0c, 'prewarmPortraitFilter')):
    ok(d.sym(a).split('+')[0] != {'sharedCaptureDeviceVendor': '+[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor]',
       'prewarmPortraitFilter': '+[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:]'}[what],
       'HakiOS\'s 16.5 address of %s (%#x) is NOT that method in 16.7 (so an address check would have refused to hook)' % (what, a), d.sym(a))

print('== 3. exported functions the code dlsym()s or links')
for nm, lib in (('FigCaptureDeviceGetClassID', CMC), ('FigCaptureStreamGetClassID', CMC), ('FigCaptureISPProcessingSessionGetClassID', CMC),
                ('CMDerivedObjectCreate', '/CoreMedia'), ('CMBaseObjectGetDerivedStorage', '/CoreMedia'), ('CMClockMakeHostTimeFromSystemUnits', '/CoreMedia'),
                ('CVPixelBufferPoolCreate', '/CoreVideo'), ('CVPixelBufferPoolCreatePixelBuffer', '/CoreVideo'),
                ('objc_getClass', '/libobjc.A.dylib'), ('_dyld_get_shared_cache_range', '/libdyld.dylib'), ('dispatch_apply_f', '/libdispatch.dylib')):
    nm2 = nm
    ok(exp(nm2, lib) is not None, 'export _%s in %s' % (nm2, lib.split('/')[-1]))
want = {'kFigCapturePortType_BackFacingCamera': 'PortTypeBack', 'kFigCapturePortType_BackFacingSuperWideCamera': 'PortTypeBackSuperWide',
        'kFigCapturePortType_FrontFacingCamera': 'PortTypeFront'}
for nm, text in want.items():
    a = exp(nm, '/CMCaptureCore')
    ok(a is not None and const_string(a) == text, '%s (CMCaptureCore) = "%s"' % (nm, text), const_string(a) if a else 'missing')

print('== 4. property / key strings used literally by rwcam_device.c')
KEYS = {  # exported constant -> literal in the code
    'kFigCapturePropertyValue_SensorID': 'sensorID', 'kFigCapturePropertyValue_ModuleLensID': 'ModuleLensID',
    'kFigCapturePropertyValue_ModuleActuatorID': 'ModuleActuatorID', 'kFigCapturePropertyValue_ModuleIntegratorID': 'ModuleIntegratorID',
    'kFigCapturePropertyValue_ModuleFocusID': 'ModuleFocusID', 'kFigCaptureStreamProperty_ModuleInfo': 'ModuleInfo',
    'kFigCaptureStreamProperty_PortType': 'PortType', 'kFigCaptureStreamProperty_PixelSize': 'PixelSize',
    'kFigCaptureStreamProperty_FocalLength': 'FocalLength', 'kFigCaptureStreamProperty_LensFNumber': 'LensFNumber',
    'kFigCaptureStreamProperty_SupportedFormatsArray': 'SupportedFormatsArray', 'kFigCaptureStreamProperty_FormatIndex': 'FormatIndex',
    'kFigCaptureStreamProperty_VideoOutputConfigurations': 'VideoOutputConfigurations', 'kFigCaptureStreamProperty_VideoOutputsEnabled': 'VideoOutputsEnabled',
    'kFigCaptureStreamProperty_VideoOutputHandlers': 'VideoOutputHandlers', 'kFigCaptureStreamProperty_VideoOutputsColorInfo': 'VideoOutputsColorInfo',
    'kFigCaptureStreamProperty_VideoOutputGeometricDistortionCorrectedRects': 'GeometricDistortionCorrectedRects',
    'kFigCaptureStreamProperty_MaxIntegrationTime': 'MaxIntegrationTime', 'kFigCaptureStreamProperty_TorchLevel': 'TorchLevel',
    'kFigCaptureStreamVideoOutputHandlerKey_PixelBufferHandler': 'PixelBufferHandler',
    'kFigCaptureStreamVideoOutputConfigurationKey_BufferPool': 'BufferPool', 'kFigCaptureStreamVideoOutputConfigurationKey_Width': 'Width',
    'kFigCaptureStreamVideoOutputConfigurationKey_Height': 'Height', 'kFigCaptureStreamVideoOutputConfigurationKey_PixelFormat': 'PixelFormat',
    'kFigCaptureStreamVideoOutputID_PrimaryScaler': 'PrimaryScaler', 'kFigCaptureStreamVideoOutputID_SecondaryScaler': 'SecondaryScaler',
    'kFigCaptureStreamVideoOutputID_StillImage': 'StillImage', 'kFigCaptureStreamVideoFormatKey_SupportedOutputs': 'SupportedOutputs',
    'kFigCaptureStreamVideoFormatKey_SensorWidth': 'SensorWidth', 'kFigCaptureStreamVideoFormatKey_SensorHeight': 'SensorHeight',
    'kFigCaptureStreamVideoFormatKey_ISOBase': 'ISOBase', 'kFigCaptureStreamVideoFormatKey_DefaultAEMaxGain': 'DefaultAEMaxGain',
    'kFigCaptureStreamVideoOutputKey_SupportedPixelFormats': 'SupportedPixelFormats', 'kFigCaptureStreamVideoOutputKey_NativeWidth': 'NativeWidth',
    'kFigCaptureStreamVideoOutputKey_NativeHeight': 'NativeHeight', 'kFigCaptureStreamVideoOutputKey_MaxDownscaleFactor': 'MaxDownscaleFactor',
    'kFigCaptureStreamVideoOutputKey_OutputWidthAlignmentRequirement': 'OutputWidthAlignmentRequirement',
    'kFigCaptureDeviceProperty_StreamArray': 'StreamArray', 'kFigCaptureDeviceProperty_Clock': 'Clock',
    'kFigCaptureDeviceProperty_SupportedSynchronizedStreamsGroups': 'SupportedSynchronizedStreamsGroups',
    'kFigCaptureProperty_SupportedPropertiesDictionary': 'SupportedPropertiesDictionary',
}
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'rwcam_device.c')).read()
for nm, lit in KEYS.items():
    a = exp(nm)
    got = const_string(a) if a else None
    used = ('"%s"' % lit) in src
    ok(got == lit and used, '%s = "%s"%s' % (nm, lit, '' if used else '  (literal not used by rwcam_device.c)'), '' if got == lit else 'cache says %r' % got)

print('== 5. derived-object tables as CMCapture calls them (disassembly)')
def fn_text(name):
    return d.fn(name)
def has(name, *pats):
    t = fn_text(name)
    return all(re.search(p, t) for p in pats)
ok(has('-[BWFigCaptureStream start]', r'ldr\tx8, \[x0, #0x10\]\n[^\n]*ldr\tx8, \[x8, #0x8\]'), 'stream derived table +0x08 = start(stream)')
ok(has('-[BWFigCaptureStream stop]', r'ldr\tx8, \[x0, #0x10\]\n[^\n]*ldr\tx8, \[x8, #0x10\]'), 'stream derived table +0x10 = stop(stream)')
ok(has('-[BWFigCaptureStream addAttachmentsToSampleBuffer:options:]', r'cmp\tx9, #0x2', r'ldr\tx8, \[x8, #0x18\]'), 'stream derived table +0x18 = addAttachments, needs version >= 2')
ok(has('-[BWFigCaptureDevice requestControlOfStreams:]', r'cmp\tx9, #0x2', r'ldr\tx8, \[x8, #0x10\]', r'mov\tx2, #0x0'), 'device derived table +0x10 = requestStreams(device, streams, NULL), version >= 2')
ok(has('-[BWFigCaptureDevice relinquishControlOfStreams:]', r'cmp\tx9, #0x2', r'ldr\tx8, \[x8, #0x18\]'), 'device derived table +0x18 = relinquishStreams, version >= 2')
ok(has('-[BWFigCaptureDevice copyISPProcessingSessionWithType:error:]', r'ldr\tx8, \[x0, #0x10\]\n[^\n]*ldr\tx8, \[x8, #0x8\]', r'mov\tx3, sp'), 'device derived table +0x08 = processingSessionCreate(device, allocator, config, &out)')
ok(has('-[BWFigCaptureISPProcessingSession prepareWithHandler:]', r'ldr\tx8, \[x8, #0x8\]'), 'session derived table +0x08 = prepare(session, handler)')
ok(has('-[BWFigCaptureISPProcessingSession processBuffer:refCon:outputDescriptors:numOutputDescriptors:parameters:]', r'ldr\tx8, \[x8, #0x10\]'), 'session derived table +0x10 = process')
ok(has('-[BWFigCaptureISPProcessingSession flush]', r'ldr\tx8, \[x8, #0x18\]'), 'session derived table +0x18 = flush')
ok(has('-[BWFigCaptureStream initWithFigCaptureStream:]', r'CMDerivedObjectCreate'), 'BWFigCaptureStream wraps the stream with CMDerivedObjectCreate')
dev_create = [a for a, n in zip(allsyms.K, allsyms.N) if n == '-[BWFigCaptureDeviceVendor _createDevice:clientPID:]'][0]
t = d.fn(dev_create)
ok(re.search(r'ldr\tx21, \[x26, #0x8\]', t) and re.search(r'ldr\tx0, \[x8\]\n[^\n]*add\tx2, x19, #0x88\n[^\n]*mov\tx1, #0x0[^\n]*\n[^\n]*blr\tx21', t),
   'vendor ivar +8 = create function, called fn(kCFAllocatorDefault, NULL, &device)')
ok(has('+[BWFigCaptureDeviceVendor sharedCaptureDeviceVendor]', r'msg\$initWithDeviceCreateFunction:'), '+sharedCaptureDeviceVendor makes the vendor with -initWithDeviceCreateFunction:')

print('== 6. (position, deviceType) -> port type, the AVCaptureSession.plist deviceType numbers')
fnaddr = 0x1a1f2efec
def port_for(tab, base, idx):
    b = dsc2.rd(tab, 12)[idx]
    t = base + b * 4
    w = struct.unpack('<2I', dsc2.rd(t, 8))
    pg = d._adrp(w[0], t); off = ((w[1] >> 10) & 0xfff) << 3
    v = P(d.q(pg + off))
    return const_string(v) if v else None
ok(port_for(0x1a23539d4, 0x1a1f2f024, 2 - 2) == 'PortTypeBack', 'back (position 1) deviceType 2 -> PortTypeBack (wide)')
ok(port_for(0x1a23539d4, 0x1a1f2f024, 7 - 2) == 'PortTypeBackSuperWide', 'back (position 1) deviceType 7 -> PortTypeBackSuperWide (ultra-wide)')
ok(port_for(0x1a23539ca, 0x1a1f2f058, 2 - 2) == 'PortTypeFront', 'front (position 2) deviceType 2 -> PortTypeFront')

print('== 7. sensor IDs, module info')
t = d.fn('+[FigCaptureCameraParameters sensorIDStringFromModuleInfo:]')
ok('0x%04x' in t, 'sensorIDStringFromModuleInfo: formats the "sensorID" as "0x%04x"')
ok('_GetFocalLengthParametersFromArrayUsingModuleInfo' in [n for k, n in zip(allsyms.K, allsyms.N)] and
   all(('kFigCapturePropertyValue_Module' + x + 'ID') in d.fn(0x1a1f9ff44) for x in ('Lens', 'Actuator', 'Integrator')),
   '_GetFocalLengthParametersFromArrayUsingModuleInfo reads ModuleLensID, ModuleActuatorID and ModuleIntegratorID (all three required)')
t = d.fn('-[FigCaptureCameraParameters sensorIDDictionaryForPortType:sensorIDString:]')
ok('DefaultSensorIDs' in t and 'msg$cameraTuningParameters' in t, 'tuning data is looked up as cameraTuningParameters[portType][sensorID] (CameraSetup.plist TuningParameters)')

print('== 8. model-specific directory')
t = d.fn(P(d.q(0x1e2067d10 + 16)))
ok('sysctlbyname' in t and '"AP"' in t, 'FigCaptureGetModelSpecificName = sysctl hw.target without "AP"/"DEV" -> "D22" (this port: hw.target D22AP)')

print('== 9. sources come from AVCaptureSession.plist')
t = d.fn(0x1a20b5a48)
ok('AVCaptureSession.plist' in t and 'copyDeviceForClient' in t, '_csu_createSourceInfoDictionariesFromAVCaptureSessionPlist registers a client, copies the DEVICE (our create function) and reads AVCaptureSession.plist')
t = d.fn(0x1a1faebd8)
ok('"vide"' in t and 'sourceInfoDictionaries' in t, 'FigCaptureSourceCopySourcesForClientAuditToken lists the plist\'s "vide" sources')
print('\n%d check(s) failed' % fails if fails else '\nall checks passed')
sys.exit(1 if fails else 0)
