// SPDX-License-Identifier: BSD-2-Clause
// NORD2 BB (0277): iOS 16.7's CommCenter/AppleBasebandManager (ABM) and BBUpdater see a Qualcomm "Mav17" PCIe baseband (the iPhone X
// D22AP one: /product/baseband-chipset = mav17) and talk QMI to it; there is no Qualcomm modem: the answers come from this file, and
// where it can from the MediaTek MT6893 modem that bsd/kern/sweet_amfi.c runs (its in-kernel AT client, nord2_bb_kpi.h).
//
// The IOKit service layer is ported from QcomBaseband baseband/coachz_baseband.cpp (HoshinoYumeka @chiba233, Trogdor-ipadOS 58b5bc8b,
// BSD-2-Clause, see qcombb_LICENSE.txt): AppleBaseband, AppleBasebandPCIControl and one AppleBasebandPCIInterface per channel
// (ABPInterfaceProtocol matching), the PDP manager class chain and AppleIPAppender, with the user clients and their selectors, the
// per-channel queues and asynchronous reads, the SAHARA device end (qcombb_sahara.h), the board methods (qcombb_control.h) and the
// BSP answers (PRI/GRI file segments + FileTransferStatus 0xa001, chip info 0xb001, SetConfig 0xe00d, activation 0xb00c with its
// indication). The QRTR modem, the IPA data path, pdp_ip0..3 and the eSIM bridge are not ported: pdp_ip0 already belongs to the
// DPMAIF path of sweet_amfi.c (n2net), and the QMUX services are virtual (nord2_bb_qmux.h).
//
// iOS 16 checks done offline (dyld cache of 20H392, D22AP device tree, Mav17 bbfw): libIOABP still matches AppleBasebandPCIInterface +
// ABPInterfaceProtocol and AppleBasebandPCIControl; libPCITransport has the same channel table (pbl ... boot_logger) and its
// PDPListener matches AppleBasebandPCIPDPManagerBase; ApplePDPHelper opens AppleIPAppender; libTelephonyBaseband opens the service
// AppleBaseband and reads the "config" property of a service named "baseband" (D22AP: /device-tree/baseband, compatible
// baseband,n41, published here as a node); the Mav17 bbfw Info.plist ChipId is 0x0003B0E1.
//
// Boot-arg n2bb (default: bit 0 only, the kernel command line has no room left; n2bb=0 turns everything off): bit 0 on; bit 1 client ids only for announced and private services
// (default: any service); bit 2 unhandled requests answer NOT_SUPPORTED everywhere (default: INVALID_QMI_CMD on standard services, as
// a Qualcomm firmware does for messages it does not know, NOT_SUPPORTED on Apple's private ones); bit 3 log the first bytes of every
// request (default: only services without identities or credentials); bit 4 no answers from the AT state (pure instrumentation);
// bit 5 no unsolicited indications; bit 6 unknown AppleBaseband selectors answer unsupported (default: success with zeros); bit 7 no
// automatic acceptance of indication registrations; bit 8 also attach the "baseband" node to IODeviceTree:/; bit 9 no "baseband" node;
// bit 10 publish the APFS preboot volume (AppleAPFSVolume below; off by default since 0277e, where bit 10 meant "no volume"); bit 11
// no trace of CommCenter/abm-helper (0277c, bb_trace_* below); bit 12 CommCenter sees a stale /tmp/ct.shutdown (0277e, bb_path_gate);
// bit 13 no reset-detect line: DMS Set Operating Mode "reset" accepted without resetting, no reset/alive messages (0277e behaviour);
// bit 14 PDS answers NOT_SUPPORTED again (0277g/h behaviour, see 0277i); bit 15 no RFS sync handshake on soft resets (0277h behaviour);
// bit 16 no GID1/GID2 in the activation indication (0277i behaviour, see 0277j); bit 17 no early IODeviceTree "baseband" node (0277j
// behaviour: MobileGestalt says HasBaseband = false, see 0277k); bit 18 cellular data stays the kernel's own (0277k behaviour: pdp_ip0
// with its address and default route, WDS calls unanswered); bit 19 no NAS current PLMN name indication (0277k behaviour); bit 20 no
// boot hold: SAHARA's DONE_RESP at once, whatever the modem's SIM (0277l behaviour); bit 21 no PDN bring-up for Start Network (0277l
// behaviour: the call waits for the boot script's PDN); bit 22 no activation hold: the activation indication goes with the answer to
// ActivationRegister (0277m behaviour); bit 23 no ActivationMode (TLV 0x15) in the activation indication (0277n behaviour: kEnabled);
// bit 24 no WDS profile answers (0277o behaviour: Create Profile 0x0027 unsupported, CommCenter retried it ~170 times a second);
// bit 25 no SMS (0277q behaviour: WMS settings accepted without action, nothing sent or received); bit 26 no voice calls (0277q
// behaviour: VOICE unanswered); bit 27 the modem acknowledges MT SMS itself (AT+CSMS=0, no AT+CNMA; CommCenter's Send Ack only
// answered); bit 28 no MediaTek call progress URCs (AT+ECPI not enabled: call states from +CLCC polls on RING/+CLIP/NO CARRIER only).
//
// 0277t, from q10cj (0277r/s on hardware: MO SMS sent and delivered, MT calls ring and answer; iOS unified log of the same boot):
//  - MT SMS: CommCenter took the Event Report (TxID, "Ack Ind : 0", "Format : GW Point to Point") and then logged "Decoding 3GPP
//    SMS failed" / "Failed to decode message" every time, so no Send Ack came. Transfer-route MT messages carry the TPDU only (no
//    SMSC part; Qualcomm's layout, ModemManager parses them without one); now sent so (nbm_sms_tpdu). Raw Read keeps the SMSC part.
//  - NAS during CS fallback (+CEREG/+CREG AcT 6, UMTS): System Info had only "LTE no service", so iOS showed "No service" for the whole
//    call. Now per RAT: GSM/WCDMA/LTE service status (0x12/0x13/0x14) and the serving RAT's system info (0x17 GSM, 0x18 WCDMA, 0x19
//    LTE), Signal Info for WCDMA (0x13 RSSI + Ec/Io, 0x19 RSCP from +CESQ) and GSM (0x12), serving system data capabilities for
//    GPRS/EDGE/WCDMA/HSPA, DSD system status RAT 1 (WCDMA) / 2 (GSM) with their SO masks.
//  - MO calls: MobilePhone made its dial request and opened InCallService with the tel: URL, but no VOICE 0x0020 and no CommCenter
//    "REST Dial" ever came (InCallService's own log lines were not persisted). Most likely its call capabilities (built from the LTE
//    registration) said no voice: System Info now carries LTE Voice Support 0x21 = 1 while CS-registered (combined attach, CS fallback),
//    and the Get System Info answer IMS Voice Support 0x29 = 0 and LTE Voice Domain 0x2A = 3 (3GPP CS fallback).
//  - AUDIO 0x002a (end-of-call tone) accepted.
//
// 0277r: SMS and voice call signalling, from q10cf2 (cellular data working: WMS 0x0001/0x0032/0x0038-3a/0x003c/0x003d/0x0043/0x0047/
// 0x005c and VOICE 0x0003/0x0044 seen, nothing sent or received). CommCenter's QMISMSCommandDriver and EurekaCallCommandDriver
// (libCommCenterMCommandDrivers; wms::/vs:: in the dyld cache) are standard QMI WMS/VOICE clients. Everything goes through the AT
// client (bb_cs_* below, sweet_amfi.c nord2_at_query/nord2_at_query_pdu), set up once the SIM is ready: AT+CMGF=0, AT+CSMS=1,
// AT+CNMI=1,2,0,1,0 (fallbacks 2,2,0,1,0 / 1,2,0,0,0 / 2,1,0,0,0), AT+CSCA?, AT+CLIP=1, AT+CRC=1, AT+CCWA=1, AT+ECPI=4294967295.
//  - MO SMS: WMS Raw Send (0x01 format 6 GW PP; the raw data with or without the SMSC part, nbm_sms_smsc_part) -> AT+CMGS=<TPDU
//    length>, "> ", PDU ("00" + TPDU when CommCenter gave no SMSC) and Ctrl-Z; the answer 0x01 message reference, or WMS delivery
//    failure with 0x12 RP/TP cause and 0x13 temporary/permanent from +CMS ERROR. One at a time, 4 waiting.
//  - MT SMS: +CMT (and +CDS status reports) -> WMS Event Report indication to the clients that set Set Event Report 0x10: transfer
//    route TLV 0x11 {ack indicator, transaction id, format 6, raw data with SMSC} + 0x12 mode GW; CommCenter's Send Ack 0x0037 ->
//    AT+CNMA=1,2,"0000" (MediaTek's inline form; else AT+CNMA), failure -> AT+CNMA=2,3,"00<TP cause>00"; no Send Ack in 4 s: the
//    bridge acks. A route of "store and notify" (Set Routes) keeps the message in the bridge's 8-slot store (indication 0x10
//    storage/index; Raw Read/Write, Modify Tag, Delete, List Messages work on it); with no listening client the message is held and
//    delivered at the next Set Event Report. +CMTI (a modem that stores anyway): AT+CMGR, delivered, AT+CMGD.
//  - Also: Get/Set SMSC Address (AT+CSCA), Get/Set Routes, Indication Register (+ Get), Get Service Ready Status (registered as
//    asked), Get/Set Broadcast Config (stored, no cell broadcast delivery: +CBM is dropped and counted), Get Message Protocol, Get Store
//    Max Size, Get Transport Layer Info (no IMS), Get Transport NW Reg, Set Memory Status/Primary Client/Message Waiting accepted.
//  - VOICE: Dial Call -> ATD<number>[I|i]; (CLIR 0x11), answered with the call id (0x10) from the next +CLCC; Answer Call -> ATA
//    (AT+CHLD=2 for a waiting call); End Call -> AT+CHLD=1<id> (ATH for a lone incoming call or as fallback, AT+CHLD=0 for a waiting
//    one); Manage Calls -> AT+CHLD=0/1/2/2x/3/4, ATH, 1x; Start/Stop Continuous DTMF -> AT+EVTS=0,"d" / AT+EVTS=1 (MediaTek; else
//    AT+VTS=d); Burst DTMF -> AT+VTS per digit; Get All Call Info; Get Config (auto answer, TTY, AMR); Set Config accepted. Call ids are
//    the modem's +CLCC indexes. RING/+CRING/+CLIP/+CCWA/+ECPI/NO CARRIER/BUSY/NO ANSWER trigger an AT+CLCC poll (1 s while calls
//    exist); every change sends the All Call Status indication 0x002E (0x01 call info, 0x10 remote numbers, 0x12 alerting type local
//    ringback, 0x14 end reasons from +ECPI 133's CC cause), ended calls once with state END. MediaTek's +EAIC (incoming call approval)
//    is approved (AT+EAIC=0,<id>,<seq>). Logs: "NORD2 BB: SMS ..." / "NORD2 BB: VOICE ...", numbers as length + last 2 digits only.
//  - Not done: call audio. The MediaTek modem says when its speech path is up (+ESPEECH, logged); a call has audio only when the AP
//    turns the modem's speech on and routes the modem's PCM through its audio front end (AFE/MRG-PCM to the codec, mic and earpiece),
//    as Android's MTK speech driver (SpeechDriverLAD: SPEECH_ON etc. over the CCCI audio channel) and audio HAL do. VoLTE (IMS on the
//    NR15 modem) is not provisioned by this bridge: calls go through CS fallback while the modem is not IMS registered.
//
// 0277p, from q10cd (0277o): the activation went out with kBypass, no WDS Start Network came: CommCenter sent WDS 0x0027 Create
// Profile (TLV 0x01 type 0 3GPP, 0x8f persistent) 14583 times in 88 s, every answer INVALID_QMI_CMD. bb_wds_profile below keeps a
// table of 16 profiles: Create (0x01 type+index), Modify, Delete, Get Profile List (0x2a), Get Profile Settings (0x2b), Get Default
// Settings (0x2c), Get/Set Default Profile Number (0x49/0x4a), APN (0x14) and PDP type (0x11) stored; profile 1 is the kernel PDN's APN.
// 0277q, from q10ce: two creates, then WDS 0x00c7 (TLV 0x01 profile id, 0x14 APN, 0x11 PDP type) 6867 times: stored, success.
//
// 0277o, from q10cc (0277n, unified log boot 57A6DBA0): the activation hold did what it should ("released after 1787 ms"), yet all data
// connections still had context id -2 ("activeAndIdByConnection: id=-2", "context is not assigned"), switched for every connection at
// once by the first postponement status change. CommCenter's DataConnection asks its service controller (DataServiceController at +0x40)
// before its own PDP lookup: ticket-less (/cc/props/is_postponement_ticket_available false, stored at +0x322) and activation enabled
// (fActivationEnabled at +0x13a, set in handlePostponementStatusChanged_sync from ActivationModel: fActivationMode == kEnabled) means -2.
// The ticket is the activation record's "ActivationTicket", kept as kPostponementTicket in com.apple.commcenter.device_specific_nobackup
// ("Ticket present: false", "No baseband activation ticket found"); fActivationMode comes from TLV 0x15 of the activation indication,
// which the bridge never sent. It now sends kBypass (qcombb_data.h cbd_activation_mode).
//
// 0277n, from q10cb (0277m, unified log boot E6C6077C, again only 18.9-26.8 s persisted): the boot hold worked ("sahara DONE_RESP
// released after 9540 ms (SIM ready, registered)", glued to the end of the cut "modem state #9" line: the kernel's printf keeps 255
// characters and drops the rest with the newline, so the long report and modem state lines are now split), CommCenter's first card
// status was ready, but CommCenter's own SIM state still goes unknown -> detected -> ready while it reads EF ICCID/GID1/GID2/IMSI, and
// the activation indication sent with the ActivationRegister answer fell in between: "Preferring legacy OTASP provisioned state",
// "No SIM info for (kOne)", baseband service provisioned kProvisioned -> kUnknown, DATA kProvisioned -> kNotProvisioned, contexts
// deactivated, "context is not assigned" again. The indication now waits for CommCenter's EF IMSI read (bb_act_*). Also: the AT
// errors of q10cb (6) were +CPIN?/+CLCK of the hold's snapshots before AT+CFUN=1 (no longer counted); its "DNS 0.0.0.0" was the
// "PDN up" line printed before the boot script's AT+CGCONTRDP (10 s later: "cid 1 DNS 151.5.216.228 151.5.216.28").
//
// 0277m, from q10ca (0277l; the q10ca boot is 5545F135 in the unified log, Persist 0x90, of which only 18.9-24.5 s survived the 4-minute
// reset; 119E90 is the 0277k daily boot before it): the operator name shows; CommCenter's DSD status, data bearer (kLTE), data mode,
// roaming (home) and the WDS QMI contexts are all fine, but its Internet connection is "kDataNotSupported (context is not assigned)" and
// never sends Start Network. The reason changed from "ActivationStatus failed in DataSettings" to "context is not assigned" at the very
// moment DATA's basebandProvisionedState went kProvisioned -> kNotProvisioned ("pp.mdl No SIM info for (kOne)" right after the BB
// activation), with every context deactivated "ServiceNotProvisioned", and it stayed so after the provisioning came back (both boots).
// Cause: CommCenter's QMI clients start ~1.5 s before the MediaTek modem is ready and ~9 s before the boot script powers the card
// (AT+CFUN=1), so it sees the SIM unknown, dead, then ready. Fix: the virtual boot is held at SAHARA's DONE until the modem's SIM is
// settled and registered (bb_hold_*, at most 25 s), the boot script turns the radio function on first (sweet_amfi.c), and a card the
// modem has not powered yet is shown as "detected", not as an error (nbm_sim_shown). For the call itself: Start Network now brings the
// PDN up at once (AT+CGDCONT/CGACT/CGPADDR/CGCONTRDP, bb_pdn_bring_up; the boot script's came 113 s after the modem start) and the
// answer waits up to 25 s, and up to 3 s more for the DNS.
//
// 0277b, after the first hardware run (ABM made the 5 AppleBaseband calls 15, 32, 23, 22(1), 3 and then nothing): iOS 16's ABM boots a
// Mav17 through BootControllerLegacy, whose prepare() stats capabilities::bb::radio::getPersonalizedFirmwarePath() before it powers the
// modem (selector 1) and runs BBUpdater "performBootup". That path is MSUDataAccessor's lookupPathForPersonalizedData(1): the mount point
// of the one APFS volume with role Preboot (libpartition2 +[LPMedia allMedia]), plus the boot-manifest-hash in hex, plus
// /usr/local/standalone/firmware/Baseband, plus /Mav17. This kernel has no APFS: the lookup fails, the path is "", prepare() fails with
// "Baseband Firmware Path Not Found" and nothing more happens. The bridge therefore publishes the preboot volume MSU looks for (bit 10).
//
// 0277e, after the trace runs (q10br): that was MSUDataAccessorRestore, the class +sharedDataAccessor picks only when /mnt1 exists (the
// restore ramdisk). On the installed OS it is MSUDataAccessor, whose -copyMountPointForVolumeType: returns the constant /private/preboot
// (type 0; /private/var/hardware for type 1) without looking at any volume. CommCenter never read the volume's Content or RoleValue,
// but it did read /chosen/asmb lp-nsih and /chosen boot-manifest-hash right after GetReset (the personalized root), so ABM's firmware
// folder is /private/preboot/<96 '0'>/usr/local/standalone/firmware/Baseband/Mav17 (getFirmwarePathSuffix: radio Mav17), not
// /<96 '0'>/...: a rootfs change (HOW-TO-TEST.md). The preboot volume is not needed and is now opt-in (bit 10). The same trace showed
// CommCenter taking every boot for a relaunch after a crash (a stale /tmp/ct.shutdown, see bb_path_gate).
//
// 0277f, after q10bs (0277e booted the virtual Mav17: BHI, SAHARA, QMI): ABM reset the baseband every 10-12 s. Each boot CommCenter
// pushed its carrier files (the bundle's DER PRI once, global_setting_A.gri every time), set the operating mode to 4 (RESET) to apply
// them and waited for the modem to go down: libBasebandManager's ResetDetection waits for AppleBaseband's interest messages
// 0xe3ff8000 (reset detected) and 0xe3ff8001 (alive). Nothing came, "Soft reset failed; timeout waiting for reset detection", ABM read
// bootstage and power-cycled the modem (selector 0) ~10 s later, and the push never completed, so it was repeated on the next boot. The
// bridge now resets like a modem (bb_soft_reset): the answer, then reset detected (GetReset 1), then alive, and BHI/SAHARA start over.
// It also answers more of DMS/NAS/UIM/WDS/WMS from the AT state (band capabilities, time, system selection preference, PLMN name, file
// attributes, SMS readiness), accepts the set/bind/registration messages ABM sends at every boot, and sends NAS Signal Info indications.
//
// 0277l, after q10bz (0277k: no SOS, 4G and bars, IMEI, carrier "Very 54.0"; no operator name, About "Network" Unknown, no data):
//  - operator name: CommCenter's NAS registration driver takes the displayed name from the current PLMN name indication (NAS 0x61:
//    "Received current PLMN indication", "Received long name (..) short name (..) service provider name (..) ... and name source (..)",
//    TLVs 0x11 SPN, 0x12/0x13 names, 0x1A source); the bridge never sent it, so the name had source kUnknown. It is sent now (UCS2-LE
//    "VERY", source kNITZ) whenever the registration or the name changes (bb_snapshot_done).
//  - cellular data: CommCenter owns it. sweet_amfi.c still activates the PDN of cid 1 (internet.it) at boot, but in bridge mode
//    (nord2_pdn_owner, set at NORD2_BB_EV_EARLY) it neither configures an address nor routes: it creates pdp_ip0..4 (iOS's PDP manager
//    interfaces) when the bridge publishes its services and sends the PDN's downlink to the interface of the WDS call. WDS answers
//    (bb_wds): Bind Mux Data Port (mux id = pdp + 1) and Set IP Family remembered per client; Start Network adopts the PDN when its APN
//    is the kernel's or empty and the family IPv4 (IPv6: call failed, 3GPP cause 50 "PDP type IPv4 only allowed"; another APN: cause 27;
//    no PDN yet: the answer waits up to 15 s for it), then Packet Service Status "connected"; Get Runtime Settings returns the PDN's
//    address, /30 peer, DNS and MTU; Stop Network and Get Packet Service Status follow the call; Get LTE Attach Parameters gives the APN.
//    configd (IPConfiguration/IPMonitor) then configures pdp_ip<n>, its routes and DNS from CommCenter's service. DSD Get System Status
//    (and its indication on registration changes) reports 3GPP LTE.
//
// 0277k, after the user looked at q10by (0277j, no reset, CommCenter registered on VERY with SIM ready and 2 bars): the status bar said
// "SOS" with empty bars, Settings had no cellular section and no IMEI. SpringBoard's SBTelephonyManager reads three MobileGestalt answers
// once, at its start: contains-cellular-radio, CellularTelephonyCapability and cellular-data ("[SBTelephonyManager] updateLocale:
// clearing country code info for non-cellular device" = cellular-data false). telephony and CellularTelephonyCapability are true for
// every iPhone (device class 1), but cellular-data and contains-cellular-radio are HasBaseband, and HasBaseband is "a node named
// 'baseband' is present in the IODeviceTree plane" (libMobileGestalt MGIOKitHelper deviceTreeNodeIsPresent:withExactName:). This ADT has
// none; the bridge's "baseband" node lives in the IOService plane only and is published ~10 s after SpringBoard started. bb_event
// (NORD2_BB_EV_EARLY, from sweet_amfi_policy_register in bsd_init, before the first exec) now attaches a device tree node "baseband"
// (name, compatible baseband,n41, device_type, config: the D22AP node) under IODeviceTree:/ unless the ADT has one (n2bb bit 17 skips
// it). The MobileGestalt cache does not hold HasBaseband or cellular-data (both computed per process), see HOW-TO-TEST.md.
//
// 0277j, from q10bw (0277i): no thermal, locationd or RFS resets any more; the only reset left was CommCenter's carrier-bundle push,
// 32 times: every boot matched the carrier bundle twice, first from the activation indication (ICCID + IMSI: the MCC/MNC bundle
// com.apple.Hutchison_Wind_it), then after its EF GID1 read (com.apple.Wind_Very_it, "22288_GID1-0000000099"), "Different Bundle
// IDs. Carrier Change", PRI + GRI pushed again and a soft reset, which clears the SIM, so the next boot flipped back. The activation
// indication now carries the card's GID1/GID2 (TLVs 0x44/0x45, qcombb_data.h), read by AT at start-up (GET RESPONSE + READ BINARY)
// and refreshed from CommCenter's own UIM reads (bb_uim_gid_note). Also: PLMN names in UCS2-LE (q10bw showed "VERY" as CJK), the RFS
// sync indication repeated until ABM asks (one soft reset of q10bw got none: "never started filesystem sync"), NAS 0x5566 (preferred
// PLMN list push) accepted.
//
// 0277i, from iOS's unified log of q10bv (0277h), read back with the journal replayed and the extents-overflow file followed (the
// fragmented tracev3 files 7-Zip could not extract were the newest ones, not unwritten ones): of 34 reset requests, 33 were locationd's
// "AP-triggered: locationd Qmi Error Response: QMI_ERR_NOT_SUPPORTED,MSG,qmi::pds::QMI_PDS_RESET" (and once EXT_SET_CELLDB_CONTROL
// 0x5f), MTBF hard resets 2-3 s after every boot: locationd's CLQmiPdsManager::handleResponse asks for a baseband reset on any error
// to a PDS request (except 0x00a1, 0x5559, 0xaa07), on a failed PDS Reset, and on a Get GPS Service State without its TLV 0x01. The
// same requests were behind some of the q10bt/q10bu resets too, hidden by the thermal ones. PDS now answers success (bb_pds). The
// other one was the carrier-bundle soft reset: "Soft reset failed: Baseband never started filesystem sync" (RemoteFSSyncError):
// FSModule (rfs.mod) marks "sync not started" when a soft reset begins and clears it only when an RFS sync completes; a real modem
// asks for one (MFSE 0x2c indication 0x0800) when told to reset, ABM sends MFSE 0x0020 and the token in the answer says whether there
// is data. bb_soft_reset_start now sends that indication 300 ms after the RESET answer and MFSE 0x0020 answers "same token" (no
// data), before the reset line goes down at 1.5 s. Also: QMI reads of a whole file (length 0) became AT+CRSM P3 0 = 256 bytes
// (CommCenter "empty ICCID for a ready SIM!!"); they now ask the file's size first (GET RESPONSE, bb_uim_read_done), and the reset
// dumps print the last requests 8 per line (the console cut the 24 of one line).
//
// 0277h, from iOS's unified log of q10bs/q10bt/q10bu (decoded on the host): the 1 s hard-reset loop was CommCenter's thermal sensor query
// (BSP 0x7702) answered with success but no sensor TLVs ("Processing Temperature Report Failed", MTBF hard reset), not the reset line;
// the first soft reset of every q10bs boot was "Resetting baseband after Failure to Push" (WDS 0x00c8 AttachAPN unsupported), the next
// ones "Resetting baseband after pushing files (due to kCarrierBundleChange)", each "Soft reset failed; timeout waiting for reset
// detection" without 0277g's reset line. 0x7702 is unsupported again, 0x00c8 accepted (see bb_accept).
//
// 0277g, after q10bt: the reset line follows power and soft resets (bb_reset_step); iOS's log shows ResetDetection taking the messages
// ("#I baseband alive detected" in CommCenter and abm-helper). Its guess that a stale line caused the 1 s loop was wrong (see 0277h).
//
// Threads and locks: ABM's threads write and read through the user clients; the modem service thread runs the AT callbacks
// (bb_at_done) and the hooks; thread calls complete asynchronous reads, refresh the AT snapshot and report. One IOLock (bb_lock)
// covers the bridge; user memory is copied outside it; nord2_at_query() only takes sweet_n2at_mtx (lock order bb_lock -> n2at).
// Logs: every line starts "NORD2 BB:". IMEI, IMSI and ICCID are never printed.
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <sys/errno.h>
extern "C" {
#include <pexpert/pexpert.h>
}
#include "../../../bsd/kern/nord2_bb_kpi.h"

// Mav17 (Mav17-9.00.02.Release.bbfw Info.plist com.apple.EmbeddedSoftwareRestore.Baseband.ChipId), BHI register 3, SAHARA HW id,
// BSP 0xb001. The serial is a constant: no real serial number leaves this machine.
#define CBS_CHIP_ID 0x0003b0e1u
#define CBS_SERIAL 0x4e324242u
#include "qcombb_channels.h"
#include "qcombb_control.h"
#include "qcombb_sahara.h"
#include "qcombb_data.h"
#include "nord2_bb_qmux.h"
#include "nord2_bb_qmi.h"

#define BB_FRAME_MAX (64u * 1024u)       // one read or write: a QMUX frame, a SAHARA packet or a READ_DATA answer
#define BB_QUEUE 1024u                   // frames waiting for ABM, per channel (the reference lost answers with 64)
#define BB_READS 8u                      // asynchronous reads waiting for a frame, per channel
#define BB_SYNC_READ_SECONDS 30u
#define BB_UIM_PENDING 8u                // UIM reads waiting for AT+CRSM
#define BB_TICK_SECONDS 10u              // AT snapshot refresh while published
#define BB_REPORT_SECONDS 60u

enum : uint32_t {
  NBB_ON = 1u, NBB_CORE_ONLY = 2u, NBB_NOT_SUPPORTED_ALL = 4u, NBB_PAYLOAD_ALL = 8u, NBB_NO_AT = 16u, NBB_NO_IND = 32u,
  NBB_STRICT_SELECTORS = 64u, NBB_NO_AUTOACCEPT = 128u, NBB_DT_NODE = 256u, NBB_NO_NODE = 512u, NBB_PREBOOT = 1024u,
  NBB_NO_TRACE = 2048u, NBB_KEEP_CT_MARKER = 4096u, NBB_NO_SOFT_RESET = 8192u, NBB_PDS_UNSUPPORTED = 16384u, NBB_NO_RFS_SYNC = 32768u,
  NBB_NO_ACT_GID = 65536u, NBB_NO_DT_EARLY = 131072u, NBB_NO_DATA = 262144u, NBB_NO_PLMN_IND = 524288u, NBB_NO_BOOT_HOLD = 1048576u,
  NBB_NO_PDN_UP = 2097152u, NBB_NO_ACT_HOLD = 4194304u, NBB_NO_ACT_BYPASS = 8388608u, NBB_NO_PROFILES = 16777216u,
  NBB_NO_SMS = 1u << 25, NBB_NO_VOICE = 1u << 26, NBB_SMS_MODEM_ACK = 1u << 27, NBB_NO_ECPI = 1u << 28,
};

// Which SAHARA images the device end asks for: SBL1 (0x15) as in the reference; ABM's log shows whether Mav17's loader wants more.
static const uint32_t bb_sahara_images[] = { 0x15 };

namespace {
struct bb_frame { uint32_t len; uint8_t *data; };
struct bb_read { OSAsyncReference64 reference; IOMemoryDescriptor *memory; uint32_t len; void *owner; };
struct bb_channel {
  bb_frame frames[BB_QUEUE];
  uint32_t head, count;
  bb_read reads[BB_READS];
  uint32_t reads_waiting;
  void *owner;
  uint32_t opens, frames_in, frames_out, frames_dropped, bytes_in, bytes_out, writes_logged;
};
// 0277i: phase 1 = the GET RESPONSE that sizes a whole-file read, 2 = that read
struct bb_uim_read { uint8_t used, channel, cid, phase; uint16_t txn, message; uint32_t gen; struct nbm_uim_read r; uint16_t sized; };
struct bb_kind { uint32_t key, count, error; uint8_t outcome; };
}

