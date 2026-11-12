#!/usr/bin/env python3
"""
Derives the two per-model camera plists of CMCapture for the Nord 2's three cameras from the device's OWN copies of the iPhone X ones
(/System/Library/PrivateFrameworks/CMCapture.framework/D22/{AVCaptureSession,CameraSetup}.plist; the hw.target of this port is D22AP, so
FigCaptureGetModelSpecificName() = "D22"). Nothing of Apple's is stored in git: run this on the files fetched from the root image.

AVCaptureSession.plist  AVCaptureDevices (these entries ARE the AVCaptureDevices of the device; +[FigCaptureSourceBackings] builds its sources from them
                        and from the capture device's streams):
                          kept     :0 back wide (deviceType 2), :1 front (position 2), the audio entry;
                          added    :5 back ULTRA WIDE: deviceType 7 (-> kFigCapturePortType_BackFacingSuperWideCamera, checked in CMCapture 16.7's
                                   (position, deviceType) -> port type function at 0x1a1f2efec), a copy of the wide entry without autofocus;
                          removed  :2 telephoto, :3 dual (virtual), :4 front infrared, :99 TrueDepth: this phone has no such stream, and a source without
                                   one is a camera Camera.app would offer and fail on.
                          added    :6 back DUAL WIDE (unless --no-dual): deviceType 8 (AVCaptureDeviceTypeBuiltInDualWideCamera in AVFCapture 16.7's
                                   -[AVCaptureFigVideoDevice deviceType]), a VIRTUAL device as the stock file's :3 "Back Dual Camera" (deviceType 4,
                                   wide + telephoto) is one: no formats of its own (CMCapture takes those its two devices share) but
                                   synchronizedStreamUniqueIDs (:0 wide, :5 ultra-wide), synchronizedStreamsSlaveFrameRates and
                                   synchronizedStreamsSwitchOverParameters with the stock entry's numbers, and none of its depth data. CMCapture
                                   (-[FigCaptureSourceStreamsContainer _populateStreamsFromDeviceVendorForPosition:...]) makes the source only when
                                   the capture device gives a synchronized streams group for those two streams, which RedwoodAudio.driver does when
                                   the kernel has /dev/redwood-camera2 (both sensors at once); otherwise this entry gives no source and the three
                                   others are as before. The zoom at which the wide camera takes over (2.0: Camera.app's "1x") is not in this file:
                                   it is the group's BaseZoomFactorsByPortType (src/rwcam_device.c).
                          NOT in any entry: the keys CMCapture turns into what an iPhone 11 can do and this capture device cannot, and which
                                   Camera.app -- told it runs on an iPhone 11 (N104) so that it offers the ultra-wide lens -- asks the photo output
                                   for before it switches them on: DigitalFlashSupported (night mode), DeepFusionSupported,
                                   DeferredPhotoProcessingSupported, ResponsiveShutterSupported, SpatialOverCaptureSupported,
                                   ContentAware...Supported, LearnedNRSupported. The stock iPhone X file has none of them and this tool adds none
                                   (checked below: a file that had one would not be written).
CameraSetup.plist       TuningParameters.PortTypeBackSuperWide.0x0355 (+ DefaultSensorIDs) as a copy of PortTypeBack.0x0343: the sensor ID the
                        ultra-wide stream reports in ModuleInfo (rwcam_device.c). Wide keeps 0x0343 and front 0x0354 (the iPhone X's: their tuning data is
                        what the capture graph asks for; ours is not available).
usage: make_camera_plists.py [--no-dual] <D22 dir with the two stock plists> <out dir>
"""
import copy, os, plistlib, sys

if len(sys.argv) == 4 and sys.argv[1] == '--still-4096':
    # usage: make_camera_plists.py --still-4096 <an AVCaptureSession.plist made by this tool> <out file>
    # The back cameras' photo format becomes 4096x3072 (the wide sensor's own frame) instead of 4032x3024: the `Photo' stream format's
    # SensorWidth/SensorHeight and the `12MP' device format's Width/Height of the wide (:0) and ultra-wide (:5) entries. Run user6: GNR's
    # pyramid of a 4032x3024 one-picture photograph has levels at offsets the Metal driver refuses; 4096x3072's are all multiples of 64.
    # RedwoodAudio.driver offers both sizes (src/rwcam_device.c, the back cameras' formats 0 and 2): either plist works with it.
    with open(sys.argv[2], 'rb') as f:
        av = plistlib.load(f)
    done = 0
    for d in av['AVCaptureDevices']:
        ms = d.get('MultiStream', {})
        if d.get('position') != 1 or 'FigCaptureStreamFormats' not in ms:
            continue
        photo, mp = ms['FigCaptureStreamFormats'].get('Photo'), ms['AVCaptureDeviceFormats'].get('12MP')
        if photo and mp and (photo.get('SensorWidth'), photo.get('SensorHeight')) == (4032, 3024) and (mp.get('Width'), mp.get('Height')) == (4032, 3024):
            photo['SensorWidth'], photo['SensorHeight'] = 4096, 3072
            mp['Width'], mp['Height'] = 4096, 3072
            done += 1
    if done != 2:
        sys.exit('expected the wide and the ultra-wide entries with a 4032x3024 Photo/12MP format, found %d' % done)
    os.makedirs(os.path.dirname(sys.argv[3]) or '.', exist_ok=True)
    with open(sys.argv[3], 'wb') as f:
        plistlib.dump(av, f, fmt=plistlib.FMT_BINARY)
    print('written', sys.argv[3], '(back photo format 4096x3072)')
    sys.exit(0)

