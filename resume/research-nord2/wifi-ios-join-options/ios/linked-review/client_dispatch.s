
$NORD2_REPO/research/ci-39823157589/xnu-kernel/kernel.development.sweet:	file format mach-o arm64

Disassembly of section __TEXT_EXEC,__text:

fffffe0007ae11f0 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)>:
fffffe0007ae11f0:      	sub	sp, sp, #320
fffffe0007ae11f4:      	stp	x28, x27, [sp, #224]
fffffe0007ae11f8:      	stp	x26, x25, [sp, #240]
fffffe0007ae11fc:      	stp	x24, x23, [sp, #256]
fffffe0007ae1200:      	stp	x22, x21, [sp, #272]
fffffe0007ae1204:      	stp	x20, x19, [sp, #288]
fffffe0007ae1208:      	stp	x29, x30, [sp, #304]
fffffe0007ae120c:      	add	x29, sp, #304
fffffe0007ae1210:      	adrp	x8, 0xfffffe0007c8c000 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x6cc>
fffffe0007ae1214:      	mov	w19, #706
fffffe0007ae1218:      	movk	w19, #57344, lsl #16
fffffe0007ae121c:      	nop
fffffe0007ae1220:      	ldr	x8, [x8, #3376]
fffffe0007ae1224:      	stur	x8, [x29, #-96]
fffffe0007ae1228:      	ldr	w25, [x0, #232]
fffffe0007ae122c:      	cbz	w25, 0xfffffe0007ae1258 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x68>
fffffe0007ae1230:      	mov	x21, x3
fffffe0007ae1234:      	mov	x24, x2
fffffe0007ae1238:      	mov	x20, x0
fffffe0007ae123c:      	mov	w23, w1
fffffe0007ae1240:      	bl	0xfffffe000737bd24 <_current_task>
fffffe0007ae1244:      	ldr	x8, [x20, #216]
fffffe0007ae1248:      	cmp	x0, x8
fffffe0007ae124c:      	b.eq	0xfffffe0007ae1260 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x70>
fffffe0007ae1250:      	sub	w22, w19, #1
fffffe0007ae1254:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1258:      	add	w22, w19, #11
fffffe0007ae125c:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1260:      	cbz	x24, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1264:      	ldr	w3, [x24, #96]
fffffe0007ae1268:      	cbz	w3, 0xfffffe0007ae1274 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x84>
fffffe0007ae126c:      	ldr	x8, [x24, #88]
fffffe0007ae1270:      	cbz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1274:      	ldr	w8, [x24]
fffffe0007ae1278:      	cmp	w8, #2
fffffe0007ae127c:      	b.ne	0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1280:      	ldr	w8, [x24, #4]
fffffe0007ae1284:      	cmp	w8, w23
fffffe0007ae1288:      	b.ne	0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae128c:      	ldr	w8, [x24, #40]
fffffe0007ae1290:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1294:      	ldr	w8, [x24, #80]
fffffe0007ae1298:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae129c:      	ldr	x8, [x24, #104]
fffffe0007ae12a0:      	cbz	x8, 0xfffffe0007ae12e8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xf8>
fffffe0007ae12a4:      	mov	w22, #706
fffffe0007ae12a8:      	movk	w22, #57344, lsl #16
fffffe0007ae12ac:      	adrp	x8, 0xfffffe0007c8c000 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x768>
fffffe0007ae12b0:      	nop
fffffe0007ae12b4:      	ldur	x9, [x29, #-96]
fffffe0007ae12b8:      	ldr	x8, [x8, #3376]
fffffe0007ae12bc:      	cmp	x8, x9
fffffe0007ae12c0:      	b.ne	0xfffffe0007ae1870 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x680>
fffffe0007ae12c4:      	mov	w0, w22
fffffe0007ae12c8:      	ldp	x29, x30, [sp, #304]
fffffe0007ae12cc:      	ldp	x20, x19, [sp, #288]
fffffe0007ae12d0:      	ldp	x22, x21, [sp, #272]
fffffe0007ae12d4:      	ldp	x24, x23, [sp, #256]
fffffe0007ae12d8:      	ldp	x26, x25, [sp, #240]
fffffe0007ae12dc:      	ldp	x28, x27, [sp, #224]
fffffe0007ae12e0:      	add	sp, sp, #320
fffffe0007ae12e4:      	ret
fffffe0007ae12e8:      	cmp	w23, #6
fffffe0007ae12ec:      	b.eq	0xfffffe0007ae12f4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x104>
fffffe0007ae12f0:      	cbnz	w23, 0xfffffe0007ae139c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x1ac>
fffffe0007ae12f4:      	ldr	w8, [x24, #24]
fffffe0007ae12f8:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae12fc:      	ldr	x8, [x24, #8]
fffffe0007ae1300:      	cbnz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1304:      	mov	w8, #49160
fffffe0007ae1308:      	cmp	w23, #0
fffffe0007ae130c:      	movk	w8, #3, lsl #16
fffffe0007ae1310:      	mov	w9, #15408
fffffe0007ae1314:      	add	x8, x8, #40
fffffe0007ae1318:      	mov	w27, #49160
fffffe0007ae131c:      	ldr	x0, [x24, #64]
fffffe0007ae1320:      	csel	x21, x9, x8, eq
fffffe0007ae1324:      	movk	w27, #3, lsl #16
fffffe0007ae1328:      	cbz	x0, 0xfffffe0007ae141c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x22c>
fffffe0007ae132c:      	ldr	w8, [x24, #56]
fffffe0007ae1330:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1334:      	ldr	x8, [x0]
fffffe0007ae1338:      	ldr	x8, [x8, #496]
fffffe0007ae133c:      	blr	x8
fffffe0007ae1340:      	cmp	x0, x21
fffffe0007ae1344:      	b.ne	0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1348:      	adrp	x0, 0xfffffe000717e000 <IOHIDEventServiceUserClient::close()+0x40>
fffffe0007ae134c:      	mov	x1, x21
fffffe0007ae1350:      	mov	w2, wzr
fffffe0007ae1354:      	add	x0, x0, #3608
fffffe0007ae1358:      	bl	0xfffffe0007a10658 <_IOMalloc_internal>
fffffe0007ae135c:      	cbz	x0, 0xfffffe0007ae1748 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x558>
fffffe0007ae1360:      	mov	x25, x0
fffffe0007ae1364:      	ldr	x0, [x24, #64]
fffffe0007ae1368:      	mov	w1, #2
fffffe0007ae136c:      	ldr	x8, [x0]
fffffe0007ae1370:      	ldr	x8, [x8, #536]
fffffe0007ae1374:      	blr	x8
fffffe0007ae1378:      	cbz	w0, 0xfffffe0007ae1750 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x560>
fffffe0007ae137c:      	mov	w22, w0
fffffe0007ae1380:      	mov	x0, x25
fffffe0007ae1384:      	mov	x1, x21
fffffe0007ae1388:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae138c:      	mov	x0, x25
fffffe0007ae1390:      	mov	x1, x21
fffffe0007ae1394:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae1398:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae139c:      	ldr	x8, [x24, #64]
fffffe0007ae13a0:      	cbnz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae13a4:      	ldr	w8, [x24, #56]
fffffe0007ae13a8:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae13ac:      	and	w8, w23, #0xfffffffb
fffffe0007ae13b0:      	cmp	w8, #1
fffffe0007ae13b4:      	b.ne	0xfffffe0007ae1520 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x330>
fffffe0007ae13b8:      	ldr	w8, [x24, #24]
fffffe0007ae13bc:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae13c0:      	ldr	x8, [x24, #8]
fffffe0007ae13c4:      	cbnz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae13c8:      	mov	w8, #63
fffffe0007ae13cc:      	mov	x9, #1
fffffe0007ae13d0:      	movk	x9, #3, lsl #32
fffffe0007ae13d4:      	ldr	x7, [x24, #88]
fffffe0007ae13d8:      	add	x0, sp, #48
fffffe0007ae13dc:      	mov	w1, w23
fffffe0007ae13e0:      	str	w8, [sp, #56]
fffffe0007ae13e4:      	sub	x8, x29, #144
fffffe0007ae13e8:      	mov	x2, xzr
fffffe0007ae13ec:      	mov	w4, wzr
fffffe0007ae13f0:      	mov	w5, wzr
fffffe0007ae13f4:      	mov	w6, wzr
fffffe0007ae13f8:      	str	x9, [sp, #48]
fffffe0007ae13fc:      	stur	xzr, [x29, #-144]
fffffe0007ae1400:      	str	x8, [sp]
fffffe0007ae1404:      	bl	0xfffffe000799e1cc <_niwb_selector>
fffffe0007ae1408:      	cbnz	w0, 0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae140c:      	ldur	x8, [x29, #-144]
fffffe0007ae1410:      	mov	w19, wzr
fffffe0007ae1414:      	str	w8, [x24, #96]
fffffe0007ae1418:      	b	0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae141c:      	ldr	w8, [x24, #56]
fffffe0007ae1420:      	cmp	x21, x8
fffffe0007ae1424:      	b.ne	0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1428:      	mov	x22, xzr
fffffe0007ae142c:      	ldr	x25, [x24, #48]
fffffe0007ae1430:      	movi.2d	v0, #0000000000000000
fffffe0007ae1434:      	add	x26, x20, #872
fffffe0007ae1438:      	mov	w0, w23
fffffe0007ae143c:      	mov	x1, x25
fffffe0007ae1440:      	mov	x2, x21
fffffe0007ae1444:      	mov	x3, x26
fffffe0007ae1448:      	stp	q0, q0, [x29, #-128]
fffffe0007ae144c:      	stur	q0, [x29, #-144]
fffffe0007ae1450:      	bl	0xfffffe000799fcec <_niwi_scan_envelope>
fffffe0007ae1454:      	cbz	w0, 0xfffffe0007ae15cc <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x3dc>
fffffe0007ae1458:      	sub	x4, x29, #148
fffffe0007ae145c:      	mov	w0, w23
fffffe0007ae1460:      	mov	x1, x25
fffffe0007ae1464:      	mov	x2, x21
fffffe0007ae1468:      	mov	x3, x26
fffffe0007ae146c:      	stur	wzr, [x29, #-148]
fffffe0007ae1470:      	bl	0xfffffe000799feec <_niwi_set_shape>
fffffe0007ae1474:      	cbz	w0, 0xfffffe0007ae15f0 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x400>
fffffe0007ae1478:      	sub	x3, x29, #144
fffffe0007ae147c:      	mov	w0, w23
fffffe0007ae1480:      	mov	x1, x25
fffffe0007ae1484:      	mov	x2, x21
fffffe0007ae1488:      	bl	0xfffffe000799eac4 <_niw_decode>
fffffe0007ae148c:      	cbnz	w0, 0xfffffe0007ae14e4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2f4>
fffffe0007ae1490:      	cbnz	w23, 0xfffffe0007ae14c8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2d8>
fffffe0007ae1494:      	ldur	w8, [x29, #-136]
fffffe0007ae1498:      	cmp	w8, #43
fffffe0007ae149c:      	b.ne	0xfffffe0007ae14c8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2d8>
fffffe0007ae14a0:      	ldur	w8, [x29, #-132]
fffffe0007ae14a4:      	cbnz	w8, 0xfffffe0007ae14c8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2d8>
fffffe0007ae14a8:      	ldur	w8, [x29, #-140]
fffffe0007ae14ac:      	cbnz	w8, 0xfffffe0007ae14c8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2d8>
fffffe0007ae14b0:      	sub	x8, x29, #144
fffffe0007ae14b4:      	mov	x1, x26
fffffe0007ae14b8:      	add	x0, x8, #32
fffffe0007ae14bc:      	mov	w2, #16
fffffe0007ae14c0:      	bl	0xfffffe0007466cfc <_memcmp>
fffffe0007ae14c4:      	cbz	w0, 0xfffffe0007ae1884 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x694>
fffffe0007ae14c8:      	sub	x1, x29, #144
fffffe0007ae14cc:      	mov	x0, x20
fffffe0007ae14d0:      	mov	x2, x25
fffffe0007ae14d4:      	mov	x3, x21
fffffe0007ae14d8:      	mov	x4, x24
fffffe0007ae14dc:      	bl	0xfffffe0007ae247c <NiwlLifecycleClient::getLocked(niw_request const*, void const*, unsigned long, IOExternalMethodArguments*)>
fffffe0007ae14e0:      	mov	w19, w0
fffffe0007ae14e4:      	ldur	w24, [x29, #-136]
fffffe0007ae14e8:      	mov	x0, x20
fffffe0007ae14ec:      	ldur	w25, [x29, #-144]
fffffe0007ae14f0:      	mov	w1, w24
fffffe0007ae14f4:      	mov	w2, w25
fffffe0007ae14f8:      	bl	0xfffffe0007ae1134 <NiwlLifecycleClient::traceControl(unsigned int, unsigned int)>
fffffe0007ae14fc:      	cbz	w0, 0xfffffe0007ae198c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x79c>
fffffe0007ae1500:      	mov	w8, w19
fffffe0007ae1504:      	ldur	w9, [x29, #-140]
fffffe0007ae1508:      	adrp	x0, 0xfffffe0007150000 <IOHIDEventServiceUserClient::copyMatchingEvent(OSDictionary*, OSData**)+0x50>
fffffe0007ae150c:      	add	x0, x0, #3907
fffffe0007ae1510:      	stp	x24, x25, [sp, #8]
fffffe0007ae1514:      	stp	x9, x8, [sp, #24]
fffffe0007ae1518:      	str	x23, [sp]
fffffe0007ae151c:      	b	0xfffffe0007ae1988 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x798>
fffffe0007ae1520:      	cbnz	w3, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1524:      	cmp	w23, #2
fffffe0007ae1528:      	b.ne	0xfffffe0007ae1648 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x458>
fffffe0007ae152c:      	cmp	w25, #2
fffffe0007ae1530:      	b.ne	0xfffffe0007ae1844 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x654>
fffffe0007ae1534:      	ldr	w8, [x24, #24]
fffffe0007ae1538:      	cmp	w8, #8
fffffe0007ae153c:      	b.ne	0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1540:      	ldr	x8, [x24, #16]
fffffe0007ae1544:      	cbz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1548:      	mov	w22, #706
fffffe0007ae154c:      	ldr	x9, [x24, #8]
fffffe0007ae1550:      	movk	w22, #57344, lsl #16
fffffe0007ae1554:      	cbz	x9, 0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1558:      	ldr	x8, [x8]
fffffe0007ae155c:      	mov	w9, #1
fffffe0007ae1560:      	mov	w22, wzr
fffffe0007ae1564:      	str	x8, [x20, #256]
fffffe0007ae1568:      	ldr	x8, [x24, #16]
fffffe0007ae156c:      	ldr	x8, [x8, #8]
fffffe0007ae1570:      	str	x8, [x20, #264]
fffffe0007ae1574:      	ldr	x8, [x24, #16]
fffffe0007ae1578:      	ldr	x8, [x8, #16]
fffffe0007ae157c:      	str	x8, [x20, #272]
fffffe0007ae1580:      	ldr	x8, [x24, #16]
fffffe0007ae1584:      	ldr	x8, [x8, #24]
fffffe0007ae1588:      	str	x8, [x20, #280]
fffffe0007ae158c:      	ldr	x8, [x24, #16]
fffffe0007ae1590:      	ldr	x8, [x8, #32]
fffffe0007ae1594:      	str	x8, [x20, #288]
fffffe0007ae1598:      	ldr	x8, [x24, #16]
fffffe0007ae159c:      	ldr	x8, [x8, #40]
fffffe0007ae15a0:      	str	x8, [x20, #296]
fffffe0007ae15a4:      	ldr	x8, [x24, #16]
fffffe0007ae15a8:      	ldr	x8, [x8, #48]
fffffe0007ae15ac:      	str	x8, [x20, #304]
fffffe0007ae15b0:      	ldr	x8, [x24, #16]
fffffe0007ae15b4:      	ldr	x8, [x8, #56]
fffffe0007ae15b8:      	strb	w9, [x20, #320]
fffffe0007ae15bc:      	str	x8, [x20, #312]
fffffe0007ae15c0:      	mov	w8, #3
fffffe0007ae15c4:      	strb	w9, [x21]
fffffe0007ae15c8:      	b	0xfffffe0007ae195c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x76c>
fffffe0007ae15cc:      	ldr	w8, [x24, #96]
fffffe0007ae15d0:      	cbnz	w8, 0xfffffe0007ae198c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x79c>
fffffe0007ae15d4:      	add	x1, x25, #8
fffffe0007ae15d8:      	mov	x0, x20
fffffe0007ae15dc:      	mov	w2, #5456
fffffe0007ae15e0:      	bl	0xfffffe0007ae1a50 <NiwlLifecycleClient::scanLocked(unsigned char const*, unsigned long)>
fffffe0007ae15e4:      	mov	w19, w0
fffffe0007ae15e8:      	cbnz	x22, 0xfffffe0007ae1990 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7a0>
fffffe0007ae15ec:      	b	0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae15f0:      	ldur	w28, [x29, #-148]
fffffe0007ae15f4:      	cmp	w23, #0
fffffe0007ae15f8:      	mov	w8, #15368
fffffe0007ae15fc:      	add	x26, x25, #8
fffffe0007ae1600:      	csel	x27, x8, x27, eq
fffffe0007ae1604:      	and	w8, w28, #0xfffffffd
fffffe0007ae1608:      	cmp	w8, #20
fffffe0007ae160c:      	b.ne	0xfffffe0007ae179c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x5ac>
fffffe0007ae1610:      	add	x8, x25, x27
fffffe0007ae1614:      	ldr	w9, [x24, #96]
fffffe0007ae1618:      	ldr	w23, [x25, #4]
fffffe0007ae161c:      	ldr	w8, [x8, #20]
fffffe0007ae1620:      	orr	w8, w9, w8
fffffe0007ae1624:      	cbnz	w8, 0xfffffe0007ae18dc <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x6ec>
fffffe0007ae1628:      	cmp	w28, #20
fffffe0007ae162c:      	b.ne	0xfffffe0007ae1874 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x684>
fffffe0007ae1630:      	mov	x0, x20
fffffe0007ae1634:      	mov	x1, x26
fffffe0007ae1638:      	mov	x2, x23
fffffe0007ae163c:      	bl	0xfffffe0007ae20f8 <NiwlLifecycleClient::joinLocked(unsigned char const*, unsigned long)>
fffffe0007ae1640:      	mov	w19, w0
fffffe0007ae1644:      	b	0xfffffe0007ae18dc <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x6ec>
fffffe0007ae1648:      	ldr	w8, [x24, #24]
fffffe0007ae164c:      	cbnz	w8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1650:      	ldr	x8, [x24, #8]
fffffe0007ae1654:      	cbnz	x8, 0xfffffe0007ae12a4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xb4>
fffffe0007ae1658:      	cmp	w23, #4
fffffe0007ae165c:      	b.eq	0xfffffe0007ae1938 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x748>
fffffe0007ae1660:      	cmp	w23, #3
fffffe0007ae1664:      	b.ne	0xfffffe0007ae1964 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x774>
fffffe0007ae1668:      	add	w22, w19, #19
fffffe0007ae166c:      	cmp	w25, #1
fffffe0007ae1670:      	b.ne	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1674:      	ldr	x8, [x20, #328]
fffffe0007ae1678:      	cmn	x8, #1
fffffe0007ae167c:      	b.eq	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1680:      	mov	w8, #4096
fffffe0007ae1684:      	sub	x0, x29, #144
fffffe0007ae1688:      	add	x3, sp, #48
fffffe0007ae168c:      	mov	w1, #4
fffffe0007ae1690:      	mov	w2, wzr
fffffe0007ae1694:      	stur	xzr, [x29, #-144]
fffffe0007ae1698:      	stp	xzr, xzr, [sp, #48]
fffffe0007ae169c:      	stur	w8, [x29, #-136]
fffffe0007ae16a0:      	str	wzr, [sp, #64]
fffffe0007ae16a4:      	bl	0xfffffe000799f5b4 <_niwe_plan>
fffffe0007ae16a8:      	cmp	w0, #1
fffffe0007ae16ac:      	b.ne	0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae16b0:      	adrp	x22, 0xfffffe0007181000 <_nbg_collect+0x1c>
fffffe0007ae16b4:      	mov	w1, #65536
fffffe0007ae16b8:      	mov	w2, #4096
fffffe0007ae16bc:      	mov	w3, #4096
fffffe0007ae16c0:      	add	x22, x22, #2016
fffffe0007ae16c4:      	ldr	x0, [x22]
fffffe0007ae16c8:      	bl	0xfffffe0007a4dbbc <IOBufferMemoryDescriptor::inTaskWithOptions(task*, unsigned int, unsigned long, unsigned long)>
fffffe0007ae16cc:      	sub	w19, w19, #5
fffffe0007ae16d0:      	cbz	x0, 0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae16d4:      	mov	x21, x0
fffffe0007ae16d8:      	ldr	x0, [x22]
fffffe0007ae16dc:      	mov	w1, #65536
fffffe0007ae16e0:      	mov	w2, #4096
fffffe0007ae16e4:      	mov	w3, #4096
fffffe0007ae16e8:      	bl	0xfffffe0007a4dbbc <IOBufferMemoryDescriptor::inTaskWithOptions(task*, unsigned int, unsigned long, unsigned long)>
fffffe0007ae16ec:      	ldr	x8, [x21]
fffffe0007ae16f0:      	cbz	x0, 0xfffffe0007ae1a3c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x84c>
fffffe0007ae16f4:      	mov	x22, x0
fffffe0007ae16f8:      	ldr	x8, [x8, #776]
fffffe0007ae16fc:      	mov	x0, x21
fffffe0007ae1700:      	blr	x8
fffffe0007ae1704:      	mov	w1, #4096
fffffe0007ae1708:      	bl	0xfffffe0007ba3200 <_bzero>
fffffe0007ae170c:      	ldr	x8, [x22]
fffffe0007ae1710:      	mov	x0, x22
fffffe0007ae1714:      	ldr	x8, [x8, #776]
fffffe0007ae1718:      	blr	x8
fffffe0007ae171c:      	mov	w1, #4096
fffffe0007ae1720:      	bl	0xfffffe0007ba3200 <_bzero>
fffffe0007ae1724:      	ldr	x8, [x20, #328]
fffffe0007ae1728:      	mov	w19, wzr
fffffe0007ae172c:      	mov	w9, #2
fffffe0007ae1730:      	str	wzr, [x20, #992]
fffffe0007ae1734:      	stp	x21, x22, [x20, #240]
fffffe0007ae1738:      	add	x8, x8, #1
fffffe0007ae173c:      	str	w9, [x20, #232]
fffffe0007ae1740:      	str	x8, [x20, #328]
fffffe0007ae1744:      	b	0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae1748:      	sub	w22, w19, #5
fffffe0007ae174c:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1750:      	ldr	x0, [x24, #64]
fffffe0007ae1754:      	mov	x1, xzr
fffffe0007ae1758:      	mov	x2, x25
fffffe0007ae175c:      	mov	x3, x21
fffffe0007ae1760:      	ldr	x8, [x0]
fffffe0007ae1764:      	ldr	x8, [x8, #520]
fffffe0007ae1768:      	blr	x8
fffffe0007ae176c:      	mov	x26, x0
fffffe0007ae1770:      	ldr	x0, [x24, #64]
fffffe0007ae1774:      	mov	w1, #2
fffffe0007ae1778:      	ldr	x8, [x0]
fffffe0007ae177c:      	ldr	x8, [x8, #544]
fffffe0007ae1780:      	blr	x8
fffffe0007ae1784:      	mov	w22, w0
fffffe0007ae1788:      	cmp	x26, x21
fffffe0007ae178c:      	b.ne	0xfffffe0007ae184c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x65c>
fffffe0007ae1790:      	cbnz	w22, 0xfffffe0007ae184c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x65c>
fffffe0007ae1794:      	mov	x22, x25
fffffe0007ae1798:      	b	0xfffffe0007ae1430 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x240>
fffffe0007ae179c:      	cmp	w28, #19
fffffe0007ae17a0:      	b.ne	0xfffffe0007ae1810 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x620>
fffffe0007ae17a4:      	ldr	w8, [x25, #4]
fffffe0007ae17a8:      	cmp	w8, #24
fffffe0007ae17ac:      	b.ne	0xfffffe0007ae17ec <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x5fc>
fffffe0007ae17b0:      	mov	x0, x20
fffffe0007ae17b4:      	mov	w1, #19
fffffe0007ae17b8:      	mov	w2, wzr
fffffe0007ae17bc:      	bl	0xfffffe0007ae1134 <NiwlLifecycleClient::traceControl(unsigned int, unsigned int)>
fffffe0007ae17c0:      	cbz	w0, 0xfffffe0007ae17ec <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x5fc>
fffffe0007ae17c4:      	ldp	w8, w9, [x25, #8]
fffffe0007ae17c8:      	ldr	w10, [x25, #16]
fffffe0007ae17cc:      	adrp	x0, 0xfffffe0007150000 <_nbg_collect+0x74>
fffffe0007ae17d0:      	add	x0, x0, #3679
fffffe0007ae17d4:      	stp	x9, x10, [sp, #8]
fffffe0007ae17d8:      	str	x8, [sp]
fffffe0007ae17dc:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae17e0:      	ldur	w28, [x29, #-148]
fffffe0007ae17e4:      	cmp	w28, #19
fffffe0007ae17e8:      	b.ne	0xfffffe0007ae1810 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x620>
fffffe0007ae17ec:      	ldr	w8, [x24, #96]
fffffe0007ae17f0:      	cbnz	w8, 0xfffffe0007ae180c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x61c>
fffffe0007ae17f4:      	ldr	w8, [x25, #4]
fffffe0007ae17f8:      	cmp	w8, #24
fffffe0007ae17fc:      	b.ne	0xfffffe0007ae180c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x61c>
fffffe0007ae1800:      	add	x8, x25, x27
fffffe0007ae1804:      	ldr	w8, [x8, #20]
fffffe0007ae1808:      	cbz	w8, 0xfffffe0007ae19b0 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7c0>
fffffe0007ae180c:      	mov	w28, #19
fffffe0007ae1810:      	mov	w2, #27080
fffffe0007ae1814:      	mov	x0, x20
fffffe0007ae1818:      	mov	w1, w28
fffffe0007ae181c:      	movk	w2, #32808, lsl #16
fffffe0007ae1820:      	bl	0xfffffe0007ae1134 <NiwlLifecycleClient::traceControl(unsigned int, unsigned int)>
fffffe0007ae1824:      	cbz	w0, 0xfffffe0007ae1838 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x648>
fffffe0007ae1828:      	adrp	x0, 0xfffffe0007150000 <_nbg_collect+0xd0>
fffffe0007ae182c:      	add	x0, x0, #3824
fffffe0007ae1830:      	stp	x23, x28, [sp]
fffffe0007ae1834:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae1838:      	add	w19, w19, #5
fffffe0007ae183c:      	cbnz	x22, 0xfffffe0007ae1990 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7a0>
fffffe0007ae1840:      	b	0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae1844:      	add	w22, w19, #22
fffffe0007ae1848:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae184c:      	mov	x0, x25
fffffe0007ae1850:      	mov	x1, x21
fffffe0007ae1854:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae1858:      	mov	x0, x25
fffffe0007ae185c:      	mov	x1, x21
fffffe0007ae1860:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae1864:      	cmp	w22, #0
fffffe0007ae1868:      	csel	w22, w22, w19, ne
fffffe0007ae186c:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1870:      	bl	0xfffffe00079fc494 <___stack_chk_fail>
fffffe0007ae1874:      	tst	w23, #0xfffffff7
fffffe0007ae1878:      	b.eq	0xfffffe0007ae18c0 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x6d0>
fffffe0007ae187c:      	add	w19, w19, #5
fffffe0007ae1880:      	b	0xfffffe0007ae18dc <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x6ec>
fffffe0007ae1884:      	ldr	x4, [x24, #88]
fffffe0007ae1888:      	adrp	x2, 0xfffffe0007036000 <IOHIDEventServiceUserClient::copyEvent(unsigned int, IOHIDEvent*, IOHIDEvent**, unsigned int)+0xc>
fffffe0007ae188c:      	add	x2, x2, #2359
fffffe0007ae1890:      	ldr	w5, [x24, #96]
fffffe0007ae1894:      	add	x6, sp, #48
fffffe0007ae1898:      	mov	x0, x25
fffffe0007ae189c:      	mov	x1, x21
fffffe0007ae18a0:      	mov	w3, #37
fffffe0007ae18a4:      	str	xzr, [sp, #48]
fffffe0007ae18a8:      	bl	0xfffffe000799e26c <_niwb_driver_version>
fffffe0007ae18ac:      	cbnz	w0, 0xfffffe0007ae14e4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2f4>
fffffe0007ae18b0:      	ldr	x8, [sp, #48]
fffffe0007ae18b4:      	mov	w19, wzr
fffffe0007ae18b8:      	str	w8, [x24, #96]
fffffe0007ae18bc:      	b	0xfffffe0007ae14e4 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x2f4>
fffffe0007ae18c0:      	bl	0xfffffe000750e528 <_nord2_wifi_join_disconnect>
fffffe0007ae18c4:      	add	w8, w19, #22
fffffe0007ae18c8:      	cmp	w0, #2
fffffe0007ae18cc:      	mov	w9, #16
fffffe0007ae18d0:      	csel	w8, w9, w8, eq
fffffe0007ae18d4:      	cmp	w0, #0
fffffe0007ae18d8:      	csel	w19, wzr, w8, eq
fffffe0007ae18dc:      	ldur	w24, [x29, #-148]
fffffe0007ae18e0:      	mov	w2, #27080
fffffe0007ae18e4:      	mov	x0, x20
fffffe0007ae18e8:      	movk	w2, #32808, lsl #16
fffffe0007ae18ec:      	mov	w1, w24
fffffe0007ae18f0:      	bl	0xfffffe0007ae1134 <NiwlLifecycleClient::traceControl(unsigned int, unsigned int)>
fffffe0007ae18f4:      	cbz	w0, 0xfffffe0007ae198c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x79c>
fffffe0007ae18f8:      	cmp	w24, #22
fffffe0007ae18fc:      	b.ne	0xfffffe0007ae1970 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x780>
fffffe0007ae1900:      	mov	w8, w23
fffffe0007ae1904:      	cbz	w23, 0xfffffe0007ae1914 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x724>
fffffe0007ae1908:      	cmp	w23, #8
fffffe0007ae190c:      	b.ne	0xfffffe0007ae196c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x77c>
fffffe0007ae1910:      	ldr	w8, [x25, #12]
fffffe0007ae1914:      	mov	w9, w19
fffffe0007ae1918:      	adrp	x0, 0xfffffe0007150000 <_nbg_collect+0x1c0>
fffffe0007ae191c:      	add	x0, x0, #3553
fffffe0007ae1920:      	mov	w10, w23
fffffe0007ae1924:      	stp	x8, x9, [sp, #8]
fffffe0007ae1928:      	str	x10, [sp]
fffffe0007ae192c:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae1930:      	ldur	w24, [x29, #-148]
fffffe0007ae1934:      	b	0xfffffe0007ae1970 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x780>
fffffe0007ae1938:      	ldr	x8, [x20, #336]
fffffe0007ae193c:      	cmn	x8, #1
fffffe0007ae1940:      	b.eq	0xfffffe0007ae194c <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x75c>
fffffe0007ae1944:      	add	x8, x8, #1
fffffe0007ae1948:      	str	x8, [x20, #336]
fffffe0007ae194c:      	mov	x0, x20
fffffe0007ae1950:      	bl	0xfffffe0007ae0fec <NiwlLifecycleClient::discardLocked()>
fffffe0007ae1954:      	mov	w8, #1
fffffe0007ae1958:      	mov	w22, wzr
fffffe0007ae195c:      	str	w8, [x20, #232]
fffffe0007ae1960:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1964:      	add	w22, w19, #5
fffffe0007ae1968:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae196c:      	mov	w24, #22
fffffe0007ae1970:      	bl	0xfffffe0007ae2430 <niw_ms()>
fffffe0007ae1974:      	adrp	x8, 0xfffffe0007150000 <_nbg_collect+0x21c>
fffffe0007ae1978:      	add	x8, x8, #3609
fffffe0007ae197c:      	stp	x19, x0, [sp, #16]
fffffe0007ae1980:      	mov	x0, x8
fffffe0007ae1984:      	stp	x24, x23, [sp]
fffffe0007ae1988:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae198c:      	cbz	x22, 0xfffffe0007ae19a8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7b8>
fffffe0007ae1990:      	mov	x0, x22
fffffe0007ae1994:      	mov	x1, x21
fffffe0007ae1998:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae199c:      	mov	x0, x22
fffffe0007ae19a0:      	mov	x1, x21
fffffe0007ae19a4:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae19a8:      	mov	w22, w19
fffffe0007ae19ac:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae19b0:      	mov	x0, x26
fffffe0007ae19b4:      	mov	w1, #24
fffffe0007ae19b8:      	bl	0xfffffe00079a0480 <_niwi_power_on_admit>
fffffe0007ae19bc:      	cbz	w0, 0xfffffe0007ae19c8 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x7d8>
fffffe0007ae19c0:      	ldur	w28, [x29, #-148]
fffffe0007ae19c4:      	b	0xfffffe0007ae1810 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x620>
fffffe0007ae19c8:      	movi.2d	v0, #0000000000000000
fffffe0007ae19cc:      	add	x0, sp, #48
fffffe0007ae19d0:      	str	wzr, [sp, #144]
fffffe0007ae19d4:      	stp	q0, q0, [sp, #48]
fffffe0007ae19d8:      	stp	q0, q0, [sp, #80]
fffffe0007ae19dc:      	stp	q0, q0, [sp, #112]
fffffe0007ae19e0:      	bl	0xfffffe000750dacc <_nord2_wifi_scan_info>
fffffe0007ae19e4:      	mov	w2, #27080
fffffe0007ae19e8:      	cmp	w0, #0
fffffe0007ae19ec:      	mov	x0, x20
fffffe0007ae19f0:      	mov	w1, #19
fffffe0007ae19f4:      	movk	w2, #32808, lsl #16
fffffe0007ae19f8:      	add	w8, w19, #22
fffffe0007ae19fc:      	csel	w19, wzr, w8, eq
fffffe0007ae1a00:      	bl	0xfffffe0007ae1134 <NiwlLifecycleClient::traceControl(unsigned int, unsigned int)>
fffffe0007ae1a04:      	cbz	w0, 0xfffffe0007ae1a18 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x828>
fffffe0007ae1a08:      	adrp	x0, 0xfffffe0007150000 <_nbg_collect+0x2b0>
fffffe0007ae1a0c:      	add	x0, x0, #3735
fffffe0007ae1a10:      	stp	x23, x19, [sp]
fffffe0007ae1a14:      	bl	0xfffffe000735dae4 <_printf>
fffffe0007ae1a18:      	cbz	x22, 0xfffffe0007ae1a34 <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0x844>
fffffe0007ae1a1c:      	mov	x0, x22
fffffe0007ae1a20:      	mov	x1, x21
fffffe0007ae1a24:      	bl	0xfffffe00079a1d04 <_niwj_clear>
fffffe0007ae1a28:      	mov	x0, x22
fffffe0007ae1a2c:      	mov	x1, x21
fffffe0007ae1a30:      	bl	0xfffffe0007a10838 <_IOFree>
fffffe0007ae1a34:      	mov	w22, w19
fffffe0007ae1a38:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
fffffe0007ae1a3c:      	ldr	x8, [x8, #40]
fffffe0007ae1a40:      	mov	x0, x21
fffffe0007ae1a44:      	blr	x8
fffffe0007ae1a48:      	mov	w22, w19
fffffe0007ae1a4c:      	b	0xfffffe0007ae12ac <NiwlLifecycleClient::dispatchLocked(unsigned int, IOExternalMethodArguments*, bool*)+0xbc>