static IOLock *bb_lock;
static thread_call_t bb_complete_call, bb_tick_call, bb_refresh_call, bb_publish_call;
static uint32_t bb_flags;
static volatile int bb_published;
static struct nbq_bridge bb_qmux;
static struct cbs_device bb_sahara;
static struct cbc_state bb_control;
static bb_channel bb_ch[CBH_PUBLISHED];
static uint8_t bb_out[BB_FRAME_MAX];     // answers built under the lock
static uint8_t bb_tlv[4096];             // their TLVs
static bool bb_boot_image_sent;
// 0277f: the modem resetting itself (DMS Set Operating Mode 4): AppleBaseband sends ResetDetection's interest messages
#define BB_MSG_RESET_DETECTED 0xe3ff8000u
#define BB_MSG_ALIVE 0xe3ff8001u
static IOService *bb_baseband_svc;
static thread_call_t bb_reset_call;
static uint8_t bb_reset_held, bb_reset_phase;
static uint32_t bb_soft_resets, bb_hard_resets, bb_power_ons, bb_up_ms;
// the last requests, printed when the baseband is reset (who asked last before ABM gave up on it)
struct bb_recent { uint16_t message, error; uint8_t service, outcome; uint32_t ms; };
#define BB_RING 24u
static bb_recent bb_ring[BB_RING];
static uint32_t bb_ring_at;
static uint32_t bb_sig_ind_sig, bb_sig_inds;
// 0277i: PDS GPS service state as locationd last set it; MFSE RFS sync handshakes (indications sent, sync requests answered)
static uint8_t bb_pds_gps_on;
static uint32_t bb_pds_answers, bb_rfs_inds, bb_rfs_syncs;
// 0277j: the GIDs the last activation indication carried; the RFS sync indications of the soft reset in progress and whether ABM synced
static uint32_t bb_act_gid_sig, bb_gid_logged_sig;
static uint32_t bb_soft_start_ms, bb_rfs_tries, bb_rfs_synced;
// 0277l: CommCenter's WDS clients (by QMUX client slot): bound mux, IP family, call state, a Start Network waiting for the PDN
struct bb_wds_call { uint8_t used, channel, cid, mux, family, connected, pending; uint16_t pending_txn; uint32_t handle, pending_ms; };
static bb_wds_call bb_wdsq[NBQ_MAX_CLIENTS];
static uint32_t bb_wds_starts, bb_wds_adopted, bb_wds_failed, bb_wds_stops, bb_pdn_sig, bb_plmn_ind_sig, bb_dsd_sig, bb_plmn_inds;
// 0277m: SAHARA's DONE_RESP (the end of the virtual baseband's boot for ABM) held until the MediaTek modem's SIM is settled, so that
// CommCenter's first QMI requests see the card it will keep seeing (bb_hold_*); ABM's BBUpdater waits 40 s for DONE_RESP
// (ETLSAHCommandReceive timeout 40000 ms), the hold ends after 25 s at the latest
#define BB_HOLD_MAX_MS 25000u
#define BB_HOLD_SIM_ERROR_MS 8000u       // +CPIN? still failing this long after the radio function is on: a real card error
#define BB_HOLD_REG_MS 6000u             // a ready SIM: this long for a registration, then go on without one
#define BB_HOLD_POLL_MS 1000u
static thread_call_t bb_hold_call;
static struct cbs_out bb_hold_out;
static uint8_t bb_hold;
static uint32_t bb_hold_ms, bb_hold_settled_ms, bb_hold_err_ms, bb_holds;
// 0277m: CommCenter's Start Network brings the PDN of cid 1 up at once (bb_pdn_bring_up); a waiting call is answered when the PDN
// has its address and DNS (or 3 s after the address), or failed after BB_WDS_WAIT_MS
#define BB_WDS_WAIT_MS 25000u
#define BB_PDN_DNS_WAIT_MS 3000u
#define BB_PDN_UP_STEPS 4u
static uint32_t bb_pdn_up_ms, bb_pdn_up_runs, bb_pdn_up_fail, bb_pdn_seen_ms;
static char bb_pdn_up_cmd[64];
static void bb_hold_check(void);
static void bb_refresh_soon(uint32_t ms);
// AppleBasebandPCIControl's "bootstage" (CFNumber): libTelephonyBaseband's IsBootStageDownABP reads it on safe resets and takes
// < 2 as "down". 0 until SAHARA finishes, then 2.
static IOService *bb_pci_control;
static uint32_t bb_ticks, bb_qmux_in[NBQ_CHANNELS], bb_qmux_out[NBQ_CHANNELS], bb_dropped_logged, bb_ctl_logged;
// AT snapshot
static struct nbm_state bb_st;
static uint32_t bb_q_inflight, bb_q_epoch, bb_q_errors, bb_snapshots, bb_refresh_again, bb_identity_tries;
static uint32_t bb_nas_sig, bb_uim_sig, bb_log_sig, bb_sig_valid;
static uint8_t bb_had_iccid, bb_had_imsi;
static bb_uim_read bb_uimq[BB_UIM_PENDING];
static uint32_t bb_uim_gen, bb_uim_reads, bb_uim_fail;
// activation (BSP 0xb00c)
static uint8_t bb_act_known, bb_act_channel, bb_act_cid;
static uint32_t bb_activations;
// 0277n: the activation indication held until CommCenter has read the SIM (bb_act_*): EF IMSI read, then its next activation query
// (BSP 0xb001) or 1.5 s; 10 s after the registration at the latest
#define BB_ACT_AFTER_IMSI_MS 1500u
#define BB_ACT_WAIT_MS 10000u
#define BB_ACT_POLL_MS 250u
static thread_call_t bb_act_call;
static uint8_t bb_act_pending;
static uint32_t bb_act_reg_ms, bb_cc_imsi_ms, bb_act_holds;
// DMS operating mode as ABM last set it (phase A does not change the MediaTek modem's CFUN); -1 before the first Set
static int bb_opmode = -1;

extern "C" void proc_selfname(char *buf, int size);