args = [a for a in sys.argv[1:] if a != '--no-dual']
dual_wide = '--no-dual' not in sys.argv[1:]
src, out = args[0], args[1]
os.makedirs(out, exist_ok=True)

with open(os.path.join(src, 'AVCaptureSession.plist'), 'rb') as f:
    av = plistlib.load(f)
by_id = {d.get('uniqueID'): d for d in av['AVCaptureDevices']}
WIDE = 'com.apple.avfoundation.avcapturedevice.built-in_video:0'
FRONT = 'com.apple.avfoundation.avcapturedevice.built-in_video:1'
AUDIO = 'com.apple.avfoundation.avcapturedevice.built-in_audio:0'
ULTRA = 'com.apple.avfoundation.avcapturedevice.built-in_video:5'
DUAL = 'com.apple.avfoundation.avcapturedevice.built-in_video:6'
STOCK_DUAL = 'com.apple.avfoundation.avcapturedevice.built-in_video:3'

ultra = copy.deepcopy(by_id[WIDE])
ultra.update({'uniqueID': ULTRA, 'modelID': ULTRA, 'localizedName': 'Back Ultra Wide Camera', 'deviceType': 7, 'position': 1,
              'hasAutoFocus': False, 'minimumFocusDistance': -1, 'autoFocusPositionSensorMode': 0})
for k in ('smoothAutoFocusSupported', 'ContinuousAutoFocusWindowWidthOverride', 'ContinuousAutoFocusWindowHeightOverride',
          'FocusPointOfInterestWindowSize', 'FocusCenterWindowSize', 'stillImageCaptureNowAfterAutofocusTimeout',
          'stillImagePhaseDetectionAutofocusTimeout', 'stillImageContrastBasedAutofocusTimeout', 'sphereModeStillActivePreviewSupported'):
    ultra.pop(k, None)
for fmt in ultra['MultiStream']['FigCaptureStreamFormats'].values():
    fmt.pop('AutoFocusSystem', None)

# the virtual dual-wide device, shaped as the stock virtual dual (wide + telephoto) one
stock_dual = by_id[STOCK_DUAL]
assert stock_dual['deviceType'] == 4 and stock_dual['synchronizedStreamUniqueIDs'][0] == WIDE
dual = {
    'uniqueID': DUAL, 'modelID': DUAL, 'localizedName': 'Back Dual Wide Camera', 'deviceType': 8, 'position': 1, 'mediaType': 'vide',
    'synchronizedStreamUniqueIDs': [WIDE, ULTRA],
    # the slave's frames a second, as the stock dual camera has them; the range of zoom with the higher rate around this pair's switch-over (2.0)
    'synchronizedStreamsSlaveFrameRates': dict(copy.deepcopy(stock_dual['synchronizedStreamsSlaveFrameRates']),
                                               ElevatedSlaveFrameRateZoomFactorRange=[1.5, 2.5]),
    # (the stock numbers that are not about the telephoto lens)
    'synchronizedStreamsSwitchOverParameters': {k: v for k, v in stock_dual['synchronizedStreamsSwitchOverParameters'].items() if 'ele' not in k},
    'manualFocusSupported': False, 'manualExposureSupported': False, 'manualWhiteBalanceSupported': False,
    'MultiStream': {'AVCaptureDeviceFormats': {'Common': {}}},
}
# order: wide, ultra wide, dual wide, front, audio (the stock order is wide, tele, dual, front, ...)
av['AVCaptureDevices'] = [by_id[WIDE], ultra] + ([dual] if dual_wide else []) + [by_id[FRONT], by_id[AUDIO]]
NOT_HERE = ('DigitalFlashSupported', 'DeepFusionSupported', 'DeferredPhotoProcessingSupported', 'ResponsiveShutterSupported', 'SpatialOverCaptureSupported',
            'ContentAwareEnhancementSupported', 'ContentAwareAWBSupported', 'LearnedNRSupported')
def keys_of(o):
    if isinstance(o, dict):
        for k, v in o.items():
            yield k
            yield from keys_of(v)
    elif isinstance(o, list):
        for v in o:
            yield from keys_of(v)
found = sorted(set(keys_of(av)) & set(NOT_HERE))
assert not found, 'keys of what this capture device cannot do: %r' % found
with open(os.path.join(out, 'AVCaptureSession.plist'), 'wb') as f:
    plistlib.dump(av, f, fmt=plistlib.FMT_BINARY)

with open(os.path.join(src, 'CameraSetup.plist'), 'rb') as f:
    cs = plistlib.load(f)
tp = cs['TuningParameters']
tp['PortTypeBackSuperWide'] = {'0x0355': copy.deepcopy(tp['PortTypeBack']['0x0343'])}
tp['DefaultSensorIDs']['PortTypeBackSuperWide'] = '0x0355'
with open(os.path.join(out, 'CameraSetup.plist'), 'wb') as f:
    plistlib.dump(cs, f, fmt=plistlib.FMT_BINARY)
print('devices:', [d['uniqueID'].rsplit(':', 1)[1] + ' ' + d.get('localizedName', '') for d in av['AVCaptureDevices']])
print('port types with tuning:', sorted(k for k in tp if k.startswith('PortType')), 'default ids:', tp['DefaultSensorIDs'])