// Which user process reads which property of, or registers which interest on, the bridge's services: the first 48 distinct
// (what, class, key, process) are logged. Kernel accesses are not.
static void bb_note_access(const char *what, const char *cls, const char *key)
{
  static uint32_t seen[48], used;
  char name[32];
  proc_selfname(name, sizeof name);
  if (!strcmp(name, "kernel_task") || !key) return;
  const char *const parts[4] = { what, cls, key, name };
  uint32_t h = 2166136261u;
  for (const char *s : parts)
    for (; s && *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  const uint32_t n = __atomic_load_n(&used, __ATOMIC_RELAXED);
  for (uint32_t i = 0; i < n && i < 48u; i++) if (seen[i] == h) return;
  const uint32_t slot = __atomic_fetch_add(&used, 1u, __ATOMIC_RELAXED);
  if (slot >= 48u) return;
  seen[slot] = h;
  printf("NORD2 BB: %s %s '%s' by %s\n", what, cls, key, name);
}

// Property reads and interest registrations on a bridge service are logged (bb_note_access); behaviour is the base class's.
#define BB_WATCHED(Base)                                                                                                         \
  using Base::copyProperty;                                                                                                      \
  using Base::registerInterest;                                                                                                  \
  OSObject *copyProperty(const char *key) const override                                                                         \
  {                                                                                                                              \
    bb_note_access("property read", getMetaClass()->getClassName(), key);                                                        \
    return Base::copyProperty(key);                                                                                              \
  }                                                                                                                              \
  IONotifier *registerInterest(const OSSymbol *type, IOServiceInterestHandler handler, void *target, void *ref = nullptr) override \
  {                                                                                                                              \
    bb_note_access("interest registered on", getMetaClass()->getClassName(), type ? type->getCStringNoCopy() : "?");             \
    return Base::registerInterest(type, handler, target, ref);                                                                   \
  }

static uint32_t bb_now_ms(void)
{
  uint64_t ticks, ns;
  clock_get_uptime(&ticks);
  absolutetime_to_nanoseconds(ticks, &ns);
  return (uint32_t)(ns / 1000000u);
}

// ------------------------------------------------------------------ request statistics and the log budget
#define BB_KINDS 256u
#define BB_LOG_BUDGET 3000u
enum { BB_UNHANDLED = 0, BB_ANSWERED = 1, BB_DEFERRED = 2 };
static bb_kind bb_kinds[BB_KINDS];
static uint32_t bb_log_lines, bb_log_held, bb_requests;

static bb_kind *bb_kind_note(uint32_t service, uint32_t message, uint8_t outcome, uint32_t error)
{
  const uint32_t key = 1u + (service << 16 | (message & 0xffffu));
  for (uint32_t i = 0, at = (key * 2654435761u >> 24) % BB_KINDS; i < BB_KINDS; i++, at = (at + 1) % BB_KINDS) {
    if (bb_kinds[at].key && bb_kinds[at].key != key) continue;
    bb_kinds[at].key = key;
    bb_kinds[at].count++;
    bb_kinds[at].outcome = outcome;
    bb_kinds[at].error = error;
    return &bb_kinds[at];
  }
  return nullptr;
}
// first 4 of a kind, then each power of two, within the budget
static bool bb_log_ok(uint32_t count)
{
  if (count > 4u && (count & (count - 1u))) return false;
  if (bb_log_lines >= BB_LOG_BUDGET) { bb_log_held++; return false; }
  bb_log_lines++;
  return true;
}
// Request bytes may be shown only where they cannot carry identities, numbers, credentials, PINs or APDUs.
static bool bb_payload_ok(uint32_t service, uint32_t message)
{
  if (bb_flags & NBB_PAYLOAD_ALL) return true;
  switch (service) {
  case NBQ_SVC_DMS: return message < 0x0026u || message > 0x002bu;     // not the UIM PIN messages
  case NBQ_SVC_NAS: case NBQ_SVC_QOS: case NBQ_SVC_WDA: case 0x11: case 0x22: case 0x24: case 0x2a: case 0x2b: case 0x2f: return true;
  case NBQ_SVC_BSP: return message != CBD_BSP_SEND_FILE;
  default: return nbq_private_service(service) && service != NBQ_SVC_VINYL;
  }
}

static void bb_log_request(uint32_t qmux, uint32_t service, uint32_t cid, const uint8_t *sdu, uint32_t n, const char *what,
                           uint32_t error, uint32_t count)
{
  char tl[112], pl[40];
  unsigned k = 0, shown = 0;
  const uint32_t message = nbq_rd16(sdu + 3), end = 7u + nbq_rd16(sdu + 5);
  uint32_t at = 7;
  tl[0] = pl[0] = 0;
  while (at + 3u <= end && end <= n && shown < 10) {
    const uint32_t l = nbq_rd16(sdu + at + 1);
    if (at + 3u + l > end) break;
    k += (unsigned)snprintf(tl + k, sizeof tl - k, " %02x/%u", sdu[at], l);
    if (k >= sizeof tl) { k = sizeof tl - 1; break; }
    at += 3u + l;
    shown++;
  }
  if (at < end && k + 5 < sizeof tl) snprintf(tl + k, sizeof tl - k, " ...");
  if (end > 7u && end <= n && bb_payload_ok(service, message)) {
    unsigned p = 0;
    p += (unsigned)snprintf(pl, sizeof pl, " [");
    for (uint32_t i = 7; i < end && i < 7u + 12u && p + 4 < sizeof pl; i++) p += (unsigned)snprintf(pl + p, sizeof pl - p, "%02x", sdu[i]);
    snprintf(pl + p, sizeof pl - p, "%s]", end > 7u + 12u ? ".." : "");
  }
  printf("NORD2 BB: q%u %s(0x%02x) cid %u msg 0x%04x txn %u len %u tlv%s%s -> %s err 0x%x (#%u)\n", qmux + 1u, nbq_service_name(service),
         service, cid, message, nbq_rd16(sdu + 1), n, k ? tl : " -", pl, what, error, count);
}

// ------------------------------------------------------------------ queues (bb_lock held)
static void bb_complete(thread_call_param_t, thread_call_param_t);

static void bb_push(uint32_t channel, const uint8_t *data, uint32_t len)
{
  bb_channel *c = &bb_ch[channel];
  uint8_t *copy = len && len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
  if (!copy || c->count == BB_QUEUE) {
    if (copy) IOFreeData(copy, len);
    if (c->frames_dropped < 32u)
      printf("NORD2 BB: dropped frame for ABM on %s (%s, %u queued, %u bytes) svc 0x%x type %u msg 0x%x\n", cbh_published[channel],
             copy ? "queue full" : "no memory", c->count, len, len > 4 ? data[4] : 0u, len > NBQ_HDR ? data[NBQ_HDR] : 0u,
             len > NBQ_HDR + 4 ? (uint32_t)(data[NBQ_HDR + 3] | data[NBQ_HDR + 4] << 8) : 0u);
    c->frames_dropped++;
    return;
  }
  memcpy(copy, data, len);
  c->frames[(c->head + c->count++) % BB_QUEUE] = { len, copy };
  c->frames_out++;
  c->bytes_out += len;
  if (channel == CBH_QMUX1 || channel == CBH_QMUX2) bb_qmux_out[channel == CBH_QMUX1 ? 0 : 1]++;
  IOLockWakeup(bb_lock, c, false);
  if (c->reads_waiting && bb_complete_call) thread_call_enter(bb_complete_call);
}

static bb_frame bb_pop(uint32_t channel)
{
  bb_channel *c = &bb_ch[channel];
  bb_frame f = { 0, nullptr };
  if (!c->count) return f;
  f = c->frames[c->head];
  c->head = (c->head + 1) % BB_QUEUE;
  c->count--;
  return f;
}

static void bb_flush(uint32_t channel)
{
  for (bb_frame f = bb_pop(channel); f.data; f = bb_pop(channel)) IOFreeData(f.data, f.len);
}

static uint32_t bb_qmux_channel(uint32_t qmux) { return qmux ? CBH_QMUX2 : CBH_QMUX1; }

// BBUEUR13Loader pushes the SBL with SendImage on pbl, frees that transport, opens sahara and waits for the loaded SBL's HELLO there.
static void bb_sahara_restart(void)
{
  struct cbs_out o;
  bb_hold = 0;                                                     // 0277m: a DONE_RESP still held belongs to the old boot
  bb_act_pending = 0;                                              // 0277n: so does a held activation indication
  bb_flush(CBH_SAHARA);
  cbs_init(&bb_sahara, bb_sahara_images, sizeof bb_sahara_images / sizeof bb_sahara_images[0], 0x10000);
  cbs_start(&bb_sahara, &o);
  for (uint32_t i = 0; i < o.count; i++) bb_push(CBH_SAHARA, o.data[i], o.len[i]);
  printf("NORD2 BB: sahara hello (images %u, chip id 0x%08x)\n", bb_sahara.image_count, CBS_CHIP_ID);
}

// ------------------------------------------------------------------ answers (bb_lock held)
static void bb_reply(uint32_t qmux, uint32_t service, uint32_t cid, const uint8_t *sdu, uint32_t error, const uint8_t *tlvs, uint32_t t)
{
  const uint32_t n = nbq_answer(sdu, service, cid, error, tlvs, t, bb_out, sizeof bb_out);
  if (n) bb_push(bb_qmux_channel(qmux), bb_out, n);
}

// The same indication to every client of `service` on both channels.
static uint32_t bb_indicate_all(uint32_t service, uint32_t message, const uint8_t *tlvs, uint32_t t)
{
  uint32_t sent = 0;
  for (uint32_t i = 0; i < NBQ_MAX_CLIENTS; i++) {
    const struct nbq_client *c = &bb_qmux.client[i];
    if (!c->used || c->service != service) continue;
    const uint32_t n = nbq_indication(message, service, c->cid, tlvs, t, bb_out, sizeof bb_out);
    if (n) { bb_push(bb_qmux_channel(c->channel), bb_out, n); sent++; }
  }
  return sent;
}

// ------------------------------------------------------------------ 0277f: resets (bb_lock held unless noted)
static void bb_ring_note(uint32_t service, uint32_t message, uint32_t outcome, uint32_t error)
{
  bb_recent &e = bb_ring[bb_ring_at++ % BB_RING];
  e = { (uint16_t)message, (uint16_t)error, (uint8_t)service, (uint8_t)outcome, bb_now_ms() };
}
// "why": who reset; the boot's age and the last requests, oldest first, as SVC/msg:error ('d' deferred, 'a' accepted/answered)
// 0277i: the requests 8 per line (q10bv's console cut the one line of 24 after ~250 characters)
static void bb_ring_dump(const char *why)
{
  char line[200];
  unsigned k = 0;
  const uint32_t now = bb_now_ms(), n = bb_ring_at < BB_RING ? bb_ring_at : BB_RING;
  printf("NORD2 BB: %s, %u s after power-on, last request %u ms ago; last %u requests, oldest first:\n", why,
         bb_up_ms ? (now - bb_up_ms) / 1000u : 0u, n ? now - bb_ring[(bb_ring_at - 1) % BB_RING].ms : 0u, n);
  line[0] = 0;
  for (uint32_t i = 0; i < n; i++) {
    const bb_recent &e = bb_ring[(bb_ring_at - n + i) % BB_RING];
    k += (unsigned)snprintf(line + k, sizeof line - k, " %s/%04x:%s%x", nbq_service_name(e.service), e.message,
                            e.outcome == BB_DEFERRED ? "d" : e.outcome == BB_ANSWERED && !e.error ? "a" : "", e.error);
    if (k >= sizeof line - 24 || (i + 1) % 8 == 0 || i + 1 == n) { printf("NORD2 BB:   ring%s\n", line); k = 0; line[0] = 0; }
  }
}

// The reset-detect line as libBasebandManager's ResetDetection (log category reset.detect) sees it: interest messages on AppleBaseband
// 0xe3ff8000 "baseband reset detected" / 0xe3ff8001 "baseband alive detected", GetReset (selector 3) = the line at start, 1 in reset.
// BootModule adds named one-shot waits ("Added detection with key ..., reset=1/0"): after every boot "Baseband Recovered Gate" (alive)
// and "Baseband Crash Recovery" (reset: a reset nobody asked for is a crash), "Soft Reset Wait for Reset" after "Soft reset issued"
// (timeout: "Soft reset failed; timeout waiting for reset detection", then a hard reset), "Poweroff: Wait for reset detection" (10 s).
// 0277g, as a board drives the line:
//  - DMS Set Operating Mode 4 (RESET): the answer, then 1.5 s later reset detected (by then ABM has registered "Soft Reset Wait for
//    Reset": in q10bt the 100 ms of 0277f came before it and ABM took the reset for a crash), transport back to before SendImage,
//    bootstage 0, then 300 ms later alive;
//  - every power-on (AppleBaseband selector 1 [1], after a hard reset too): alive 200 ms later, as the modem comes out of reset;
//  - power off (selector 1 [0]): reset detected 50 ms later ("Poweroff: Wait for reset detection").
// ABM's own hard reset (selector 0) sends nothing by itself: its power-on that follows sends alive.
enum : uint8_t { BB_RD_NONE = 0, BB_RD_RESET = 1, BB_RD_ALIVE = 2, BB_RD_RFS = 3 };
static uint8_t bb_rd_next, bb_rd_then_alive;
static const char *bb_rd_why = "";
static uint32_t bb_rd_resets_sent, bb_rd_alives_sent, bb_get_reset_polls;

static void bb_rd_schedule(uint8_t event, uint32_t ms, bool then_alive, const char *why)     // bb_lock held
{
  if (!bb_reset_call) return;
  bb_rd_next = event;
  bb_rd_then_alive = then_alive;
  bb_rd_why = why;
  uint64_t deadline = 0;
  clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
  thread_call_enter_delayed(bb_reset_call, deadline);
}

static void bb_soft_reset_start(void)
{
  if (!bb_reset_call || bb_reset_phase) return;
  bb_reset_phase = 1;
  bb_soft_resets++;
  char why[64];
  snprintf(why, sizeof why, "soft reset #%u (DMS operating mode RESET)", bb_soft_resets);
  bb_ring_dump(why);
  // 0277i: the RFS sync a modem asks for before it resets (FSModule's "sync not started" check), then the reset line at 1.5 s.
  // 0277j: repeated every 300 ms (3 at most) until ABM's sync request comes, the reset line still at 1.5 s
  bb_soft_start_ms = bb_now_ms(); bb_rfs_tries = 0; bb_rfs_synced = 0;
  if (!(bb_flags & (NBB_NO_RFS_SYNC | NBB_NO_IND))) bb_rd_schedule(BB_RD_RFS, 300, true, "soft reset");
  else bb_rd_schedule(BB_RD_RESET, 1500, true, "soft reset");
}

// 0277i: MFSE indication 0x0800 "Received RFS Sync indication", 0x10 reset after sync = false (the reset is already under way): ABM's
// FSModule::requestFSSyncIndication -> requestFSSync -> QMIRFSCommandDriver::sync (MFSE 0x0020, answered in bb_qmi_request)
static uint32_t bb_rfs_indicate(void)                // bb_lock held
{
  const uint32_t t = nbm_tlv_u8(bb_tlv, sizeof bb_tlv, 0x10, 0);
  const uint32_t sent = bb_indicate_all(NBM_SVC_MFSE, NBM_MFSE_RFS_SYNC_IND, bb_tlv, t);
  bb_rfs_inds++;
  return sent;
}

static void bb_reset_step(thread_call_param_t, thread_call_param_t)
{
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  const uint8_t event = bb_rd_next;
  const char *why = bb_rd_why;
  bb_rd_next = BB_RD_NONE;
  if (event == BB_RD_RFS) {               // only while the soft reset is still on (ABM's own power calls end it)
    if (bb_reset_phase) {
      const uint32_t age = bb_now_ms() - bb_soft_start_ms;
      if (!bb_rfs_synced && bb_rfs_tries < 3) {
        const uint32_t sent = bb_rfs_indicate();
        bb_rfs_tries++;
        if (bb_rfs_inds <= 32) printf("NORD2 BB: soft reset: RFS sync indication #%u to %u MFSE clients\n", bb_rfs_tries, sent);
      }
      if (!bb_rfs_synced && bb_rfs_tries < 3 && age + 300u < 1400u) bb_rd_schedule(BB_RD_RFS, 300, true, why);
      else bb_rd_schedule(BB_RD_RESET, age < 1400u ? 1500u - age : 100u, true, why);
    }
    IOLockUnlock(bb_lock);
    return;
  }
  if (event == BB_RD_RESET) {
    bb_reset_held = 1;
    bb_boot_image_sent = false;
    bb_flush(CBH_SAHARA);
    bb_opmode = -1;
    bb_up_ms = 0;
    if (bb_pci_control) bb_pci_control->setProperty("bootstage", 0ull, 32);
    if (bb_rd_then_alive) bb_rd_schedule(BB_RD_ALIVE, 300, false, why);
  } else if (event == BB_RD_ALIVE) {
    bb_reset_held = 0;
    bb_reset_phase = 0;
  }
  IOLockUnlock(bb_lock);
  IOService *svc = bb_baseband_svc;
  if (event == BB_RD_RESET) {
    const IOReturn r = svc ? svc->messageClients(BB_MSG_RESET_DETECTED) : kIOReturnNotReady;
    if (bb_rd_resets_sent++ < 24) printf("NORD2 BB: reset line: reset detected sent (%s, 0x%x)\n", why, (uint32_t)r);
  } else if (event == BB_RD_ALIVE) {
    const IOReturn r = svc ? svc->messageClients(BB_MSG_ALIVE) : kIOReturnNotReady;
    if (bb_rd_alives_sent++ < 24) printf("NORD2 BB: reset line: alive sent (%s, 0x%x)\n", why, (uint32_t)r);
  }
}

static uint32_t bb_hash(uint32_t h, const void *p, uint32_t n);
// 0277j: what the GIDs are now (known state, length, bytes), to see when the activation indication has to be sent again
static uint32_t bb_gid_sig(void)
{
  uint32_t h = 2166136261u;
  for (unsigned g = 0; g < 2; g++) {
    h = bb_hash(h, &bb_st.gid_known[g], 1);
    h = bb_hash(h, &bb_st.gid_len[g], 1);
    h = bb_hash(h, bb_st.gid[g], bb_st.gid_len[g]);
  }
  return h;
}

// The activation indication (qcombb_data.h) to the BSP client that registered, with the card's ICCID and IMSI once AT gave them.
static void bb_activation_indicate(void)
{
  if (!bb_act_known || bb_act_pending) return;                     // 0277n: held until CommCenter has read the SIM
  uint8_t tlvs[CBD_ACTIVATION_SIM_TLVS + CBD_ACTIVATION_GID_TLVS + CBD_ACTIVATION_MODE_TLV], iccid[CBD_ICCID_BYTES], imsi[CBD_IMSI_BYTES];
  const bool have_iccid = bb_st.have_iccid && cbd_iccid_from_ef(bb_st.iccid_ef, sizeof bb_st.iccid_ef, iccid);
  const bool have_imsi = nbm_imsi_bcd(&bb_st, imsi) != 0;
  uint32_t t = cbd_activation_state(tlvs, sizeof tlvs, have_iccid ? iccid : nullptr, have_iccid && have_imsi ? imsi : nullptr);
  // 0277j: the GIDs, so the first carrier bundle match of a boot is the same as the one after CommCenter reads EF GID1
  if (t && have_iccid && !(bb_flags & NBB_NO_ACT_GID))
    t += cbd_activation_gids(tlvs + t, sizeof tlvs - t, bb_st.gid_known[0] == 1 ? bb_st.gid[0] : nullptr, bb_st.gid_len[0],
                             bb_st.gid_known[1] == 1 ? bb_st.gid[1] : nullptr, bb_st.gid_len[1]);
  // 0277o: ActivationMode kBypass (qcombb_data.h): no postponement ticket needed for data contexts (n2bb bit 23: left out)
  if (t && !(bb_flags & NBB_NO_ACT_BYPASS)) t += cbd_activation_mode(tlvs + t, sizeof tlvs - t, CBD_ACTIVATION_MODE_BYPASS);
  bb_act_gid_sig = bb_gid_sig();
  const uint32_t n = nbq_indication(CBD_BSP_ACTIVATION_REGISTER, CBD_SVC_BSP, bb_act_cid, tlvs, t, bb_out, sizeof bb_out);
  if (n) bb_push(bb_act_channel, bb_out, n);
}

// 0277n, from q10cb (0277m): with the boot held, CommCenter's first card status said ready, but CommCenter itself goes unknown ->
// detected -> ready only after reading EF ICCID, GID1, GID2 and IMSI. The activation indication the bridge sent with the answer to
// ActivationRegister came between: pp took "activated, provisioned" ("Preferring legacy OTASP provisioned state: kProvisioned"), then
// "No SIM info for (kOne)" put the baseband service provisioned state back to kUnknown, DATA saw kProvisioned -> kNotProvisioned and
// deactivated every context (ServiceNotProvisioned): "context is not assigned" for the rest of the boot, as in q10ca. A Qualcomm
// baseband reports itself activated after the SIM (and the activation ticket), so the indication now waits for CommCenter's read of EF
// IMSI, then goes with its next activation query (BSP 0xb001, "Reached steady state, querying activation status from BB") or 1.5 s
// later, 10 s after the registration at the latest. n2bb bit 22: at once (0277m behaviour).
static void bb_act_release(const char *why)
{
  if (!bb_act_pending) return;
  bb_act_pending = 0;
  bb_activation_indicate();
  printf("NORD2 BB: activation indication released after %u ms (%s), ICCID %s, IMSI %s, mode %s\n", bb_now_ms() - bb_act_reg_ms, why,
         bb_st.have_iccid ? "included" : "not read", bb_st.imsi[0] ? "included" : "not read",
         (bb_flags & NBB_NO_ACT_BYPASS) ? "kEnabled (not sent)" : "kBypass");
}
static void bb_act_check(void)
{
  if (!bb_act_pending) return;
  const uint32_t now = bb_now_ms();
  if (bb_cc_imsi_ms && now - bb_cc_imsi_ms >= BB_ACT_AFTER_IMSI_MS) bb_act_release("1.5 s after CommCenter read EF IMSI");
  else if (now - bb_act_reg_ms >= BB_ACT_WAIT_MS) bb_act_release("10 s cap");
}
static void bb_act_arm(void)
{
  uint64_t deadline = 0;
  clock_interval_to_deadline(BB_ACT_POLL_MS, kMillisecondScale, &deadline);
  thread_call_enter_delayed(bb_act_call, deadline);
}
static void bb_act_tick(thread_call_param_t, thread_call_param_t)
{
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  bb_act_check();
  const bool again = bb_act_pending != 0;
  IOLockUnlock(bb_lock);
  if (again) bb_act_arm();
}

// Apple's private BSP service, as the reference answers it.
static int bb_bsp(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t n, uint32_t *error)
{
  const uint32_t message = nbq_rd16(sdu + 3);
  *error = 0;
  if (message == CBD_BSP_SEND_FILE) {
    // Carrier bundle files: accepted and dropped, each segment acknowledged with the next segment number, then FileTransferStatus.
    struct cbd_file_segment f;
    if (!cbd_bsp_file_segment(sdu, n, &f)) return BB_UNHANDLED;
    uint8_t tlvs[8];
    const uint32_t t = cbd_bsp_file_answer(&f, tlvs, sizeof tlvs);
    bb_reply(qmux, CBD_SVC_BSP, cid, sdu, 0, tlvs, t);
    if (f.segment + 1u == f.count) {
      uint8_t done[12];
      const uint32_t d = cbd_bsp_file_done(&f, done, sizeof done);
      const uint32_t m = nbq_indication(CBD_BSP_FILE_TRANSFER_STATUS, CBD_SVC_BSP, cid, done, d, bb_out, sizeof bb_out);
      if (m) bb_push(bb_qmux_channel(qmux), bb_out, m);
      printf("NORD2 BB: carrier file type %u taken (%u segments, last %u bytes), dropped, transfer status sent\n", f.type, f.count, f.bytes);
    }
    return BB_ANSWERED;
  }
  if (message == CBD_BSP_CHIP_INFO) {
    uint8_t tlvs[12];
    const uint32_t t = cbd_bsp_chip_info(CBS_CHIP_ID, CBS_SERIAL, tlvs, sizeof tlvs);
    bb_reply(qmux, CBD_SVC_BSP, cid, sdu, 0, tlvs, t);
    if (bb_act_pending && bb_cc_imsi_ms) bb_act_release("activation query after CommCenter's SIM read");   // 0277n
    return BB_ANSWERED;
  }
  if (message == CBD_BSP_SET_CONFIG) {
    bb_reply(qmux, CBD_SVC_BSP, cid, sdu, 0, nullptr, 0);
    printf("NORD2 BB: baseband config (SetConfig) taken\n");
    return BB_ANSWERED;
  }
  if (message == CBD_BSP_ACTIVATION_REGISTER) {
    bb_reply(qmux, CBD_SVC_BSP, cid, sdu, 0, nullptr, 0);
    bb_act_known = 1; bb_act_channel = (uint8_t)bb_qmux_channel(qmux); bb_act_cid = (uint8_t)cid;
    const bool hold = !(bb_flags & NBB_NO_ACT_HOLD) && bb_act_call;
    if (hold) {                                                    // 0277n: after CommCenter's own SIM read
      bb_act_pending = 1;
      bb_act_reg_ms = bb_now_ms();
      bb_cc_imsi_ms = 0;
      bb_act_holds++;
      bb_act_arm();
    } else {
      bb_activation_indicate();
    }
    if (bb_activations++ < 4)
      printf("NORD2 BB: activation register answered; indication (activated) %s\n", hold ? "held until CommCenter reads the SIM" : "sent");
    return BB_ANSWERED;
  }
  return BB_UNHANDLED;
}

static int bb_dms(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t n, uint32_t *error)
{
  const uint32_t message = nbq_rd16(sdu + 3), room = sizeof bb_tlv;
  uint32_t t = 0, len = 0;
  char digits[24];
  *error = 0;
  switch (message) {
  case NBM_DMS_GET_IDS:
    if (bb_st.imei[0]) t = nbm_tlv_str(bb_tlv, room, 0x11, bb_st.imei); else *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  case NBM_DMS_GET_REVISION:
  case NBM_DMS_GET_SOFTWARE_VERSION:
    if (bb_st.rev[0]) t = nbm_tlv_str(bb_tlv, room, 0x01, bb_st.rev); else *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  case NBM_DMS_GET_MANUFACTURER:
    t = nbm_tlv_str(bb_tlv, room, 0x01, "QUALCOMM INCORPORATED");
    break;
  case NBM_DMS_GET_CAPABILITIES:
    t = nbm_dms_capabilities(bb_tlv, room);
    break;
  case NBM_DMS_GET_OPERATING_MODE:
    t = nbm_tlv_u8(bb_tlv, room, 0x01, bb_opmode >= 0 ? (uint32_t)bb_opmode : nbm_operating_mode(&bb_st));
    break;
  case NBM_DMS_SET_OPERATING_MODE: {
    // Phase A: remembered and confirmed (response, then the Event Report indication with TLV 0x14), without touching the MediaTek
    // modem's CFUN; the log says what ABM asked. 0277f: RESET (4) resets the virtual modem (bb_soft_reset_start).
    const uint8_t *v = nbq_tlv(sdu + 7, nbq_rd16(sdu + 5), 0x01, &len);
    if (!v || len < 1) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    if (v[0] == NBM_DMS_OPMODE_RESET && !(bb_flags & NBB_NO_SOFT_RESET)) {
      bb_reply(qmux, NBQ_SVC_DMS, cid, sdu, 0, nullptr, 0);
      bb_soft_reset_start();
      return BB_ANSWERED;
    }
    bb_opmode = v[0];
    printf("NORD2 BB: DMS set operating mode %u accepted without action (modem CFUN %d)\n", v[0], bb_st.cfun);
    bb_reply(qmux, NBQ_SVC_DMS, cid, sdu, 0, nullptr, 0);
    if (!(bb_flags & NBB_NO_IND)) {
      const uint32_t i = nbm_tlv_u8(bb_tlv, room, 0x14, v[0]);
      bb_indicate_all(NBQ_SVC_DMS, NBM_DMS_SET_EVENT_REPORT, bb_tlv, i);
    }
    return BB_ANSWERED;
  }
  case NBM_DMS_UIM_GET_ICCID:
    if (nbm_iccid_digits(&bb_st, digits, sizeof digits)) t = nbm_tlv_str(bb_tlv, room, 0x01, digits); else *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  case NBM_DMS_UIM_GET_IMSI:
    if (bb_st.imsi[0]) t = nbm_tlv_str(bb_tlv, room, 0x01, bb_st.imsi); else *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  case NBM_DMS_UIM_GET_STATE:
    t = nbm_tlv_u8(bb_tlv, room, 0x01, nbm_dms_uim_state(&bb_st));
    break;
  case NBM_DMS_GET_MSISDN:             // the card has no number for us (no AT+CNUM here): what a modem answers for an empty EF_MSISDN
    *error = 0x10u;                    // QMI_ERR_NOT_PROVISIONED
    break;
  case NBM_DMS_GET_BAND_CAPABILITIES:
    t = nbm_dms_band_capabilities(bb_tlv, room);
    break;
  case NBM_DMS_GET_TIME: {             // the AP clock, when it looks set (after 2024); otherwise not available
    clock_sec_t sec = 0;
    clock_usec_t usec = 0;
    clock_get_calendar_microtime(&sec, &usec);
    if (sec > 1704067200) t = nbm_dms_time((uint64_t)sec * 1000u + usec / 1000u, bb_tlv, room); else *error = NBQ_ERR_INFO_UNAVAILABLE;
    break;
  }
  default:
    return BB_UNHANDLED;
  }
  (void)n;
  bb_reply(qmux, NBQ_SVC_DMS, cid, sdu, *error, bb_tlv, *error ? 0 : t);
  return BB_ANSWERED;
}

static int bb_nas(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t n, uint32_t *error)
{
  const uint32_t message = nbq_rd16(sdu + 3), room = sizeof bb_tlv;
  uint32_t t = 0;
  *error = 0;
  switch (message) {
  case NBM_NAS_SERVING_SYSTEM: t = nbm_nas_serving_system(&bb_st, bb_tlv, room); break;
  case NBM_NAS_GET_SYSTEM_INFO: t = nbm_nas_system_info_ex(&bb_st, 1, bb_tlv, room); break;   // 0277t: + LTE voice TLVs
  case NBM_NAS_GET_SIGNAL_INFO: t = nbm_nas_signal_info(&bb_st, bb_tlv, room); break;   // the serving RAT's TLV (0277t: also WCDMA/GSM)
  case NBM_NAS_GET_SIGNAL_STRENGTH:
    if (!(t = nbm_nas_signal_strength(&bb_st, bb_tlv, room))) *error = NBQ_ERR_INFO_UNAVAILABLE;
    break;
  case NBM_NAS_GET_HOME_NETWORK:
    if (!(t = nbm_nas_home_network(&bb_st, bb_tlv, room))) *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  case NBM_NAS_GET_SYSTEM_SELECTION_PREF:
    t = nbm_nas_sys_sel_pref(bb_tlv, room);
    break;
  case 0x003bu:                        // 0277h: Get CSP PLMN Mode Bit, 0x10 = 0 (manual PLMN selection not restricted): "PLMN mode bit
    t = nbm_tlv_u8(bb_tlv, room, 0x10, 0);   // request failed ... QMI_ERR_INVALID_QMI_CMD" in CommCenter's log
    break;
  case NBM_NAS_GET_PLMN_NAME: {        // TLV 0x01 {u16 mcc, u16 mnc}: the registered PLMN's name from +COPS
    uint32_t len = 0;
    const uint8_t *v = nbq_tlv(sdu + 7, nbq_rd16(sdu + 5), 0x01, &len);
    if (!v || len < 4) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    if (!(t = nbm_nas_plmn_name(&bb_st, nbq_rd16(v), nbq_rd16(v + 2), bb_tlv, room))) *error = NBQ_ERR_INFO_UNAVAILABLE;
    break;
  }
  default:
    return BB_UNHANDLED;
  }
  (void)n;
  bb_reply(qmux, NBQ_SVC_NAS, cid, sdu, *error, bb_tlv, *error ? 0 : t);
  return BB_ANSWERED;
}

static void bb_at_done(void *ctx, int final, const char *text, unsigned len);
#define BB_CTX_UIM 0x80000000u

// UIM Read Transparent / Read Record: AT+CRSM on the same file, the answer when it returns (bb_uim_read_done).
static int bb_uim_read_start(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t n, uint32_t *error)
{
  struct nbm_uim_read r;
  char cmd[64];
  const uint32_t message = nbq_rd16(sdu + 3);
  *error = nbm_uim_read_parse(message, sdu, n, &r);
  if (!*error && r.indication_token) return BB_UNHANDLED;        // "response in indication": not emulated, see the log
  if (!*error && !nbm_crsm_read_cmd(&r, cmd, sizeof cmd)) *error = r.session == 0 || r.session == 1 || r.session == 6 ? NBQ_ERR_INVALID_ARG : NBQ_ERR_NOT_SUPPORTED;
  uint32_t slot = BB_UIM_PENDING;
  for (uint32_t i = 0; !*error && i < BB_UIM_PENDING && slot == BB_UIM_PENDING; i++) if (!bb_uimq[i].used) slot = i;
  if (!*error && slot == BB_UIM_PENDING) *error = NBQ_ERR_DEVICE_NOT_READY;
  // 0277i: a whole-file read (length 0) asks for the file's FCP first (phase 1), the read follows with its size (bb_uim_read_done)
  const bool whole = !*error && !r.attributes && r.length == 0;
  if (whole) {
    struct nbm_uim_read probe = r;
    probe.attributes = 1;
    probe.record_read = 0;
    if (!nbm_crsm_read_cmd(&probe, cmd, sizeof cmd)) *error = NBQ_ERR_INVALID_ARG;
  }
  if (!*error) {
    bb_uim_gen = (bb_uim_gen + 1u) & 0xffffu;
    bb_uim_read &u = bb_uimq[slot];
    u = { 1, (uint8_t)qmux, (uint8_t)cid, (uint8_t)(whole ? 1 : 0), nbq_rd16(sdu + 1), (uint16_t)message, bb_uim_gen, r, 0 };
    const int e = nord2_at_query(cmd, bb_at_done, reinterpret_cast<void *>(static_cast<uintptr_t>(BB_CTX_UIM | bb_uim_gen << 8 | slot)));
    if (e) { u.used = 0; *error = e == ENXIO ? NBQ_ERR_INTERNAL : NBQ_ERR_DEVICE_NOT_READY; }
  }
  if (*error) {
    bb_reply(qmux, NBQ_SVC_UIM, cid, sdu, *error, nullptr, 0);
    bb_uim_fail++;
    return BB_ANSWERED;
  }
  bb_uim_reads++;
  return BB_DEFERRED;
}

static void bb_uim_read_done(uint32_t ctx, int final, const char *text, unsigned len)
{
  const uint32_t slot = ctx & 0xffu, gen = ctx >> 8 & 0xffffu;
  if (slot >= BB_UIM_PENDING || !bb_uimq[slot].used || bb_uimq[slot].gen != gen) return;
  bb_uim_read &u = bb_uimq[slot];
  if (nbq_client_find(&bb_qmux, u.channel, NBQ_SVC_UIM, u.cid) < 0) { u.used = 0; return; }    // released meanwhile
  uint8_t sw1 = 0, sw2 = 0, data[512];
  uint32_t got = 0, error = 0, t = 0;
  const bool crsm = final == NORD2_AT_FINAL_OK && nbm_crsm(text, len, &sw1, &sw2, data, sizeof data, &got);
  if (u.phase == 1) {
    // 0277i: the FCP sized the read; without one (or for a file it does not size) the specs' sizes, else the old P3 0 (256 bytes)
    struct nbm_fcp f;
    const int have = crsm && (sw1 == 0x90 || sw1 == 0x91 || sw1 == 0x92) && nbm_fcp_parse(data, got, u.r.file, &f);
    const uint32_t want = nbm_whole_read_length(&u.r, &f, have);
    char cmd[64];
    struct nbm_uim_read rd = u.r;
    rd.length = want >= 256 ? 0 : (uint16_t)want;        // P3 0 = 256
    u.sized = (uint16_t)want;
    u.phase = 2;
    if (crsm && (sw1 == 0x6a || sw1 == 0x94) && !want) {
      error = NBQ_ERR_SIM_FILE_NOT_FOUND;                // no such file: the answer the read would have had
      t = nbm_uim_read_answer(sw1, sw2, nullptr, 0, bb_tlv, sizeof bb_tlv);
    } else if (!nbm_crsm_read_cmd(&rd, cmd, sizeof cmd)) {
      error = NBQ_ERR_INVALID_ARG;
    } else if (nord2_at_query(cmd, bb_at_done, reinterpret_cast<void *>(static_cast<uintptr_t>(BB_CTX_UIM | u.gen << 8 | slot)))) {
      error = NBQ_ERR_DEVICE_NOT_READY;
    } else {
      return;                                            // phase 2 answers
    }
  } else if (!crsm) {
    error = NBQ_ERR_INTERNAL;
  } else {
    if (u.phase == 2 && u.sized && got > u.sized) got = u.sized;   // what the modem padded beyond the file
    t = u.message == NBM_UIM_GET_FILE_ATTRIBUTES ? nbm_uim_attributes_answer(sw1, sw2, u.r.file, data, got, bb_tlv, sizeof bb_tlv)
                                                 : nbm_uim_read_answer(sw1, sw2, data, got, bb_tlv, sizeof bb_tlv);
    if (sw1 != 0x90 && sw1 != 0x91 && sw1 != 0x92) error = sw1 == 0x6a || sw1 == 0x94 ? NBQ_ERR_SIM_FILE_NOT_FOUND : NBQ_ERR_INTERNAL;
  }
  u.used = 0;
  // 0277j: CommCenter's own read of EF GID1/GID2 is the reference for the activation indication's GIDs
  if (!error && !u.r.record_read && u.message != NBM_UIM_GET_FILE_ATTRIBUTES && u.r.offset == 0 && got &&
      (u.r.file == 0x6f3e || u.r.file == 0x6f3f) && (sw1 == 0x90 || sw1 == 0x91 || sw1 == 0x92)) {
    const unsigned g = u.r.file == 0x6f3f;
    const uint32_t k = got < sizeof bb_st.gid[g] ? got : (uint32_t)sizeof bb_st.gid[g];
    const uint32_t before = bb_gid_sig();
    memcpy(bb_st.gid[g], data, k);
    bb_st.gid_len[g] = (uint8_t)k;
    bb_st.gid_known[g] = 1;
    if (bb_gid_sig() != before) {
      printf("NORD2 BB: SIM GID%u from CommCenter's read (%u bytes): activation indication again\n", g + 1, k);
      bb_activation_indicate();
    }
  }
  uint8_t sdu[7];
  sdu[0] = 0; nbq_wr16(sdu + 1, u.txn); nbq_wr16(sdu + 3, u.message); nbq_wr16(sdu + 5, 0);
  bb_reply(u.channel, NBQ_SVC_UIM, u.cid, sdu, error, bb_tlv, t);
  // 0277n: CommCenter has read EF IMSI: its SIM goes ready now, the held activation indication can follow (bb_act_check)
  if (!error && u.r.file == 0x6f07 && !u.r.record_read && u.message != NBM_UIM_GET_FILE_ATTRIBUTES && got) {
    if (!bb_cc_imsi_ms) bb_cc_imsi_ms = bb_now_ms() ? bb_now_ms() : 1u;
  }
  static uint32_t logged;
  if (logged++ < 64 || (error && logged < 512)) {
    char sized[32] = "";
    if (u.phase == 2) snprintf(sized, sizeof sized, " (whole: %u)", (unsigned)u.sized);
    printf("NORD2 BB: UIM %s file 0x%04x len %u%s -> %s sw %02x%02x, %u bytes, err 0x%x\n",
           u.message == NBM_UIM_GET_FILE_ATTRIBUTES ? "file attributes" : u.r.record_read ? "read record" : "read binary", u.r.file,
           u.r.length, sized, crsm ? "CRSM" : final == NORD2_AT_FINAL_TIMEOUT ? "AT timeout" : "AT error", sw1, sw2, got, error);
  }
}

static int bb_uim(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t n, uint32_t *error)
{
  const uint32_t message = nbq_rd16(sdu + 3), room = sizeof bb_tlv;
  uint32_t t = 0, len = 0;
  *error = 0;
  switch (message) {
  case NBM_UIM_GET_CARD_STATUS:
    t = nbm_uim_card_status(&bb_st, bb_tlv, room);
    break;
  case NBM_UIM_EVENT_REGISTRATION: {
    const uint8_t *v = nbq_tlv(sdu + 7, nbq_rd16(sdu + 5), 0x01, &len);
    uint8_t mask[4] = { 0, 0, 0, 0 };
    if (v && len >= 4) memcpy(mask, v, 4);
    t = nbm_tlv(bb_tlv, room, 0x10, mask, 4);
    break;
  }
  case NBM_UIM_READ_TRANSPARENT:
  case NBM_UIM_READ_RECORD:
  case NBM_UIM_GET_FILE_ATTRIBUTES:    // 0277f: AT+CRSM GET RESPONSE
    return bb_uim_read_start(qmux, cid, sdu, n, error);
  default:
    return BB_UNHANDLED;
  }
  bb_reply(qmux, NBQ_SVC_UIM, cid, sdu, 0, bb_tlv, t);
  return BB_ANSWERED;
}

// Indication registrations and report configuration: accepted without action (n2bb bit 7 turns this off). 0x555b on NAS/WDS is
// Apple's vendor reporting the reference also accepts. 0277f adds, as "set": settings, subscription bindings and data-port bindings that
// ABM sends at every boot and that have nothing to change on this modem (the data path is phase B), all of them requests that carry
// their value and expect only the result TLV:
//   DMS  0x004b Set Time, 0x0054 bind subscription, 0x555c sendSmartDataMode (libCommCenterMCommandDrivers QMIBasebandSettingsDriver)
//   NAS  0x0033 Set System Selection Preference, 0x0045 Bind Subscription, 0x006c Config Signal Info v2
//   UIM  0x002a Refresh Register
//   WMS  0x0032 Set Routes, 0x0038/0x0039 retry period/interval, 0x003a DC disconnect timer, 0x0047 Indication Register,
//        0x004c Bind Subscription
//   VOICE 0x0044, PBM 0x001a bind subscription; DSD 0x0025 System Status Change (indication registration)
//   0277g: WMS 0x003c/0x003d Set Broadcast Activation/Config (cell broadcast), NAS 0x5557 QMICellMonitorCommandDriver's LASD status hint
//   QOS  0x0001 event report, 0x002a bind subscription, 0x002b Bind Mux Data Port; WDS 0x00a2 Bind Mux Data Port, 0x004d Set IP Family
//   COEX 0x0020 indication registration
//   BSP (libBasebandCommandDriversQMI antenna::QMICommandDriver): 0x000a/0x0012 e75AccessoryStatusRF/ARTD, 0xe000 txPowerCappingEnable,
//        0xe001/0xe01a transmitIndicationEnable, 0xe010 txPowerConfiguration, 0xe020 ppmEnable, 0xe01c workoutInfo; 0x000b, 0xe00f,
//        0xe012 (one flag), 0xd005, 0x7701 (thermal reporting period). NOT 0x7702: that is CommCenter's thermal sensor query, whose
//        answer must carry TLVs 0x11 sensors, 0x12 filtered and 0x13 raw values (u32 arrays); 0277f's bare success made it log
//        "Couldn't find sensors TLV" and request a hard reset "Processing Temperature Report Failed" (MTBF) right after every boot: the
//        1 s hard-reset loop of q10bt/q10bu. Unsupported, it logs "Querying sensors failed" and goes on (q10bs, 0277e).
//   0277j: NAS 0x5566, the preferred PLMN list push (pnp.qmi "Preferred PLMN List push failed ... NOT_SUPPORTED", 160 times in q10bw)
//   0277h: WDS 0x00c8 AttachAPN push (QMIAttachApnCommandDriver "Sending 3GPP APN": unsupported, CommCenter reset the baseband with
//        "Resetting baseband after Failure to Push"); MFSE 0x2c/0x0800 RFS sync indication registration ("Failed to enable RFS Sync
//        indication")
//   private 0xe7 0x0001/0x0004 (flags), 0xe4 0x1000/0x1002 (trace configuration)
// Gets whose answer layout is not known stay unhandled.
struct bb_accept_entry { uint8_t service; uint16_t message; uint8_t setting; };
static const bb_accept_entry bb_accept[] = {
  { NBQ_SVC_DMS, 0x0001, 0 }, { NBQ_SVC_NAS, 0x0002, 0 }, { NBQ_SVC_NAS, 0x0003, 0 }, { NBQ_SVC_NAS, 0x0050, 0 }, { NBQ_SVC_NAS, 0x555b, 0 },
  { NBQ_SVC_WDS, 0x0001, 0 }, { NBQ_SVC_WDS, 0x0003, 0 }, { NBQ_SVC_WDS, 0x555b, 0 }, { NBQ_SVC_WMS, 0x0001, 0 }, { NBQ_SVC_VOICE, 0x0003, 0 },
  { NBQ_SVC_PBM, 0x0001, 0 }, { NBQ_SVC_CAT2, 0x0001, 0 }, { 0x10, 0x0021, 0 },
  { NBQ_SVC_DMS, 0x004b, 1 }, { NBQ_SVC_DMS, 0x0054, 1 }, { NBQ_SVC_DMS, 0x555c, 1 },
  { NBQ_SVC_NAS, 0x0033, 1 }, { NBQ_SVC_NAS, 0x0045, 1 }, { NBQ_SVC_NAS, 0x006c, 1 },
  { NBQ_SVC_UIM, 0x002a, 1 },
  { NBQ_SVC_WMS, 0x0032, 1 }, { NBQ_SVC_WMS, 0x0038, 1 }, { NBQ_SVC_WMS, 0x0039, 1 }, { NBQ_SVC_WMS, 0x003a, 1 }, { NBQ_SVC_WMS, 0x0047, 0 },
  { NBQ_SVC_WMS, 0x004c, 1 }, { NBQ_SVC_VOICE, 0x0044, 1 }, { NBQ_SVC_PBM, 0x001a, 1 }, { 0x2a, 0x0025, 0 },
  { NBQ_SVC_WMS, 0x003c, 1 }, { NBQ_SVC_WMS, 0x003d, 1 }, { NBQ_SVC_NAS, 0x5557, 1 }, { NBQ_SVC_NAS, 0x5566, 1 },
  { NBQ_SVC_QOS, 0x0001, 0 }, { NBQ_SVC_QOS, 0x002a, 1 }, { NBQ_SVC_QOS, 0x002b, 1 }, { NBQ_SVC_WDS, 0x00a2, 1 }, { NBQ_SVC_WDS, 0x004d, 1 },
  { 0x22, 0x0020, 0 },
  { NBQ_SVC_BSP, 0x000a, 1 }, { NBQ_SVC_BSP, 0x0012, 1 }, { NBQ_SVC_BSP, 0xe000, 1 }, { NBQ_SVC_BSP, 0xe001, 1 }, { NBQ_SVC_BSP, 0xe01a, 1 },
  { NBQ_SVC_BSP, 0xe010, 1 }, { NBQ_SVC_BSP, 0xe020, 1 }, { NBQ_SVC_BSP, 0xe01c, 1 }, { NBQ_SVC_BSP, 0x000b, 1 }, { NBQ_SVC_BSP, 0xe00f, 1 },
  { NBQ_SVC_BSP, 0xe012, 1 }, { NBQ_SVC_BSP, 0xd005, 1 }, { NBQ_SVC_BSP, 0x7701, 1 },
  { NBQ_SVC_WDS, 0x00c8, 1 }, { 0x2c, 0x0800, 0 },
  { 0xe7, 0x0001, 1 }, { 0xe7, 0x0004, 1 }, { 0xe4, 0x1000, 1 }, { 0xe4, 0x1002, 1 },
  { 0xe1, 0x002a, 1 },                  // 0277t: AUDIO play tone (QMICallAudioDriver's end-of-call tone): no baseband tones here
};
static int bb_auto_accept(uint32_t service, uint32_t message)
{
  if (bb_flags & NBB_NO_AUTOACCEPT) return 0;
  for (const auto &a : bb_accept) if (a.service == service && a.message == message) return a.setting ? 2 : 1;
  return 0;
}

// 0277f: WDS and WMS questions with fixed answers: WDS Get Dormancy Status (dormant: no data call here), WMS Get Service Ready Status
// (0x10 not registered for the indication, 0x11 3GPP ready), WMS Get Message Waiting (0x01 an empty list).
static int bb_fixed(uint32_t qmux, uint32_t service, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  const uint32_t message = nbq_rd16(sdu + 3), room = sizeof bb_tlv;
  uint32_t t = 0;
  uint8_t b[4];
  *error = 0;
  if (service == NBQ_SVC_WDS && message == NBM_WDS_GET_DORMANCY_STATUS) {
    t = nbm_tlv_u8(bb_tlv, room, 0x01, 1);
  } else if (service == NBQ_SVC_WMS && message == NBM_WMS_GET_SERVICE_READY_STATUS) {
    t = nbm_tlv_u8(bb_tlv, room, 0x10, 0);
    nbq_wr32(b, 1);
    t += nbm_tlv(bb_tlv + t, room - t, 0x11, b, 4);
  } else if (service == NBQ_SVC_WMS && message == NBM_WMS_GET_MESSAGE_WAITING) {
    t = nbm_tlv_u8(bb_tlv, room, 0x01, 0);
  } else {
    return BB_UNHANDLED;
  }
  bb_reply(qmux, service, cid, sdu, 0, bb_tlv, t);
  return BB_ANSWERED;
}

// 0277i: PDS for locationd (CLQmiPdsManager asks for a baseband reset on any PDS error, see the header): success to every request,
// Get GPS Service State with its mandatory TLV 0x01 (the state Set GPS Service State last asked for; no position ever comes, the AP's
// own location sources stay). n2bb bit 14 restores NOT_SUPPORTED.
static int bb_pds(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if (bb_flags & NBB_PDS_UNSUPPORTED) return BB_UNHANDLED;
  const uint32_t message = nbq_rd16(sdu + 3);
  uint32_t t = 0, len = 0;
  *error = 0;
  if (message == NBM_PDS_GET_GPS_SERVICE_STATE) {
    t = nbm_pds_gps_service_state(bb_pds_gps_on, bb_tlv, sizeof bb_tlv);
  } else if (message == NBM_PDS_SET_GPS_SERVICE_STATE) {
    const uint8_t *v = nbq_tlv(sdu + 7, nbq_rd16(sdu + 5), 0x01, &len);
    if (v && len >= 1) bb_pds_gps_on = v[0] != 0;
  }
  bb_reply(qmux, NBM_SVC_PDS, cid, sdu, 0, bb_tlv, t);
  bb_pds_answers++;
  return BB_ANSWERED;
}

// 0277i: MFSE RFS sync (QMIRFSCommandDriver::sync, after the indication of bb_rfs_indicate or ABM's periodic sync): the same sync
// token every time, i.e. "no new data" (n2bb bit 15 leaves it unhandled)
static int bb_mfse(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if ((bb_flags & NBB_NO_RFS_SYNC) || nbq_rd16(sdu + 3) != NBM_MFSE_RFS_SYNC) return BB_UNHANDLED;
  *error = 0;
  const uint32_t t = nbm_mfse_sync_answer(0, bb_tlv, sizeof bb_tlv);
  bb_reply(qmux, NBM_SVC_MFSE, cid, sdu, 0, bb_tlv, t);
  if (bb_reset_phase) bb_rfs_synced = 1;
  if (bb_rfs_syncs++ < 24) printf("NORD2 BB: MFSE RFS sync request #%u answered: same token (no new data)\n", bb_rfs_syncs);
  return BB_ANSWERED;
}

// ------------------------------------------------------------------ 0277l: cellular data (bb_lock held)
static bb_wds_call *bb_wds_for(uint32_t qmux, uint32_t cid)
{
  const int i = nbq_client_find(&bb_qmux, qmux, NBQ_SVC_WDS, cid);
  if (i < 0) return nullptr;
  bb_wds_call &c = bb_wdsq[i];
  if (!c.used || c.channel != qmux || c.cid != cid) c = { 1, (uint8_t)qmux, (uint8_t)cid, 0, 4, 0, 0, 0, 0, 0 };
  return &c;
}
static uint32_t bb_pdn_signature(const struct nord2_pdn_info &p)
{
  uint32_t h = bb_hash(2166136261u, &p.up, 4);
  return p.up ? bb_hash(bb_hash(h, &p.ip, 4), p.dns, 8) : h;
}
// the call's interface gets the downlink; with no call left nothing does
static void bb_wds_route(void)
{
  int pdp = -1;
  for (const auto &c : bb_wdsq) if (c.used && c.connected) { pdp = c.mux ? c.mux - 1 : 0; break; }
  nord2_pdn_route(pdp);
}
static void bb_wds_send_status(const bb_wds_call &c, uint8_t reconfigure, uint16_t end_reason)
{
  const uint32_t t = nbm_wds_pkt_status_ind(c.connected, reconfigure, end_reason, bb_tlv, sizeof bb_tlv);
  const uint32_t n = nbq_indication(NBM_WDS_PKT_SRVC_STATUS, NBQ_SVC_WDS, c.cid, bb_tlv, t, bb_out, sizeof bb_out);
  if (n) bb_push(bb_qmux_channel(c.channel), bb_out, n);
}
// the answer of Start Network (now, or when the PDN came / BB_WDS_WAIT_MS passed); sdu: the request's header (txn, message)
static void bb_wds_start_answer(bb_wds_call &c, const uint8_t *sdu, bool timed_out)
{
  struct nord2_pdn_info p;
  const bool up = nord2_pdn_get(&p) == 0;
  uint32_t t = 0, error = 0;
  c.pending = 0;
  if (up) {
    c.connected = 1;
    c.handle = 0x4e320000u | (uint32_t)(&c - bb_wdsq) << 4 | (++bb_wds_adopted & 0xfu);
    uint8_t h[4];
    nbq_wr32(h, c.handle);
    t = nbm_tlv(bb_tlv, sizeof bb_tlv, 0x01, h, 4);
  } else {
    error = NBM_QMI_ERR_CALL_FAILED;
    t = nbm_wds_call_failed(1, 6, 26, bb_tlv, sizeof bb_tlv);       // unspecified; 3GPP cause 26 insufficient resources: retry later
    bb_wds_failed++;
  }
  bb_reply(c.channel, NBQ_SVC_WDS, c.cid, sdu, error, bb_tlv, t);
  if (up) {
    bb_wds_send_status(c, 0, 0);
    bb_wds_route();
  }
  printf("NORD2 BB: WDS start network on mux %u (pdp_ip%d): %s%s, PDN %u.%u.%u.%u mtu %u\n", c.mux, c.mux ? c.mux - 1 : 0,
         up ? "adopted the kernel's PDN" : "CALL FAILED, no PDN", timed_out ? " (after waiting)" : "", up ? p.ip >> 24 : 0,
         up ? p.ip >> 16 & 0xff : 0, up ? p.ip >> 8 & 0xff : 0, up ? p.ip & 0xff : 0, up ? p.mtu : 0);
}
// 0277m: the PDN of cid 1 for CommCenter's Start Network, now (q10ca: the boot script's AT+CGACT=1,1 came 113 s after the modem start,
// long after a waiting call would have failed). The AT queue sends these one after the other; sweet_amfi.c's AT parser takes the
// address (+EIF / +CGPADDR) and the DNS and MTU (+CGCONTRDP) from the replies as from the boot script's, and NORD2_BB_EV_PDN follows.
// The replies are never logged (only the final codes). n2bb bit 21: no bring-up (the call waits for the boot script's PDN).
static void bb_pdn_up_done(void *ctx, int final, const char *, unsigned)
{
  static const char *const name[BB_PDN_UP_STEPS] = { "AT+CGDCONT=1", "AT+CGACT=1,1", "AT+CGPADDR=1", "AT+CGCONTRDP=1" };
  const uint32_t step = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx)) & 0xffu;
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  if (final != NORD2_AT_FINAL_OK) bb_pdn_up_fail++;
  if (bb_pdn_up_runs <= 8 && step < BB_PDN_UP_STEPS)
    printf("NORD2 BB: PDN bring-up #%u: %s -> %s\n", bb_pdn_up_runs, name[step],
           final == NORD2_AT_FINAL_OK ? "OK" : final == NORD2_AT_FINAL_ERROR ? "ERROR" : "timeout");
  IOLockUnlock(bb_lock);
  if (step == 1 || step + 1 == BB_PDN_UP_STEPS) bb_refresh_soon(50);   // the address, then the DNS: answer a waiting call
}
static void bb_pdn_bring_up(const char *apn)
{
  if ((bb_flags & NBB_NO_PDN_UP) || !nord2_pdn_owner) return;
  const uint32_t now = bb_now_ms();
  if (bb_pdn_up_ms && now - bb_pdn_up_ms < BB_WDS_WAIT_MS) return;      // one bring-up per waiting period
  if (!nbm_pdn_define(bb_pdn_up_cmd, sizeof bb_pdn_up_cmd - 4u, apn)) return;     // nord2_at_query: < 61 characters
  bb_pdn_up_ms = now ? now : 1u;
  bb_pdn_up_runs++;
  static const char *const rest[BB_PDN_UP_STEPS] = { nullptr, "AT+CGACT=1,1", "AT+CGPADDR=1", "AT+CGCONTRDP=1" };
  int e = 0;
  uint32_t queued = 0;
  for (uint32_t i = 0; i < BB_PDN_UP_STEPS && !e; i++) {
    e = nord2_at_query(i ? rest[i] : bb_pdn_up_cmd, bb_pdn_up_done, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    queued += !e;
  }
  if (bb_pdn_up_runs <= 8)
    printf("NORD2 BB: PDN bring-up #%u for CommCenter's call (APN \"%s\"): %u of %u AT commands queued\n", bb_pdn_up_runs, apn, queued,
           BB_PDN_UP_STEPS);
}
// 0277p: WDS profiles (see the header)
struct bb_profile { uint8_t used, pdp, apn_len, persistent; char apn[64]; };
static bb_profile bb_profiles[17];
static uint32_t bb_profile_creates, bb_profile_ops, bb_profile_c7;
static void bb_profile_seed()
{
  if (bb_profiles[1].used) return;
  struct nord2_pdn_info p;
  (void)nord2_pdn_get(&p);
  const char *a = p.apn ? p.apn : "internet.it";
  bb_profile &f = bb_profiles[1];
  f.used = 1; f.pdp = 0; f.persistent = 1;
  uint32_t k = 0;
  for (; a[k] && k < sizeof f.apn; k++) f.apn[k] = a[k];
  f.apn_len = (uint8_t)k;
}
static void bb_profile_store(bb_profile &f, const uint8_t *tlvs, uint32_t tl)
{
  uint32_t len = 0;
  const uint8_t *v;
  if ((v = nbq_tlv(tlvs, tl, 0x14, &len))) {
    if (len > sizeof f.apn) len = sizeof f.apn;
    for (uint32_t k = 0; k < len; k++) f.apn[k] = (char)v[k];
    f.apn_len = (uint8_t)len;
  }
  if ((v = nbq_tlv(tlvs, tl, 0x11, &len)) && len >= 1) f.pdp = v[0];
  if ((v = nbq_tlv(tlvs, tl, 0x8f, &len)) && len >= 1) f.persistent = v[0];
}
static uint32_t bb_profile_settings(const bb_profile &f, uint8_t *t, uint32_t room)
{
  uint32_t w = nbm_tlv(t, room, 0x10, "", 0);                       // profile name
  w += nbm_tlv_u8(t + w, room - w, 0x11, f.pdp);                      // PDP type
  w += nbm_tlv(t + w, room - w, 0x14, f.apn, f.apn_len);              // APN
  return w;
}
// the profile index of a request: TLV 0x01 {type, index}
static int bb_profile_index(const uint8_t *tlvs, uint32_t tl)
{
  uint32_t len = 0;
  const uint8_t *v = nbq_tlv(tlvs, tl, 0x01, &len);
  if (!v || len < 2) return -1;
  return v[1] >= 1 && v[1] < 17 ? v[1] : 0;
}
static int bb_wds_profile(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if (bb_flags & (NBB_NO_DATA | NBB_NO_PROFILES)) return BB_UNHANDLED;
  const uint32_t message = nbq_rd16(sdu + 3), tl = nbq_rd16(sdu + 5);
  const uint8_t *tlvs = sdu + 7;
  uint32_t t = 0, room = sizeof bb_tlv;
  uint8_t b[2];
  int i;
  *error = 0;
  bb_profile_seed();
  switch (message) {
  case 0x0027: {                                                      // Create Profile
    for (i = 2; i < 17 && bb_profiles[i].used; i++) {}
    if (i == 17) i = 2 + (int)(bb_profile_creates % 15);              // full: reuse (CommCenter's are not persistent)
    bb_profile &f = bb_profiles[i];
    f = bb_profiles[1];
    bb_profile_store(f, tlvs, tl);
    f.used = 1;
    b[0] = 0; b[1] = (uint8_t)i;
    t = nbm_tlv(bb_tlv, room, 0x01, b, 2);
    if (++bb_profile_creates <= 12) printf("NORD2 BB: WDS create profile -> #%d, APN length %u, PDP type %u\n", i, f.apn_len, f.pdp);
    break;
  }
  case 0x0028:                                                        // Modify Profile Settings
    i = bb_profile_index(tlvs, tl);
    if (i <= 0 || !bb_profiles[i].used) { *error = 0x0029; break; }   // QMI_ERR_INVALID_PROFILE
    bb_profile_store(bb_profiles[i], tlvs, tl);
    break;
  case 0x00c7:                                                        // 0277q: profile id 0x01 + APN 0x14 + PDP type 0x11 (q10ce: 6867
    i = bb_profile_index(tlvs, tl);                                   // INVALID_QMI_CMD answers in 88 s); stored, success
    if (i > 0) { bb_profiles[i].used = 1; bb_profile_store(bb_profiles[i], tlvs, tl); }
    if (++bb_profile_c7 <= 6) printf("NORD2 BB: WDS 0x00c7 on profile #%d: APN length %u, PDP type %u\n", i, i > 0 ? (unsigned)bb_profiles[i].apn_len : 0u, i > 0 ? (unsigned)bb_profiles[i].pdp : 0u);
    break;
  case 0x0029:                                                        // Delete Profile
    i = bb_profile_index(tlvs, tl);
    if (i > 1) bb_profiles[i].used = 0;
    break;
  case 0x002a: {                                                      // Get Profile List
    uint32_t n = 0, k = 1;
    uint8_t list[1 + 16 * 3];
    for (i = 1; i < 17; i++) if (bb_profiles[i].used) { list[k++] = 0; list[k++] = (uint8_t)i; list[k++] = 0; n++; }
    list[0] = (uint8_t)n;
    t = nbm_tlv(bb_tlv, room, 0x01, list, k);
    break;
  }
  case 0x002b:                                                        // Get Profile Settings
    i = bb_profile_index(tlvs, tl);
    if (i <= 0 || !bb_profiles[i].used) { *error = 0x0029; break; }
    t = bb_profile_settings(bb_profiles[i], bb_tlv, room);
    break;
  case 0x002c:                                                        // Get Default Settings
    t = bb_profile_settings(bb_profiles[1], bb_tlv, room);
    break;
  case 0x0049:                                                        // Get Default Profile Number
    t = nbm_tlv_u8(bb_tlv, room, 0x01, 1);
    break;
  case 0x004a:                                                        // Set Default Profile Number
    break;
  default:
    return BB_UNHANDLED;
  }
  bb_profile_ops++;
  bb_reply(qmux, NBQ_SVC_WDS, cid, sdu, *error, bb_tlv, t);
  return BB_ANSWERED;
}
static int bb_wds(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if (bb_wds_profile(qmux, cid, sdu, error) == BB_ANSWERED) return BB_ANSWERED;
  if (bb_flags & NBB_NO_DATA) return BB_UNHANDLED;
  const uint32_t message = nbq_rd16(sdu + 3), tl = nbq_rd16(sdu + 5);
  const uint8_t *tlvs = sdu + 7, *v;
  uint32_t len = 0, t = 0;
  bb_wds_call *c = bb_wds_for(qmux, cid);
  struct nord2_pdn_info p;
  *error = 0;
  if (!c) return BB_UNHANDLED;
  switch (message) {
  case NBM_WDS_BIND_MUX_DATA_PORT:
    if ((v = nbq_tlv(tlvs, tl, 0x11, &len)) && len >= 1) c->mux = v[0];
    break;
  case NBM_WDS_SET_IP_FAMILY:
    if ((v = nbq_tlv(tlvs, tl, 0x01, &len)) && len >= 1) c->family = v[0];
    break;
  case NBM_WDS_START_NETWORK: {
    bb_wds_starts++;
    uint8_t family = c->family;
    if ((v = nbq_tlv(tlvs, tl, 0x19, &len)) && len >= 1) family = v[0];
    const uint8_t *apn = nbq_tlv(tlvs, tl, 0x14, &len);
    const uint32_t apn_len = apn ? len : 0;
    (void)nord2_pdn_get(&p);
    char shown[40];
    uint32_t k = 0;
    for (; k < apn_len && k + 1 < sizeof shown; k++) shown[k] = apn[k] >= 32 && apn[k] < 127 ? (char)apn[k] : '?';
    shown[k] = 0;
    if (family == 6) {
      *error = NBM_QMI_ERR_CALL_FAILED;
      t = nbm_wds_call_failed(1, 6, 50, bb_tlv, sizeof bb_tlv);      // 3GPP cause 50: PDP type IPv4 only allowed
    } else if (apn_len && !nbm_apn_eq(p.apn, apn, apn_len)) {
      *error = NBM_QMI_ERR_CALL_FAILED;
      t = nbm_wds_call_failed(1, 6, 27, bb_tlv, sizeof bb_tlv);      // 3GPP cause 27: missing or unknown APN (one PDN here)
    } else if (!c->connected && nord2_pdn_get(&p) != 0) {
      c->pending = 1;                                                 // wait for the PDN (bb_wds_poll), 0277m: brought up now
      c->pending_txn = (uint16_t)nbq_rd16(sdu + 1);
      c->pending_ms = bb_now_ms();
      printf("NORD2 BB: WDS start network on mux %u, APN \"%s\": waiting for the kernel's PDN\n", c->mux, shown);
      bb_pdn_bring_up(p.apn ? p.apn : "internet.it");
      bb_refresh_soon(1000);
      return BB_DEFERRED;
    } else {
      bb_wds_start_answer(*c, sdu, false);
      return BB_ANSWERED;
    }
    bb_wds_failed++;
    if (bb_wds_failed <= 24)
      printf("NORD2 BB: WDS start network on mux %u, APN \"%s\", family %u: refused (%s)\n", c->mux, shown, family,
             family == 6 ? "IPv4 only" : "not the kernel's APN");
    break;
  }
  case NBM_WDS_STOP_NETWORK:
    bb_wds_stops++;
    if (c->connected) {
      c->connected = 0;
      bb_reply(qmux, NBQ_SVC_WDS, cid, sdu, 0, nullptr, 0);
      bb_wds_send_status(*c, 0, 2);                                   // client end
      bb_wds_route();
      printf("NORD2 BB: WDS stop network on mux %u\n", c->mux);
      return BB_ANSWERED;
    }
    break;
  case NBM_WDS_PKT_SRVC_STATUS:
    t = nbm_tlv_u8(bb_tlv, sizeof bb_tlv, 0x01, c->connected ? 2 : 1);
    break;
  case NBM_WDS_GET_RUNTIME_SETTINGS:
    if (!c->connected || nord2_pdn_get(&p) != 0) { *error = NBM_QMI_ERR_OUT_OF_CALL; break; }
    t = nbm_wds_runtime(p.ip, p.dns[0], p.dns[1], p.mtu, p.apn, bb_tlv, sizeof bb_tlv);
    break;
  case NBM_WDS_GET_LTE_ATTACH_PARAMS:
    (void)nord2_pdn_get(&p);
    t = nbm_wds_lte_attach(p.apn, bb_tlv, sizeof bb_tlv);
    break;
  default:
    return BB_UNHANDLED;
  }
  bb_reply(qmux, NBQ_SVC_WDS, cid, sdu, *error, bb_tlv, t);
  return BB_ANSWERED;
}
// PDN changes (NORD2_BB_EV_PDN, every snapshot) and Start Network requests waiting for the PDN
static void bb_wds_poll(void)
{
  if (bb_flags & NBB_NO_DATA) return;
  struct nord2_pdn_info p;
  const bool up = nord2_pdn_get(&p) == 0;
  const uint32_t sig = bb_pdn_signature(p), now = bb_now_ms();
  bool released = false;
  for (auto &c : bb_wdsq) {                                            // clients CommCenter released (CTL) end their calls
    if (c.used && nbq_client_find(&bb_qmux, c.channel, NBQ_SVC_WDS, c.cid) < 0) { released |= c.connected; c.used = 0; }
  }
  if (released) bb_wds_route();
  if (!up) bb_pdn_seen_ms = 0;
  else if (!bb_pdn_seen_ms) bb_pdn_seen_ms = now ? now : 1u;
  bool waiting = false;
  for (auto &c : bb_wdsq) {
    if (!c.used || !c.pending) continue;
    const bool expired = now - c.pending_ms >= BB_WDS_WAIT_MS;
    // 0277m: an address without DNS yet (+CGCONTRDP follows +CGACT): give the DNS up to 3 s, CommCenter asks for it right after
    if (expired || (up && (p.dns[0] || now - bb_pdn_seen_ms >= BB_PDN_DNS_WAIT_MS))) {
      uint8_t sdu[7];
      sdu[0] = 0; nbq_wr16(sdu + 1, c.pending_txn); nbq_wr16(sdu + 3, NBM_WDS_START_NETWORK); nbq_wr16(sdu + 5, 0);
      bb_wds_start_answer(c, sdu, true);
    } else {
      waiting = true;
    }
  }
  if (waiting) bb_refresh_soon(1000);
  if (sig == bb_pdn_sig) return;
  const bool first = bb_pdn_sig == 0;
  bb_pdn_sig = sig;
  if (first) return;
  for (auto &c : bb_wdsq) {                                            // connected calls follow the PDN
    if (!c.used || !c.connected) continue;
    if (!up) { c.connected = 0; bb_wds_send_status(c, 0, 3); } else bb_wds_send_status(c, 1, 0);
  }
  if (!up) bb_wds_route();
  printf("NORD2 BB: kernel PDN %s: WDS calls told\n", up ? "changed address" : "lost");
}

// ------------------------------------------------------------------ 0277r: SMS (WMS) and voice calls (VOICE) from the AT client
// See the header. Deferred QMI answers (bb_cs_req) wait for AT results like UIM reads; one thread call (bb_cs_call) runs the SMS ack
// fallback and the +CLCC polls while calls exist. Numbers and message bodies are never logged: a number is "length + last 2 digits".
#define BB_CS_REQS 8u
#define BB_SMS_STORE 8u
#define BB_SMS_DATA 184u
#define BB_SMS_ACK_MS 4000u              // no Send Ack from CommCenter by then: the bridge acknowledges the MT SMS to the modem
#define BB_SMS_SENDQ 4u
#define BB_CLCC_MS 1000u                 // +CLCC poll period while calls exist
#define BB_DIAL_TRIES 4u                 // +CLCC polls (300 ms apart) that look for the new MO call after ATD's OK
enum : uint32_t {
  BB_OP_SETUP = 1, BB_OP_CMGS, BB_OP_CNMA, BB_OP_NACK, BB_OP_GET_SMSC, BB_OP_SET_SMSC, BB_OP_CMGR, BB_OP_CMGD, BB_OP_DIAL, BB_OP_CLCC,
  BB_OP_ANSWER, BB_OP_END, BB_OP_MANAGE, BB_OP_DTMF, BB_OP_DTMF_STOP, BB_OP_EAIC, BB_OP_VTS,
};
struct bb_cs_req { uint8_t used, channel, cid, service, call_id; uint16_t txn, message; uint32_t gen, ms; char alt[24]; };
static bb_cs_req bb_csq[BB_CS_REQS];
static uint32_t bb_cs_gen;
static thread_call_t bb_cs_call;
static uint8_t bb_cs_armed, bb_cs_setup_state;   // setup: 0 to do, 1 queued
static uint32_t bb_cs_armed_at, bb_cs_setups;
// SMS
struct bb_sms_route { uint8_t type, cls, storage, action; };
struct bb_sms_msg { uint8_t used, storage, tag, pending; uint16_t len; uint8_t data[BB_SMS_DATA]; };
struct bb_sms_send { uint8_t slot; char cmd[16]; char pdu[NORD2_AT_PDU_MAX + 1]; };
struct bb_wms_client { uint8_t on, channel, cid; };
static bb_sms_route bb_sms_routes[8];
static uint32_t bb_sms_nroutes;
static uint8_t bb_sms_report_status, bb_wms_reg[5], bb_cb_active, bb_cb_cfg[160];
static uint32_t bb_cb_cfg_len;
static bb_wms_client bb_wms_rep[NBQ_MAX_CLIENTS];
static bb_sms_msg bb_sms_store[BB_SMS_STORE];
static bb_sms_send bb_sms_sendq[BB_SMS_SENDQ];
static uint32_t bb_sms_send_head, bb_sms_send_count;
static uint8_t bb_sms_send_busy, bb_sms_expect, bb_sms_cnma_style, bb_sms_ack_pending;
static int8_t bb_sms_csms = -1;                  // AT+CSMS=1 answer: 1 the modem waits for AT+CNMA, 0 it acknowledges itself
static uint32_t bb_sms_expect_len, bb_sms_ack_txn, bb_sms_ack_ms, bb_sms_txn;
static char bb_sms_smsc[24];
static unsigned bb_sms_smsc_type;
static uint32_t bb_sms_mo, bb_sms_mo_fail, bb_sms_mt_n, bb_sms_reports, bb_sms_acks, bb_sms_nacks, bb_sms_ack_timeouts, bb_sms_held, bb_sms_cbm,
    bb_sms_bad_pdu;
// calls (index = the modem's call id 1..7 = the QMI call id)
struct bb_call { uint8_t used, state, dir, mpty, mode, pi, local_end; int cause; char number[NBM_QLEN]; };
static bb_call bb_calls[8];
static uint8_t bb_clcc_inflight, bb_clcc_again, bb_dial_active, bb_dial_atd_ok, bb_dial_slot, bb_dial_tries, bb_dial_before, bb_evts_ok = 1;
static int bb_ecpi_mode = -1;
static uint16_t bb_call_hint;                     // QMI end reason from a BUSY / NO ANSWER result
static uint32_t bb_clcc_due, bb_v_dials, bb_v_dial_fail, bb_v_incoming, bb_v_answers, bb_v_ends, bb_v_manage, bb_v_dtmf,
    bb_v_polls, bb_v_inds, bb_v_eaic, bb_v_speech;

static void bb_cs_at_done(void *ctx, int final, const char *text, unsigned len);
static void *bb_cs_ctx(uint32_t op, uint32_t aux, uint32_t gen)
{
  return reinterpret_cast<void *>(static_cast<uintptr_t>((uint64_t)gen << 32 | (uint64_t)(aux & 0xffffffu) << 8 | (op & 0xffu)));
}
static int bb_cs_at(const char *cmd, uint32_t op, uint32_t aux, uint32_t gen = 0)
{
  return nord2_at_query(cmd, bb_cs_at_done, bb_cs_ctx(op, aux, gen));
}
// the request waits for AT: its slot (-1 none free)
static int bb_cs_hold(uint32_t qmux, uint32_t service, uint32_t cid, const uint8_t *sdu, uint8_t call_id = 0)
{
  for (uint32_t i = 0; i < BB_CS_REQS; i++) {
    if (bb_csq[i].used) continue;
    bb_cs_gen = (bb_cs_gen + 1u) ? bb_cs_gen + 1u : 1u;
    bb_csq[i] = { 1, (uint8_t)qmux, (uint8_t)cid, (uint8_t)service, call_id, (uint16_t)nbq_rd16(sdu + 1), (uint16_t)nbq_rd16(sdu + 3),
                  bb_cs_gen, bb_now_ms(), "" };
    return (int)i;
  }
  return -1;
}
static bb_cs_req *bb_cs_req_of(uint32_t slot, uint32_t gen)
{
  return slot < BB_CS_REQS && bb_csq[slot].used && bb_csq[slot].gen == gen ? &bb_csq[slot] : nullptr;
}
static void bb_cs_answer(bb_cs_req &q, uint32_t error, const uint8_t *tlvs, uint32_t t)
{
  q.used = 0;
  if (nbq_client_find(&bb_qmux, q.channel, q.service, q.cid) < 0) return;     // released meanwhile
  uint8_t sdu[7];
  sdu[0] = 0; nbq_wr16(sdu + 1, q.txn); nbq_wr16(sdu + 3, q.message); nbq_wr16(sdu + 5, 0);
  bb_reply(q.channel, q.service, q.cid, sdu, error, tlvs, error ? 0 : t);
}
static uint32_t bb_cs_err(int e) { return e == ENXIO ? NBQ_ERR_INTERNAL : NBQ_ERR_DEVICE_NOT_READY; }
static void bb_cs_arm(uint32_t ms)
{
  if (!bb_cs_call) return;
  const uint32_t at = bb_now_ms() + ms;
  if (bb_cs_armed && (int32_t)(bb_cs_armed_at - at) <= 0) return;
  bb_cs_armed = 1;
  bb_cs_armed_at = at;
  uint64_t deadline = 0;
  clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
  thread_call_enter_delayed(bb_cs_call, deadline);
}
static const char *bb_final_name(int final)
{
  return final == NORD2_AT_FINAL_OK ? "OK" : final == NORD2_AT_FINAL_ERROR ? "ERROR" : "timeout";
}

// The modem side: PDU mode, phase 2+ service (the AP acknowledges MT SMS with AT+CNMA), MT SMS and status reports routed straight to
// the AP (+CMT/+CDS, no storage), the SMSC; caller id, extended RING, call waiting and MediaTek's call progress URCs (+ECPI). Queued
// once a snapshot sees the SIM ready, again after every modem restart (NORD2_BB_EV_READY/STEADY). n2bb bits 25-28.
static const char *const bb_cs_setup_cmd[] = { "AT+CMGF=0", "AT+CSMS=1", "AT+CNMI=1,2,0,1,0", "AT+CSCA?", "AT+CLIP=1", "AT+CRC=1",
                                               "AT+CCWA=1", "AT+ECPI=4294967295" };
static const char *const bb_cnmi_alt[] = { "AT+CNMI=1,2,0,1,0", "AT+CNMI=2,2,0,1,0", "AT+CNMI=1,2,0,0,0", "AT+CNMI=2,1,0,0,0" };
static void bb_cs_setup(void)
{
  if (bb_cs_setup_state || bb_st.sim != NBM_SIM_READY || (bb_flags & NBB_NO_AT) || (nord2_modem_state() & NORD2_MODEM_DEAD)) return;
  if ((bb_flags & NBB_NO_SMS) && (bb_flags & NBB_NO_VOICE)) return;
  bb_cs_setup_state = 1;
  bb_cs_setups++;
  uint32_t queued = 0, wanted = 0;
  for (uint32_t i = 0; i < sizeof bb_cs_setup_cmd / sizeof bb_cs_setup_cmd[0]; i++) {
    if (i < 4 && (bb_flags & NBB_NO_SMS)) continue;
    if (i >= 4 && (bb_flags & NBB_NO_VOICE)) continue;
    if (i == 7 && (bb_flags & NBB_NO_ECPI)) continue;
    const char *cmd = i == 1 && (bb_flags & NBB_SMS_MODEM_ACK) ? "AT+CSMS=0" : bb_cs_setup_cmd[i];
    queued += bb_cs_at(cmd, BB_OP_SETUP, i) == 0;
    wanted++;
  }
  if (queued < wanted) bb_cs_setup_state = 0;                     // the AT queue was full: all again at the next snapshot
  printf("NORD2 BB: SMS/VOICE setup #%u: %u AT commands queued (SMS %s, voice %s, ECPI %s)\n", bb_cs_setups, queued,
         (bb_flags & NBB_NO_SMS) ? "off" : (bb_flags & NBB_SMS_MODEM_ACK) ? "on, modem acks" : "on",
         (bb_flags & NBB_NO_VOICE) ? "off" : "on", (bb_flags & (NBB_NO_ECPI | NBB_NO_VOICE)) ? "off" : "on");
}
// 0277s, from q10ch: the setup went out at SIM ready, before MediaTek's "+EIND: 1" (SMS ready): AT+CMGF/CSMS/CSCA? answered ERROR and
// only AT+CNMI=1,2,0,0,0 (no status reports) was taken. The SMS part is queued again when +EIND reports SMS ready (bit 0), at most 4 times.
static uint32_t bb_sms_ready_setups;
static void bb_sms_ready_setup(void)
{
  if ((bb_flags & (NBB_NO_SMS | NBB_NO_AT)) || bb_sms_ready_setups >= 4) return;
  bb_sms_ready_setups++;
  uint32_t queued = 0;
  for (uint32_t i = 0; i < 4; i++) {
    const char *cmd = i == 1 && (bb_flags & NBB_SMS_MODEM_ACK) ? "AT+CSMS=0" : bb_cs_setup_cmd[i];
    queued += bb_cs_at(cmd, BB_OP_SETUP, i) == 0;
  }
  printf("NORD2 BB: SMS ready (+EIND): SMS setup #%u again, %u AT commands queued\n", bb_sms_ready_setups, queued);
}
static void bb_cs_setup_done(uint32_t aux, int final, const char *text, unsigned len)
{
  const uint32_t i = aux & 0xffu, alt = aux >> 8 & 0xffu;
  const char *cmd = i == 2 ? bb_cnmi_alt[alt < 4 ? alt : 3] : i == 1 && (bb_flags & NBB_SMS_MODEM_ACK) ? "AT+CSMS=0"
                    : i < 8 ? bb_cs_setup_cmd[i] : "?";
  if (i == 1) bb_sms_csms = final == NORD2_AT_FINAL_OK && !(bb_flags & NBB_SMS_MODEM_ACK) ? 1 : 0;
  if (i == 3 && final == NORD2_AT_FINAL_OK) {
    char d[24];
    unsigned type = 0;
    if (nbm_csca(text, len, d, sizeof d, &type)) {
      memcpy(bb_sms_smsc, d, sizeof bb_sms_smsc);
      bb_sms_smsc_type = type;
      printf("NORD2 BB: SMS setup AT+CSCA? -> SMSC cached (%u digits, type %u)\n", (unsigned)strlen(d), type);
      return;
    }
  }
  if (i == 2 && final != NORD2_AT_FINAL_OK && alt + 1u < 4u) {          // another +CNMI the modem may take
    printf("NORD2 BB: SMS setup %s -> %s, trying %s\n", cmd, bb_final_name(final), bb_cnmi_alt[alt + 1u]);
    (void)bb_cs_at(bb_cnmi_alt[alt + 1u], BB_OP_SETUP, 2u | (alt + 1u) << 8);
    return;
  }
  const long code = final == NORD2_AT_FINAL_ERROR ? nbm_at_error_code(text, len) : -1;
  printf("NORD2 BB: %s setup %s -> %s (code %ld)\n", i < 4 ? "SMS" : "VOICE", cmd, bb_final_name(final), code);
}

// ---- SMS: MO (Raw Send -> AT+CMGS, one at a time, 4 waiting)
static void bb_sms_send_next(void)
{
  while (!bb_sms_send_busy && bb_sms_send_count) {
    bb_sms_send &s = bb_sms_sendq[bb_sms_send_head];
    bb_sms_send_head = (bb_sms_send_head + 1u) % BB_SMS_SENDQ;
    bb_sms_send_count--;
    bb_cs_req &q = bb_csq[s.slot];
    if (!q.used) continue;
    const int e = nord2_at_query_pdu(s.cmd, s.pdu, bb_cs_at_done, bb_cs_ctx(BB_OP_CMGS, s.slot, q.gen));
    if (e) {
      bb_cs_answer(q, bb_cs_err(e), nullptr, 0);
      bb_sms_mo_fail++;
      printf("NORD2 BB: SMS send: AT+CMGS not queued (%d)\n", e);
      continue;
    }
    bb_sms_send_busy = 1;
  }
}
static int bb_sms_raw_send(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  uint32_t len = 0;
  const uint8_t *v = nbq_tlv(sdu + 7, nbq_rd16(sdu + 5), 0x01, &len);
  if (!v || len < 3u || 3u + nbq_rd16(v + 1) > len) { *error = NBQ_ERR_MISSING_ARGUMENT; return BB_UNHANDLED; }
  const uint32_t n = nbq_rd16(v + 1);
  if (v[0] != NBM_WMS_FORMAT_GW_PP) {                      // 3GPP2 (CDMA) formats: no CDMA here
    *error = NBQ_ERR_NOT_SUPPORTED;
    bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, *error, nullptr, 0);
    printf("NORD2 BB: SMS send: format %u refused (only GW point-to-point)\n", v[0]);
    return BB_ANSWERED;
  }
  if (bb_sms_send_count == BB_SMS_SENDQ) { *error = NBQ_ERR_DEVICE_NOT_READY; bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, *error, nullptr, 0); return BB_ANSWERED; }
  bb_sms_send &s = bb_sms_sendq[(bb_sms_send_head + bb_sms_send_count) % BB_SMS_SENDQ];
  const int sp = nbm_sms_smsc_part(v + 3, n);
  const uint32_t tpdu = nbm_sms_cmgs(v + 3, n, s.cmd, sizeof s.cmd, s.pdu, sizeof s.pdu);
  if (!tpdu) {
    *error = NBQ_ERR_INVALID_ARG;
    bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, *error, nullptr, 0);
    bb_sms_mo_fail++;
    printf("NORD2 BB: SMS send: %u bytes that are no SMS-SUBMIT (first octet 0x%02x): refused\n", n, n ? v[3] : 0u);
    return BB_ANSWERED;
  }
  const int slot = bb_cs_hold(qmux, NBQ_SVC_WMS, cid, sdu);
  if (slot < 0) { *error = NBQ_ERR_DEVICE_NOT_READY; bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, *error, nullptr, 0); return BB_ANSWERED; }
  s.slot = (uint8_t)slot;
  bb_sms_send_count++;
  bb_sms_mo++;
  printf("NORD2 BB: SMS send #%u: TPDU %u bytes, SMSC %s, %s\n", bb_sms_mo, tpdu, sp > 0 ? "from CommCenter" : "the SIM's (00)",
         bb_sms_send_busy ? "queued behind another" : "AT+CMGS now");
  bb_sms_send_next();
  return BB_DEFERRED;
}
static void bb_sms_send_done(bb_cs_req &q, int final, const char *text, unsigned len)
{
  uint32_t t = 0, error = 0;
  const long mr = final == NORD2_AT_FINAL_OK ? nbm_cmgs_mr(text, len) : -1;
  const long cms = final == NORD2_AT_FINAL_ERROR ? nbm_at_error_code(text, len) : -1;
  if (final == NORD2_AT_FINAL_OK) {
    uint8_t b[2];
    nbq_wr16(b, mr >= 0 ? (uint32_t)mr : 0u);
    t = nbm_tlv(bb_tlv, sizeof bb_tlv, 0x01, b, 2);
  } else {
    t = nbm_wms_send_failure(cms, bb_tlv, sizeof bb_tlv, &error);
    bb_sms_mo_fail++;
  }
  const uint32_t ms = bb_now_ms() - q.ms;
  // the failure TLVs go with the error (bb_cs_answer drops TLVs on errors): answer here
  q.used = 0;
  if (nbq_client_find(&bb_qmux, q.channel, q.service, q.cid) >= 0) {
    uint8_t sdu[7];
    sdu[0] = 0; nbq_wr16(sdu + 1, q.txn); nbq_wr16(sdu + 3, q.message); nbq_wr16(sdu + 5, 0);
    bb_reply(q.channel, q.service, q.cid, sdu, error, bb_tlv, t);
  }
  printf("NORD2 BB: SMS send -> %s (message reference %ld, error code %ld) after %u ms\n", final == NORD2_AT_FINAL_OK ? "sent" :
         final == NORD2_AT_FINAL_ERROR ? "FAILED" : "TIMEOUT", mr, cms, ms);
  bb_sms_send_busy = 0;
  bb_sms_send_next();
}

// ---- SMS: MT (+CMT/+CDS -> Event Report indication, Send Ack -> AT+CNMA)
static uint32_t bb_wms_indicate(const uint8_t *tlvs, uint32_t t)
{
  uint32_t sent = 0;
  if (bb_flags & NBB_NO_IND) return 0;                             // n2bb bit 5: MT messages are held (and acknowledged)
  for (uint32_t i = 0; i < NBQ_MAX_CLIENTS; i++) {
    const struct nbq_client *c = &bb_qmux.client[i];
    const bb_wms_client &w = bb_wms_rep[i];
    if (!c->used || c->service != NBQ_SVC_WMS || !w.on || w.channel != c->channel || w.cid != c->cid) continue;
    const uint32_t n = nbq_indication(NBM_WMS_EVENT_REPORT, NBQ_SVC_WMS, c->cid, tlvs, t, bb_out, sizeof bb_out);
    if (n) { bb_push(bb_qmux_channel(c->channel), bb_out, n); sent++; }
  }
  return sent;
}
// the modem's acknowledgement (only with AT+CSMS=1); MediaTek's inline form first (AT+CNMA=1,2,"0000": RP-ACK with an empty
// SMS-DELIVER-REPORT), then 27.005's bare AT+CNMA
static void bb_sms_modem_ack(bool ok, uint32_t tp_cause, const char *why)
{
  bb_sms_ack_pending = 0;
  if (bb_sms_csms != 1 || (bb_flags & NBB_SMS_MODEM_ACK)) return;
  char cmd[40];
  if (ok) {
    snprintf(cmd, sizeof cmd, bb_sms_cnma_style ? "AT+CNMA" : "AT+CNMA=1,2,\"0000\"");
    bb_sms_acks++;
  } else {
    snprintf(cmd, sizeof cmd, "AT+CNMA=2,3,\"00%02X00\"", tp_cause ? tp_cause & 0xffu : 0xffu);
    bb_sms_nacks++;
  }
  const int e = bb_cs_at(cmd, ok ? BB_OP_CNMA : BB_OP_NACK, bb_sms_cnma_style);
  if (bb_sms_acks + bb_sms_nacks <= 16 || e)
    printf("NORD2 BB: SMS MT %s to the modem (%s)%s\n", ok ? "ack" : "NACK", why, e ? ": AT not queued" : "");
}
static int bb_sms_store_put(const uint8_t *raw, uint32_t n, uint8_t storage, uint8_t tag, uint8_t pending)
{
  if (n > BB_SMS_DATA) return -1;
  for (uint32_t i = 0; i < BB_SMS_STORE; i++) {
    bb_sms_msg &m = bb_sms_store[i];
    if (m.used) continue;
    m.used = 1; m.storage = storage; m.tag = tag; m.pending = pending; m.len = (uint16_t)n;
    memcpy(m.data, raw, n);
    return (int)i;
  }
  return -1;
}
// the route CommCenter set (WMS Set Routes) for this message class, else class "none"; default transfer only (CommCenter acks)
static bb_sms_route bb_sms_route_for(unsigned cls)
{
  for (uint32_t i = 0; i < bb_sms_nroutes; i++) if (bb_sms_routes[i].cls == cls) return bb_sms_routes[i];
  for (uint32_t i = 0; i < bb_sms_nroutes; i++) if (bb_sms_routes[i].cls == 4) return bb_sms_routes[i];
  return { 0, 4, 0xff, 2 };
}
static void bb_sms_mt(const uint8_t *raw, uint32_t n, bool report, bool from_storage)
{
  if (report) bb_sms_reports++;
  else bb_sms_mt_n++;
  const unsigned cls = report ? 4u : nbm_sms_class(raw, n);
  const bb_sms_route r = bb_sms_route_for(cls);
  const bool acks = bb_sms_csms == 1 && !(bb_flags & NBB_SMS_MODEM_ACK) && !from_storage;
  const char *what;
  uint32_t sent = 0;
  if (r.action == 1 && r.storage <= 1) {                   // store and notify: the bridge's store stands for the card / NV
    const int i = bb_sms_store_put(raw, n, r.storage, NBM_WMS_TAG_MT_NOT_READ, 0);
    if (i >= 0) {
      const uint32_t t = nbm_wms_mt_stored(r.storage, (uint32_t)i, bb_tlv, sizeof bb_tlv);
      sent = bb_wms_indicate(bb_tlv, t);
      what = "stored, index sent";
      if (acks) bb_sms_modem_ack(true, 0, "stored");
    } else {
      what = "store full: NACK";
      if (acks) bb_sms_modem_ack(false, 0xd3, "store full");
    }
  } else {
    const bool client_acks = acks && r.action != 3;
    bb_sms_txn = bb_sms_txn + 1u ? bb_sms_txn + 1u : 1u;
    uint32_t off = 0;
    const uint32_t tn = nbm_sms_tpdu(raw, n, &off);             // 0277t: the TPDU only (see nbm_sms_tpdu)
    const uint32_t t = tn ? nbm_wms_mt_transfer(client_acks ? 0u : 1u, bb_sms_txn, raw + off, tn, bb_tlv, sizeof bb_tlv) : 0;
    sent = t ? bb_wms_indicate(bb_tlv, t) : 0;
    if (!sent) {                                           // nobody listens (CommCenter restarting): held until Set Event Report
      const int i = bb_sms_store_put(raw, n, 1, NBM_WMS_TAG_MT_NOT_READ, 1);
      what = i >= 0 ? "no WMS client: held" : "no WMS client and store full: NACK";
      if (i >= 0) bb_sms_held++;
      if (acks) bb_sms_modem_ack(i >= 0, 0xd3, "held for CommCenter");
    } else if (client_acks) {
      what = "transfer route, waiting for Send Ack";
      bb_sms_ack_pending = 1;
      bb_sms_ack_txn = bb_sms_txn;
      bb_sms_ack_ms = bb_now_ms();
      bb_cs_arm(BB_SMS_ACK_MS);
    } else {
      what = "transfer route";
      if (acks) bb_sms_modem_ack(true, 0, "transfer and ack route");
    }
  }
  printf("NORD2 BB: SMS MT %s #%u: %u bytes, class %u, to %u WMS clients: %s%s\n", report ? "status report" : "message",
         report ? bb_sms_reports : bb_sms_mt_n, n, cls, sent, what, from_storage ? " (read from the modem's storage)" : "");
}
// messages held while no WMS client had its MT report on: transfer route, already acknowledged
static void bb_sms_flush_held(void)
{
  for (uint32_t i = 0; i < BB_SMS_STORE; i++) {
    bb_sms_msg &m = bb_sms_store[i];
    if (!m.used || !m.pending) continue;
    bb_sms_txn = bb_sms_txn + 1u ? bb_sms_txn + 1u : 1u;
    uint32_t off = 0;
    const uint32_t tn = nbm_sms_tpdu(m.data, m.len, &off);      // 0277t: the TPDU only
    const uint32_t t = tn ? nbm_wms_mt_transfer(1, bb_sms_txn, m.data + off, tn, bb_tlv, sizeof bb_tlv) : 0;
    const uint32_t sent = t ? bb_wms_indicate(bb_tlv, t) : 0;
    if (!sent) return;
    m.used = 0;
    printf("NORD2 BB: SMS held message (%u bytes) delivered to %u WMS clients\n", m.len, sent);
  }
}

static int bb_wms(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if (bb_flags & NBB_NO_SMS) return BB_UNHANDLED;
  const uint32_t message = nbq_rd16(sdu + 3), tl = nbq_rd16(sdu + 5), room = sizeof bb_tlv;
  const uint8_t *tlvs = sdu + 7, *v;
  uint32_t t = 0, len = 0;
  uint8_t b[8];
  *error = 0;
  switch (message) {
  case NBM_WMS_EVENT_REPORT: {                             // Set Event Report: 0x10 report new MT messages
    const int i = nbq_client_find(&bb_qmux, qmux, NBQ_SVC_WMS, cid);
    if ((v = nbq_tlv(tlvs, tl, 0x10, &len)) && len >= 1 && i >= 0) bb_wms_rep[i] = { (uint8_t)(v[0] != 0), (uint8_t)qmux, (uint8_t)cid };
    bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, 0, nullptr, 0);
    if (v && len >= 1 && v[0]) bb_sms_flush_held();
    return BB_ANSWERED;
  }
  case NBM_WMS_RAW_SEND:
    if (bb_flags & NBB_NO_AT) return BB_UNHANDLED;
    { const int o = bb_sms_raw_send(qmux, cid, sdu, error); if (o != BB_UNHANDLED) return o; }
    break;
  case NBM_WMS_RAW_WRITE: {                                // 0x01 {storage, format, u16 len, data}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 4u || 4u + nbq_rd16(v + 2) > len) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    const int i = bb_sms_store_put(v + 4, nbq_rd16(v + 2), v[0], NBM_WMS_TAG_MO_NOT_SENT, 0);
    if (i < 0) { *error = NBM_QMI_ERR_STORAGE_FULL; break; }
    nbq_wr32(b, (uint32_t)i);
    t = nbm_tlv(bb_tlv, room, 0x01, b, 4);
    break;
  }
  case NBM_WMS_RAW_READ:                                   // 0x01 {storage, u32 index}
  case NBM_WMS_MODIFY_TAG: {                               // 0x01 {storage, u32 index, tag}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < (message == NBM_WMS_MODIFY_TAG ? 6u : 5u)) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    const uint32_t i = nbq_rd32(v + 1);
    if (i >= BB_SMS_STORE || !bb_sms_store[i].used || bb_sms_store[i].pending) { *error = NBM_QMI_ERR_INVALID_INDEX; break; }
    bb_sms_msg &m = bb_sms_store[i];
    if (message == NBM_WMS_MODIFY_TAG) m.tag = v[5];
    else t = nbm_wms_raw_read_answer(m.tag, m.data, m.len, bb_tlv, room);
    break;
  }
  case NBM_WMS_DELETE: {                                   // 0x01 storage, 0x10 index, 0x11 tag
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 1) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    const uint8_t storage = v[0];
    uint32_t il = 0, gl = 0;
    const uint8_t *ix = nbq_tlv(tlvs, tl, 0x10, &il), *tg = nbq_tlv(tlvs, tl, 0x11, &gl);
    for (uint32_t i = 0; i < BB_SMS_STORE; i++) {
      bb_sms_msg &m = bb_sms_store[i];
      if (!m.used || m.pending || m.storage != storage) continue;
      if (ix && il >= 4 && nbq_rd32(ix) != i) continue;
      if (tg && gl >= 1 && m.tag != tg[0]) continue;
      m.used = 0;
    }
    break;
  }
  case NBM_WMS_LIST_MESSAGES: {                            // 0x01 storage, 0x11 tag -> 0x01 {u32 n, n x {u32 index, u8 tag}}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    uint32_t gl = 0;
    const uint8_t *tg = nbq_tlv(tlvs, tl, 0x11, &gl);
    uint8_t list[4 + BB_SMS_STORE * 5];
    uint32_t k = 4, cnt = 0;
    for (uint32_t i = 0; v && len >= 1 && i < BB_SMS_STORE; i++) {
      const bb_sms_msg &m = bb_sms_store[i];
      if (!m.used || m.pending || m.storage != v[0] || (tg && gl >= 1 && m.tag != tg[0])) continue;
      nbq_wr32(list + k, i); list[k + 4] = m.tag; k += 5; cnt++;
    }
    nbq_wr32(list, cnt);
    t = nbm_tlv(bb_tlv, room, 0x01, list, k);
    break;
  }
  case NBM_WMS_SET_ROUTES: {                               // 0x01 {u16 n, n x {type, class, storage, action}}, 0x10 status reports
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (v && len >= 2) {
      const uint32_t n = nbq_rd16(v);
      bb_sms_nroutes = 0;
      char shown[96];
      unsigned k = 0;
      shown[0] = 0;
      for (uint32_t i = 0; i < n && 2u + 4u * (i + 1u) <= len && bb_sms_nroutes < 8; i++) {
        const uint8_t *e = v + 2 + 4 * i;
        bb_sms_routes[bb_sms_nroutes++] = { e[0], e[1], e[2], e[3] };
        if (k + 12 < sizeof shown) k += (unsigned)snprintf(shown + k, sizeof shown - k, " c%u:s%u/a%u", e[1], e[2], e[3]);
      }
      if ((v = nbq_tlv(tlvs, tl, 0x10, &len)) && len >= 1) bb_sms_report_status = v[0];
      printf("NORD2 BB: SMS routes (class:storage/action):%s, status reports %u\n", shown, bb_sms_report_status);
    }
    break;
  }
  case NBM_WMS_GET_ROUTES: {
    uint8_t r[2 + 8 * 4];
    nbq_wr16(r, bb_sms_nroutes);
    for (uint32_t i = 0; i < bb_sms_nroutes; i++) memcpy(r + 2 + 4 * i, &bb_sms_routes[i], 4);
    t = nbm_tlv(bb_tlv, room, 0x01, r, 2 + 4 * bb_sms_nroutes);
    t += nbm_tlv_u8(bb_tlv + t, room - t, 0x10, bb_sms_report_status);
    break;
  }
  case NBM_WMS_GET_MESSAGE_PROTOCOL:
    t = nbm_tlv_u8(bb_tlv, room, 0x01, 1);                 // WCDMA (3GPP)
    break;
  case NBM_WMS_GET_STORE_MAX_SIZE: {
    uint32_t used = 0;
    for (const auto &m : bb_sms_store) used += m.used;
    nbq_wr32(b, BB_SMS_STORE);
    t = nbm_tlv(bb_tlv, room, 0x01, b, 4);
    nbq_wr32(b, BB_SMS_STORE - used);
    t += nbm_tlv(bb_tlv + t, room - t, 0x10, b, 4);
    break;
  }
  case NBM_WMS_SEND_ACK: {                                 // 0x01 {u32 transaction, u8 protocol, u8 success}, 0x11 {RP, TP cause}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 6) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    const uint32_t txn = nbq_rd32(v);
    const bool ok = v[5] != 0;
    uint32_t fl = 0;
    const uint8_t *f = nbq_tlv(tlvs, tl, 0x11, &fl);
    if (bb_sms_ack_pending && txn == bb_sms_ack_txn) bb_sms_modem_ack(ok, f && fl >= 2 ? f[1] : 0, ok ? "CommCenter's Send Ack" : "CommCenter's Send Ack: failure");
    else if (bb_sms_acks + bb_sms_nacks <= 16) printf("NORD2 BB: SMS Send Ack %s for transaction %u: nothing waits for it\n", ok ? "ok" : "failure", txn);
    break;
  }
  case NBM_WMS_GET_SMSC_ADDRESS: {
    if (bb_flags & NBB_NO_AT) return BB_UNHANDLED;
    const int slot = bb_cs_hold(qmux, NBQ_SVC_WMS, cid, sdu);
    const int e = slot < 0 ? EBUSY : bb_cs_at("AT+CSCA?", BB_OP_GET_SMSC, (uint32_t)slot, bb_csq[slot].gen);
    if (!e) return BB_DEFERRED;
    if (slot >= 0) bb_csq[slot].used = 0;
    if (bb_sms_smsc[0]) t = nbm_wms_smsc_answer(bb_sms_smsc, bb_sms_smsc_type, bb_tlv, room);
    else *error = NBQ_ERR_DEVICE_NOT_READY;
    break;
  }
  case NBM_WMS_SET_SMSC_ADDRESS: {                         // 0x01 address, 0x10 type ("145")
    if (bb_flags & NBB_NO_AT) return BB_UNHANDLED;
    uint32_t tn = 0;
    const uint8_t *ty = nbq_tlv(tlvs, tl, 0x10, &tn);
    char cmd[64];
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || !nbm_csca_set_cmd(v, len, ty ? ty : (const uint8_t *)"", ty ? tn : 0, cmd, sizeof cmd)) { *error = NBQ_ERR_INVALID_ARG; break; }
    const int slot = bb_cs_hold(qmux, NBQ_SVC_WMS, cid, sdu);
    const int e = slot < 0 ? EBUSY : bb_cs_at(cmd, BB_OP_SET_SMSC, (uint32_t)slot, bb_csq[slot].gen);
    if (!e) { printf("NORD2 BB: SMS set SMSC (%u characters)\n", len); return BB_DEFERRED; }
    if (slot >= 0) bb_csq[slot].used = 0;
    *error = bb_cs_err(e);
    break;
  }
  case NBM_WMS_INDICATION_REGISTER:                        // 0x10..0x14: transport layer, network registration, call status,
    for (uint32_t k = 0; k < 5; k++)                       // service ready, broadcast config events
      if ((v = nbq_tlv(tlvs, tl, 0x10 + k, &len)) && len >= 1) bb_wms_reg[k] = v[0];
    break;
  case NBM_WMS_GET_INDICATION_REGISTER:
    for (uint32_t k = 0; k < 5; k++) t += nbm_tlv_u8(bb_tlv + t, room - t, 0x10 + k, bb_wms_reg[k]);
    break;
  case NBM_WMS_GET_SERVICE_READY_STATUS:                   // 0x10 registered for the indication, 0x11 u32 ready status (1 3GPP)
    t = nbm_tlv_u8(bb_tlv, room, 0x10, bb_wms_reg[3]);
    nbq_wr32(b, 1);
    t += nbm_tlv(bb_tlv + t, room - t, 0x11, b, 4);
    break;
  case 0x003c:                                             // Set Broadcast Activation: 0x01 {mode, activate}
    if ((v = nbq_tlv(tlvs, tl, 0x01, &len)) && len >= 2 && v[0] == NBM_WMS_MODE_GW) bb_cb_active = v[1];
    break;
  case 0x003d:                                             // Set Broadcast Config: 0x01 mode, 0x10 3GPP {u16 n, n x {from, to, sel}}
    if ((v = nbq_tlv(tlvs, tl, 0x10, &len)) && len >= 2 && len <= sizeof bb_cb_cfg) { memcpy(bb_cb_cfg, v, len); bb_cb_cfg_len = len; }
    break;
  case NBM_WMS_GET_BROADCAST_CONFIG: {                     // 0x01 mode -> 0x10 3GPP {activated, u16 n, ...} / 0x11 3GPP2
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    uint8_t c[1 + sizeof bb_cb_cfg];
    if (v && len >= 1 && v[0] == 0) {
      c[0] = 0; c[1] = 0; c[2] = 0;
      t = nbm_tlv(bb_tlv, room, 0x11, c, 3);
    } else {
      c[0] = bb_cb_active;
      uint32_t k = 1;
      if (bb_cb_cfg_len >= 2) { memcpy(c + 1, bb_cb_cfg, bb_cb_cfg_len); k += bb_cb_cfg_len; } else { c[1] = c[2] = 0; k = 3; }
      t = nbm_tlv(bb_tlv, room, 0x10, c, k);
    }
    break;
  }
  case NBM_WMS_GET_TRANSPORT_LAYER_INFO:                   // 0x10 registered on an IMS transport: no (SMS over SGs/CS)
    t = nbm_tlv_u8(bb_tlv, room, 0x10, 0);
    break;
  case NBM_WMS_GET_TRANSPORT_NW_REG: {                     // 0x10 0 no service .. 4 full service
    uint8_t radio = 0;
    int roaming = 0;
    t = nbm_tlv_u8(bb_tlv, room, 0x10, nbm_reg(&bb_st, &radio, &roaming) == 1 ? 4 : 0);
    break;
  }
  case NBM_WMS_SET_MEMORY_STATUS: case NBM_WMS_SET_PRIMARY_CLIENT: case NBM_WMS_SET_MESSAGE_WAITING:
    break;
  default:
    return BB_UNHANDLED;
  }
  bb_reply(qmux, NBQ_SVC_WMS, cid, sdu, *error, bb_tlv, *error ? 0 : t);
  return BB_ANSWERED;
}

// ---- calls
static uint8_t bb_call_mode(void)
{
  if (bb_ecpi_mode >= 20) return NBM_CALL_MODE_LTE;       // MediaTek call modes 20+ are IMS (VoLTE) calls
  const int act = bb_st.cs_act;
  if (act == 0 || act == 1 || act == 3) return NBM_CALL_MODE_GSM;
  return NBM_CALL_MODE_UMTS;                               // UTRAN, or CS fallback from LTE
}
static uint32_t bb_call_list(struct nbm_call *out, bool with_ended)
{
  uint32_t n = 0;
  for (uint32_t id = 1; id < 8; id++) {
    const bb_call &c = bb_calls[id];
    if (!c.used || (!with_ended && c.state == NBM_CALL_END)) continue;
    nbm_call &o = out[n++];
    memset(&o, 0, sizeof o);
    o.id = (uint8_t)id; o.state = c.state; o.dir = c.dir; o.mode = c.mode; o.mpty = c.mpty; o.pi = c.pi;
    o.end_reason = c.state == NBM_CALL_END ? (c.cause >= 0 ? nbm_cc_cause_end_reason(c.cause) : bb_call_hint ? bb_call_hint : 145) : 0;
    memcpy(o.number, c.number, sizeof o.number);
  }
  return n;
}
static void bb_clcc_poll(void)
{
  if (bb_clcc_inflight) { bb_clcc_again = 1; return; }
  if (bb_cs_at("AT+CLCC", BB_OP_CLCC, 0) == 0) { bb_clcc_inflight = 1; bb_v_polls++; }
  else bb_clcc_due = bb_now_ms() + 500u;
}
static void bb_clcc_soon(uint32_t ms)
{
  const uint32_t at = bb_now_ms() + ms;
  if (bb_clcc_due && (int32_t)(bb_clcc_due - at) <= 0) return;
  bb_clcc_due = at ? at : 1u;
  bb_cs_arm(ms);
}
static bool bb_calls_active(void)
{
  for (uint32_t id = 1; id < 8; id++) if (bb_calls[id].used) return true;
  return false;
}
static void bb_clcc_done(int final, const char *text, unsigned len)
{
  bb_clcc_inflight = 0;
  uint8_t present = 0;
  bool changed = false;
  if (final == NORD2_AT_FINAL_OK) {
    for (unsigned i = 0; i < len;) {
      unsigned e = i;
      while (e < len && text[e] != '\n') e++;
      char line[128];
      const unsigned k = e - i < sizeof line - 1 ? e - i : (unsigned)sizeof line - 1;
      memcpy(line, text + i, k);
      line[k] = 0;
      i = e + 1;
      struct nbm_call c;
      if (!nbm_clcc(line, &c)) continue;
      present |= (uint8_t)(1u << c.id);
      bb_call &b = bb_calls[c.id];
      char tail[3];
      nbm_tail2(c.number, tail);
      if (!b.used) {
        b = { 1, 0, c.dir, 0, bb_call_mode(), c.pi, 0, -1, "" };
        memcpy(b.number, c.number, sizeof b.number);
        if (c.dir == 2) bb_v_incoming++;
        printf("NORD2 BB: VOICE call %u new: %s%s, number length %u ..%s\n", c.id, c.dir == 2 ? "incoming (MT)" : "outgoing (MO)",
               c.state == NBM_CALL_WAITING ? " waiting" : "", (unsigned)strlen(c.number), tail);
        changed = true;
      }
      if (b.state != c.state || b.mpty != c.mpty) {
        printf("NORD2 BB: VOICE call %u: state %u -> %u%s\n", c.id, b.state, c.state, c.mpty ? " (multiparty)" : "");
        b.state = c.state; b.mpty = c.mpty;
        changed = true;
      }
      if (c.number[0] && strcmp(b.number, c.number)) { memcpy(b.number, c.number, sizeof b.number); b.pi = c.pi; changed = true; }
    }
    for (uint32_t id = 1; id < 8; id++) {
      bb_call &b = bb_calls[id];
      if (b.used && !(present & 1u << id) && b.state != NBM_CALL_END) {
        printf("NORD2 BB: VOICE call %u ended (%s, CC cause %d)\n", id, b.local_end ? "by CommCenter" : "by the network", b.cause);
        b.state = NBM_CALL_END;
        changed = true;
      }
    }
  }
  // a Dial Call waiting for its call id, answered before the call's first status indication (a failed +CLCC counts as a try)
  if (bb_dial_active) {
    bb_cs_req &q = bb_csq[bb_dial_slot];
    uint32_t found = 0;
    for (uint32_t id = 1; id < 8 && !found; id++)
      if ((present & 1u << id) && !(bb_dial_before & 1u << id) && bb_calls[id].dir == 1) found = id;
    if (found || !q.used || (bb_dial_atd_ok && ++bb_dial_tries >= BB_DIAL_TRIES)) {
      bb_dial_active = 0;
      if (q.used) {
        uint8_t b[1] = { (uint8_t)found };
        const uint32_t t = found ? nbm_tlv(bb_tlv, sizeof bb_tlv, 0x10, b, 1) : 0;
        const uint32_t ms = bb_now_ms() - q.ms;
        if (!found) bb_v_dial_fail++;
        bb_cs_answer(q, found ? 0 : NBM_QMI_ERR_CALL_FAILED, bb_tlv, t);
        printf("NORD2 BB: VOICE dial -> %s%u (after %u ms)\n", found ? "call id " : "CALL FAILED, no new call in +CLCC ", found, ms);
      }
    }
  }
  if (changed && !(bb_flags & NBB_NO_IND)) {
    struct nbm_call list[8];
    const uint32_t n = bb_call_list(list, true);
    const uint32_t t = nbm_voice_calls(list, n, 0, bb_tlv, sizeof bb_tlv);
    const uint32_t sent = bb_indicate_all(NBQ_SVC_VOICE, NBM_VOICE_ALL_CALL_STATUS_IND, bb_tlv, t);
    bb_v_inds++;
    char states[32];
    unsigned k = 0;
    states[0] = 0;
    for (uint32_t i = 0; i < n && k + 6 < sizeof states; i++) k += (unsigned)snprintf(states + k, sizeof states - k, "%s%u:%u", i ? " " : "", list[i].id, list[i].state);
    if (bb_v_inds <= 64) printf("NORD2 BB: VOICE all call status indication (id:state %s) to %u clients\n", n ? states : "none", sent);
  }
  for (uint32_t id = 1; id < 8; id++) if (bb_calls[id].used && bb_calls[id].state == NBM_CALL_END) bb_calls[id].used = 0;
  if (!bb_calls_active()) { bb_call_hint = 0; bb_ecpi_mode = -1; }
  if (bb_clcc_again) { bb_clcc_again = 0; bb_clcc_poll(); }
  else if (bb_calls_active() || bb_dial_active) bb_clcc_soon(bb_dial_active && bb_dial_atd_ok ? 300 : BB_CLCC_MS);
}
// requests that end with an AT final result and an answer carrying the call id (0x10), then a +CLCC poll
static void bb_voice_simple_done(bb_cs_req &q, uint32_t op, int final, const char *text, unsigned len)
{
  if (final != NORD2_AT_FINAL_OK && q.alt[0]) {            // the fallback command (ATH, AT+VTS)
    char cmd[24];
    memcpy(cmd, q.alt, sizeof cmd);
    q.alt[0] = 0;
    if (op == BB_OP_DTMF) bb_evts_ok = 0;
    if (bb_cs_at(cmd, op, (uint32_t)(&q - bb_csq), q.gen) == 0) return;
  }
  const long code = final == NORD2_AT_FINAL_ERROR ? nbm_at_error_code(text, len) : -1;
  uint8_t b[1] = { q.call_id };
  const uint32_t t = q.call_id ? nbm_tlv(bb_tlv, sizeof bb_tlv, 0x10, b, 1) : 0;
  const uint16_t message = q.message;
  const uint8_t id = q.call_id;
  bb_cs_answer(q, final == NORD2_AT_FINAL_OK || op == BB_OP_DTMF_STOP ? 0 : NBQ_ERR_INTERNAL, bb_tlv, t);
  if (op != BB_OP_DTMF && op != BB_OP_DTMF_STOP)
    printf("NORD2 BB: VOICE msg 0x%04x call %u -> %s (code %ld)\n", message, id, bb_final_name(final), code);
  if (op != BB_OP_DTMF && op != BB_OP_DTMF_STOP) bb_clcc_soon(50);
}
static int bb_voice(uint32_t qmux, uint32_t cid, const uint8_t *sdu, uint32_t *error)
{
  if (bb_flags & NBB_NO_VOICE) return BB_UNHANDLED;
  const uint32_t message = nbq_rd16(sdu + 3), tl = nbq_rd16(sdu + 5), room = sizeof bb_tlv;
  const uint8_t *tlvs = sdu + 7, *v;
  uint32_t t = 0, len = 0, op = 0;
  char cmd[64], alt[24];
  uint8_t id = 0;
  cmd[0] = alt[0] = 0;
  *error = 0;
  switch (message) {
  case NBM_VOICE_DIAL_CALL: {                              // 0x01 number, 0x11 CLIR (1 suppression, 2 invocation)
    char num[48], tail[3];
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || !nbm_dial_number(v, len, num, sizeof num)) { *error = NBQ_ERR_INVALID_ARG; break; }
    if (bb_dial_active) { *error = 0x0017; break; }       // QMI_ERR_DEVICE_IN_USE
    uint32_t cl = 0;
    const uint8_t *clir = nbq_tlv(tlvs, tl, 0x11, &cl);
    snprintf(cmd, sizeof cmd, "ATD%s%s;", num, clir && cl >= 1 && clir[0] == 2 ? "I" : clir && cl >= 1 && clir[0] == 1 ? "i" : "");
    const int slot = bb_cs_hold(qmux, NBQ_SVC_VOICE, cid, sdu);
    if (slot < 0) { *error = NBQ_ERR_DEVICE_NOT_READY; break; }
    uint8_t before = 0;
    for (uint32_t i = 1; i < 8; i++) if (bb_calls[i].used) before |= (uint8_t)(1u << i);
    const int e = bb_cs_at(cmd, BB_OP_DIAL, (uint32_t)slot, bb_csq[slot].gen);
    if (e) { bb_csq[slot].used = 0; *error = bb_cs_err(e); break; }
    bb_dial_active = 1; bb_dial_atd_ok = 0; bb_dial_slot = (uint8_t)slot; bb_dial_tries = 0; bb_dial_before = before;
    bb_v_dials++;
    nbm_tail2(num, tail);
    printf("NORD2 BB: VOICE dial #%u: number length %u ..%s%s -> ATD queued\n", bb_v_dials, (unsigned)strlen(num), tail,
           clir && cl >= 1 ? (clir[0] == 2 ? " (CLIR on)" : " (CLIR off)") : "");
    return BB_DEFERRED;
  }
  case NBM_VOICE_ANSWER_CALL:                              // 0x01 call id
  case NBM_VOICE_END_CALL: {
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 1 || v[0] < 1 || v[0] > 7) { *error = NBQ_ERR_INVALID_ARG; break; }
    id = v[0];
    const bb_call &c = bb_calls[id];
    uint32_t others = 0;
    for (uint32_t i = 1; i < 8; i++) others += i != id && bb_calls[i].used;
    if (message == NBM_VOICE_ANSWER_CALL) {
      snprintf(cmd, sizeof cmd, c.used && c.state == NBM_CALL_WAITING ? "AT+CHLD=2" : "ATA");
      op = BB_OP_ANSWER;
      bb_v_answers++;
    } else {
      if (c.used && c.state == NBM_CALL_INCOMING && !others) snprintf(cmd, sizeof cmd, "ATH");
      else if (c.used && c.state == NBM_CALL_WAITING) snprintf(cmd, sizeof cmd, "AT+CHLD=0");
      else snprintf(cmd, sizeof cmd, "AT+CHLD=1%u", id);
      if (!others && strcmp(cmd, "ATH")) snprintf(alt, sizeof alt, "ATH");
      if (bb_calls[id].used) bb_calls[id].local_end = 1;
      op = BB_OP_END;
      bb_v_ends++;
    }
    break;
  }
  case NBM_VOICE_MANAGE_CALLS: {                           // 0x01 sups type, 0x10 call id
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 1) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    uint32_t il = 0;
    const uint8_t *ix = nbq_tlv(tlvs, tl, 0x10, &il);
    id = ix && il >= 1 && ix[0] >= 1 && ix[0] <= 7 ? ix[0] : 0;
    switch (v[0]) {
    case 1: snprintf(cmd, sizeof cmd, "AT+CHLD=0"); break;              // release held or waiting
    case 2: snprintf(cmd, sizeof cmd, "AT+CHLD=1"); break;              // release active, accept held or waiting
    case 3: snprintf(cmd, sizeof cmd, "AT+CHLD=2"); break;              // hold active, accept waiting or held
    case 4: if (id) snprintf(cmd, sizeof cmd, "AT+CHLD=2%u", id); break; // hold all except the call
    case 5: snprintf(cmd, sizeof cmd, "AT+CHLD=3"); break;              // conference
    case 6: snprintf(cmd, sizeof cmd, "AT+CHLD=4"); break;              // explicit call transfer
    case 8: snprintf(cmd, sizeof cmd, "ATH"); break;                    // end all calls
    case 9: if (id) snprintf(cmd, sizeof cmd, "AT+CHLD=1%u", id); break; // release the call
    default: break;
    }
    if (!cmd[0]) { *error = NBQ_ERR_NOT_SUPPORTED; printf("NORD2 BB: VOICE manage calls type %u: not supported\n", v[0]); break; }
    printf("NORD2 BB: VOICE manage calls type %u call %u -> %s\n", v[0], id, cmd);
    id = 0;                                                // the answer has no call id
    op = BB_OP_MANAGE;
    bb_v_manage++;
    break;
  }
  case NBM_VOICE_START_DTMF: {                             // 0x01 {call id, digit}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 2) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    const char d = (char)v[1];
    if (!((d >= '0' && d <= '9') || d == '*' || d == '#' || (d >= 'A' && d <= 'D'))) { *error = NBQ_ERR_INVALID_ARG; break; }
    id = v[0];
    if (bb_evts_ok) { snprintf(cmd, sizeof cmd, "AT+EVTS=0,\"%c\"", d); snprintf(alt, sizeof alt, "AT+VTS=%c", d); }
    else snprintf(cmd, sizeof cmd, "AT+VTS=%c", d);
    op = BB_OP_DTMF;
    bb_v_dtmf++;
    break;
  }
  case NBM_VOICE_STOP_DTMF:                                // 0x01 {call id}
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    id = v && len >= 1 ? v[0] : 0;
    if (bb_evts_ok) { snprintf(cmd, sizeof cmd, "AT+EVTS=1"); op = BB_OP_DTMF_STOP; break; }
    { uint8_t b[1] = { id }; t = id ? nbm_tlv(bb_tlv, room, 0x10, b, 1) : 0; }
    break;
  case NBM_VOICE_BURST_DTMF: {                             // 0x01 {call id, u8 n, digits}: one AT+VTS per digit
    v = nbq_tlv(tlvs, tl, 0x01, &len);
    if (!v || len < 2 || 2u + v[1] > len) { *error = NBQ_ERR_MISSING_ARGUMENT; break; }
    for (uint32_t i = 0; i < v[1] && i < 16; i++) {
      const char d = (char)v[2 + i];
      if (!((d >= '0' && d <= '9') || d == '*' || d == '#')) continue;
      char c2[16];
      snprintf(c2, sizeof c2, "AT+VTS=%c", d);
      (void)bb_cs_at(c2, BB_OP_VTS, 0);
    }
    bb_v_dtmf++;
    uint8_t b[1] = { v[0] };
    t = nbm_tlv(bb_tlv, room, 0x10, b, 1);
    break;
  }
  case NBM_VOICE_GET_ALL_CALL_INFO: {
    struct nbm_call list[8];
    t = nbm_voice_calls(list, bb_call_list(list, false), 1, bb_tlv, room);
    break;
  }
  case NBM_VOICE_GET_CONFIG:                               // requested items: 0x10 auto answer, 0x13 TTY, 0x15 AMR
    if (nbq_tlv(tlvs, tl, 0x10, &len)) t += nbm_tlv_u8(bb_tlv + t, room - t, 0x10, 0);
    if (nbq_tlv(tlvs, tl, 0x13, &len)) t += nbm_tlv_u8(bb_tlv + t, room - t, 0x13, 3);      // TTY off
    if (nbq_tlv(tlvs, tl, 0x15, &len)) { uint8_t a[2] = { 1, 1 }; t += nbm_tlv(bb_tlv + t, room - t, 0x15, a, 2); }
    break;
  case NBM_VOICE_SET_CONFIG:
    break;
  default:
    return BB_UNHANDLED;
  }
  if (!*error && op) {
    if (bb_flags & NBB_NO_AT) return BB_UNHANDLED;
    const int slot = bb_cs_hold(qmux, NBQ_SVC_VOICE, cid, sdu, id);
    if (slot < 0) { *error = NBQ_ERR_DEVICE_NOT_READY; }
    else {
      memcpy(bb_csq[slot].alt, alt, sizeof bb_csq[slot].alt);
      const int e = bb_cs_at(cmd, op, (uint32_t)slot, bb_csq[slot].gen);
      if (!e) return BB_DEFERRED;
      bb_csq[slot].used = 0;
      *error = bb_cs_err(e);
    }
  }
  bb_reply(qmux, NBQ_SVC_VOICE, cid, sdu, *error, bb_tlv, *error ? 0 : t);
  return BB_ANSWERED;
}

// AT results of the SMS and call commands (modem service thread, no AT lock)
static void bb_cs_at_done(void *ctx, int final, const char *text, unsigned len)
{
  const uint64_t c = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ctx));
  const uint32_t op = (uint32_t)(c & 0xffu), aux = (uint32_t)(c >> 8 & 0xffffffu), gen = (uint32_t)(c >> 32);
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  bb_cs_req *q = gen ? bb_cs_req_of(aux, gen) : nullptr;
  switch (op) {
  case BB_OP_SETUP: bb_cs_setup_done(aux, final, text, len); break;
  case BB_OP_CMGS: if (q) bb_sms_send_done(*q, final, text, len); else { bb_sms_send_busy = 0; bb_sms_send_next(); } break;
  case BB_OP_CNMA:
    if (final != NORD2_AT_FINAL_OK && aux == 0) {          // MediaTek's inline form refused: 27.005's bare AT+CNMA from now on
      bb_sms_cnma_style = 1;
      (void)bb_cs_at("AT+CNMA", BB_OP_CNMA, 1);
      printf("NORD2 BB: SMS AT+CNMA=1,2,\"0000\" -> %s, using AT+CNMA\n", bb_final_name(final));
    } else if (final != NORD2_AT_FINAL_OK || bb_sms_acks <= 4) {
      printf("NORD2 BB: SMS ack (style %u) -> %s\n", aux, bb_final_name(final));
    }
    break;
  case BB_OP_NACK:
    if (final != NORD2_AT_FINAL_OK) (void)bb_cs_at("AT+CNMA=2", BB_OP_CNMA, 2);
    printf("NORD2 BB: SMS nack -> %s\n", bb_final_name(final));
    break;
  case BB_OP_GET_SMSC:
    if (q) {
      char d[24];
      unsigned type = 0;
      if (final == NORD2_AT_FINAL_OK && nbm_csca(text, len, d, sizeof d, &type)) {
        memcpy(bb_sms_smsc, d, sizeof bb_sms_smsc);
        bb_sms_smsc_type = type;
      }
      const uint32_t t = bb_sms_smsc[0] ? nbm_wms_smsc_answer(bb_sms_smsc, bb_sms_smsc_type, bb_tlv, sizeof bb_tlv) : 0;
      bb_cs_answer(*q, t ? 0 : NBQ_ERR_DEVICE_NOT_READY, bb_tlv, t);
      printf("NORD2 BB: SMS get SMSC -> %s (%u digits, type %u)\n", t ? "answered" : "none", (unsigned)strlen(bb_sms_smsc), bb_sms_smsc_type);
    }
    break;
  case BB_OP_SET_SMSC:
    if (q) {
      bb_cs_answer(*q, final == NORD2_AT_FINAL_OK ? 0 : NBQ_ERR_INTERNAL, nullptr, 0);
      bb_sms_smsc[0] = 0;
      (void)bb_cs_at("AT+CSCA?", BB_OP_SETUP, 3);         // the cache again
      printf("NORD2 BB: SMS set SMSC -> %s\n", bb_final_name(final));
    }
    break;
  case BB_OP_CMGR: {                                       // +CMTI fallback: "+CMGR: <stat>,[<alpha>],<length>" then the PDU
    uint8_t raw[BB_SMS_DATA];
    uint32_t n = 0;
    for (unsigned i = 0; final == NORD2_AT_FINAL_OK && i + 6u < len && !n; i++) {
      if (!((i == 0 || text[i - 1] == '\n') && nbm_prefix(text + i, "+CMGR:"))) continue;
      unsigned s = i;
      while (s < len && text[s] != '\n') s++;
      unsigned e = ++s;
      while (e < len && text[e] != '\n') e++;
      if (e > s) n = nbm_hex_decode(text + s, e - s, raw, sizeof raw);
    }
    if (n) bb_sms_mt(raw, n, false, true);
    else bb_sms_bad_pdu++;
    char cmd[24];
    snprintf(cmd, sizeof cmd, "AT+CMGD=%u", aux);
    (void)bb_cs_at(cmd, BB_OP_CMGD, aux);
    printf("NORD2 BB: SMS stored message %u read (%s, %u bytes), deleting it from the modem\n", aux, bb_final_name(final), n);
    break;
  }
  case BB_OP_CMGD: break;
  case BB_OP_DIAL:
    if (q && bb_dial_active) {
      if (final == NORD2_AT_FINAL_OK) {
        bb_dial_atd_ok = 1;
        bb_clcc_poll();                                    // the call id comes from +CLCC (bb_clcc_done answers)
      } else {
        const long code = final == NORD2_AT_FINAL_ERROR ? nbm_at_error_code(text, len) : -1;
        bb_dial_active = 0;
        bb_v_dial_fail++;
        bb_cs_answer(*q, NBM_QMI_ERR_CALL_FAILED, nullptr, 0);
        printf("NORD2 BB: VOICE dial -> %s (code %ld)\n", final == NORD2_AT_FINAL_ERROR ? "ERROR / NO CARRIER" : "timeout", code);
        bb_clcc_soon(50);
      }
    }
    break;
  case BB_OP_CLCC: bb_clcc_done(final, text, len); break;
  case BB_OP_ANSWER: case BB_OP_END: case BB_OP_MANAGE: case BB_OP_DTMF: case BB_OP_DTMF_STOP:
    if (q) bb_voice_simple_done(*q, op, final, text, len);
    break;
  case BB_OP_EAIC:
    printf("NORD2 BB: VOICE incoming call approval (AT+EAIC) -> %s\n", bb_final_name(final));
    break;
  default: break;
  }
  IOLockUnlock(bb_lock);
}

// SMS and call URCs (modem service thread); true when the line was one. Takes bb_lock. Never logs the line.
static bool bb_cs_urc(const char *line)
{
  if (!bb_lock || (bb_flags & NBB_NO_AT)) return false;
  const bool sms = !(bb_flags & NBB_NO_SMS), voice = !(bb_flags & NBB_NO_VOICE);
  IOLockLock(bb_lock);
  bool used = true;
  const uint8_t expect = bb_sms_expect;
  bb_sms_expect = 0;
  int id = 0, msg = 0, mode = 0, cause = 0;
  if (expect && line[0] != '+') {                          // the PDU line of +CMT/+CDS/+CBM
    uint8_t raw[BB_SMS_DATA];
    const uint32_t n = nbm_hex_decode(line, (uint32_t)strlen(line), raw, sizeof raw);
    if (expect == 3) bb_sms_cbm++;                         // cell broadcast: not forwarded (no WMS broadcast indication yet)
    else if (!n || n < bb_sms_expect_len) { bb_sms_bad_pdu++; printf("NORD2 BB: SMS MT PDU unusable (%u characters)\n", (unsigned)strlen(line)); }
    else bb_sms_mt(raw, n, expect == 2, false);
  } else if (sms && (nbm_prefix(line, "+CMT:") || nbm_prefix(line, "+CDS:") || nbm_prefix(line, "+CBM:"))) {
    const long l = nbm_last_number(line);
    bb_sms_expect = line[2] == 'M' ? 1 : line[2] == 'D' ? 2 : 3;
    bb_sms_expect_len = l > 0 ? (uint32_t)l : 0;
  } else if (sms && (nbm_prefix(line, "+CMTI:") || nbm_prefix(line, "+CDSI:"))) {
    const long index = nbm_last_number(line);
    char cmd[24];
    snprintf(cmd, sizeof cmd, "AT+CMGR=%ld", index);
    if (index >= 0 && bb_cs_at(cmd, BB_OP_CMGR, (uint32_t)index) == 0) printf("NORD2 BB: SMS stored in the modem (index %ld): reading it\n", index);
  } else if (voice && (!strcmp(line, "RING") || nbm_prefix(line, "+CRING:") || nbm_prefix(line, "+CLIP:") || nbm_prefix(line, "+CCWA:"))) {
    bb_clcc_soon(0);
  } else if (voice && (!strcmp(line, "NO CARRIER") || !strcmp(line, "BUSY") || !strcmp(line, "NO ANSWER"))) {
    bb_call_hint = line[0] == 'B' ? 146 : line[3] == 'A' ? 148 : 0;
    bb_clcc_soon(0);
  } else if (voice && nbm_ecpi(line, &id, &msg, &mode, &cause)) {
    if (id >= 1 && id <= 7 && cause >= 0) bb_calls[id].cause = cause;
    if (mode >= 0 && msg != 133 && msg != 129) bb_ecpi_mode = mode;
    bb_clcc_soon(msg == 133 || msg == 129 ? 0 : 100);
  } else if (voice && nbm_prefix(line, "+EAIC:")) {        // MediaTek asks the AP to approve an incoming call: approve
    long f[8];
    const int n = nbm_numbers(line, f, 8);
    char cmd[40];
    if (n >= 2 && f[0] >= 0 && f[n - 1] >= 0) {
      snprintf(cmd, sizeof cmd, "AT+EAIC=0,%ld,%ld", f[0], f[n - 1]);
      (void)bb_cs_at(cmd, BB_OP_EAIC, 0);
      bb_v_eaic++;
    }
    bb_clcc_soon(200);
  } else if (voice && nbm_prefix(line, "+ESPEECH:")) {     // the modem's speech path on/off: no AP audio path here (see the header)
    if (bb_v_speech++ < 8) printf("NORD2 BB: VOICE modem speech indication %s (no audio path on the AP side)\n", line + 9);
  } else {
    used = false;
  }
  IOLockUnlock(bb_lock);
  return used;
}

// ack fallback and +CLCC polls
static void bb_cs_tick(thread_call_param_t, thread_call_param_t)
{
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  bb_cs_armed = 0;
  const uint32_t now = bb_now_ms();
  if (bb_sms_ack_pending) {
    if (now - bb_sms_ack_ms >= BB_SMS_ACK_MS) { bb_sms_ack_timeouts++; bb_sms_modem_ack(true, 0, "no Send Ack from CommCenter in 4 s"); }
    else bb_cs_arm(BB_SMS_ACK_MS - (now - bb_sms_ack_ms));
  }
  if (bb_clcc_due) {
    if ((int32_t)(now - bb_clcc_due) >= 0) { bb_clcc_due = 0; bb_clcc_poll(); }
    else bb_cs_arm(bb_clcc_due - now);
  }
  IOLockUnlock(bb_lock);
}

// One QMI request from ABM: answered here, deferred to AT, or answered as unhandled; logged (first ones of each kind).
static void bb_qmi_request(uint32_t qmux, const struct nbq_result &r, const uint8_t *sdu, uint32_t n)
{
  const uint32_t service = r.service, message = nbq_rd16(sdu + 3);
  uint32_t error = 0;
  int outcome = BB_UNHANDLED;
  const bool from_at = !(bb_flags & NBB_NO_AT);
  bb_requests++;
  if (service == CBD_SVC_BSP) outcome = bb_bsp(qmux, r.cid, sdu, n, &error);
  else if (from_at && service == NBQ_SVC_DMS) outcome = bb_dms(qmux, r.cid, sdu, n, &error);
  else if (from_at && service == NBQ_SVC_NAS) outcome = bb_nas(qmux, r.cid, sdu, n, &error);
  else if (from_at && service == NBQ_SVC_UIM) outcome = bb_uim(qmux, r.cid, sdu, n, &error);
  else if (service == NBQ_SVC_WDS && (outcome = bb_wds(qmux, r.cid, sdu, &error)) != BB_UNHANDLED) {}
  else if (from_at && service == NBQ_SVC_WMS && (outcome = bb_wms(qmux, r.cid, sdu, &error)) != BB_UNHANDLED) {}   // 0277r
  else if (from_at && service == NBQ_SVC_VOICE) outcome = bb_voice(qmux, r.cid, sdu, &error);                     // 0277r
  else if (service == NBQ_SVC_WDS || service == NBQ_SVC_WMS) outcome = bb_fixed(qmux, service, r.cid, sdu, &error);
  else if (service == NBM_SVC_DSD && message == NBM_DSD_GET_SYSTEM_STATUS && !(bb_flags & NBB_NO_DATA)) {
    bb_reply(qmux, NBM_SVC_DSD, r.cid, sdu, 0, bb_tlv, nbm_dsd_system_status(&bb_st, bb_tlv, sizeof bb_tlv));
    outcome = BB_ANSWERED;
  }
  else if (service == NBM_SVC_PDS) outcome = bb_pds(qmux, r.cid, sdu, &error);
  else if (service == NBM_SVC_MFSE) outcome = bb_mfse(qmux, r.cid, sdu, &error);
  const int accept = outcome == BB_UNHANDLED ? bb_auto_accept(service, message) : 0;
  if (accept) {
    bb_reply(qmux, service, r.cid, sdu, 0, nullptr, 0);
    outcome = BB_ANSWERED;
    error = 0;
    bb_ring_note(service, message, (uint32_t)outcome, 0);
    const bb_kind *k = bb_kind_note(service, message, (uint8_t)outcome, 0);
    if (k && bb_log_ok(k->count))
      bb_log_request(qmux, service, r.cid, sdu, n, accept == 2 ? "accepted (setting, no action)" : "accepted (registration)", 0, k->count);
    return;
  }
  if (outcome == BB_UNHANDLED) {
    // 0277g: NOT_SUPPORTED also for PDS (no GNSS behind this modem: location stays with the AP) and for Apple's vendor messages
    // 0x55xx on the standard services (a feature this modem lacks rather than an unknown command). 0277i: PDS reaches this only with
    // n2bb bit 14 (locationd turns any PDS error into a baseband reset request)
    const bool not_supported = (bb_flags & NBB_NOT_SUPPORTED_ALL) || nbq_private_service(service) || service == 0x06u ||
                               (message & 0xff00u) == 0x5500u;
    error = not_supported ? NBQ_ERR_NOT_SUPPORTED : NBQ_ERR_INVALID_QMI_CMD;
    bb_reply(qmux, service, r.cid, sdu, error, nullptr, 0);
  }
  bb_ring_note(service, message, (uint32_t)outcome, error);
  const bb_kind *k = bb_kind_note(service, message, (uint8_t)outcome, error);
  if (k && bb_log_ok(k->count))
    bb_log_request(qmux, service, r.cid, sdu, n, outcome == BB_UNHANDLED ? "UNHANDLED" : outcome == BB_DEFERRED ? "deferred to AT" : "answered",
                   error, k->count);
}

// ------------------------------------------------------------------ the AT snapshot (bb_lock held unless noted)
static uint32_t bb_hash(uint32_t h, const void *p, uint32_t n)
{
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (uint32_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

static void bb_refresh_locked(void)
{
  if (!bb_published || (bb_flags & NBB_NO_AT) || (nord2_modem_state() & NORD2_MODEM_DEAD)) return;
  if (bb_q_inflight) { bb_refresh_again = 1; return; }
  bb_refresh_again = 0;
  uint32_t want = NBM_Q_STATE_MASK;
  if (bb_identity_tries < 6) {
    if (!bb_st.imei[0]) want |= 1u << NBM_Q_CGSN;
    if (!bb_st.rev[0]) want |= 1u << NBM_Q_CGMR;
    if (bb_st.sim == NBM_SIM_READY && !bb_st.imsi[0]) want |= 1u << NBM_Q_CIMI;
    if (bb_st.sim == NBM_SIM_READY && !bb_st.have_iccid) want |= 1u << NBM_Q_ICCID;
    if (bb_st.sim == NBM_SIM_READY && !bb_st.gid_known[0]) want |= 1u << NBM_Q_GID1_FCP | 1u << NBM_Q_GID1;      // 0277j
    if (bb_st.sim == NBM_SIM_READY && !bb_st.gid_known[1]) want |= 1u << NBM_Q_GID2_FCP | 1u << NBM_Q_GID2;
    if (want & NBM_Q_IDENTITY_MASK) bb_identity_tries++;
  }
  bb_q_epoch = (bb_q_epoch + 1u) & 0xffffu;
  for (uint32_t q = 0; q < NBM_Q_COUNT; q++) {
    if (!(want & 1u << q)) continue;
    const int e = nord2_at_query(nbm_q_cmd[q], bb_at_done, reinterpret_cast<void *>(static_cast<uintptr_t>(bb_q_epoch << 8 | q)));
    if (e) { bb_q_errors++; break; }
    bb_q_inflight |= 1u << q;
  }
}

static void bb_snapshot_done(void)
{
  uint8_t radio = 0, reg;
  int roaming = 0;
  char iccid[24];
  bb_snapshots++;
  reg = nbm_reg(&bb_st, &radio, &roaming);
  uint32_t nas = 2166136261u, uim = 2166136261u;
  nas = bb_hash(nas, &reg, 1); nas = bb_hash(nas, &radio, 1); nas = bb_hash(nas, &roaming, sizeof roaming);
  nas = bb_hash(nas, &bb_st.cs_stat, sizeof bb_st.cs_stat); nas = bb_hash(nas, &bb_st.eps_stat, sizeof bb_st.eps_stat);
  nas = bb_hash(nas, bb_st.plmn, sizeof bb_st.plmn); nas = bb_hash(nas, bb_st.oper, sizeof bb_st.oper);
  nas = bb_hash(nas, &bb_st.tac, 4); nas = bb_hash(nas, &bb_st.eci, 4);
  uim = bb_hash(uim, &bb_st.sim, sizeof bb_st.sim); uim = bb_hash(uim, &bb_st.pin_lock, sizeof bb_st.pin_lock);
  const uint32_t logsig = bb_hash(bb_hash(nas, &uim, 4), &bb_st.cfun, sizeof bb_st.cfun);
  const bool have_iccid = nbm_iccid_digits(&bb_st, iccid, sizeof iccid) != 0;
  const bool identity_new = (have_iccid && !bb_had_iccid) || (bb_st.imsi[0] && !bb_had_imsi);
  if (logsig != bb_log_sig || identity_new || bb_snapshots == 1) {
    bb_log_sig = logsig;
    // 0277n: two lines, each under the 255 characters the kernel's printf keeps (q10cb lost the tail and the newline, and the next
    // line was glued to the cut one)
    printf("NORD2 BB: modem state #%u: CFUN %d SIM %d PIN-lock %d | EPS stat %d act %d tac %s | CS stat %d act %d | PLMN %s \"%.24s\" -> "
           "QMI reg %u radio %u roaming %d\n", bb_snapshots, bb_st.cfun, bb_st.sim, bb_st.pin_lock, bb_st.eps_stat, bb_st.eps_act,
           bb_st.have_tac ? "yes" : "no", bb_st.cs_stat, bb_st.cs_act, bb_st.plmn[0] ? bb_st.plmn : "-", bb_st.oper, reg, radio, roaming);
    printf("NORD2 BB: modem state #%u (cont): CSQ %d,%d CESQ rsrq %d rsrp %d | IMEI %s, rev %.48s, IMSI %s, ICCID %s (%u AT errors)\n",
           bb_snapshots, bb_st.rssi, bb_st.ber, bb_st.rsrq, bb_st.rsrp, bb_st.imei[0] ? "cached" : "missing", bb_st.rev[0] ? bb_st.rev : "missing",
           bb_st.imsi[0] ? "cached" : "missing", have_iccid ? "cached" : "missing", bb_q_errors);
  }
  if (!(bb_flags & NBB_NO_IND)) {
    if (!bb_sig_valid || nas != bb_nas_sig) {
      uint32_t sent = 0, t = nbm_nas_serving_system(&bb_st, bb_tlv, sizeof bb_tlv);
      sent += bb_indicate_all(NBQ_SVC_NAS, NBM_NAS_SERVING_SYSTEM, bb_tlv, t);
      t = nbm_nas_system_info(&bb_st, bb_tlv, sizeof bb_tlv);
      sent += bb_indicate_all(NBQ_SVC_NAS, NBM_NAS_SYSTEM_INFO_IND, bb_tlv, t);
      if (sent) printf("NORD2 BB: NAS serving system + system info indications to %u clients (reg %u radio %u)\n", sent / 2u, reg, radio);
    }
    // 0277l: the current PLMN name (CommCenter's displayed operator name) and DSD's system status, when they change
    if (!(bb_flags & NBB_NO_PLMN_IND)) {
      const uint32_t t = nbm_nas_current_plmn_name_ind(&bb_st, bb_tlv, sizeof bb_tlv);
      const uint32_t sig = t ? bb_hash(2166136261u, bb_tlv, t) : 0;
      if (t && sig != bb_plmn_ind_sig) {
        const uint32_t sent = bb_indicate_all(NBQ_SVC_NAS, NBM_NAS_CURRENT_PLMN_NAME_IND, bb_tlv, t);
        if (sent && bb_plmn_inds++ < 8) printf("NORD2 BB: NAS current PLMN name indication (\"%s\", source NITZ) to %u clients\n", bb_st.oper, sent);
        if (sent) bb_plmn_ind_sig = sig;
      }
    }
    if (!(bb_flags & NBB_NO_DATA)) {
      const uint32_t t = nbm_dsd_system_status(&bb_st, bb_tlv, sizeof bb_tlv);
      const uint32_t sig = bb_hash(2166136261u, bb_tlv, t);
      if (sig != bb_dsd_sig) {
        const uint32_t sent = bb_indicate_all(NBM_SVC_DSD, NBM_DSD_SYSTEM_STATUS_IND, bb_tlv, t);
        if (sent) bb_dsd_sig = sig;
      }
    }
    // 0277f: NAS Signal Info indication (the bars) when the LTE values change
    {
      const uint32_t t = nbm_nas_signal_ind(&bb_st, bb_tlv, sizeof bb_tlv);
      const uint32_t sig = t ? bb_hash(2166136261u, bb_tlv, t) : 0;
      if (t && sig != bb_sig_ind_sig) {
        const uint32_t sent = bb_indicate_all(NBQ_SVC_NAS, NBM_NAS_SIGNAL_INFO_IND, bb_tlv, t);
        if (sent && bb_sig_inds++ < 8) printf("NORD2 BB: NAS signal info indication to %u clients (rssi %d dBm)\n", sent, nbm_rssi_dbm(&bb_st));
      }
      bb_sig_ind_sig = sig;
    }
    if (!bb_sig_valid || uim != bb_uim_sig) {
      const uint32_t t = nbm_uim_card_status(&bb_st, bb_tlv, sizeof bb_tlv);
      const uint32_t sent = bb_indicate_all(NBQ_SVC_UIM, NBM_UIM_CARD_STATUS_IND, bb_tlv, t);
      if (sent) printf("NORD2 BB: UIM card status indication to %u clients (SIM %d)\n", sent, bb_st.sim);
    }
  }
  bb_nas_sig = nas; bb_uim_sig = uim; bb_sig_valid = 1;
  bb_wds_poll();                                                  // 0277l
  if (identity_new || (have_iccid && bb_gid_sig() != bb_act_gid_sig)) {      // 0277j: also when the GIDs became known or changed
    bb_had_iccid = have_iccid; bb_had_imsi = bb_st.imsi[0] != 0;
    if (bb_gid_sig() != bb_gid_logged_sig && (bb_gid_logged_sig = bb_gid_sig()) != 0)
      printf("NORD2 BB: SIM GID1 %s (%u bytes), GID2 %s (%u bytes)\n", bb_st.gid_known[0] == 1 ? "read" : bb_st.gid_known[0] ? "absent" : "unknown",
             bb_st.gid_len[0], bb_st.gid_known[1] == 1 ? "read" : bb_st.gid_known[1] ? "absent" : "unknown", bb_st.gid_len[1]);
    bb_activation_indicate();
  }
  // a SIM that just became ready: read its identity now rather than at the next tick
  if (bb_st.sim == NBM_SIM_READY && (!bb_st.imsi[0] || !bb_st.have_iccid || !bb_st.gid_known[0] || !bb_st.gid_known[1]) &&
      bb_identity_tries < 6)
    bb_refresh_again = 1;
  bb_hold_check();                                                // 0277m: the virtual boot waits for a settled SIM
  bb_cs_setup();                                                  // 0277r: SMS and call AT settings once the SIM is ready
  if (bb_refresh_again) bb_refresh_locked();
}

// modem service thread, no AT lock held
static void bb_at_done(void *ctx, int final, const char *text, unsigned len)
{
  const uint32_t c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx));
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  if (c & BB_CTX_UIM) {
    bb_uim_read_done(c & ~BB_CTX_UIM, final, text, len);
  } else {
    const uint32_t q = c & 0xffu;
    if (q < NBM_Q_COUNT && (bb_q_inflight & 1u << q)) {
      // 0277n: +CPIN? and +CLCK fail while the radio function (and with it the card) is off: expected, not counted (q10cb's "AT
      // errors 6" were these, from the snapshots of the boot hold before AT+CFUN=1)
      if (final != NORD2_AT_FINAL_OK && !(bb_st.cfun != 1 && (q == NBM_Q_CPIN || q == NBM_Q_CLCK))) bb_q_errors++;
      (void)nbm_parse(&bb_st, q, final, text, len);
      bb_q_inflight &= ~(1u << q);
      if (!bb_q_inflight) bb_snapshot_done();
    }
  }
  IOLockUnlock(bb_lock);
}

// ------------------------------------------------------------------ 0277m: the virtual boot held until the modem's SIM is settled
// q10ca (0277l, boot 5545F135 of the unified log): CommCenter asked for the card status ~1.5 s before the MediaTek modem was even
// ready, and the boot script turned the radio function on (AT+CFUN=1, which powers the card) 8.6 s after the modem start, so
// CommCenter saw the card "inserted, state unknown", then dead (SimUnreadable), then ready. In between its provisioning model went
// "activated / provisioned" (DMS activation state) and, with no SIM info, back to "unknown": DATA turned "basebandProvisionedState"
// to kNotProvisioned, deactivated every context (kDataContextDeactivateServiceNotProvisioned), and the Internet connection stayed
// "kDataNotSupported (context is not assigned)" for the rest of the boot (also in 119E90, 0277k), so no Start Network ever came. A
// Qualcomm baseband has read its card before ABM sees it booted. The bridge now keeps ABM at SAHARA's DONE (the last step of the
// virtual boot: no QMI client exists before it) until the modem's SIM state is final and, for a ready card, the modem registered (or
// 6 s passed). n2bb bit 20 turns it off.
static const char *bb_hold_reason(uint32_t now)
{
  if (now - bb_hold_ms >= BB_HOLD_MAX_MS) return "25 s cap";
  if (nord2_modem_state() & NORD2_MODEM_DEAD) return "modem dead";
  if (!(nord2_modem_state() & NORD2_MODEM_READY)) return nullptr;
  const int st = nbm_sim_settled(&bb_st);
  if (st != 2) bb_hold_err_ms = 0;
  if (st == 0) { bb_hold_settled_ms = 0; return nullptr; }
  if (st == 2) {
    if (!bb_hold_err_ms) bb_hold_err_ms = now ? now : 1u;
    return now - bb_hold_err_ms >= BB_HOLD_SIM_ERROR_MS ? "SIM error with the radio on" : nullptr;
  }
  if (bb_st.sim != NBM_SIM_READY) return "SIM locked or absent";
  uint8_t radio = 0;
  int roaming = 0;
  if (nbm_reg(&bb_st, &radio, &roaming) == 1) return "SIM ready, registered";
  if (!bb_hold_settled_ms) bb_hold_settled_ms = now ? now : 1u;
  return now - bb_hold_settled_ms >= BB_HOLD_REG_MS ? "SIM ready, not registered yet" : nullptr;
}
static void bb_hold_release(const char *why)
{
  if (!bb_hold) return;
  bb_hold = 0;
  for (uint32_t i = 0; i < bb_hold_out.count; i++) bb_push(CBH_SAHARA, bb_hold_out.data[i], bb_hold_out.len[i]);
  if (bb_pci_control) bb_pci_control->setProperty("bootstage", 2ull, 32);
  uint8_t radio = 0;
  int roaming = 0;
  const uint8_t reg = nbm_reg(&bb_st, &radio, &roaming);
  printf("NORD2 BB: sahara DONE_RESP released after %u ms (%s): CFUN %d SIM %d, QMI reg %u radio %u, IMEI %s, %u snapshots\n",
         bb_now_ms() - bb_hold_ms, why, bb_st.cfun, bb_st.sim, reg, radio, bb_st.imei[0] ? "cached" : "missing", bb_snapshots);
}
static void bb_hold_check(void)
{
  if (!bb_hold) return;
  const char *why = bb_hold_reason(bb_now_ms());
  if (why) bb_hold_release(why);
}
static void bb_hold_arm(void)
{
  uint64_t deadline = 0;
  clock_interval_to_deadline(BB_HOLD_POLL_MS, kMillisecondScale, &deadline);
  thread_call_enter_delayed(bb_hold_call, deadline);
}
// SAHARA just answered DONE (o: the DONE_RESP): true when it is held, false to send it now (the SIM is settled already, or no hold)
static bool bb_hold_start(const struct cbs_out &o)
{
  if ((bb_flags & (NBB_NO_BOOT_HOLD | NBB_NO_AT)) || !bb_hold_call || !o.count) return false;
  bb_hold_ms = bb_now_ms();
  bb_hold_settled_ms = bb_hold_err_ms = 0;
  if (bb_hold_reason(bb_hold_ms)) return false;
  bb_hold = 1;
  bb_hold_out = o;
  bb_holds++;
  printf("NORD2 BB: sahara DONE_RESP held (#%u) until the modem's SIM settles: modem %s, CFUN %d, SIM %d\n", bb_holds,
         (nord2_modem_state() & NORD2_MODEM_READY) ? "ready" : "not ready", bb_st.cfun, bb_st.sim);
  bb_hold_arm();
  bb_refresh_locked();
  return true;
}
// every second while held: a fresh snapshot (the release itself is decided when one completes, bb_snapshot_done) and the 25 s cap
static void bb_hold_tick(thread_call_param_t, thread_call_param_t)
{
  if (!bb_lock) return;
  IOLockLock(bb_lock);
  bb_hold_check();
  const bool again = bb_hold != 0;
  if (again) bb_refresh_locked();
  IOLockUnlock(bb_lock);
  if (again) bb_hold_arm();
}

// ------------------------------------------------------------------ one write from ABM (bb_lock held)
static void bb_host_write(uint32_t channel, const uint8_t *data, uint32_t len)
{
  bb_channel *c = &bb_ch[channel];
  c->frames_in++;
  c->bytes_in += len;
  if (channel == CBH_SAHARA) {
    const enum cbs_state before = bb_sahara.state;
    bool held = false;
    for (uint32_t at = 0; at < len && bb_sahara.state != CBS_FAILED;) {
      struct cbs_out o;
      const enum cbs_state was = bb_sahara.state;
      const uint32_t used = cbs_receive(&bb_sahara, data + at, len - at, &o);
      if (was == CBS_WAIT_DONE && bb_sahara.state == CBS_FINISHED && bb_hold_start(o)) held = true;   // 0277m
      else for (uint32_t i = 0; i < o.count; i++) bb_push(CBH_SAHARA, o.data[i], o.len[i]);
      if (!used) break;
      at += used;
    }
    if (bb_sahara.state != before && bb_sahara.state == CBS_COMMAND)
      printf("NORD2 BB: sahara command mode\n");
    if (bb_sahara.state != before && bb_sahara.state == CBS_FINISHED && bb_pci_control && !held)
      bb_pci_control->setProperty("bootstage", 2ull, 32);
    if (bb_sahara.state != before && (bb_sahara.state == CBS_FINISHED || bb_sahara.state == CBS_FAILED || bb_sahara.state == CBS_WAIT_DONE))
      printf("NORD2 BB: sahara %s, image %u of %u (id 0x%x), host mode %u status %u, last command %u, %u executes, %u requests, %llu bytes\n",
             bb_sahara.state == CBS_FINISHED ? "finished" : bb_sahara.state == CBS_FAILED ? "FAILED" : "image read",
             bb_sahara.image_index + 1, bb_sahara.image_count, bb_sahara.images[bb_sahara.image_index], bb_sahara.host_mode,
             bb_sahara.host_status, bb_sahara.last_command, bb_sahara.executes, bb_sahara.requests, bb_sahara.bytes);
    return;
  }
  if (channel != CBH_QMUX1 && channel != CBH_QMUX2) {
    // nothing behind it: counted; the first writes are shown (binary protocol headers, no identities expected)
    if (c->writes_logged < 4) {
      char hex[40];
      unsigned p = 0;
      hex[0] = 0;
      for (uint32_t i = 0; i < len && i < 12 && p + 3 < sizeof hex; i++) p += (unsigned)snprintf(hex + p, sizeof hex - p, "%02x", data[i]);
      printf("NORD2 BB: %s write %u bytes [%s] (nothing behind this channel)\n", cbh_published[channel], len, hex);
      c->writes_logged++;
    }
    return;
  }
  const uint32_t qmux = channel == CBH_QMUX1 ? 0 : 1;
  bb_qmux_in[qmux]++;
  const struct nbq_result r = nbq_from_host(&bb_qmux, qmux, data, len, bb_out, sizeof bb_out);
  switch (r.kind) {
  case NBQ_TO_HOST: {
    bb_push(channel, bb_out, r.len);
    const bb_kind *k = bb_kind_note(0, r.ctl_message, BB_ANSWERED, 0);
    if (k && bb_log_ok(k->count)) {
      if (r.ctl_message == NBQ_CTL_GET_CLIENT_ID)
        printf("NORD2 BB: q%u CTL GetClientId %s(0x%02x) -> %s cid %u (#%u, %u clients)\n", qmux + 1u, nbq_service_name(r.service), r.service,
               r.cid ? "granted" : "REFUSED", r.cid, k->count, nbq_clients(&bb_qmux, 0xffffffffu));
      else
        printf("NORD2 BB: q%u CTL msg 0x%04x %s(0x%02x) cid %u -> answered (#%u)\n", qmux + 1u, r.ctl_message, nbq_service_name(r.service),
               r.service, r.cid, k->count);
    }
    if (bb_qmux.ctl_other != bb_ctl_logged) {
      bb_ctl_logged = bb_qmux.ctl_other;
      printf("NORD2 BB: CTL message 0x%04x accepted without action\n", bb_qmux.last_ctl_other);
    }
    break;
  }
  case NBQ_LOCAL:
    bb_qmi_request(qmux, r, data + NBQ_HDR, r.len);
    break;
  case NBQ_DROP:
    if (bb_dropped_logged++ < 16)
      printf("NORD2 BB: q%u frame dropped (%u bytes: if %02x len %u flags %02x svc 0x%02x cid %u type %u), unknown client or malformed\n",
             qmux + 1u, len, len > 0 ? data[0] : 0u, len > 2 ? nbq_rd16(data + 1) : 0u, len > 3 ? data[3] : 0u, len > 4 ? data[4] : 0u,
             len > 5 ? data[5] : 0u, len > NBQ_HDR ? data[NBQ_HDR] : 0u);
    break;
  default:
    break;
  }
}

// ------------------------------------------------------------------ asynchronous completions
// IOUserClient::sendAsyncResult64 is a protected static; this never-instantiated subclass only forwards to it.
class Nord2BasebandAsync : public IOUserClient {
public:
  static IOReturn send(OSAsyncReference64 reference, IOReturn result, io_user_reference_t *args, UInt32 count)
  {
    return sendAsyncResult64(reference, result, args, count);
  }
};

static void bb_finish_read(bb_read *read, IOReturn result, const uint8_t *data, uint32_t len)
{
  io_user_reference_t args[1] = { 0 };
  if (data && read->memory) {
    const uint32_t n = len < read->len ? len : read->len;
    args[0] = read->memory->writeBytes(0, data, n);
    if (len > read->len) result = kIOReturnOverrun;
  }
  if (read->memory) {
    read->memory->complete();
    read->memory->release();
    read->memory = nullptr;
  }
  Nord2BasebandAsync::send(read->reference, result, args, 1);
}

static void bb_complete(thread_call_param_t, thread_call_param_t)
{
  for (;;) {
    bb_read read;
    bb_frame frame = { 0, nullptr };
    bool found = false;
    IOLockLock(bb_lock);
    for (uint32_t c = 0; c < CBH_PUBLISHED && !found; c++) {
      bb_channel *ch = &bb_ch[c];
      if (!ch->reads_waiting || !ch->count) continue;
      read = ch->reads[0];
      memmove(&ch->reads[0], &ch->reads[1], (--ch->reads_waiting) * sizeof ch->reads[0]);
      frame = bb_pop(c);
      found = true;
    }
    IOLockUnlock(bb_lock);
    if (!found) return;
    bb_finish_read(&read, kIOReturnSuccess, frame.data, frame.len);
    IOFreeData(frame.data, frame.len);
  }
}

// Every read a client has waiting ends with `result` (close, abort).
static void bb_abort_reads(uint32_t channel, void *owner, IOReturn result)
{
  bb_read ended[BB_READS];
  uint32_t n = 0;
  IOLockLock(bb_lock);
  bb_channel *ch = &bb_ch[channel];
  for (uint32_t i = 0; i < ch->reads_waiting;) {
    if (!owner || ch->reads[i].owner == owner) {
      ended[n++] = ch->reads[i];
      memmove(&ch->reads[i], &ch->reads[i + 1], (--ch->reads_waiting - i) * sizeof ch->reads[0]);
    } else {
      i++;
    }
  }
  IOLockWakeup(bb_lock, ch, false);
  IOLockUnlock(bb_lock);
  for (uint32_t i = 0; i < n; i++) bb_finish_read(&ended[i], result, nullptr, 0);
}

// ------------------------------------------------------------------ user memory
static uint8_t *bb_copy_in(task_t task, mach_vm_address_t address, uint32_t len)
{
  if (!len || len > BB_FRAME_MAX) return nullptr;
  IOMemoryDescriptor *memory = IOMemoryDescriptor::withAddressRange(address, len, kIODirectionOut, task);
  uint8_t *copy = static_cast<uint8_t *>(IOMallocData(len));
  bool ok = memory && copy && memory->prepare() == kIOReturnSuccess;
  if (ok) {
    ok = memory->readBytes(0, copy, len) == len;
    memory->complete();
  }
  OSSafeReleaseNULL(memory);
  if (!ok && copy) {
    IOFreeData(copy, len);
    copy = nullptr;
  }
  return copy;
}

static bool bb_async(IOExternalMethodArguments *args) { return args->asyncWakePort != MACH_PORT_NULL && args->asyncReference; }

static void bb_async_reply(IOExternalMethodArguments *args, IOReturn result, uint32_t count, uint64_t a = 0, uint64_t b = 0)
{
  io_user_reference_t values[2] = { a, b };
  OSAsyncReference64 reference;
  memset(reference, 0, sizeof reference);
  memcpy(reference, args->asyncReference, sizeof(io_user_reference_t) *
         (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
  Nord2BasebandAsync::send(reference, result, values, count);
}

// ------------------------------------------------------------------ AppleBasebandPCIInterface
class AppleBasebandPCIUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandPCIUserClient)
  task_t fTask;
  uint32_t fChannel;
public:
  bool initWithTask(task_t task, void *security, UInt32 type, OSDictionary *properties) override
  {
    fTask = task;
    return IOUserClient::initWithTask(task, security, type, properties);
  }
  void setChannel(uint32_t channel) { fChannel = channel; }
  IOReturn clientClose() override
  {
    closeChannel();
    terminate();
    return kIOReturnSuccess;
  }
  void closeChannel()
  {
    bb_abort_reads(fChannel, this, kIOReturnAborted);
    IOLockLock(bb_lock);
    if (bb_ch[fChannel].owner == this) bb_ch[fChannel].owner = nullptr;
    IOLockUnlock(bb_lock);
  }
  IOReturn write(IOExternalMethodArguments *args);
  IOReturn read(IOExternalMethodArguments *args);
  IOReturn readRegister(IOExternalMethodArguments *args);
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override;
};
OSDefineMetaClassAndStructors(AppleBasebandPCIUserClient, IOUserClient)

IOReturn AppleBasebandPCIUserClient::write(IOExternalMethodArguments *args)
{
  uint8_t *copy = nullptr;
  uint32_t len = 0;
  if (bb_async(args)) {
    if (args->scalarInputCount < 2) return kIOReturnBadArgument;
    len = (uint32_t)args->scalarInput[1];
    copy = bb_copy_in(fTask, args->scalarInput[0], len);
  } else if (args->structureInputDescriptor) {
    IOMemoryDescriptor *memory = args->structureInputDescriptor;
    len = (uint32_t)memory->getLength();
    copy = len && len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
    bool ok = copy && memory->prepare() == kIOReturnSuccess;
    if (ok) {
      ok = memory->readBytes(0, copy, len) == len;
      memory->complete();
    }
    if (!ok && copy) {
      IOFreeData(copy, len);
      copy = nullptr;
    }
  } else if (args->structureInput && args->structureInputSize) {
    len = args->structureInputSize;
    copy = len <= BB_FRAME_MAX ? static_cast<uint8_t *>(IOMallocData(len)) : nullptr;
    if (copy) memcpy(copy, args->structureInput, len);
  }
  if (!copy) return kIOReturnBadArgument;
  IOLockLock(bb_lock);
  bb_host_write(fChannel, copy, len);
  IOLockUnlock(bb_lock);
  IOFreeData(copy, len);
  if (bb_async(args)) bb_async_reply(args, kIOReturnSuccess, 1, len);
  return kIOReturnSuccess;
}

IOReturn AppleBasebandPCIUserClient::read(IOExternalMethodArguments *args)
{
  if (bb_async(args)) {
    if (args->scalarInputCount < 2 || !args->scalarInput[1]) return kIOReturnBadArgument;
    bb_read r;
    memset(&r, 0, sizeof r);
    memcpy(r.reference, args->asyncReference, sizeof(io_user_reference_t) *
           (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
    r.len = (uint32_t)args->scalarInput[1];
    r.owner = this;
    r.memory = IOMemoryDescriptor::withAddressRange(args->scalarInput[0], r.len, kIODirectionIn, fTask);
    if (!r.memory || r.memory->prepare() != kIOReturnSuccess) {
      OSSafeReleaseNULL(r.memory);
      return kIOReturnVMError;
    }
    IOLockLock(bb_lock);
    bb_channel *ch = &bb_ch[fChannel];
    const bool queued = ch->reads_waiting < BB_READS;
    if (queued) ch->reads[ch->reads_waiting++] = r;
    if (queued && ch->count && bb_complete_call) thread_call_enter(bb_complete_call);
    IOLockUnlock(bb_lock);
    if (!queued) {
      r.memory->complete();
      r.memory->release();
      return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
  }
  // synchronous: the next frame, waiting up to BB_SYNC_READ_SECONDS
  uint64_t deadline = 0;
  clock_interval_to_deadline(BB_SYNC_READ_SECONDS, kSecondScale, &deadline);
  IOLockLock(bb_lock);
  bb_channel *ch = &bb_ch[fChannel];
  int waited = THREAD_AWAKENED;
  while (!ch->count && ch->owner == this && waited == THREAD_AWAKENED)
    waited = IOLockSleepDeadline(bb_lock, ch, deadline, THREAD_INTERRUPTIBLE);
  bb_frame frame = bb_pop(fChannel);
  IOLockUnlock(bb_lock);
  if (!frame.data) return waited == THREAD_TIMED_OUT ? kIOReturnTimeout : kIOReturnAborted;
  IOReturn result = kIOReturnSuccess;
  if (args->structureOutputDescriptor) {
    IOMemoryDescriptor *memory = args->structureOutputDescriptor;
    const uint32_t room = (uint32_t)memory->getLength(), n = frame.len < room ? frame.len : room;
    if (memory->prepare() == kIOReturnSuccess) {
      memory->writeBytes(0, frame.data, n);
      memory->complete();
      args->structureOutputDescriptorSize = n;
    } else {
      result = kIOReturnVMError;
    }
    if (frame.len > room) result = kIOReturnOverrun;
  } else if (args->structureOutput) {
    const uint32_t n = frame.len < args->structureOutputSize ? frame.len : args->structureOutputSize;
    memcpy(args->structureOutput, frame.data, n);
    if (frame.len > args->structureOutputSize) result = kIOReturnOverrun;
    args->structureOutputSize = n;
  } else {
    result = kIOReturnBadArgument;
  }
  IOFreeData(frame.data, frame.len);
  return result;
}

// IOABPClass::readRegister: one scalar, the register (kABPBHISerialNum 0 ... kABPBHIOEMPkHash 4), and a structure output.
IOReturn AppleBasebandPCIUserClient::readRegister(IOExternalMethodArguments *args)
{
  if (args->scalarInputCount < 1) return kIOReturnBadArgument;
  const uint32_t reg = (uint32_t)args->scalarInput[0];
  static uint32_t seen;
  if (reg < 32 && !(__atomic_fetch_or(&seen, 1u << reg, __ATOMIC_RELAXED) & (1u << reg)))
    printf("NORD2 BB: %s register %u read, %u bytes\n", cbh_published[fChannel], reg,
           args->structureOutputDescriptor ? (uint32_t)args->structureOutputDescriptor->getLength() : args->structureOutputSize);
  if (args->structureOutput && args->structureOutputSize) {
    cbs_read_register(reg, static_cast<uint8_t *>(args->structureOutput), args->structureOutputSize);
    return kIOReturnSuccess;
  }
  if (IOMemoryDescriptor *memory = args->structureOutputDescriptor) {
    const uint32_t size = (uint32_t)memory->getLength();
    if (!size || size > BB_FRAME_MAX) return kIOReturnBadArgument;
    uint8_t *data = static_cast<uint8_t *>(IOMallocData(size));
    if (!data) return kIOReturnNoMemory;
    cbs_read_register(reg, data, size);
    IOReturn result = memory->prepare();
    if (result == kIOReturnSuccess) {
      memory->writeBytes(0, data, size);
      memory->complete();
      args->structureOutputDescriptorSize = size;
    }
    IOFreeData(data, size);
    return result;
  }
  return kIOReturnBadArgument;
}

// Selectors and shapes: qcombb_channels.h (libIOABP's IOConnectCall* sites, iPadOS 17; iOS 16 libIOABP has the same method set).
IOReturn AppleBasebandPCIUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *,
                                                    OSObject *, void *)
{
  if (!args) return kIOReturnBadArgument;
  switch (selector) {
  case 0: {   // open
    IOLockLock(bb_lock);
    bb_channel *ch = &bb_ch[fChannel];
    const bool free_channel = !ch->owner || ch->owner == this;
    if (free_channel) {
      ch->owner = this;
      if (!ch->opens++) printf("NORD2 BB: %s opened\n", cbh_published[fChannel]);
      if (fChannel == CBH_SAHARA && bb_boot_image_sent) {
        bb_boot_image_sent = false;
        bb_sahara_restart();
      }
    }
    IOLockUnlock(bb_lock);
    if (!free_channel) printf("NORD2 BB: %s open refused (already owned)\n", cbh_published[fChannel]);
    return free_channel ? kIOReturnSuccess : kIOReturnExclusiveAccess;
  }
  case 1:     // close
    closeChannel();
    return kIOReturnSuccess;
  case 2:
    return write(args);
  case 3:
    return read(args);
  case 4:     // sendImage (BHI): nothing to load; the image is accepted and dropped
    printf("NORD2 BB: %s sendImage %llu bytes dropped\n", cbh_published[fChannel], args->scalarInputCount > 1 ? args->scalarInput[1] : 0ull);
    IOLockLock(bb_lock);
    if (fChannel == CBH_PBL) bb_boot_image_sent = true;
    IOLockUnlock(bb_lock);
    if (bb_async(args)) bb_async_reply(args, kIOReturnSuccess, 2, args->scalarInputCount > 1 ? args->scalarInput[1] : 0, 0);
    return kIOReturnSuccess;
  case 5:
    return readRegister(args);
  case 6:     // abort channel: pending reads end
    bb_abort_reads(fChannel, this, kIOReturnAborted);
    if (bb_async(args)) bb_async_reply(args, kIOReturnSuccess, 0);
    return kIOReturnSuccess;
  case 7:     // start channel
  case 9:     // map shared memory: nothing here reads it
  case 10:    // unmap
  case 12:    // deregister time event
    if (bb_async(args)) bb_async_reply(args, kIOReturnSuccess, 0);
    return kIOReturnSuccess;
  case 11:    // register time event: none is ever sent
    return kIOReturnSuccess;
  default: {  // 8 time sync: no time base behind this channel
    static uint32_t seen;
    if (selector < 32 && !(__atomic_fetch_or(&seen, 1u << selector, __ATOMIC_RELAXED) & (1u << selector)))
      printf("NORD2 BB: channel %s selector %u unsupported (scalars in %u)\n", cbh_published[fChannel], selector, args->scalarInputCount);
    return kIOReturnUnsupported;
  }
  }
}

class AppleBasebandPCIInterface : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIInterface)
  uint32_t fChannel;
public:
  bool setup(uint32_t channel)
  {
    fChannel = channel;
    return setProperty(CBH_PROTOCOL_KEY, cbh_published[channel]);
  }
  // IOABPClass::start_nl puts ABPInterfaceProtocol straight in the IOServiceMatching dictionary; IOKit does not compare a key it does
  // not know, the family's matchPropertyTable does, and without it every client gets whichever channel comes first.
  bool matchPropertyTable(OSDictionary *table, SInt32 *score) override
  {
    return IOService::matchPropertyTable(table, score) && matchProtocol(table);
  }
  bool matchPropertyTable(OSDictionary *table) override
  {
    return IOService::matchPropertyTable(table) && matchProtocol(table);
  }
  bool matchProtocol(OSDictionary *table) const
  {
    OSString *wanted = OSDynamicCast(OSString, table->getObject(CBH_PROTOCOL_KEY));
    return !wanted || wanted->isEqualTo(cbh_published[fChannel]);
  }
  IOReturn newUserClient(task_t task, void *security, UInt32 type, OSDictionary *properties, IOUserClient **out) override
  {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    static uint32_t seen;
    if (!(__atomic_fetch_or(&seen, 1u << fChannel, __ATOMIC_RELAXED) & (1u << fChannel)))
      printf("NORD2 BB: first user client on %s\n", cbh_published[fChannel]);
    AppleBasebandPCIUserClient *client = OSTypeAlloc(AppleBasebandPCIUserClient);
    if (!client) return kIOReturnNoMemory;
    client->setChannel(fChannel);
    if (!client->initWithTask(task, security, type, properties) || !client->attach(this)) {
      client->release();
      return kIOReturnError;
    }
    if (!client->start(this)) {
      client->detach(this);
      client->release();
      return kIOReturnError;
    }
    *out = client;
    return kIOReturnSuccess;
  }
  IOReturn newUserClient(task_t task, void *security, UInt32 type, IOUserClient **out) override
  {
    return newUserClient(task, security, type, nullptr, out);
  }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIInterface, IOService)

// ------------------------------------------------------------------ AppleBasebandPCIControl
class AppleBasebandPCIControlUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandPCIControlUserClient)
  OSAsyncReference64 fLogs;
  bool fLogsPending;
public:
  void endLogs()
  {
    if (!fLogsPending) return;
    fLogsPending = false;
    io_user_reference_t none[1] = { 0 };
    Nord2BasebandAsync::send(fLogs, kIOReturnAborted, none, 1);
  }
  IOReturn clientClose() override
  {
    endLogs();
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    static uint32_t seen;
    if (selector < 32 && !(__atomic_fetch_or(&seen, 1u << selector, __ATOMIC_RELAXED) & (1u << selector)))
      printf("NORD2 BB: AppleBasebandPCIControl selector %u (scalars in %u, async %d)\n", selector, args->scalarInputCount, bb_async(args));
    switch (selector) {
    case 0:   // open
    case 3:   // logger tune
      return kIOReturnSuccess;
    case 1:   // close
      endLogs();
      return kIOReturnSuccess;
    case 2:   // read logs: there are none; the request waits until close
      if (!bb_async(args) || fLogsPending) return kIOReturnBadArgument;
      memset(fLogs, 0, sizeof fLogs);
      memcpy(fLogs, args->asyncReference, sizeof(io_user_reference_t) *
             (args->asyncReferenceCount < kOSAsyncRef64Count ? args->asyncReferenceCount : kOSAsyncRef64Count));
      fLogsPending = true;
      return kIOReturnSuccess;
    default:
      return kIOReturnUnsupported;
    }
  }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIControlUserClient, IOUserClient)

template <class Client>
static IOReturn bb_open(IOService *service, task_t task, void *security, UInt32 type, OSDictionary *properties, IOUserClient **out)
{
  if (!out) return kIOReturnBadArgument;
  *out = nullptr;
  Client *client = OSTypeAlloc(Client);
  if (!client) return kIOReturnNoMemory;
  if (!client->initWithTask(task, security, type, properties) || !client->attach(service)) {
    client->release();
    return kIOReturnError;
  }
  if (!client->start(service)) {
    client->detach(service);
    client->release();
    return kIOReturnError;
  }
  *out = client;
  return kIOReturnSuccess;
}

class AppleBasebandPCIControl : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIControl)
public:
  BB_WATCHED(IOService)
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    static uint32_t seen;
    if (!__atomic_exchange_n(&seen, 1, __ATOMIC_RELAXED)) printf("NORD2 BB: first user client on AppleBasebandPCIControl\n");
    return bb_open<AppleBasebandPCIControlUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleBasebandPCIControl, IOService)

// ------------------------------------------------------------------ AppleBaseband
// libTelephonyBasebandDynamic's __TelephonyBasebandControllerCallMethod; selectors as the reference read them on iPadOS 17
// (qcombb_control.h). iOS 16's numbering is not verified: the first call of every selector is logged with its scalars.
class AppleBasebandUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleBasebandUserClient)
public:
  IOReturn clientClose() override
  {
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    uint64_t out[4] = { 0 };
    uint32_t nout = args->scalarOutputCount < 4 ? args->scalarOutputCount : 4;
    IOLockLock(bb_lock);
    int result = cbc_call(&bb_control, selector, args->scalarInput, args->scalarInputCount, out, &nout);
    const bool power_on = selector == CBC_POWER && args->scalarInputCount && args->scalarInput[0];
    // A reset or power-on goes back to before SendImage: nothing greets until the next one.
    if (!result && (selector == CBC_RESET_MODEM || selector == CBC_POWERCYCLE || power_on)) {
      bb_boot_image_sent = false;
      bb_flush(CBH_SAHARA);
      // 0277f: a hard reset (selector 0 / powercycle): who asked last, and how long this boot lived
      if (selector != CBC_POWER) {
        char why[80];
        bb_hard_resets++;
        snprintf(why, sizeof why, "hard reset #%u by ABM (selector %u [%llx])", bb_hard_resets, selector,
                 args->scalarInputCount ? args->scalarInput[0] : 0ull);
        if (bb_hard_resets <= 24) bb_ring_dump(why);
      }
      bb_up_ms = bb_now_ms();
      if (power_on) bb_power_ons++;
    }
    // 0277g: the reset-detect line follows power (bb_reset_step): alive after a power-on, reset detected after a power-off. ABM's own
    // hard reset or power change ends a soft reset still in progress (its pending reset detected is replaced).
    if (!result && (selector == CBC_RESET_MODEM || selector == CBC_POWERCYCLE || selector == CBC_POWER)) bb_reset_phase = 0;
    if (!result && !(bb_flags & NBB_NO_SOFT_RESET) && selector == CBC_POWER && args->scalarInputCount)
      bb_rd_schedule(args->scalarInput[0] ? BB_RD_ALIVE : BB_RD_RESET, args->scalarInput[0] ? 200 : 50, false,
                     args->scalarInput[0] ? "power-on" : "power-off");
    // 0277f: GetReset reads the reset-detect line: 1 between "reset detected" and "alive"
    if (!result && selector == CBC_GET_RESET && nout) { out[0] = bb_reset_held; bb_get_reset_polls++; }
    IOLockUnlock(bb_lock);
    const bool unknown = result == CBC_UNSUPPORTED;
    if (unknown && !(bb_flags & NBB_STRICT_SELECTORS)) {
      nout = args->scalarOutputCount < 4 ? args->scalarOutputCount : 4;
      for (uint32_t i = 0; i < nout; i++) out[i] = 0;
      result = kIOReturnSuccess;
    }
    static uint32_t seen[2], power_logged;
    const bool first = selector < 64 && !(__atomic_fetch_or(&seen[selector / 32], 1u << (selector % 32), __ATOMIC_RELAXED) & (1u << (selector % 32)));
    // 0277f: every reset/power call (the first 48), not only the first of each selector
    const bool power = (selector == CBC_RESET_MODEM || selector == CBC_POWER || selector == CBC_POWERCYCLE) &&
                       __atomic_fetch_add(&power_logged, 1u, __ATOMIC_RELAXED) < 48u;
    if (first || power)
      printf("NORD2 BB: AppleBaseband selector %u in %u [%llx %llx] struct-in %u out %u -> 0x%x%s [%llx]\n", selector, args->scalarInputCount,
             args->scalarInputCount > 0 ? args->scalarInput[0] : 0ull, args->scalarInputCount > 1 ? args->scalarInput[1] : 0ull,
             args->structureInputSize, nout, (uint32_t)result, unknown ? (bb_flags & NBB_STRICT_SELECTORS ? " (unknown: unsupported)" : " (unknown: zeros)") : "",
             nout ? out[0] : 0ull);
    for (uint32_t i = 0; i < nout; i++) args->scalarOutput[i] = out[i];
    args->scalarOutputCount = nout;
    return result;
  }
};
OSDefineMetaClassAndStructors(AppleBasebandUserClient, IOUserClient)

class AppleBaseband : public IOService {
  OSDeclareDefaultStructors(AppleBaseband)
public:
  BB_WATCHED(IOService)
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    static uint32_t seen;
    if (!__atomic_exchange_n(&seen, 1, __ATOMIC_RELAXED)) printf("NORD2 BB: first user client on AppleBaseband (type %u)\n", type);
    return bb_open<AppleBasebandUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleBaseband, IOService)

// The D22AP /device-tree/baseband node: libTelephonyBaseband's _TelephonyBasebandControllerLoadConfigOnce reads its "config" (OSData,
// 40 bytes on D22AP). Our ADT has no such node, so it is published here, named "baseband", with D22AP's values.
class Nord2BasebandNode : public IOService {
  OSDeclareDefaultStructors(Nord2BasebandNode)
public:
  BB_WATCHED(IOService)
};
OSDefineMetaClassAndStructors(Nord2BasebandNode, IOService)
static const uint8_t bb_d22_config[40] = {
  0xfa, 0x00, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0x44, 0x48, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
  0x64, 0x00, 0x00, 0x00, 0x83, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
};

// ------------------------------------------------------------------ the data plane's kernel services
// ABM's data::TransportService calls ApplePDPHelperInterface::getInstance()->enableMetadataTrailer() without a null check, and getInstance
// is null without an AppleIPAppender service to open. Every AppleIPAppender method succeeds with zeroed outputs; the first use of each
// selector is logged with its shape (sizes and counts only).
class AppleIPAppenderUserClient : public IOUserClient {
  OSDeclareDefaultStructors(AppleIPAppenderUserClient)
public:
  IOReturn clientClose() override
  {
    terminate();
    return kIOReturnSuccess;
  }
  IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *, OSObject *, void *) override
  {
    if (!args) return kIOReturnBadArgument;
    static uint32_t seen;
    if (selector < 32 && !(__atomic_fetch_or(&seen, 1u << selector, __ATOMIC_RELAXED) & (1u << selector)))
      printf("NORD2 BB: AppleIPAppender selector %u scalar-in %u scalar-out %u struct-in %u struct-out %u descriptor-in %llu descriptor-out %llu, "
             "answered with zeros\n", selector, args->scalarInputCount, args->scalarOutputCount, args->structureInputSize, args->structureOutputSize,
             static_cast<unsigned long long>(args->structureInputDescriptor ? args->structureInputDescriptor->getLength() : 0),
             static_cast<unsigned long long>(args->structureOutputDescriptor ? args->structureOutputDescriptor->getLength() : 0));
    for (uint32_t i = 0; i < args->scalarOutputCount; i++) args->scalarOutput[i] = 0;
    if (args->structureOutput && args->structureOutputSize) bzero(args->structureOutput, args->structureOutputSize);
    if (args->structureOutputDescriptor) args->structureOutputDescriptorSize = 0;
    return kIOReturnSuccess;
  }
};
OSDefineMetaClassAndStructors(AppleIPAppenderUserClient, IOUserClient)

class AppleIPAppender : public IOService {
  OSDeclareDefaultStructors(AppleIPAppender)
public:
  BB_WATCHED(IOService)
  IOReturn newUserClient(task_t t, void *s, UInt32 type, OSDictionary *p, IOUserClient **out) override
  {
    return bb_open<AppleIPAppenderUserClient>(this, t, s, type, p, out);
  }
  IOReturn newUserClient(task_t t, void *s, UInt32 type, IOUserClient **out) override { return newUserClient(t, s, type, nullptr, out); }
};
OSDefineMetaClassAndStructors(AppleIPAppender, IOService)

// libPCITransport's pci::event::PDPListener matches AppleBasebandPCIPDPManagerBase (iOS 16 too); the leaf is the reference's Mav class.
class AppleBasebandPCIPDPManagerBase : public IOService {
  OSDeclareDefaultStructors(AppleBasebandPCIPDPManagerBase)
public:
  BB_WATCHED(IOService)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIPDPManagerBase, IOService)

class AppleBasebandPCIPDPManager : public AppleBasebandPCIPDPManagerBase {
  OSDeclareDefaultStructors(AppleBasebandPCIPDPManager)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIPDPManager, AppleBasebandPCIPDPManagerBase)

class AppleBasebandPCIMAVPDPManager : public AppleBasebandPCIPDPManager {
  OSDeclareDefaultStructors(AppleBasebandPCIMAVPDPManager)
};
OSDefineMetaClassAndStructors(AppleBasebandPCIMAVPDPManager, AppleBasebandPCIPDPManager)

// ------------------------------------------------------------------ the APFS preboot volume (0277b; opt-in since 0277e, n2bb bit 10)
// Only MSUDataAccessorRestore (restore ramdisk, /mnt1 present) looks for volumes; the installed OS's MSUDataAccessor uses /private/preboot.
// MSUDataAccessorRestore -[copyMountPointForVolumeType:0] wants exactly one LPAPFSVolume with role Preboot (6) and a mount point.
// libpartition2 builds it from an IOMedia whose "Content" is the APFS volume type; -[LPAPFSVolume role] is APFSVolumeRole(BSD Name):
// the AppleAPFSVolume matched by IOBSDNameMatching, its "RoleValue" (16 = APFS_VOL_ROLE_PREBOOT, mapped to 6); -[LPMedia mountPoint]
// is the getmntinfo entry whose f_mntfromname is "/dev/<BSD Name>". The volume below claims the preboot role for the root device
// (boot-arg rd, "/dev/ufsroot0 on / (hfs)"), so the personalized root is "/" + the boot-manifest-hash in hex (48 zero bytes on this
// device: 96 '0', the same /private/preboot/<hash> name launchd uses) and the firmware folder BBUpdater gets is
// /<96 zeros>/usr/local/standalone/firmware/Baseband/Mav17 (the rootfs has to provide it, see HOW-TO-TEST.md).
// This kernel has no IOStorageFamily, so IOMedia is defined here. The identifying properties (BSD Name, Content, RoleValue, ...) are
// served only to CommCenter and abm-helper; every other process and the kernel see a bare IOMedia that matches no BSD name, has no
// content type and so is not a volume to diskarbitrationd, apfs_iosd, deleted or the kernel's own IOMedia users.
class IOMedia : public IOService {
  OSDeclareDefaultStructors(IOMedia)
};
OSDefineMetaClassAndStructors(IOMedia, IOService)

class AppleAPFSVolume : public IOMedia {
  OSDeclareDefaultStructors(AppleAPFSVolume)
  OSDictionary *fHidden;
  static bool allowed()
  {
    char name[32];
    proc_selfname(name, sizeof name);
    return !strcmp(name, "CommCenter") || !strcmp(name, "abm-helper");
  }
  OSObject *hidden(const char *key) const
  {
    OSObject *value = fHidden && key ? fHidden->getObject(key) : nullptr;
    if (!value) return nullptr;
    const bool ok = allowed();
    bb_note_access(ok ? "preboot volume serves" : "preboot volume hides", "AppleAPFSVolume", key);
    return ok ? value : nullptr;
  }
public:
  using IOMedia::copyProperty;
  bool setupPreboot(const char *bsd)
  {
    static const char content[] = "41504653-0000-11AA-AA11-00306543ECAC";   // APFS volume
    fHidden = OSDictionary::withCapacity(12);
    OSString *role = OSString::withCString("Preboot");
    const OSObject *role_object = role;
    OSArray *roles = role ? OSArray::withObjects(&role_object, 1) : nullptr;
    OSString *b = OSString::withCString(bsd), *c = OSString::withCString(content), *u = OSString::withCString("4E324242-5052-4542-4F4F-543030303030"),
             *full = OSString::withCString("Preboot");
    OSNumber *rv = OSNumber::withNumber(16ull, 16), *size = OSNumber::withNumber(1ull << 30, 64);
    bool ok = fHidden && roles && b && c && u && full && rv && size;
    if (ok) {
      fHidden->setObject("BSD Name", b);
      fHidden->setObject("Content", c);
      fHidden->setObject("RoleValue", rv);
      fHidden->setObject("Role", roles);
      fHidden->setObject("UUID", u);
      fHidden->setObject("Size", size);
      fHidden->setObject("Whole", kOSBooleanFalse);
      fHidden->setObject("Leaf", kOSBooleanTrue);
      fHidden->setObject("Writable", kOSBooleanTrue);
      fHidden->setObject("Ejectable", kOSBooleanFalse);
      fHidden->setObject("Removable", kOSBooleanFalse);
      fHidden->setObject("FullName", full);
    }
    OSSafeReleaseNULL(role); OSSafeReleaseNULL(roles); OSSafeReleaseNULL(b); OSSafeReleaseNULL(c); OSSafeReleaseNULL(u); OSSafeReleaseNULL(full);
    OSSafeReleaseNULL(rv); OSSafeReleaseNULL(size);
    if (ok) setName("Preboot");
    return ok;
  }
  void free() override
  {
    OSSafeReleaseNULL(fHidden);
    IOMedia::free();
  }
  OSObject *copyProperty(const OSSymbol *key) const override
  {
    if (OSObject *v = key ? hidden(key->getCStringNoCopy()) : nullptr) { v->retain(); return v; }
    if (key && fHidden && fHidden->getObject(key)) return nullptr;
    return IOMedia::copyProperty(key);
  }
  OSObject *copyProperty(const OSString *key) const override
  {
    if (OSObject *v = key ? hidden(key->getCStringNoCopy()) : nullptr) { v->retain(); return v; }
    if (key && fHidden && fHidden->getObject(key)) return nullptr;
    return IOMedia::copyProperty(key);
  }
  OSObject *copyProperty(const char *key) const override
  {
    if (OSObject *v = hidden(key)) { v->retain(); return v; }
    if (key && fHidden && fHidden->getObject(key)) return nullptr;
    return IOMedia::copyProperty(key);
  }
  bool serializeProperties(OSSerialize *s) const override
  {
    if (!fHidden || !allowed()) return IOMedia::serializeProperties(s);
    bb_note_access("preboot volume serves", "AppleAPFSVolume", "(all properties)");
    OSDictionary *all = dictionaryWithProperties();
    if (!all) return false;
    all->merge(fHidden);
    const bool ok = all->serialize(s);
    all->release();
    return ok;
  }
};
OSDefineMetaClassAndStructors(AppleAPFSVolume, IOMedia)

static IOService *bb_publish_preboot(IOService *provider)
{
  char bsd[64] = "ufsroot0";
  char rd[64] = "";
  if (PE_parse_boot_argn("rd", rd, sizeof rd) && rd[0]) strlcpy(bsd, strncmp(rd, "/dev/", 5) ? rd : rd + 5, sizeof bsd);
  AppleAPFSVolume *volume = OSTypeAlloc(AppleAPFSVolume);
  bool ok = volume && volume->init() && volume->setupPreboot(bsd) && volume->attach(provider);
  if (ok && !volume->start(provider)) {
    volume->detach(provider);
    ok = false;
  }
  if (!ok) {
    OSSafeReleaseNULL(volume);
    printf("NORD2 BB: preboot volume NOT published\n");
    return nullptr;
  }
  volume->registerService();
  printf("NORD2 BB: APFS preboot volume published for /dev/%s (RoleValue 16, properties for CommCenter/abm-helper only): ABM's firmware "
         "folder is /<boot-manifest-hash hex>/usr/local/standalone/firmware/Baseband/Mav17\n", bsd);
  return volume;
}

// ------------------------------------------------------------------ publication, periodic work, the modem hooks
template <class Service>
static Service *bb_publish(IOService *provider, uint32_t channel = ~0u)
{
  Service *service = OSTypeAlloc(Service);
  bool ok = service && service->init();
  if (ok && channel != ~0u) {
    AppleBasebandPCIInterface *interface = OSDynamicCast(AppleBasebandPCIInterface, service);
    ok = interface && interface->setup(channel);
  }
  ok = ok && service->attach(provider);
  if (ok && !service->start(provider)) {
    service->detach(provider);
    ok = false;
  }
  if (!ok) {
    OSSafeReleaseNULL(service);
    return nullptr;
  }
  service->registerService();
  return service;
}

static Nord2BasebandNode *bb_publish_node(IOService *provider)
{
  Nord2BasebandNode *node = OSTypeAlloc(Nord2BasebandNode);
  bool ok = node && node->init();
  if (ok) {
    node->setName("baseband");
    ok = node->setProperty("name", (void *)"baseband", sizeof "baseband") && node->setProperty("compatible", (void *)"baseband,n41", sizeof "baseband,n41") &&
         node->setProperty("device_type", (void *)"baseband", sizeof "baseband") && node->setProperty("config", (void *)bb_d22_config, sizeof bb_d22_config);
  }
  ok = ok && node->attach(provider);
  if (ok && !node->start(provider)) {
    node->detach(provider);
    ok = false;
  }
  if (!ok) {
    OSSafeReleaseNULL(node);
    return nullptr;
  }
  if (bb_flags & NBB_DT_NODE) {
    IORegistryEntry *root = IORegistryEntry::fromPath("/", gIODTPlane), *existing = IORegistryEntry::fromPath("/baseband", gIODTPlane);
    const bool attached = root && !existing && node->attachToParent(root, gIODTPlane);
    printf("NORD2 BB: baseband node %s IODeviceTree:/\n", attached ? "attached to" : existing ? "not attached (exists) to" : "NOT attached to");
    OSSafeReleaseNULL(existing);
    OSSafeReleaseNULL(root);
  }
  node->registerService();
  return node;
}

static void bb_report(void)
{
  uint32_t unhandled = 0, kinds = 0;
  for (uint32_t i = 0; i < BB_KINDS; i++) if (bb_kinds[i].key) { kinds++; unhandled += bb_kinds[i].outcome == BB_UNHANDLED; }
  // 0277n: four lines, each under the kernel printf's 255 characters (q10cb's one line lost everything after "UIM reads")
  const uint32_t secs = bb_now_ms() / 1000u;
  printf("NORD2 BB: report t=%u s: %u clients, %u requests (%u kinds, %u unhandled kinds), qmux1 in %u out %u, qmux2 in %u out %u, "
         "dropped %u, refused %u (last svc 0x%x); sahara state %u\n", secs, nbq_clients(&bb_qmux, 0xffffffffu), bb_requests, kinds,
         unhandled, bb_qmux_in[0], bb_qmux_out[0], bb_qmux_in[1], bb_qmux_out[1], bb_qmux.dropped, bb_qmux.refused, bb_qmux.last_refused,
         bb_sahara.state);
  printf("NORD2 BB: report t=%u s (2): board calls %u (%u unknown, last %u); AT snapshots %u, AT errors %u, UIM reads %u (failed %u); "
         "log %u lines, %u held back\n", secs, bb_control.calls, bb_control.unsupported, bb_control.last_unsupported, bb_snapshots,
         bb_q_errors, bb_uim_reads, bb_uim_fail, bb_log_lines, bb_log_held);
  printf("NORD2 BB: report t=%u s (3): power-ons %u, soft resets %u, hard resets %u; reset line: %u reset, %u alive sent, %u GetReset "
         "polls; PDS answers %u (GPS %s); RFS sync indications %u, sync requests %u\n", secs, bb_power_ons, bb_soft_resets, bb_hard_resets,
         bb_rd_resets_sent, bb_rd_alives_sent, bb_get_reset_polls, bb_pds_answers, bb_pds_gps_on ? "on" : "off", bb_rfs_inds, bb_rfs_syncs);
  printf("NORD2 BB: report t=%u s (4): WDS starts %u (adopted %u, failed %u), stops %u; PLMN name indications %u; boot holds %u%s; "
         "activation holds %u%s; PDN bring-ups %u (%u AT failures); profiles %u created, %u ops\n", secs, bb_wds_starts, bb_wds_adopted, bb_wds_failed, bb_wds_stops,
         bb_plmn_inds, bb_holds, bb_hold ? " (holding)" : "", bb_act_holds, bb_act_pending ? " (holding)" : "", bb_pdn_up_runs,
         bb_pdn_up_fail, bb_profile_creates, bb_profile_ops);
  // 0277r
  printf("NORD2 BB: report t=%u s (5): SMS MO %u (failed %u), MT %u, status reports %u, acks %u, nacks %u, ack timeouts %u, held %u, "
         "bad PDUs %u, CBM dropped %u; CSMS %d, CNMA style %u, routes %u, setups %u\n", secs, bb_sms_mo, bb_sms_mo_fail, bb_sms_mt_n,
         bb_sms_reports, bb_sms_acks, bb_sms_nacks, bb_sms_ack_timeouts, bb_sms_held, bb_sms_bad_pdu, bb_sms_cbm, bb_sms_csms,
         bb_sms_cnma_style, bb_sms_nroutes, bb_cs_setups);
  {
    uint32_t calls = 0;
    for (uint32_t id = 1; id < 8; id++) calls += bb_calls[id].used;
    printf("NORD2 BB: report t=%u s (6): VOICE dials %u (failed %u), incoming %u, answers %u, ends %u, manage %u, DTMF %u, +CLCC polls "
           "%u, call status indications %u, EAIC %u, speech URCs %u, calls now %u\n", secs, bb_v_dials, bb_v_dial_fail, bb_v_incoming,
           bb_v_answers, bb_v_ends, bb_v_manage, bb_v_dtmf, bb_v_polls, bb_v_inds, bb_v_eaic, bb_v_speech, calls);
  }
  // every unhandled kind with its count: what the next phase has to implement
  char line[160];
  unsigned k = 0, n = 0;
  for (uint32_t i = 0; i < BB_KINDS; i++) {
    const bb_kind &e = bb_kinds[i];
    if (!e.key || e.outcome != BB_UNHANDLED) continue;
    const uint32_t key = e.key - 1u;
    k += (unsigned)snprintf(line + k, sizeof line - k, " %s/%04x x%u", nbq_service_name(key >> 16), key & 0xffffu, e.count);
    if (k > sizeof line - 28 || ++n % 6 == 0) { printf("NORD2 BB: unhandled:%s\n", line); k = 0; }
  }
  if (k) printf("NORD2 BB: unhandled:%s\n", line);
  for (uint32_t c = 0; c < CBH_PUBLISHED; c++) {
    const bb_channel *ch = &bb_ch[c];
    if (ch->opens)
      printf("NORD2 BB: %s: opens %u, in %u frames %u bytes, out %u frames %u bytes, dropped %u, queued %u, reads waiting %u\n",
             cbh_published[c], ch->opens, ch->frames_in, ch->bytes_in, ch->frames_out, ch->bytes_out, ch->frames_dropped, ch->count,
             ch->reads_waiting);
  }
}

static void bb_tick(thread_call_param_t, thread_call_param_t)
{
  uint64_t deadline = 0;
  IOLockLock(bb_lock);
  bb_ticks++;
  bb_refresh_locked();
  if (bb_ticks % (BB_REPORT_SECONDS / BB_TICK_SECONDS) == 0) bb_report();
  IOLockUnlock(bb_lock);
  clock_interval_to_deadline(BB_TICK_SECONDS, kSecondScale, &deadline);
  thread_call_enter_delayed(bb_tick_call, deadline);
}

static void bb_refresh_now(thread_call_param_t, thread_call_param_t)
{
  IOLockLock(bb_lock);
  bb_refresh_locked();
  IOLockUnlock(bb_lock);
}

static void bb_refresh_soon(uint32_t ms)
{
  uint64_t deadline = 0;
  if (!bb_published || !bb_refresh_call) return;
  clock_interval_to_deadline(ms, kMillisecondScale, &deadline);
  thread_call_enter_delayed(bb_refresh_call, deadline);
}

// The modem thread started: publish the services ABM looks for (thread call: the modem bring-up does not wait for IOKit).
static void bb_publish_all(thread_call_param_t, thread_call_param_t)
{
  if (bb_published) return;
  IOService *provider = IOService::getResourceService();
  IOService *node = (bb_flags & NBB_NO_NODE) || !provider ? nullptr : bb_publish_node(provider);
  IOService *preboot = !(bb_flags & NBB_PREBOOT) || !provider ? nullptr : bb_publish_preboot(provider);
  int made = 0;
  bb_baseband_svc = provider ? bb_publish<AppleBaseband>(node ? node : provider) : nullptr;
  made += bb_baseband_svc != nullptr;
  bb_pci_control = provider ? bb_publish<AppleBasebandPCIControl>(provider) : nullptr;
  if (bb_pci_control) bb_pci_control->setProperty("bootstage", 0ull, 32);
  made += bb_pci_control != nullptr;
  for (uint32_t c = 0; c < CBH_PUBLISHED; c++) made += provider && bb_publish<AppleBasebandPCIInterface>(provider, c);
  made += provider && bb_publish<AppleBasebandPCIMAVPDPManager>(provider);
  made += provider && bb_publish<AppleIPAppender>(provider);
  if (nord2_pdn_owner) (void)nord2_pdn_ifaces();          // 0277l: pdp_ip0..4 before CommCenter's data contexts come up
  bb_published = 1;
  printf("NORD2 BB: %d of %u services published (baseband node %s, preboot volume %s, AppleBaseband, AppleBasebandPCIControl, %u channels, "
         "PDP manager, AppleIPAppender), n2bb=%#x, Mav17 chip id 0x%08x\n", made, 4u + CBH_PUBLISHED, node ? "yes" : "no",
         preboot ? "yes" : "no", CBH_PUBLISHED, bb_flags, CBS_CHIP_ID);
  uint64_t deadline = 0;
  clock_interval_to_deadline(BB_TICK_SECONDS, kSecondScale, &deadline);
  thread_call_enter_delayed(bb_tick_call, deadline);
  if (nord2_modem_state() & NORD2_MODEM_READY) bb_refresh_soon(10);
}

// 0277k: a device tree node "baseband" under IODeviceTree:/, from bsd_init (before the first exec): MobileGestalt's HasBaseband, hence
// cellular-data and contains-cellular-radio, for every process. An IOService like the ADT's own nodes, never registered (no matching).
static void bb_dt_node_early(void)
{
  uint32_t flags = NBB_ON;
  if (!PE_parse_boot_argn("n2bb", &flags, sizeof flags)) flags = NBB_ON;
  if ((flags & NBB_ON) && !(flags & NBB_NO_DATA)) {      // 0277l: CommCenter owns cellular data (sweet_amfi.c pdp_ip0..4)
    nord2_pdn_owner = 1;
    nord2_pdn_route(-1);
  }
  if (!(flags & NBB_ON) || (flags & NBB_NO_DT_EARLY)) return;
  IORegistryEntry *root = IORegistryEntry::fromPath("/", gIODTPlane), *existing = IORegistryEntry::fromPath("/baseband", gIODTPlane);
  const char *what = "NOT attached (no IODeviceTree root)";
  if (existing) {
    what = "already in the device tree";
  } else if (root) {
    IOService *node = OSTypeAlloc(IOService);
    bool ok = node && node->init();
    if (ok) {
      node->setName("baseband");
      ok = node->setProperty("name", (void *)"baseband", sizeof "baseband") &&
           node->setProperty("compatible", (void *)"baseband,n41", sizeof "baseband,n41") &&
           node->setProperty("device_type", (void *)"baseband", sizeof "baseband") &&
           node->setProperty("config", (void *)bb_d22_config, sizeof bb_d22_config) && node->attachToParent(root, gIODTPlane);
    }
    what = ok ? "attached to IODeviceTree:/ (MobileGestalt HasBaseband)" : "NOT attached (no memory)";
    OSSafeReleaseNULL(node);                             // the plane holds it
  }
  printf("NORD2 BB: early device tree node \"baseband\" %s\n", what);
  OSSafeReleaseNULL(existing);
  OSSafeReleaseNULL(root);
}

static void bb_event(int what)
{
  switch (what) {
  case NORD2_BB_EV_EARLY:
    bb_dt_node_early();
    break;
  case NORD2_BB_EV_THREAD: {
    uint32_t flags = 0;
    if (!PE_parse_boot_argn("n2bb", &flags, sizeof flags)) flags = NBB_ON;   /* default ON: the kernel command line has no room left */
    if (!(flags & NBB_ON)) { printf("NORD2 BB: bridge off (boot-arg n2bb=%#x)\n", flags); return; }
    if (bb_lock) return;
    bb_flags = flags;
    bb_lock = IOLockAlloc();
    bb_complete_call = thread_call_allocate(bb_complete, nullptr);
    bb_tick_call = thread_call_allocate(bb_tick, nullptr);
    bb_refresh_call = thread_call_allocate(bb_refresh_now, nullptr);
    bb_publish_call = thread_call_allocate(bb_publish_all, nullptr);
    bb_reset_call = thread_call_allocate(bb_reset_step, nullptr);
    bb_hold_call = thread_call_allocate(bb_hold_tick, nullptr);       // 0277m (none: no hold)
    bb_act_call = thread_call_allocate(bb_act_tick, nullptr);         // 0277n (none: no activation hold)
    bb_cs_call = thread_call_allocate(bb_cs_tick, nullptr);           // 0277r (none: no SMS ack fallback, no +CLCC polls)
    if (!bb_lock || !bb_complete_call || !bb_tick_call || !bb_refresh_call || !bb_publish_call || !bb_reset_call) {
      printf("NORD2 BB: no memory for the bridge\n");
      return;
    }
    nbq_init(&bb_qmux, !(flags & NBB_CORE_ONLY));
    nbm_init(&bb_st);
    cbc_init(&bb_control);
    cbs_init(&bb_sahara, bb_sahara_images, sizeof bb_sahara_images / sizeof bb_sahara_images[0], 0x10000);
    thread_call_enter(bb_publish_call);
    break;
  }
  case NORD2_BB_EV_READY:
  case NORD2_BB_EV_STEADY:
    if (bb_lock) {                                                 // 0277r: SMS/call settings again (a restarted modem lost them)
      IOLockLock(bb_lock);
      bb_cs_setup_state = 0;
      bb_sms_ready_setups = 0;
      IOLockUnlock(bb_lock);
    }
    bb_refresh_soon(what == NORD2_BB_EV_READY ? 2500u : 100u);   // after READY the boot script sends ATE0 at +2 s
    break;
  case NORD2_BB_EV_PDN:                                            // 0277l: answer waiting Start Network, tell connected calls
    bb_refresh_soon(50);
    break;
  case NORD2_BB_EV_DEAD:
    if (bb_published) printf("NORD2 BB: the MediaTek modem died: answers stay at the last snapshot\n");
    break;
  default:
    break;
  }
}

// An unconsumed AT line (URC): registration, SIM and network changes refresh the snapshot. Never logged.
static void bb_urc(const char *line)
{
  if (!bb_published || !line) return;
  if (nbm_prefix(line, "+EIND:")) {                               // 0277s: MediaTek's "+EIND: 1" = SMS ready
    const char *v = line + 6;
    while (*v == ' ') v++;
    unsigned n = 0;
    while (*v >= '0' && *v <= '9') n = n * 10u + (unsigned)(*v++ - '0');
    if ((n & 1u) && bb_lock) { IOLockLock(bb_lock); bb_sms_ready_setup(); IOLockUnlock(bb_lock); }
    return;
  }
  if (bb_cs_urc(line)) return;                                     // 0277r: SMS and call URCs
  if (line[0] != '+') return;
  if (nbm_prefix(line, "+CEREG:") || nbm_prefix(line, "+CREG:") || nbm_prefix(line, "+CGREG:") || nbm_prefix(line, "+CPIN:") ||
      nbm_prefix(line, "+ESIMS:") || nbm_prefix(line, "+EUSIM:") || nbm_prefix(line, "+CGEV:"))
    bb_refresh_soon(300);
}

// ------------------------------------------------------------------ 0277c: trace of the baseband stack's processes
// sweet_amfi.c's Sandbox MAC hooks hand over CommCenter's and abm-helper's path lookups ("lookup", the path as the caller gave it,
// whether it exists or not), existing-file stats ("stat", the resolved path), IOKit property reads and IOKit opens, and since 0277e
// sysctl names ("sysctl", "sysctl-set") and Sandbox checks about them ("check <operation> <name>", launchd's mach-lookups). Each distinct line
// is logged once ("NORD2 BB: trace ..."), at most BB_TRACE_LINES; from process start (CommCenter starts before the bridge publishes).
// Boot-arg n2bb bit 11 turns it off. Paths can name carrier bundles and preferences, no identities.
#define BB_TRACE_SLOTS 8192u
#define BB_TRACE_LINES 2500u
static uint32_t bb_trace_seen[BB_TRACE_SLOTS], bb_trace_lines;

// n2bb for the hooks that run before the modem thread (bb_flags is set at NORD2_BB_EV_THREAD); CommCenter starts earlier.
static uint32_t bb_bootflags(void)
{
  static uint32_t cached;      // flags | 0x80000000 once read
  uint32_t v = __atomic_load_n(&cached, __ATOMIC_RELAXED);
  if (!v) {
    uint32_t flags = NBB_ON;
    if (!PE_parse_boot_argn("n2bb", &flags, sizeof flags)) flags = NBB_ON;
    v = (flags & 0x7fffffffu) | 0x80000000u;
    __atomic_store_n(&cached, v, __ATOMIC_RELAXED);
  }
  return v & 0x7fffffffu;
}

static bool bb_trace_on(void)
{
  const uint32_t flags = bb_bootflags();
  return (flags & NBB_ON) && !(flags & NBB_NO_TRACE) && __atomic_load_n(&bb_trace_lines, __ATOMIC_RELAXED) < BB_TRACE_LINES;
}

// true the first time this line is seen (lock-free open addressing on a 32-bit hash; 0 marks a free slot)
static bool bb_trace_first(const char *a, const char *b, const char *c, unsigned long clen)
{
  uint32_t h = 2166136261u;
  for (const char *s = a; s && *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  h = (h ^ 0xffu) * 16777619u;
  for (const char *s = b; s && *s; s++) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  h = (h ^ 0xfeu) * 16777619u;
  for (unsigned long i = 0; c && i < clen && c[i]; i++) h = (h ^ static_cast<uint8_t>(c[i])) * 16777619u;
  if (!h) h = 1;
  for (uint32_t i = 0, at = h % BB_TRACE_SLOTS; i < 64; i++, at = (at + 1) % BB_TRACE_SLOTS) {
    uint32_t cur = __atomic_load_n(&bb_trace_seen[at], __ATOMIC_RELAXED);
    if (cur == h) return false;
    if (!cur) {
      uint32_t expected = 0;
      if (__atomic_compare_exchange_n(&bb_trace_seen[at], &expected, h, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) break;
      if (expected == h) return false;
    }
  }
  return __atomic_fetch_add(&bb_trace_lines, 1u, __ATOMIC_RELAXED) < BB_TRACE_LINES;
}

static void bb_trace_path(const char *tag, const char *path, unsigned long len)
{
  char name[32], shown[200];
  if (!bb_trace_on() || !path) return;
  proc_selfname(name, sizeof name);
  if (!bb_trace_first(name, tag, path, len)) return;
  unsigned long k = 0;
  for (unsigned long i = 0; i < len && path[i] && k + 1 < sizeof shown; i++)
    shown[k++] = path[i] >= 32 && path[i] < 127 ? path[i] : '?';
  shown[k] = 0;
  printf("NORD2 BB: trace %s %s %s%s\n", name, tag, shown, len >= sizeof shown ? "..." : "");
}

static void bb_trace_object(const char *tag, void *object, const char *what, unsigned type)
{
  char name[32], cls[160];
  if (!bb_trace_on() || !object) return;
  const IORegistryEntry *entry = OSDynamicCast(IORegistryEntry, static_cast<OSObject *>(object));
  snprintf(cls, sizeof cls, "%s(%s)%s%s", entry ? entry->getMetaClass()->getClassName() : "?", entry ? entry->getName() : "?",
           what ? " " : "", what ? what : "");
  proc_selfname(name, sizeof name);
  if (!bb_trace_first(name, tag, cls, sizeof cls)) return;
  if (what) printf("NORD2 BB: trace %s %s %s\n", name, tag, cls);
  else printf("NORD2 BB: trace %s %s %s type %u\n", name, tag, cls, type);
}

static void bb_trace_ioprop(void *entry, const char *key) { bb_trace_object("ioprop", entry, key ? key : "?", 0); }
static void bb_trace_ioopen(void *service, unsigned type) { bb_trace_object("ioopen", service, nullptr, type); }

// 0277e: a Sandbox check about CommCenter/abm-helper, mostly launchd's mach-lookup checks ("trace CommCenter check mach-lookup <name>").
static void bb_trace_check(const char *subject, const char *op, const char *arg)
{
  char what[220];
  if (!bb_trace_on() || !subject || !op) return;
  snprintf(what, sizeof what, "%s%s%s", op, arg && *arg ? " " : "", arg ? arg : "");
  if (!bb_trace_first(subject, "check", what, sizeof what)) return;
  printf("NORD2 BB: trace %s check %s\n", subject, what);
}

// 0277e: CommCenter creates /tmp/ct.shutdown when it starts and removes it after a clean ServiceManager shutdown. A launch that finds
// the file takes the previous instance for crashed: it has ABM collect CommCenter crash logs (abm::kCommandTraceDumpState with
// kCollectCommCenterCrashLogs: abm.workspace/uncompressed/<time>, abm-helper, /tmp/CommCenter.dumping), as every q10b* run showed. On
// iOS /tmp is empty after a reboot; here the file stays on /private/var/tmp because the phone is reset, never shut down. So CommCenter's
// first lookup of the marker after boot fails with ENOENT, as after a reboot. Later lookups (the create that follows, a relaunch after a
// real crash) see the file. n2bb bit 12 keeps the marker visible; n2bb=0 too.
static uint32_t bb_ct_marker_done;
static int bb_path_gate(const char *path, unsigned long len)
{
  static const char tmp_marker[] = "/tmp/ct.shutdown", var_marker[] = "/private/var/tmp/ct.shutdown";
  char name[32];
  if (!path || __atomic_load_n(&bb_ct_marker_done, __ATOMIC_RELAXED)) return 0;
  if (!(len == sizeof tmp_marker - 1 && !memcmp(path, tmp_marker, len)) && !(len == sizeof var_marker - 1 && !memcmp(path, var_marker, len)))
    return 0;
  if ((bb_bootflags() & (NBB_ON | NBB_KEEP_CT_MARKER)) != NBB_ON) return 0;
  proc_selfname(name, sizeof name);
  if (strcmp(name, "CommCenter") || __atomic_exchange_n(&bb_ct_marker_done, 1u, __ATOMIC_RELAXED)) return 0;
  printf("NORD2 BB: CommCenter's first-launch check: %s answered ENOENT once, as after a reboot (a marker left by the previous boot "
         "would make CommCenter collect crash logs; n2bb bit 12 keeps it)\n", tmp_marker);
  return ENOENT;
}

static const struct nord2_bb_hooks bb_hooks = { bb_event, bb_urc, bb_trace_path, bb_trace_ioprop, bb_trace_ioopen, bb_trace_check,
                                                bb_path_gate };
// Installed from the kernel's C++ static initialization (OSlibkernInit), like the OSMetaClass objects above.
namespace {
struct Nord2BasebandInstaller {
  Nord2BasebandInstaller() { nord2_bb_hooks = &bb_hooks; }
};
}
static Nord2BasebandInstaller bb_installer;
