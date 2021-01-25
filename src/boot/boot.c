#include <ultra64.h>
#include "boot/boot.h"
#include "boot/reset.h"
#include "constants.h"
#include "game/data/data_000000.h"
#include "game/data/data_0083d0.h"
#include "game/data/data_00e460.h"
#include "game/data/data_0160b0.h"
#include "game/data/data_01a3a0.h"
#include "game/data/data_020df0.h"
#include "game/data/data_02da90.h"
#include "game/game_0e0770.h"
#include "gvars/gvars.h"
#include "lib/lib_04a80.h"
#include "lib/lib_05e40.h"
#include "lib/lib_074f0.h"
#include "lib/lib_08a20.h"
#include "lib/lib_09660.h"
#include "lib/lib_0c000.h"
#include "lib/main.h"
#include "lib/lib_0e9d0.h"
#include "lib/lib_12dc0.h"
#include "lib/lib_13710.h"
#include "lib/lib_13750.h"
#include "lib/lib_13900.h"
#include "lib/lib_2fa00.h"
#include "lib/lib_48120.h"
#include "lib/lib_48150.h"
#include "lib/lib_481d0.h"
#include "lib/lib_485e0.h"
#include "lib/lib_48830.h"
#include "lib/lib_48c00.h"
#include "lib/lib_48cd0.h"
#include "lib/lib_48dc0.h"
#include "lib/lib_48ef0.h"
#include "lib/lib_48f50.h"
#include "lib/lib_490b0.h"
#include "types.h"

GLOBAL_ASM(
glabel func00001000
/*     1000:	3c088009 */ 	lui	$t0,%hi(var8008ae20)
/*     1004:	3c090002 */ 	lui	$t1,0x2
/*     1008:	2508ae20 */ 	addiu	$t0,$t0,%lo(var8008ae20)
/*     100c:	352923a0 */ 	ori	$t1,$t1,0x23a0
.L00001010:
/*     1010:	2129fff8 */ 	addi	$t1,$t1,-8
/*     1014:	ad000000 */ 	sw	$zero,0x0($t0)
/*     1018:	ad000004 */ 	sw	$zero,0x4($t0)
/*     101c:	1520fffc */ 	bnez	$t1,.L00001010
/*     1020:	21080008 */ 	addi	$t0,$t0,0x8
/*     1024:	3c0a8000 */ 	lui	$t2,0x8000
/*     1028:	3c1d8000 */ 	lui	$sp,0x8000
/*     102c:	254a1050 */ 	addiu	$t2,$t2,0x1050
/*     1030:	01400008 */ 	jr	$t2
/*     1034:	27bd0f10 */ 	addiu	$sp,$sp,0xf10
/*     1038:	00000000 */ 	nop
/*     103c:	00000000 */ 	nop
/*     1040:	00000000 */ 	nop
/*     1044:	00000000 */ 	nop
/*     1048:	00000000 */ 	nop
/*     104c:	00000000 */ 	nop
);

GLOBAL_ASM(
glabel func00001050
/*     1050:	3c08007f */ 	lui	$t0,0x7f
/*     1054:	3508e000 */ 	ori	$t0,$t0,0xe000
/*     1058:	40882800 */ 	mtc0	$t0,$5
/*     105c:	3c087000 */ 	lui	$t0,0x7000
/*     1060:	40885000 */ 	mtc0	$t0,$10
/*     1064:	2408001f */ 	addiu	$t0,$zero,0x1f
/*     1068:	40881000 */ 	mtc0	$t0,$2
/*     106c:	3c080001 */ 	lui	$t0,0x1
/*     1070:	3508001f */ 	ori	$t0,$t0,0x1f
/*     1074:	40881800 */ 	mtc0	$t0,$3
/*     1078:	24080000 */ 	addiu	$t0,$zero,0x0
/*     107c:	40880000 */ 	mtc0	$t0,$0
/*     1080:	00000000 */ 	nop
/*     1084:	42000002 */ 	tlbwi
/*     1088:	00000000 */ 	nop
/*     108c:	00000000 */ 	nop
/*     1090:	00000000 */ 	nop
/*     1094:	3c087000 */ 	lui	$t0,%hi(func000016cc)
/*     1098:	250816cc */ 	addiu	$t0,$t0,%lo(func000016cc)
/*     109c:	01000008 */ 	jr	$t0
/*     10a0:	00000000 */ 	nop
);

GLOBAL_ASM(
glabel func000010a4
/*     10a4:	27bdfff8 */ 	addiu	$sp,$sp,-8
/*     10a8:	afbf0000 */ 	sw	$ra,0x0($sp)
/*     10ac:	40802000 */ 	mtc0	$zero,$4
/*     10b0:	24080002 */ 	addiu	$t0,$zero,0x2
/*     10b4:	40883000 */ 	mtc0	$t0,$6
/*     10b8:	240901ff */ 	addiu	$t1,$zero,0x1ff
/*     10bc:	3c018009 */ 	lui	$at,%hi(var8008d264+0x2)
/*     10c0:	a429d266 */ 	sh	$t1,%lo(var8008d264+0x2)($at)
/*     10c4:	2404010c */ 	addiu	$a0,$zero,0x10c
/*     10c8:	3c018009 */ 	lui	$at,%hi(var8008ae28+0x2)
/*     10cc:	a424ae2a */ 	sh	$a0,%lo(var8008ae28+0x2)($at)
/*     10d0:	3c018009 */ 	lui	$at,%hi(var8008d258+0x2)
/*     10d4:	a424d25a */ 	sh	$a0,%lo(var8008d258+0x2)($at)
/*     10d8:	00042300 */ 	sll	$a0,$a0,0xc
/*     10dc:	3c028009 */ 	lui	$v0,%hi(var8008ae20)
/*     10e0:	8c42ae20 */ 	lw	$v0,%lo(var8008ae20)($v0)
/*     10e4:	3c097f1c */ 	lui	$t1,%hi(_gameSegmentEnd)
/*     10e8:	252999e0 */ 	addiu	$t1,$t1,%lo(_gameSegmentEnd)
/*     10ec:	3c0a7f00 */ 	lui	$t2,%hi(func0f000000)
/*     10f0:	254a0000 */ 	addiu	$t2,$t2,%lo(func0f000000)
/*     10f4:	012a4823 */ 	subu	$t1,$t1,$t2
/*     10f8:	3c080fff */ 	lui	$t0,0xfff
/*     10fc:	3508ffff */ 	ori	$t0,$t0,0xffff
/*     1100:	00481024 */ 	and	$v0,$v0,$t0
/*     1104:	3c018009 */ 	lui	$at,%hi(var8008d268)
/*     1108:	ac22d268 */ 	sw	$v0,%lo(var8008d268)($at)
/*     110c:	3c028009 */ 	lui	$v0,%hi(var8008d238)
/*     1110:	2442d238 */ 	addiu	$v0,$v0,%lo(var8008d238)
/*     1114:	24040021 */ 	addiu	$a0,$zero,0x21
/*     1118:	3c018009 */ 	lui	$at,%hi(var8008d25c)
/*     111c:	ac22d25c */ 	sw	$v0,%lo(var8008d25c)($at)
/*     1120:	00441821 */ 	addu	$v1,$v0,$a0
/*     1124:	3c018009 */ 	lui	$at,%hi(var8008d260)
/*     1128:	ac23d260 */ 	sw	$v1,%lo(var8008d260)($at)
/*     112c:	8fbf0000 */ 	lw	$ra,0x0($sp)
/*     1130:	27bd0008 */ 	addiu	$sp,$sp,0x8
/*     1134:	03e00008 */ 	jr	$ra
/*     1138:	00000000 */ 	nop
);

GLOBAL_ASM(
glabel func0000113c
/*     113c:	240800ff */ 	addiu	$t0,$zero,0xff
/*     1140:	3c028009 */ 	lui	$v0,%hi(var8008d25c)
/*     1144:	8c42d25c */ 	lw	$v0,%lo(var8008d25c)($v0)
/*     1148:	3c038009 */ 	lui	$v1,%hi(var8008d260)
/*     114c:	8c63d260 */ 	lw	$v1,%lo(var8008d260)($v1)
.L00001150:
/*     1150:	a0480000 */ 	sb	$t0,0x0($v0)
/*     1154:	1443fffe */ 	bne	$v0,$v1,.L00001150
/*     1158:	24420001 */ 	addiu	$v0,$v0,0x1
/*     115c:	24040004 */ 	addiu	$a0,$zero,0x4
/*     1160:	10800005 */ 	beqz	$a0,.L00001178
/*     1164:	2484ffff */ 	addiu	$a0,$a0,-1
/*     1168:	24080002 */ 	addiu	$t0,$zero,0x2
/*     116c:	00884004 */ 	sllv	$t0,$t0,$a0
/*     1170:	2508ffff */ 	addiu	$t0,$t0,-1
/*     1174:	a0680000 */ 	sb	$t0,0x0($v1)
.L00001178:
/*     1178:	03e00008 */ 	jr	$ra
/*     117c:	00000000 */ 	nop
);

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel func00001180
/*     1180:	40082000 */ 	mfc0	$t0,$4
/*     1184:	0008aa40 */ 	sll	$s5,$t0,0x9
/*     1188:	3c097f00 */ 	lui	$t1,0x7f00
/*     118c:	02a94022 */ 	sub	$t0,$s5,$t1
/*     1190:	00084302 */ 	srl	$t0,$t0,0xc
/*     1194:	000840c0 */ 	sll	$t0,$t0,0x3
/*     1198:	3c098009 */ 	lui	$t1,%hi(var8008ae24)
/*     119c:	8d29ae24 */ 	lw	$t1,%lo(var8008ae24)($t1)
/*     11a0:	01098021 */ 	addu	$s0,$t0,$t1
/*     11a4:	3c1e7f00 */ 	lui	$s8,0x7f00
/*     11a8:	02be082a */ 	slt	$at,$s5,$s8
/*     11ac:	1420011f */ 	bnez	$at,.L0000162c
/*     11b0:	00000000 */ 	nop
/*     11b4:	3c098009 */ 	lui	$t1,%hi(var80090b04)
/*     11b8:	8d290b04 */ 	lw	$t1,%lo(var80090b04)($t1)
/*     11bc:	02a9082a */ 	slt	$at,$s5,$t1
/*     11c0:	1020011a */ 	beqz	$at,.L0000162c
/*     11c4:	00000000 */ 	nop
/*     11c8:	40194000 */ 	mfc0	$t9,$8
/*     11cc:	0019cb02 */ 	srl	$t9,$t9,0xc
/*     11d0:	33390001 */ 	andi	$t9,$t9,0x1
/*     11d4:	53200002 */ 	beqzl	$t9,.L000011e0
/*     11d8:	8e110000 */ 	lw	$s1,0x0($s0)
/*     11dc:	8e110008 */ 	lw	$s1,0x8($s0)
.L000011e0:
/*     11e0:	562000b8 */ 	bnezl	$s1,.L000014c4
/*     11e4:	240d0001 */ 	addiu	$t5,$zero,0x1
/*     11e8:	240d0000 */ 	addiu	$t5,$zero,0x0
/*     11ec:	3c098009 */ 	lui	$t1,%hi(var8008d25c)
/*     11f0:	8d29d25c */ 	lw	$t1,%lo(var8008d25c)($t1)
/*     11f4:	3c0a8009 */ 	lui	$t2,%hi(var8008d260)
/*     11f8:	8d4ad260 */ 	lw	$t2,%lo(var8008d260)($t2)
.L000011fc:
/*     11fc:	91280000 */ 	lbu	$t0,0x0($t1)
/*     1200:	1100000a */ 	beqz	$t0,.L0000122c
/*     1204:	00000000 */ 	nop
/*     1208:	240e0000 */ 	addiu	$t6,$zero,0x0
/*     120c:	240f0001 */ 	addiu	$t7,$zero,0x1
.L00001210:
/*     1210:	010fc024 */ 	and	$t8,$t0,$t7
/*     1214:	17000009 */ 	bnez	$t8,.L0000123c
/*     1218:	00000000 */ 	nop
/*     121c:	25ce0001 */ 	addiu	$t6,$t6,0x1
/*     1220:	24010008 */ 	addiu	$at,$zero,0x8
/*     1224:	15c1fffa */ 	bne	$t6,$at,.L00001210
/*     1228:	000f7840 */ 	sll	$t7,$t7,0x1
.L0000122c:
/*     122c:	152afff3 */ 	bne	$t1,$t2,.L000011fc
/*     1230:	25290001 */ 	addiu	$t1,$t1,0x1
/*     1234:	0800055c */ 	j	0x1570
/*     1238:	00000000 */ 	nop
.L0000123c:
/*     123c:	010f4026 */ 	xor	$t0,$t0,$t7
/*     1240:	a1280000 */ 	sb	$t0,0x0($t1)
/*     1244:	3c0a8009 */ 	lui	$t2,%hi(var8008d25c)
/*     1248:	8d4ad25c */ 	lw	$t2,%lo(var8008d25c)($t2)
/*     124c:	3c118009 */ 	lui	$s1,%hi(var8008d268)
/*     1250:	8e31d268 */ 	lw	$s1,%lo(var8008d268)($s1)
/*     1254:	012a4823 */ 	subu	$t1,$t1,$t2
/*     1258:	000948c0 */ 	sll	$t1,$t1,0x3
/*     125c:	01c94021 */ 	addu	$t0,$t6,$t1
/*     1260:	00084300 */ 	sll	$t0,$t0,0xc
/*     1264:	02288821 */ 	addu	$s1,$s1,$t0
/*     1268:	400a4000 */ 	mfc0	$t2,$8
/*     126c:	3c0800ff */ 	lui	$t0,0xff
/*     1270:	3508f000 */ 	ori	$t0,$t0,0xf000
/*     1274:	01485024 */ 	and	$t2,$t2,$t0
/*     1278:	000a5282 */ 	srl	$t2,$t2,0xa
/*     127c:	3c088009 */ 	lui	$t0,%hi(var8008ae30)
/*     1280:	8d08ae30 */ 	lw	$t0,%lo(var8008ae30)($t0)
/*     1284:	010a4021 */ 	addu	$t0,$t0,$t2
/*     1288:	8d0a0000 */ 	lw	$t2,0x0($t0)
/*     128c:	8d080004 */ 	lw	$t0,0x4($t0)
/*     1290:	010a4023 */ 	subu	$t0,$t0,$t2
/*     1294:	2409fff0 */ 	addiu	$t1,$zero,-16
/*     1298:	2508000f */ 	addiu	$t0,$t0,0xf
/*     129c:	01097024 */ 	and	$t6,$t0,$t1
/*     12a0:	3c08a460 */ 	lui	$t0,0xa460
/*     12a4:	35080010 */ 	ori	$t0,$t0,0x10
.L000012a8:
/*     12a8:	8d090000 */ 	lw	$t1,0x0($t0)
/*     12ac:	31290003 */ 	andi	$t1,$t1,0x3
/*     12b0:	1520fffd */ 	bnez	$t1,.L000012a8
/*     12b4:	00000000 */ 	nop
/*     12b8:	3c16a430 */ 	lui	$s6,0xa430
/*     12bc:	8ed60008 */ 	lw	$s6,0x8($s6)
/*     12c0:	32d60010 */ 	andi	$s6,$s6,0x10
/*     12c4:	3c08a460 */ 	lui	$t0,0xa460
/*     12c8:	3c098009 */ 	lui	$t1,%hi(var8008ae2c)
/*     12cc:	8d29ae2c */ 	lw	$t1,%lo(var8008ae2c)($t1)
/*     12d0:	3c0f0fff */ 	lui	$t7,0xfff
/*     12d4:	35efffff */ 	ori	$t7,$t7,0xffff
/*     12d8:	012f4824 */ 	and	$t1,$t1,$t7
/*     12dc:	ad090000 */ 	sw	$t1,0x0($t0)
/*     12e0:	3c08a460 */ 	lui	$t0,0xa460
/*     12e4:	35080004 */ 	ori	$t0,$t0,0x4
/*     12e8:	3c098000 */ 	lui	$t1,0x8000
/*     12ec:	8d290308 */ 	lw	$t1,0x308($t1)
/*     12f0:	012a4825 */ 	or	$t1,$t1,$t2
/*     12f4:	3c0a1fff */ 	lui	$t2,0x1fff
/*     12f8:	354affff */ 	ori	$t2,$t2,0xffff
/*     12fc:	012a4824 */ 	and	$t1,$t1,$t2
/*     1300:	ad090000 */ 	sw	$t1,0x0($t0)
/*     1304:	3c08a460 */ 	lui	$t0,0xa460
/*     1308:	3508000c */ 	ori	$t0,$t0,0xc
/*     130c:	25ceffff */ 	addiu	$t6,$t6,-1
/*     1310:	ad0e0000 */ 	sw	$t6,0x0($t0)
/*     1314:	53200003 */ 	beqzl	$t9,.L00001324
/*     1318:	00000000 */ 	nop
/*     131c:	50000002 */ 	beqzl	$zero,.L00001328
/*     1320:	ae110008 */ 	sw	$s1,0x8($s0)
.L00001324:
/*     1324:	ae110000 */ 	sw	$s1,0x0($s0)
.L00001328:
/*     1328:	3c08a460 */ 	lui	$t0,0xa460
/*     132c:	35080010 */ 	ori	$t0,$t0,0x10
.L00001330:
/*     1330:	8d090000 */ 	lw	$t1,0x0($t0)
/*     1334:	31290003 */ 	andi	$t1,$t1,0x3
/*     1338:	1520fffd */ 	bnez	$t1,.L00001330
/*     133c:	00000000 */ 	nop
/*     1340:	3c088009 */ 	lui	$t0,%hi(var8008ae2c)
/*     1344:	8d08ae2c */ 	lw	$t0,%lo(var8008ae2c)($t0)
/*     1348:	25091000 */ 	addiu	$t1,$t0,0x1000
.L0000134c:
/*     134c:	bd150000 */ 	cache	0x15,0x0($t0)
/*     1350:	0109082b */ 	sltu	$at,$t0,$t1
/*     1354:	1420fffd */ 	bnez	$at,.L0000134c
/*     1358:	25080010 */ 	addiu	$t0,$t0,16
/*     135c:	16c00004 */ 	bnez	$s6,.L00001370
/*     1360:	00000000 */ 	nop
/*     1364:	24080002 */ 	addiu	$t0,$zero,0x2
/*     1368:	3c01a460 */ 	lui	$at,0xa460
/*     136c:	ac280010 */ 	sw	$t0,0x10($at)
.L00001370:
/*     1370:	3c048009 */ 	lui	$a0,%hi(var8008ae38)
/*     1374:	2484ae38 */ 	addiu	$a0,$a0,%lo(var8008ae38)
/*     1378:	24840ff8 */ 	addiu	$a0,$a0,0xff8
/*     137c:	ac9d0000 */ 	sw	$sp,0x0($a0)
/*     1380:	249d0000 */ 	addiu	$sp,$a0,0x0
/*     1384:	3c048009 */ 	lui	$a0,%hi(var8008ae2c)
/*     1388:	8c84ae2c */ 	lw	$a0,%lo(var8008ae2c)($a0)
/*     138c:	24840002 */ 	addiu	$a0,$a0,0x2
/*     1390:	3c088000 */ 	lui	$t0,0x8000
/*     1394:	02282825 */ 	or	$a1,$s1,$t0
/*     1398:	3c068009 */ 	lui	$a2,%hi(var8008be38)
/*     139c:	24c6be38 */ 	addiu	$a2,$a2,%lo(var8008be38)
/*     13a0:	27bdff80 */ 	addiu	$sp,$sp,-128
/*     13a4:	afbf0000 */ 	sw	$ra,0x0($sp)
/*     13a8:	afa10004 */ 	sw	$at,0x4($sp)
/*     13ac:	afa20008 */ 	sw	$v0,0x8($sp)
/*     13b0:	afa3000c */ 	sw	$v1,0xc($sp)
/*     13b4:	afa40010 */ 	sw	$a0,0x10($sp)
/*     13b8:	afa50014 */ 	sw	$a1,0x14($sp)
/*     13bc:	afa60018 */ 	sw	$a2,0x18($sp)
/*     13c0:	afa7001c */ 	sw	$a3,0x1c($sp)
/*     13c4:	afa80020 */ 	sw	$t0,0x20($sp)
/*     13c8:	afa90024 */ 	sw	$t1,0x24($sp)
/*     13cc:	afaa0028 */ 	sw	$t2,0x28($sp)
/*     13d0:	afab002c */ 	sw	$t3,0x2c($sp)
/*     13d4:	afac0030 */ 	sw	$t4,0x30($sp)
/*     13d8:	afad0034 */ 	sw	$t5,0x34($sp)
/*     13dc:	afae0038 */ 	sw	$t6,0x38($sp)
/*     13e0:	afaf003c */ 	sw	$t7,0x3c($sp)
/*     13e4:	afb00040 */ 	sw	$s0,0x40($sp)
/*     13e8:	afb10044 */ 	sw	$s1,0x44($sp)
/*     13ec:	afb20048 */ 	sw	$s2,0x48($sp)
/*     13f0:	afb3004c */ 	sw	$s3,0x4c($sp)
/*     13f4:	afb40050 */ 	sw	$s4,0x50($sp)
/*     13f8:	afb50054 */ 	sw	$s5,0x54($sp)
/*     13fc:	afb60058 */ 	sw	$s6,0x58($sp)
/*     1400:	afb7005c */ 	sw	$s7,0x5c($sp)
/*     1404:	afb80060 */ 	sw	$t8,0x60($sp)
/*     1408:	afb90064 */ 	sw	$t9,0x64($sp)
/*     140c:	afbc0070 */ 	sw	$gp,0x70($sp)
/*     1410:	afbd0074 */ 	sw	$sp,0x74($sp)
/*     1414:	afbe0078 */ 	sw	$s8,0x78($sp)
/*     1418:	0c001d3c */ 	jal	func000074f0
/*     141c:	00000000 */ 	nop
/*     1420:	8fbf0000 */ 	lw	$ra,0x0($sp)
/*     1424:	8fa10004 */ 	lw	$at,0x4($sp)
/*     1428:	8fa20008 */ 	lw	$v0,0x8($sp)
/*     142c:	8fa3000c */ 	lw	$v1,0xc($sp)
/*     1430:	8fa40010 */ 	lw	$a0,0x10($sp)
/*     1434:	8fa50014 */ 	lw	$a1,0x14($sp)
/*     1438:	8fa60018 */ 	lw	$a2,0x18($sp)
/*     143c:	8fa7001c */ 	lw	$a3,0x1c($sp)
/*     1440:	8fa80020 */ 	lw	$t0,0x20($sp)
/*     1444:	8fa90024 */ 	lw	$t1,0x24($sp)
/*     1448:	8faa0028 */ 	lw	$t2,0x28($sp)
/*     144c:	8fab002c */ 	lw	$t3,0x2c($sp)
/*     1450:	8fac0030 */ 	lw	$t4,0x30($sp)
/*     1454:	8fad0034 */ 	lw	$t5,0x34($sp)
/*     1458:	8fae0038 */ 	lw	$t6,0x38($sp)
/*     145c:	8faf003c */ 	lw	$t7,0x3c($sp)
/*     1460:	8fb00040 */ 	lw	$s0,0x40($sp)
/*     1464:	8fb10044 */ 	lw	$s1,0x44($sp)
/*     1468:	8fb20048 */ 	lw	$s2,0x48($sp)
/*     146c:	8fb3004c */ 	lw	$s3,0x4c($sp)
/*     1470:	8fb40050 */ 	lw	$s4,0x50($sp)
/*     1474:	8fb50054 */ 	lw	$s5,0x54($sp)
/*     1478:	8fb60058 */ 	lw	$s6,0x58($sp)
/*     147c:	8fb7005c */ 	lw	$s7,0x5c($sp)
/*     1480:	8fb80060 */ 	lw	$t8,0x60($sp)
/*     1484:	8fb90064 */ 	lw	$t9,0x64($sp)
/*     1488:	8fbc0070 */ 	lw	$gp,0x70($sp)
/*     148c:	8fbd0074 */ 	lw	$sp,0x74($sp)
/*     1490:	8fbe0078 */ 	lw	$s8,0x78($sp)
/*     1494:	27bd0080 */ 	addiu	$sp,$sp,0x80
/*     1498:	3c048009 */ 	lui	$a0,%hi(var8008ae38)
/*     149c:	2484ae38 */ 	addiu	$a0,$a0,%lo(var8008ae38)
/*     14a0:	24840ff8 */ 	addiu	$a0,$a0,4088
/*     14a4:	8c9d0000 */ 	lw	$sp,0x0($a0)
/*     14a8:	3c088000 */ 	lui	$t0,0x8000
/*     14ac:	02284025 */ 	or	$t0,$s1,$t0
/*     14b0:	25090ff0 */ 	addiu	$t1,$t0,0xff0
.L000014b4:
/*     14b4:	bd190000 */ 	cache	0x19,0x0($t0)
/*     14b8:	0109082b */ 	sltu	$at,$t0,$t1
/*     14bc:	1420fffd */ 	bnez	$at,.L000014b4
/*     14c0:	25080010 */ 	addiu	$t0,$t0,0x10
.L000014c4:
/*     14c4:	24080000 */ 	addiu	$t0,$zero,0x0
/*     14c8:	40882800 */ 	mtc0	$t0,$5
/*     14cc:	40955000 */ 	mtc0	$s5,$10
/*     14d0:	00000000 */ 	nop
/*     14d4:	00000000 */ 	nop
/*     14d8:	00000000 */ 	nop
/*     14dc:	42000008 */ 	tlbp
/*     14e0:	00000000 */ 	nop
/*     14e4:	00000000 */ 	nop
/*     14e8:	40090000 */ 	mfc0	$t1,$0
/*     14ec:	40882800 */ 	mtc0	$t0,$5
/*     14f0:	40955000 */ 	mtc0	$s5,$10
/*     14f4:	8e080000 */ 	lw	$t0,0x0($s0)
/*     14f8:	00084302 */ 	srl	$t0,$t0,0xc
/*     14fc:	00084180 */ 	sll	$t0,$t0,0x6
/*     1500:	55000001 */ 	bnezl	$t0,.L00001508
/*     1504:	3508001e */ 	ori	$t0,$t0,0x1e
.L00001508:
/*     1508:	40881000 */ 	mtc0	$t0,$2
/*     150c:	8e080008 */ 	lw	$t0,0x8($s0)
/*     1510:	00084302 */ 	srl	$t0,$t0,0xc
/*     1514:	00084180 */ 	sll	$t0,$t0,0x6
/*     1518:	55000001 */ 	bnezl	$t0,.L00001520
/*     151c:	3508001e */ 	ori	$t0,$t0,0x1e
.L00001520:
/*     1520:	40881800 */ 	mtc0	$t0,$3
/*     1524:	05220003 */ 	bltzl	$t1,.L00001534
/*     1528:	00000000 */ 	nop
/*     152c:	10000002 */ 	b	.L00001538
/*     1530:	42000002 */ 	tlbwi
.L00001534:
/*     1534:	42000006 */ 	tlbwr
.L00001538:
/*     1538:	15a0000b */ 	bnez	$t5,.L00001568
/*     153c:	00000000 */ 	nop
/*     1540:	02a04025 */ 	or	$t0,$s5,$zero
/*     1544:	57200001 */ 	bnezl	$t9,.L0000154c
/*     1548:	25081000 */ 	addiu	$t0,$t0,0x1000
.L0000154c:
/*     154c:	25090fe0 */ 	addiu	$t1,$t0,0xfe0
/*     1550:	310a001f */ 	andi	$t2,$t0,0x1f
/*     1554:	010a4023 */ 	subu	$t0,$t0,$t2
.L00001558:
/*     1558:	bd100000 */ 	cache	0x10,0x0($t0)
/*     155c:	0109082b */ 	sltu	$at,$t0,$t1
/*     1560:	1420fffd */ 	bnez	$at,.L00001558
/*     1564:	25080020 */ 	addiu	$t0,$t0,0x20
.L00001568:
/*     1568:	03e00008 */ 	jr	$ra
/*     156c:	00000000 */ 	nop
.L00001570:
/*     1570:	3c148009 */ 	lui	$s4,%hi(var8008ae24)
/*     1574:	8e94ae24 */ 	lw	$s4,%lo(var8008ae24)($s4)
/*     1578:	3c1c8009 */ 	lui	$gp,%hi(var80090b08)
/*     157c:	8f9c0b08 */ 	lw	$gp,%lo(var80090b08)($gp)
/*     1580:	40084800 */ 	mfc0	$t0,$9
/*     1584:	3c098009 */ 	lui	$t1,%hi(var8008d264+0x2)
/*     1588:	9529d266 */ 	lhu	$t1,%lo(var8008d264+0x2)($t1)
/*     158c:	3c0a8006 */ 	lui	$t2,%hi(var8005cf84)
/*     1590:	8d4acf84 */ 	lw	$t2,%lo(var8005cf84)($t2)
/*     1594:	01094024 */ 	and	$t0,$t0,$t1
/*     1598:	010a082a */ 	slt	$at,$t0,$t2
/*     159c:	50200001 */ 	beqzl	$at,.L000015a4
/*     15a0:	010a4023 */ 	subu	$t0,$t0,$t2
.L000015a4:
/*     15a4:	000848c0 */ 	sll	$t1,$t0,0x3
/*     15a8:	01344821 */ 	addu	$t1,$t1,$s4
.L000015ac:
/*     15ac:	8d310000 */ 	lw	$s1,0x0($t1)
/*     15b0:	16200006 */ 	bnez	$s1,.L000015cc
/*     15b4:	00000000 */ 	nop
/*     15b8:	25290008 */ 	addiu	$t1,$t1,8
/*     15bc:	153cfffb */ 	bne	$t1,$gp,.L000015ac
/*     15c0:	00000000 */ 	nop
/*     15c4:	0800056b */ 	j	0x15ac
/*     15c8:	02804825 */ 	or	$t1,$s4,$zero
.L000015cc:
/*     15cc:	01344023 */ 	subu	$t0,$t1,$s4
/*     15d0:	000840c2 */ 	srl	$t0,$t0,0x3
/*     15d4:	00084300 */ 	sll	$t0,$t0,0xc
/*     15d8:	011e4021 */ 	addu	$t0,$t0,$s8
/*     15dc:	240af000 */ 	addiu	$t2,$zero,-4096
/*     15e0:	010a4024 */ 	and	$t0,$t0,$t2
/*     15e4:	40885000 */ 	mtc0	$t0,$10
/*     15e8:	24080000 */ 	addiu	$t0,$zero,0x0
/*     15ec:	40882800 */ 	mtc0	$t0,$5
/*     15f0:	00000000 */ 	nop
/*     15f4:	00000000 */ 	nop
/*     15f8:	00000000 */ 	nop
/*     15fc:	42000008 */ 	tlbp
/*     1600:	00000000 */ 	nop
/*     1604:	00000000 */ 	nop
/*     1608:	400a0000 */ 	mfc0	$t2,$0
/*     160c:	40085000 */ 	mfc0	$t0,$10
/*     1610:	05400003 */ 	bltz	$t2,.L00001620
/*     1614:	00000000 */ 	nop
/*     1618:	0800055c */ 	j	0x1570
/*     161c:	00000000 */ 	nop
.L00001620:
/*     1620:	ad200000 */ 	sw	$zero,0x0($t1)
/*     1624:	0800049a */ 	j	0x1268
/*     1628:	00000000 */ 	nop
.L0000162c:
/*     162c:	08000ea2 */ 	j	0x3a88
/*     1630:	00000000 */ 	nop
);
#else
GLOBAL_ASM(
glabel func00001180
/*     1180:	40082000 */ 	mfc0	$t0,$4
/*     1184:	0008aa40 */ 	sll	$s5,$t0,0x9
/*     1188:	3c097f00 */ 	lui	$t1,0x7f00
/*     118c:	02a94022 */ 	sub	$t0,$s5,$t1
/*     1190:	00084302 */ 	srl	$t0,$t0,0xc
/*     1194:	000840c0 */ 	sll	$t0,$t0,0x3
/*     1198:	3c098009 */ 	lui	$t1,0x8009
/*     119c:	8d29d454 */ 	lw	$t1,-0x2bac($t1)
/*     11a0:	01098021 */ 	addu	$s0,$t0,$t1
/*     11a4:	3c1e7f00 */ 	lui	$s8,0x7f00
/*     11a8:	02be082a */ 	slt	$at,$s5,$s8
/*     11ac:	1420012e */ 	bnez	$at,.L00001668
/*     11b0:	00000000 */ 	nop
/*     11b4:	3c098009 */ 	lui	$t1,0x8009
/*     11b8:	8d2930f4 */ 	lw	$t1,0x30f4($t1)
/*     11bc:	02a9082a */ 	slt	$at,$s5,$t1
/*     11c0:	10200129 */ 	beqz	$at,.L00001668
/*     11c4:	00000000 */ 	nop
/*     11c8:	3c0a8009 */ 	lui	$t2,0x8009
/*     11cc:	254a30e4 */ 	addiu	$t2,$t2,0x30e4
/*     11d0:	8d4e0000 */ 	lw	$t6,0x0($t2)
/*     11d4:	25ce0001 */ 	addiu	$t6,$t6,0x1
/*     11d8:	ad4e0000 */ 	sw	$t6,0x0($t2)
/*     11dc:	40194000 */ 	mfc0	$t9,$8
/*     11e0:	0019cb02 */ 	srl	$t9,$t9,0xc
/*     11e4:	33390001 */ 	andi	$t9,$t9,0x1
/*     11e8:	53200002 */ 	beqzl	$t9,.L000011f4
/*     11ec:	8e110000 */ 	lw	$s1,0x0($s0)
/*     11f0:	8e110008 */ 	lw	$s1,0x8($s0)
.L000011f4:
/*     11f4:	562000bd */ 	bnezl	$s1,.L000014ec
/*     11f8:	240d0001 */ 	li	$t5,0x1
/*     11fc:	240d0000 */ 	li	$t5,0x0
/*     1200:	3c098009 */ 	lui	$t1,0x8009
/*     1204:	8d29f88c */ 	lw	$t1,-0x774($t1)
/*     1208:	3c0a8009 */ 	lui	$t2,0x8009
/*     120c:	8d4af890 */ 	lw	$t2,-0x770($t2)
.L00001210:
/*     1210:	91280000 */ 	lbu	$t0,0x0($t1)
/*     1214:	1100000a */ 	beqz	$t0,.L00001240
/*     1218:	00000000 */ 	nop
/*     121c:	240e0000 */ 	li	$t6,0x0
/*     1220:	240f0001 */ 	li	$t7,0x1
.L00001224:
/*     1224:	010fc024 */ 	and	$t8,$t0,$t7
/*     1228:	17000009 */ 	bnez	$t8,.L00001250
/*     122c:	00000000 */ 	nop
/*     1230:	25ce0001 */ 	addiu	$t6,$t6,0x1
/*     1234:	24010008 */ 	li	$at,0x8
/*     1238:	15c1fffa */ 	bne	$t6,$at,.L00001224
/*     123c:	000f7840 */ 	sll	$t7,$t7,0x1
.L00001240:
/*     1240:	152afff3 */ 	bne	$t1,$t2,.L00001210
/*     1244:	25290001 */ 	addiu	$t1,$t1,0x1
/*     1248:	08000566 */ 	j	0x1598
/*     124c:	00000000 */ 	nop
.L00001250:
/*     1250:	010f4026 */ 	xor	$t0,$t0,$t7
/*     1254:	a1280000 */ 	sb	$t0,0x0($t1)
/*     1258:	3c0a8009 */ 	lui	$t2,0x8009
/*     125c:	8d4af88c */ 	lw	$t2,-0x774($t2)
/*     1260:	3c118009 */ 	lui	$s1,0x8009
/*     1264:	8e31f898 */ 	lw	$s1,-0x768($s1)
/*     1268:	012a4823 */ 	subu	$t1,$t1,$t2
/*     126c:	000948c0 */ 	sll	$t1,$t1,0x3
/*     1270:	01c94021 */ 	addu	$t0,$t6,$t1
/*     1274:	00084300 */ 	sll	$t0,$t0,0xc
/*     1278:	02288821 */ 	addu	$s1,$s1,$t0
/*     127c:	3c0a8009 */ 	lui	$t2,0x8009
/*     1280:	254a30e8 */ 	addiu	$t2,$t2,0x30e8
/*     1284:	8d480000 */ 	lw	$t0,0x0($t2)
/*     1288:	25080001 */ 	addiu	$t0,$t0,0x1
/*     128c:	ad480000 */ 	sw	$t0,0x0($t2)
/*     1290:	400a4000 */ 	mfc0	$t2,$8
/*     1294:	3c0800ff */ 	lui	$t0,0xff
/*     1298:	3508f000 */ 	ori	$t0,$t0,0xf000
/*     129c:	01485024 */ 	and	$t2,$t2,$t0
/*     12a0:	000a5282 */ 	srl	$t2,$t2,0xa
/*     12a4:	3c088009 */ 	lui	$t0,0x8009
/*     12a8:	8d08d460 */ 	lw	$t0,-0x2ba0($t0)
/*     12ac:	010a4021 */ 	addu	$t0,$t0,$t2
/*     12b0:	8d0a0000 */ 	lw	$t2,0x0($t0)
/*     12b4:	8d080004 */ 	lw	$t0,0x4($t0)
/*     12b8:	010a4023 */ 	subu	$t0,$t0,$t2
/*     12bc:	2409fff0 */ 	li	$t1,-16
/*     12c0:	2508000f */ 	addiu	$t0,$t0,0xf
/*     12c4:	01097024 */ 	and	$t6,$t0,$t1
/*     12c8:	3c08a460 */ 	lui	$t0,0xa460
/*     12cc:	35080010 */ 	ori	$t0,$t0,0x10
.L000012d0:
/*     12d0:	8d090000 */ 	lw	$t1,0x0($t0)
/*     12d4:	31290003 */ 	andi	$t1,$t1,0x3
/*     12d8:	1520fffd */ 	bnez	$t1,.L000012d0
/*     12dc:	00000000 */ 	nop
/*     12e0:	3c16a430 */ 	lui	$s6,0xa430
/*     12e4:	8ed60008 */ 	lw	$s6,0x8($s6)
/*     12e8:	32d60010 */ 	andi	$s6,$s6,0x10
/*     12ec:	3c08a460 */ 	lui	$t0,0xa460
/*     12f0:	3c098009 */ 	lui	$t1,0x8009
/*     12f4:	8d29d45c */ 	lw	$t1,-0x2ba4($t1)
/*     12f8:	3c0f0fff */ 	lui	$t7,0xfff
/*     12fc:	35efffff */ 	ori	$t7,$t7,0xffff
/*     1300:	012f4824 */ 	and	$t1,$t1,$t7
/*     1304:	ad090000 */ 	sw	$t1,0x0($t0)
/*     1308:	3c08a460 */ 	lui	$t0,0xa460
/*     130c:	35080004 */ 	ori	$t0,$t0,0x4
/*     1310:	3c098000 */ 	lui	$t1,0x8000
/*     1314:	8d290308 */ 	lw	$t1,0x308($t1)
/*     1318:	012a4825 */ 	or	$t1,$t1,$t2
/*     131c:	3c0a1fff */ 	lui	$t2,0x1fff
/*     1320:	354affff */ 	ori	$t2,$t2,0xffff
/*     1324:	012a4824 */ 	and	$t1,$t1,$t2
/*     1328:	ad090000 */ 	sw	$t1,0x0($t0)
/*     132c:	3c08a460 */ 	lui	$t0,0xa460
/*     1330:	3508000c */ 	ori	$t0,$t0,0xc
/*     1334:	25ceffff */ 	addiu	$t6,$t6,-1
/*     1338:	ad0e0000 */ 	sw	$t6,0x0($t0)
/*     133c:	53200003 */ 	beqzl	$t9,.L0000134c
/*     1340:	00000000 */ 	nop
/*     1344:	50000002 */ 	beqzl	$zero,.L00001350
/*     1348:	ae110008 */ 	sw	$s1,0x8($s0)
.L0000134c:
/*     134c:	ae110000 */ 	sw	$s1,0x0($s0)
.L00001350:
/*     1350:	3c08a460 */ 	lui	$t0,0xa460
/*     1354:	35080010 */ 	ori	$t0,$t0,0x10
.L00001358:
/*     1358:	8d090000 */ 	lw	$t1,0x0($t0)
/*     135c:	31290003 */ 	andi	$t1,$t1,0x3
/*     1360:	1520fffd */ 	bnez	$t1,.L00001358
/*     1364:	00000000 */ 	nop
/*     1368:	3c088009 */ 	lui	$t0,0x8009
/*     136c:	8d08d45c */ 	lw	$t0,-0x2ba4($t0)
/*     1370:	25091000 */ 	addiu	$t1,$t0,0x1000
.L00001374:
/*     1374:	bd150000 */ 	cache	0x15,0x0($t0)
/*     1378:	0109082b */ 	sltu	$at,$t0,$t1
/*     137c:	1420fffd */ 	bnez	$at,.L00001374
/*     1380:	25080010 */ 	addiu	$t0,$t0,0x10
/*     1384:	16c00004 */ 	bnez	$s6,.L00001398
/*     1388:	00000000 */ 	nop
/*     138c:	24080002 */ 	li	$t0,0x2
/*     1390:	3c01a460 */ 	lui	$at,0xa460
/*     1394:	ac280010 */ 	sw	$t0,0x10($at)
.L00001398:
/*     1398:	3c048009 */ 	lui	$a0,0x8009
/*     139c:	2484d468 */ 	addiu	$a0,$a0,-11160
/*     13a0:	24840ff8 */ 	addiu	$a0,$a0,0xff8
/*     13a4:	ac9d0000 */ 	sw	$sp,0x0($a0)
/*     13a8:	249d0000 */ 	addiu	$sp,$a0,0x0
/*     13ac:	3c048009 */ 	lui	$a0,0x8009
/*     13b0:	8c84d45c */ 	lw	$a0,-0x2ba4($a0)
/*     13b4:	24840002 */ 	addiu	$a0,$a0,0x2
/*     13b8:	3c088000 */ 	lui	$t0,0x8000
/*     13bc:	02282825 */ 	or	$a1,$s1,$t0
/*     13c0:	3c068009 */ 	lui	$a2,0x8009
/*     13c4:	24c6e468 */ 	addiu	$a2,$a2,-7064
/*     13c8:	27bdff80 */ 	addiu	$sp,$sp,-128
/*     13cc:	afbf0000 */ 	sw	$ra,0x0($sp)
/*     13d0:	afa10004 */ 	sw	$at,0x4($sp)
/*     13d4:	afa20008 */ 	sw	$v0,0x8($sp)
/*     13d8:	afa3000c */ 	sw	$v1,0xc($sp)
/*     13dc:	afa40010 */ 	sw	$a0,0x10($sp)
/*     13e0:	afa50014 */ 	sw	$a1,0x14($sp)
/*     13e4:	afa60018 */ 	sw	$a2,0x18($sp)
/*     13e8:	afa7001c */ 	sw	$a3,0x1c($sp)
/*     13ec:	afa80020 */ 	sw	$t0,0x20($sp)
/*     13f0:	afa90024 */ 	sw	$t1,0x24($sp)
/*     13f4:	afaa0028 */ 	sw	$t2,0x28($sp)
/*     13f8:	afab002c */ 	sw	$t3,0x2c($sp)
/*     13fc:	afac0030 */ 	sw	$t4,0x30($sp)
/*     1400:	afad0034 */ 	sw	$t5,0x34($sp)
/*     1404:	afae0038 */ 	sw	$t6,0x38($sp)
/*     1408:	afaf003c */ 	sw	$t7,0x3c($sp)
/*     140c:	afb00040 */ 	sw	$s0,0x40($sp)
/*     1410:	afb10044 */ 	sw	$s1,0x44($sp)
/*     1414:	afb20048 */ 	sw	$s2,0x48($sp)
/*     1418:	afb3004c */ 	sw	$s3,0x4c($sp)
/*     141c:	afb40050 */ 	sw	$s4,0x50($sp)
/*     1420:	afb50054 */ 	sw	$s5,0x54($sp)
/*     1424:	afb60058 */ 	sw	$s6,0x58($sp)
/*     1428:	afb7005c */ 	sw	$s7,0x5c($sp)
/*     142c:	afb80060 */ 	sw	$t8,0x60($sp)
/*     1430:	afb90064 */ 	sw	$t9,0x64($sp)
/*     1434:	afbc0070 */ 	sw	$gp,0x70($sp)
/*     1438:	afbd0074 */ 	sw	$sp,0x74($sp)
/*     143c:	afbe0078 */ 	sw	$s8,0x78($sp)
/*     1440:	0c001da4 */ 	jal	0x7690
/*     1444:	00000000 */ 	nop
/*     1448:	8fbf0000 */ 	lw	$ra,0x0($sp)
/*     144c:	8fa10004 */ 	lw	$at,0x4($sp)
/*     1450:	8fa20008 */ 	lw	$v0,0x8($sp)
/*     1454:	8fa3000c */ 	lw	$v1,0xc($sp)
/*     1458:	8fa40010 */ 	lw	$a0,0x10($sp)
/*     145c:	8fa50014 */ 	lw	$a1,0x14($sp)
/*     1460:	8fa60018 */ 	lw	$a2,0x18($sp)
/*     1464:	8fa7001c */ 	lw	$a3,0x1c($sp)
/*     1468:	8fa80020 */ 	lw	$t0,0x20($sp)
/*     146c:	8fa90024 */ 	lw	$t1,0x24($sp)
/*     1470:	8faa0028 */ 	lw	$t2,0x28($sp)
/*     1474:	8fab002c */ 	lw	$t3,0x2c($sp)
/*     1478:	8fac0030 */ 	lw	$t4,0x30($sp)
/*     147c:	8fad0034 */ 	lw	$t5,0x34($sp)
/*     1480:	8fae0038 */ 	lw	$t6,0x38($sp)
/*     1484:	8faf003c */ 	lw	$t7,0x3c($sp)
/*     1488:	8fb00040 */ 	lw	$s0,0x40($sp)
/*     148c:	8fb10044 */ 	lw	$s1,0x44($sp)
/*     1490:	8fb20048 */ 	lw	$s2,0x48($sp)
/*     1494:	8fb3004c */ 	lw	$s3,0x4c($sp)
/*     1498:	8fb40050 */ 	lw	$s4,0x50($sp)
/*     149c:	8fb50054 */ 	lw	$s5,0x54($sp)
/*     14a0:	8fb60058 */ 	lw	$s6,0x58($sp)
/*     14a4:	8fb7005c */ 	lw	$s7,0x5c($sp)
/*     14a8:	8fb80060 */ 	lw	$t8,0x60($sp)
/*     14ac:	8fb90064 */ 	lw	$t9,0x64($sp)
/*     14b0:	8fbc0070 */ 	lw	$gp,0x70($sp)
/*     14b4:	8fbd0074 */ 	lw	$sp,0x74($sp)
/*     14b8:	8fbe0078 */ 	lw	$s8,0x78($sp)
/*     14bc:	27bd0080 */ 	addiu	$sp,$sp,0x80
/*     14c0:	3c048009 */ 	lui	$a0,0x8009
/*     14c4:	2484d468 */ 	addiu	$a0,$a0,-11160
/*     14c8:	24840ff8 */ 	addiu	$a0,$a0,0xff8
/*     14cc:	8c9d0000 */ 	lw	$sp,0x0($a0)
/*     14d0:	3c088000 */ 	lui	$t0,0x8000
/*     14d4:	02284025 */ 	or	$t0,$s1,$t0
/*     14d8:	25090ff0 */ 	addiu	$t1,$t0,0xff0
.L000014dc:
/*     14dc:	bd190000 */ 	cache	0x19,0x0($t0)
/*     14e0:	0109082b */ 	sltu	$at,$t0,$t1
/*     14e4:	1420fffd */ 	bnez	$at,.L000014dc
/*     14e8:	25080010 */ 	addiu	$t0,$t0,0x10
.L000014ec:
/*     14ec:	24080000 */ 	li	$t0,0x0
/*     14f0:	40882800 */ 	mtc0	$t0,$5
/*     14f4:	40955000 */ 	mtc0	$s5,$10
/*     14f8:	00000000 */ 	nop
/*     14fc:	00000000 */ 	nop
/*     1500:	00000000 */ 	nop
/*     1504:	42000008 */ 	tlbp
/*     1508:	00000000 */ 	nop
/*     150c:	00000000 */ 	nop
/*     1510:	40090000 */ 	mfc0	$t1,$0
/*     1514:	40882800 */ 	mtc0	$t0,$5
/*     1518:	40955000 */ 	mtc0	$s5,$10
/*     151c:	8e080000 */ 	lw	$t0,0x0($s0)
/*     1520:	00084302 */ 	srl	$t0,$t0,0xc
/*     1524:	00084180 */ 	sll	$t0,$t0,0x6
/*     1528:	55000001 */ 	bnezl	$t0,.L00001530
/*     152c:	3508001e */ 	ori	$t0,$t0,0x1e
.L00001530:
/*     1530:	40881000 */ 	mtc0	$t0,$2
/*     1534:	8e080008 */ 	lw	$t0,0x8($s0)
/*     1538:	00084302 */ 	srl	$t0,$t0,0xc
/*     153c:	00084180 */ 	sll	$t0,$t0,0x6
/*     1540:	55000001 */ 	bnezl	$t0,.L00001548
/*     1544:	3508001e */ 	ori	$t0,$t0,0x1e
.L00001548:
/*     1548:	40881800 */ 	mtc0	$t0,$3
/*     154c:	05220003 */ 	bltzl	$t1,.L0000155c
/*     1550:	00000000 */ 	nop
/*     1554:	10000002 */ 	b	.L00001560
/*     1558:	42000002 */ 	tlbwi
.L0000155c:
/*     155c:	42000006 */ 	tlbwr
.L00001560:
/*     1560:	15a0000b */ 	bnez	$t5,.L00001590
/*     1564:	00000000 */ 	nop
/*     1568:	02a04025 */ 	move	$t0,$s5
/*     156c:	57200001 */ 	bnezl	$t9,.L00001574
/*     1570:	25081000 */ 	addiu	$t0,$t0,0x1000
.L00001574:
/*     1574:	25090fe0 */ 	addiu	$t1,$t0,0xfe0
/*     1578:	310a001f */ 	andi	$t2,$t0,0x1f
/*     157c:	010a4023 */ 	subu	$t0,$t0,$t2
.L00001580:
/*     1580:	bd100000 */ 	cache	0x10,0x0($t0)
/*     1584:	0109082b */ 	sltu	$at,$t0,$t1
/*     1588:	1420fffd */ 	bnez	$at,.L00001580
/*     158c:	25080020 */ 	addiu	$t0,$t0,0x20
.L00001590:
/*     1590:	03e00008 */ 	jr	$ra
/*     1594:	00000000 */ 	nop
/*     1598:	3c088009 */ 	lui	$t0,0x8009
/*     159c:	250830ec */ 	addiu	$t0,$t0,0x30ec
/*     15a0:	8d090000 */ 	lw	$t1,0x0($t0)
/*     15a4:	25290001 */ 	addiu	$t1,$t1,0x1
/*     15a8:	ad090000 */ 	sw	$t1,0x0($t0)
/*     15ac:	3c148009 */ 	lui	$s4,0x8009
/*     15b0:	8e94d454 */ 	lw	$s4,-0x2bac($s4)
/*     15b4:	3c1c8009 */ 	lui	$gp,0x8009
/*     15b8:	8f9c30f8 */ 	lw	$gp,0x30f8($gp)
/*     15bc:	40084800 */ 	mfc0	$t0,$9
/*     15c0:	3c098009 */ 	lui	$t1,0x8009
/*     15c4:	9529f896 */ 	lhu	$t1,-0x76a($t1)
/*     15c8:	3c0a8006 */ 	lui	$t2,0x8006
/*     15cc:	8d4ae720 */ 	lw	$t2,-0x18e0($t2)
/*     15d0:	01094024 */ 	and	$t0,$t0,$t1
/*     15d4:	010a082a */ 	slt	$at,$t0,$t2
/*     15d8:	50200001 */ 	beqzl	$at,.L000015e0
/*     15dc:	010a4023 */ 	subu	$t0,$t0,$t2
.L000015e0:
/*     15e0:	000848c0 */ 	sll	$t1,$t0,0x3
/*     15e4:	01344821 */ 	addu	$t1,$t1,$s4
.L000015e8:
/*     15e8:	8d310000 */ 	lw	$s1,0x0($t1)
/*     15ec:	16200006 */ 	bnez	$s1,.L00001608
/*     15f0:	00000000 */ 	nop
/*     15f4:	25290008 */ 	addiu	$t1,$t1,0x8
/*     15f8:	153cfffb */ 	bne	$t1,$gp,.L000015e8
/*     15fc:	00000000 */ 	nop
/*     1600:	0800057a */ 	j	0x15e8
/*     1604:	02804825 */ 	move	$t1,$s4
.L00001608:
/*     1608:	01344023 */ 	subu	$t0,$t1,$s4
/*     160c:	000840c2 */ 	srl	$t0,$t0,0x3
/*     1610:	00084300 */ 	sll	$t0,$t0,0xc
/*     1614:	011e4021 */ 	addu	$t0,$t0,$s8
/*     1618:	240af000 */ 	li	$t2,-4096
/*     161c:	010a4024 */ 	and	$t0,$t0,$t2
/*     1620:	40885000 */ 	mtc0	$t0,$10
/*     1624:	24080000 */ 	li	$t0,0x0
/*     1628:	40882800 */ 	mtc0	$t0,$5
/*     162c:	00000000 */ 	nop
/*     1630:	00000000 */ 	nop
/*     1634:	00000000 */ 	nop
/*     1638:	42000008 */ 	tlbp
/*     163c:	00000000 */ 	nop
/*     1640:	00000000 */ 	nop
/*     1644:	400a0000 */ 	mfc0	$t2,$0
/*     1648:	40085000 */ 	mfc0	$t0,$10
/*     164c:	05400003 */ 	bltz	$t2,.L0000165c
/*     1650:	00000000 */ 	nop
/*     1654:	0800056b */ 	j	0x15ac
/*     1658:	00000000 */ 	nop
.L0000165c:
/*     165c:	ad200000 */ 	sw	$zero,0x0($t1)
/*     1660:	0800049f */ 	j	0x127c
/*     1664:	00000000 */ 	nop
.L00001668:
/*     1668:	08000f2a */ 	j	0x3ca8
/*     166c:	00000000 */ 	nop
);
#endif

GLOBAL_ASM(
glabel func00001634
/*     1634:	40085000 */ 	mfc0	$t0,$10
/*     1638:	3c0a8000 */ 	lui	$t2,0x8000
/*     163c:	408a5000 */ 	mtc0	$t2,$10
/*     1640:	40801000 */ 	mtc0	$zero,$2
/*     1644:	40801800 */ 	mtc0	$zero,$3
.L00001648:
/*     1648:	40850000 */ 	mtc0	$a1,$0
/*     164c:	00000000 */ 	nop
/*     1650:	42000002 */ 	tlbwi
/*     1654:	00000000 */ 	nop
/*     1658:	00000000 */ 	nop
/*     165c:	14a4fffa */ 	bne	$a1,$a0,.L00001648
/*     1660:	20a5ffff */ 	addi	$a1,$a1,-1
/*     1664:	40885000 */ 	mtc0	$t0,$10
/*     1668:	03e00008 */ 	jr	$ra
/*     166c:	00000000 */ 	nop
);

#if VERSION == VERSION_NTSC_BETA
GLOBAL_ASM(
glabel func000016acnb
/*     16ac:	3c088000 */ 	lui	$t0,0x8000
/*     16b0:	25091ff0 */ 	addiu	$t1,$t0,0x1ff0
.L000016b4:
/*     16b4:	bd010000 */ 	cache	0x1,0x0($t0)
/*     16b8:	0109082b */ 	sltu	$at,$t0,$t1
/*     16bc:	1420fffd */ 	bnez	$at,.L000016b4
/*     16c0:	25080010 */ 	addiu	$t0,$t0,0x10
/*     16c4:	03e00008 */ 	jr	$ra
/*     16c8:	00000000 */ 	nop
/*     16cc:	00000000 */ 	nop
);
#endif

extern void *_dataSegmentStart;
extern void *_datazipSegmentRomStart;
extern void *_inflateSegmentRomStart;
extern void *_gamezipSegmentRomStart;

void *getSetupRamAddr(void)
{
	return &_dataSegmentStart;
}

void *getSetupRomAddr(void)
{
	return &_datazipSegmentRomStart;
}

void *getInflateRomAddr(void)
{
	return &_inflateSegmentRomStart;
}

void *getInflateRomAddr2(void)
{
	return &_inflateSegmentRomStart;
}

void *getZiplistSegmentRomstart(void)
{
	return &_gamezipSegmentRomStart;
}

GLOBAL_ASM(
glabel func000016ac
/*     16ac:	3c077020 */ 	lui	$a3,%hi(inflate1173)
/*     16b0:	24e7126c */ 	addiu	$a3,$a3,%lo(inflate1173)
/*     16b4:	00e00008 */ 	jr	$a3
/*     16b8:	00000000 */ 	nop
/*     16bc:	00000000 */ 	nop
);

#if VERSION >= VERSION_NTSC_1_0
s32 osGetMemSize(void)
{
	return g_OsMemSize;
}
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel func000016cc
/*     16cc:	3c0e8000 */ 	lui	$t6,0x8000
/*     16d0:	8dce030c */ 	lw	$t6,0x30c($t6)
/*     16d4:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*     16d8:	24010001 */ 	addiu	$at,$zero,0x1
/*     16dc:	afbf0024 */ 	sw	$ra,0x24($sp)
/*     16e0:	15c10007 */ 	bne	$t6,$at,.L00001700
/*     16e4:	afb00020 */ 	sw	$s0,0x20($sp)
/*     16e8:	3c0f803f */ 	lui	$t7,0x803f
/*     16ec:	8df850b8 */ 	lw	$t8,0x50b8($t7)
/*     16f0:	3c028009 */ 	lui	$v0,%hi(g_OsMemSize)
/*     16f4:	2442dcb4 */ 	addiu	$v0,$v0,%lo(g_OsMemSize)
/*     16f8:	10000008 */ 	b	.L0000171c
/*     16fc:	ac580000 */ 	sw	$t8,0x0($v0)
.L00001700:
/*     1700:	3c198000 */ 	lui	$t9,0x8000
/*     1704:	8f390318 */ 	lw	$t9,0x318($t9)
/*     1708:	3c028009 */ 	lui	$v0,%hi(g_OsMemSize)
/*     170c:	2442dcb4 */ 	addiu	$v0,$v0,%lo(g_OsMemSize)
/*     1710:	3c0a803f */ 	lui	$t2,0x803f
/*     1714:	ac590000 */ 	sw	$t9,0x0($v0)
/*     1718:	ad5950b8 */ 	sw	$t9,0x50b8($t2)
.L0000171c:
/*     171c:	3c040004 */ 	lui	$a0,%hi(_datazipSegmentRomStart)
/*     1720:	3c0b0005 */ 	lui	$t3,%hi(_datazipSegmentRomEnd)
/*     1724:	3c0c0005 */ 	lui	$t4,%hi(_inflateSegmentRomEnd)
/*     1728:	3c0d0005 */ 	lui	$t5,%hi(_inflateSegmentRomStart)
/*     172c:	25ade850 */ 	addiu	$t5,$t5,%lo(_inflateSegmentRomStart)
/*     1730:	258cfc40 */ 	addiu	$t4,$t4,%lo(_inflateSegmentRomEnd)
/*     1734:	256be850 */ 	addiu	$t3,$t3,%lo(_datazipSegmentRomEnd)
/*     1738:	24849850 */ 	addiu	$a0,$a0,%lo(_datazipSegmentRomStart)
/*     173c:	01644023 */ 	subu	$t0,$t3,$a0
/*     1740:	018d7023 */ 	subu	$t6,$t4,$t5
/*     1744:	010e1821 */ 	addu	$v1,$t0,$t6
/*     1748:	2462ffff */ 	addiu	$v0,$v1,-1
/*     174c:	0440000b */ 	bltz	$v0,.L0000177c
/*     1750:	3c057000 */ 	lui	$a1,%hi(func00001050)
/*     1754:	3c017000 */ 	lui	$at,0x7000
/*     1758:	3c0f7020 */ 	lui	$t7,0x7020
/*     175c:	01e88023 */ 	subu	$s0,$t7,$t0
/*     1760:	00811825 */ 	or	$v1,$a0,$at
.L00001764:
/*     1764:	0062c021 */ 	addu	$t8,$v1,$v0
/*     1768:	93190000 */ 	lbu	$t9,0x0($t8)
/*     176c:	02024821 */ 	addu	$t1,$s0,$v0
/*     1770:	2442ffff */ 	addiu	$v0,$v0,-1
/*     1774:	0441fffb */ 	bgez	$v0,.L00001764
/*     1778:	a1390000 */ 	sb	$t9,0x0($t1)
.L0000177c:
/*     177c:	24a51050 */ 	addiu	$a1,$a1,%lo(func00001050)
/*     1780:	3c07702c */ 	lui	$a3,0x702c
/*     1784:	3c0a7020 */ 	lui	$t2,0x7020
/*     1788:	01488023 */ 	subu	$s0,$t2,$t0
/*     178c:	34e793e0 */ 	ori	$a3,$a3,0x93e0
/*     1790:	24a52000 */ 	addiu	$a1,$a1,0x2000
/*     1794:	00002025 */ 	or	$a0,$zero,$zero
/*     1798:	3c037028 */ 	lui	$v1,0x7028
.L0000179c:
/*     179c:	00a45821 */ 	addu	$t3,$a1,$a0
/*     17a0:	8d6c0000 */ 	lw	$t4,0x0($t3)
/*     17a4:	24630004 */ 	addiu	$v1,$v1,0x4
/*     17a8:	0067082b */ 	sltu	$at,$v1,$a3
/*     17ac:	24840004 */ 	addiu	$a0,$a0,0x4
/*     17b0:	1420fffa */ 	bnez	$at,.L0000179c
/*     17b4:	ac6cfffc */ 	sw	$t4,-0x4($v1)
/*     17b8:	3c047028 */ 	lui	$a0,0x7028
/*     17bc:	0c0005ab */ 	jal	func000016ac
/*     17c0:	3c068030 */ 	lui	$a2,0x8030
/*     17c4:	3c058006 */ 	lui	$a1,%hi(var80059fe0)
/*     17c8:	24a59fe0 */ 	addiu	$a1,$a1,%lo(var80059fe0)
/*     17cc:	02002025 */ 	or	$a0,$s0,$zero
/*     17d0:	0c0005ab */ 	jal	func000016ac
/*     17d4:	3c068030 */ 	lui	$a2,0x8030
/*     17d8:	3c0da000 */ 	lui	$t5,0xa000
/*     17dc:	8dae02e8 */ 	lw	$t6,0x2e8($t5)
/*     17e0:	3c01c86e */ 	lui	$at,0xc86e
/*     17e4:	34212000 */ 	ori	$at,$at,0x2000
/*     17e8:	11c10003 */ 	beq	$t6,$at,.L000017f8
/*     17ec:	24040001 */ 	addiu	$a0,$zero,0x1
.L000017f0:
/*     17f0:	1000ffff */ 	b	.L000017f0
/*     17f4:	00000000 */ 	nop
.L000017f8:
/*     17f8:	0c00058d */ 	jal	func00001634
/*     17fc:	2405001f */ 	addiu	$a1,$zero,0x1f
/*     1800:	3c048006 */ 	lui	$a0,%hi(var8005ce10)
/*     1804:	3c038006 */ 	lui	$v1,%hi(var8005ce2c)
/*     1808:	3c028006 */ 	lui	$v0,%hi(var8005ce48)
/*     180c:	2442ce48 */ 	addiu	$v0,$v0,%lo(var8005ce48)
/*     1810:	2463ce2c */ 	addiu	$v1,$v1,%lo(var8005ce2c)
/*     1814:	2484ce10 */ 	addiu	$a0,$a0,%lo(var8005ce10)
.L00001818:
/*     1818:	24630004 */ 	addiu	$v1,$v1,0x4
/*     181c:	24840004 */ 	addiu	$a0,$a0,0x4
/*     1820:	ac80fffc */ 	sw	$zero,-0x4($a0)
/*     1824:	1462fffc */ 	bne	$v1,$v0,.L00001818
/*     1828:	ac60fffc */ 	sw	$zero,-0x4($v1)
/*     182c:	0c0016d8 */ 	jal	osInitialize
/*     1830:	00000000 */ 	nop
/*     1834:	0c012048 */ 	jal	osWritebackDCacheAll
/*     1838:	00000000 */ 	nop
/*     183c:	3c048000 */ 	lui	$a0,0x8000
/*     1840:	0c012054 */ 	jal	osInvalICache
/*     1844:	24054000 */ 	addiu	$a1,$zero,0x4000
/*     1848:	0c012074 */ 	jal	func000481d0
/*     184c:	00000000 */ 	nop
/*     1850:	0c012078 */ 	jal	__osSetFpcCsr
/*     1854:	34440e80 */ 	ori	$a0,$v0,0xe80
/*     1858:	24040003 */ 	addiu	$a0,$zero,0x3
/*     185c:	0c00062b */ 	jal	allocateStack
/*     1860:	34059800 */ 	dli	$a1,0x9800
/*     1864:	3c108009 */ 	lui	$s0,%hi(var8008d6d0)
/*     1868:	2610d6d0 */ 	addiu	$s0,$s0,%lo(var8008d6d0)
/*     186c:	3c067000 */ 	lui	$a2,%hi(mainproc)
/*     1870:	2409000a */ 	addiu	$t1,$zero,0xa
/*     1874:	afa90014 */ 	sw	$t1,0x14($sp)
/*     1878:	24c61aa4 */ 	addiu	$a2,$a2,%lo(mainproc)
/*     187c:	02002025 */ 	or	$a0,$s0,$zero
/*     1880:	24050003 */ 	addiu	$a1,$zero,0x3
/*     1884:	00003825 */ 	or	$a3,$zero,$zero
/*     1888:	0c000fb8 */ 	jal	osCreateThread
/*     188c:	afa20010 */ 	sw	$v0,0x10($sp)
/*     1890:	0c01207c */ 	jal	osStartThread
/*     1894:	02002025 */ 	or	$a0,$s0,$zero
/*     1898:	8fbf0024 */ 	lw	$ra,0x24($sp)
/*     189c:	8fb00020 */ 	lw	$s0,0x20($sp)
/*     18a0:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*     18a4:	03e00008 */ 	jr	$ra
/*     18a8:	00000000 */ 	nop
);
#else
GLOBAL_ASM(
glabel func000016cc
/*     1720:	3c040003 */ 	lui	$a0,0x3
/*     1724:	3c0e0004 */ 	lui	$t6,0x4
/*     1728:	3c0f0004 */ 	lui	$t7,0x4
/*     172c:	3c180004 */ 	lui	$t8,0x4
/*     1730:	27182850 */ 	addiu	$t8,$t8,0x2850
/*     1734:	25ef3c40 */ 	addiu	$t7,$t7,0x3c40
/*     1738:	25ce2850 */ 	addiu	$t6,$t6,0x2850
/*     173c:	24840850 */ 	addiu	$a0,$a0,0x850
/*     1740:	01c44023 */ 	subu	$t0,$t6,$a0
/*     1744:	01f8c823 */ 	subu	$t9,$t7,$t8
/*     1748:	01191821 */ 	addu	$v1,$t0,$t9
/*     174c:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*     1750:	2462ffff */ 	addiu	$v0,$v1,-1
/*     1754:	afbf0024 */ 	sw	$ra,0x24($sp)
/*     1758:	0440000b */ 	bltz	$v0,.L00001788
/*     175c:	afb00020 */ 	sw	$s0,0x20($sp)
/*     1760:	3c017000 */ 	lui	$at,0x7000
/*     1764:	3c097020 */ 	lui	$t1,0x7020
/*     1768:	01288023 */ 	subu	$s0,$t1,$t0
/*     176c:	00811825 */ 	or	$v1,$a0,$at
.L00001770:
/*     1770:	00625021 */ 	addu	$t2,$v1,$v0
/*     1774:	914b0000 */ 	lbu	$t3,0x0($t2)
/*     1778:	02026021 */ 	addu	$t4,$s0,$v0
/*     177c:	2442ffff */ 	addiu	$v0,$v0,-1
/*     1780:	0441fffb */ 	bgez	$v0,.L00001770
/*     1784:	a18b0000 */ 	sb	$t3,0x0($t4)
.L00001788:
/*     1788:	3c057000 */ 	lui	$a1,0x7000
/*     178c:	24a51050 */ 	addiu	$a1,$a1,0x1050
/*     1790:	3c0d7020 */ 	lui	$t5,0x7020
/*     1794:	3c07702c */ 	lui	$a3,0x702c
/*     1798:	34e793e0 */ 	ori	$a3,$a3,0x93e0
/*     179c:	01a88023 */ 	subu	$s0,$t5,$t0
/*     17a0:	24a52000 */ 	addiu	$a1,$a1,0x2000
/*     17a4:	00002025 */ 	move	$a0,$zero
/*     17a8:	3c037028 */ 	lui	$v1,0x7028
.L000017ac:
/*     17ac:	00a47021 */ 	addu	$t6,$a1,$a0
/*     17b0:	8dcf0000 */ 	lw	$t7,0x0($t6)
/*     17b4:	24630004 */ 	addiu	$v1,$v1,0x4
/*     17b8:	0067082b */ 	sltu	$at,$v1,$a3
/*     17bc:	24840004 */ 	addiu	$a0,$a0,0x4
/*     17c0:	1420fffa */ 	bnez	$at,.L000017ac
/*     17c4:	ac6ffffc */ 	sw	$t7,-0x4($v1)
/*     17c8:	3c047028 */ 	lui	$a0,0x7028
/*     17cc:	0c0005c3 */ 	jal	0x170c
/*     17d0:	3c068030 */ 	lui	$a2,0x8030
/*     17d4:	3c058006 */ 	lui	$a1,0x8006
/*     17d8:	24a5b760 */ 	addiu	$a1,$a1,-18592
/*     17dc:	02002025 */ 	move	$a0,$s0
/*     17e0:	0c0005c3 */ 	jal	0x170c
/*     17e4:	3c068030 */ 	lui	$a2,0x8030
/*     17e8:	24040001 */ 	li	$a0,0x1
/*     17ec:	0c00059c */ 	jal	0x1670
/*     17f0:	2405001f */ 	li	$a1,0x1f
/*     17f4:	3c048006 */ 	lui	$a0,0x8006
/*     17f8:	3c038006 */ 	lui	$v1,0x8006
/*     17fc:	3c028006 */ 	lui	$v0,0x8006
/*     1800:	2442e5c8 */ 	addiu	$v0,$v0,-6712
/*     1804:	2463e5ac */ 	addiu	$v1,$v1,-6740
/*     1808:	2484e590 */ 	addiu	$a0,$a0,-6768
.L0000180c:
/*     180c:	24630004 */ 	addiu	$v1,$v1,0x4
/*     1810:	24840004 */ 	addiu	$a0,$a0,0x4
/*     1814:	ac80fffc */ 	sw	$zero,-0x4($a0)
/*     1818:	1462fffc */ 	bne	$v1,$v0,.L0000180c
/*     181c:	ac60fffc */ 	sw	$zero,-0x4($v1)
/*     1820:	0c001728 */ 	jal	0x5ca0
/*     1824:	00000000 */ 	nop
/*     1828:	0c01253c */ 	jal	0x494f0
/*     182c:	00000000 */ 	nop
/*     1830:	3c048000 */ 	lui	$a0,0x8000
/*     1834:	0c012548 */ 	jal	0x49520
/*     1838:	24054000 */ 	li	$a1,0x4000
/*     183c:	0c012568 */ 	jal	0x495a0
/*     1840:	00000000 */ 	nop
/*     1844:	0c01256c */ 	jal	0x495b0
/*     1848:	34440e80 */ 	ori	$a0,$v0,0xe80
/*     184c:	3c03bc00 */ 	lui	$v1,0xbc00
/*     1850:	34630c02 */ 	ori	$v1,$v1,0xc02
/*     1854:	3c018009 */ 	lui	$at,0x8009
/*     1858:	ac2302e4 */ 	sw	$v1,0x2e4($at)
/*     185c:	3c018009 */ 	lui	$at,0x8009
/*     1860:	240a4040 */ 	li	$t2,0x4040
/*     1864:	a42a02e8 */ 	sh	$t2,0x2e8($at)
/*     1868:	240b4040 */ 	li	$t3,0x4040
/*     186c:	a46b0000 */ 	sh	$t3,0x0($v1)
/*     1870:	24040003 */ 	li	$a0,0x3
/*     1874:	0c000631 */ 	jal	0x18c4
/*     1878:	34059800 */ 	li	$a1,0x9800
/*     187c:	3c108009 */ 	lui	$s0,0x8009
/*     1880:	2610fd00 */ 	addiu	$s0,$s0,-768
/*     1884:	3c067000 */ 	lui	$a2,0x7000
/*     1888:	240c000a */ 	li	$t4,0xa
/*     188c:	afac0014 */ 	sw	$t4,0x14($sp)
/*     1890:	24c61b04 */ 	addiu	$a2,$a2,0x1b04
/*     1894:	02002025 */ 	move	$a0,$s0
/*     1898:	24050003 */ 	li	$a1,0x3
/*     189c:	00003825 */ 	move	$a3,$zero
/*     18a0:	0c00107c */ 	jal	0x41f0
/*     18a4:	afa20010 */ 	sw	$v0,0x10($sp)
/*     18a8:	0c012570 */ 	jal	0x495c0
/*     18ac:	02002025 */ 	move	$a0,$s0
/*     18b0:	8fbf0024 */ 	lw	$ra,0x24($sp)
/*     18b4:	8fb00020 */ 	lw	$s0,0x20($sp)
/*     18b8:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*     18bc:	03e00008 */ 	jr	$ra
/*     18c0:	00000000 */ 	nop
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel allocateStack
/*     18ac:	3c098006 */ 	lui	$t1,%hi(var8005ce48)
/*     18b0:	2529ce48 */ 	addiu	$t1,$t1,%lo(var8005ce48)
/*     18b4:	8d230000 */ 	lw	$v1,0x0($t1)
/*     18b8:	00041080 */ 	sll	$v0,$a0,0x2
/*     18bc:	3c018006 */ 	lui	$at,%hi(var8005ce2c)
/*     18c0:	00220821 */ 	addu	$at,$at,$v0
/*     18c4:	ac23ce2c */ 	sw	$v1,%lo(var8005ce2c)($at)
/*     18c8:	2401fff0 */ 	addiu	$at,$zero,-16
/*     18cc:	24a5000f */ 	addiu	$a1,$a1,0xf
/*     18d0:	00a17024 */ 	and	$t6,$a1,$at
/*     18d4:	3c018006 */ 	lui	$at,%hi(var8005ce10)
/*     18d8:	006e7823 */ 	subu	$t7,$v1,$t6
/*     18dc:	ad2f0000 */ 	sw	$t7,0x0($t1)
/*     18e0:	00220821 */ 	addu	$at,$at,$v0
/*     18e4:	01c02825 */ 	or	$a1,$t6,$zero
/*     18e8:	ac2fce10 */ 	sw	$t7,%lo(var8005ce10)($at)
/*     18ec:	01e01825 */ 	or	$v1,$t7,$zero
/*     18f0:	19c0000d */ 	blez	$t6,.L00001928
/*     18f4:	00004025 */ 	or	$t0,$zero,$zero
/*     18f8:	3083000f */ 	andi	$v1,$a0,0xf
/*     18fc:	2418000f */ 	addiu	$t8,$zero,0xf
/*     1900:	0303c823 */ 	subu	$t9,$t8,$v1
/*     1904:	00195100 */ 	sll	$t2,$t9,0x4
/*     1908:	01433025 */ 	or	$a2,$t2,$v1
/*     190c:	01e01025 */ 	or	$v0,$t7,$zero
.L00001910:
/*     1910:	25080001 */ 	addiu	$t0,$t0,0x1
/*     1914:	24420001 */ 	addiu	$v0,$v0,0x1
/*     1918:	1505fffd */ 	bne	$t0,$a1,.L00001910
/*     191c:	a046ffff */ 	sb	$a2,-0x1($v0)
/*     1920:	3c038006 */ 	lui	$v1,%hi(var8005ce48)
/*     1924:	8c63ce48 */ 	lw	$v1,%lo(var8005ce48)($v1)
.L00001928:
/*     1928:	00651021 */ 	addu	$v0,$v1,$a1
/*     192c:	03e00008 */ 	jr	$ra
/*     1930:	2442fff8 */ 	addiu	$v0,$v0,-8
);
#else
GLOBAL_ASM(
glabel allocateStack
/*     18c4:	3c0a8006 */ 	lui	$t2,0x8006
/*     18c8:	254ae5c8 */ 	addiu	$t2,$t2,-6712
/*     18cc:	8d430000 */ 	lw	$v1,0x0($t2)
/*     18d0:	00041080 */ 	sll	$v0,$a0,0x2
/*     18d4:	3c018006 */ 	lui	$at,0x8006
/*     18d8:	00220821 */ 	addu	$at,$at,$v0
/*     18dc:	ac23e5ac */ 	sw	$v1,-0x1a54($at)
/*     18e0:	2401fff0 */ 	li	$at,-16
/*     18e4:	24a5000f */ 	addiu	$a1,$a1,0xf
/*     18e8:	3c188006 */ 	lui	$t8,0x8006
/*     18ec:	00a17024 */ 	and	$t6,$a1,$at
/*     18f0:	2718e590 */ 	addiu	$t8,$t8,-6768
/*     18f4:	00583821 */ 	addu	$a3,$v0,$t8
/*     18f8:	006e7823 */ 	subu	$t7,$v1,$t6
/*     18fc:	01c02825 */ 	move	$a1,$t6
/*     1900:	ad4f0000 */ 	sw	$t7,0x0($t2)
/*     1904:	acef0000 */ 	sw	$t7,0x0($a3)
/*     1908:	19c0000b */ 	blez	$t6,.L00001938
/*     190c:	00004825 */ 	move	$t1,$zero
/*     1910:	3083000f */ 	andi	$v1,$a0,0xf
/*     1914:	2419000f */ 	li	$t9,0xf
/*     1918:	03235823 */ 	subu	$t3,$t9,$v1
/*     191c:	000b6100 */ 	sll	$t4,$t3,0x4
/*     1920:	01833025 */ 	or	$a2,$t4,$v1
/*     1924:	01e01025 */ 	move	$v0,$t7
.L00001928:
/*     1928:	25290001 */ 	addiu	$t1,$t1,0x1
/*     192c:	24420001 */ 	addiu	$v0,$v0,0x1
/*     1930:	1525fffd */ 	bne	$t1,$a1,.L00001928
/*     1934:	a046ffff */ 	sb	$a2,-0x1($v0)
.L00001938:
/*     1938:	3c04dead */ 	lui	$a0,0xdead
/*     193c:	8ce20000 */ 	lw	$v0,0x0($a3)
/*     1940:	3484babe */ 	ori	$a0,$a0,0xbabe
/*     1944:	00001825 */ 	move	$v1,$zero
/*     1948:	24060008 */ 	li	$a2,0x8
.L0000194c:
/*     194c:	24630001 */ 	addiu	$v1,$v1,0x1
/*     1950:	ac440000 */ 	sw	$a0,0x0($v0)
/*     1954:	1466fffd */ 	bne	$v1,$a2,.L0000194c
/*     1958:	24420004 */ 	addiu	$v0,$v0,0x4
/*     195c:	8d4d0000 */ 	lw	$t5,0x0($t2)
/*     1960:	01a51021 */ 	addu	$v0,$t5,$a1
/*     1964:	03e00008 */ 	jr	$ra
/*     1968:	2442fff8 */ 	addiu	$v0,$v0,-8
);
#endif

#if VERSION == VERSION_NTSC_BETA
s32 osGetMemSize(void)
{
	return g_OsMemSize;
}
#endif

void idleproc(void *data)
{
	while (true);
}

void idleCreateThread(void)
{
	osCreateThread(&g_IdleThread, THREAD_IDLE, idleproc, NULL, allocateStack(THREAD_IDLE, 64), 0);
	osStartThread(&g_IdleThread);
}

void rmonCreateThread(void)
{
	osCreateThread(&g_RmonThread, 0, rmonproc, NULL, allocateStack(0, 0x300), 250);
	osStartThread(&g_RmonThread);
}

GLOBAL_ASM(
glabel func000019f4
/*     19f4:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*     19f8:	afbf0014 */ 	sw	$ra,0x14($sp)
/*     19fc:	3c048009 */ 	lui	$a0,%hi(var8008db30)
/*     1a00:	3c058009 */ 	lui	$a1,%hi(var8008db48)
/*     1a04:	24a5db48 */ 	addiu	$a1,$a1,%lo(var8008db48)
/*     1a08:	2484db30 */ 	addiu	$a0,$a0,%lo(var8008db30)
/*     1a0c:	0c0120d0 */ 	jal	osCreateMesgQueue
/*     1a10:	24060020 */ 	addiu	$a2,$zero,0x20
/*     1a14:	3c0e8000 */ 	lui	$t6,0x8000
/*     1a18:	8dce0300 */ 	lw	$t6,0x300($t6)
/*     1a1c:	24010002 */ 	addiu	$at,$zero,0x2
/*     1a20:	3c048009 */ 	lui	$a0,%hi(var8008dbd0)
/*     1a24:	15c1000a */ 	bne	$t6,$at,.L00001a50
/*     1a28:	2484dbd0 */ 	addiu	$a0,$a0,%lo(var8008dbd0)
/*     1a2c:	3c048009 */ 	lui	$a0,%hi(var8008dbd0)
/*     1a30:	3c058009 */ 	lui	$a1,%hi(var8008d900)
/*     1a34:	24a5d900 */ 	addiu	$a1,$a1,%lo(var8008d900)
/*     1a38:	2484dbd0 */ 	addiu	$a0,$a0,%lo(var8008dbd0)
/*     1a3c:	2406001e */ 	addiu	$a2,$zero,0x1e
/*     1a40:	0c000713 */ 	jal	osCreateScheduler
/*     1a44:	24070001 */ 	addiu	$a3,$zero,0x1
/*     1a48:	10000006 */ 	b	.L00001a64
/*     1a4c:	00000000 */ 	nop
.L00001a50:
/*     1a50:	3c058009 */ 	lui	$a1,%hi(var8008d900)
/*     1a54:	24a5d900 */ 	addiu	$a1,$a1,%lo(var8008d900)
/*     1a58:	24060002 */ 	addiu	$a2,$zero,0x2
/*     1a5c:	0c000713 */ 	jal	osCreateScheduler
/*     1a60:	24070001 */ 	addiu	$a3,$zero,0x1
.L00001a64:
/*     1a64:	3c048009 */ 	lui	$a0,%hi(var8008dbd0)
/*     1a68:	3c058009 */ 	lui	$a1,%hi(var8008dca8)
/*     1a6c:	3c068009 */ 	lui	$a2,%hi(var8008db30)
/*     1a70:	24c6db30 */ 	addiu	$a2,$a2,%lo(var8008db30)
/*     1a74:	24a5dca8 */ 	addiu	$a1,$a1,%lo(var8008dca8)
/*     1a78:	2484dbd0 */ 	addiu	$a0,$a0,%lo(var8008dbd0)
/*     1a7c:	0c00078c */ 	jal	osScAddClient
/*     1a80:	00003825 */ 	or	$a3,$zero,$zero
/*     1a84:	3c048009 */ 	lui	$a0,%hi(var8008dbd0)
/*     1a88:	0c0007a3 */ 	jal	osScGetCmdQ
/*     1a8c:	2484dbd0 */ 	addiu	$a0,$a0,%lo(var8008dbd0)
/*     1a90:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*     1a94:	3c018009 */ 	lui	$at,%hi(var8008dbc8)
/*     1a98:	ac22dbc8 */ 	sw	$v0,%lo(var8008dbc8)($at)
/*     1a9c:	03e00008 */ 	jr	$ra
/*     1aa0:	27bd0018 */ 	addiu	$sp,$sp,0x18
);

void mainproc(u32 value)
{
	idleCreateThread();
	func00013750();
	func00013710();
	rmonCreateThread();

	if (func00012f30()) {
		osStopThread(0);
	}

	osSetThreadPri(0, 10);
	func000019f4();
	mainEntry();
}

#if VERSION == VERSION_NTSC_BETA
GLOBAL_ASM(
glabel func00001b70nb
/*     1b70:	3c048006 */ 	lui	$a0,0x8006
/*     1b74:	3c068006 */ 	lui	$a2,0x8006
/*     1b78:	24c6e5ac */ 	addiu	$a2,$a2,-6740
/*     1b7c:	2484e590 */ 	addiu	$a0,$a0,-6768
/*     1b80:	00001025 */ 	move	$v0,$zero
/*     1b84:	240a0007 */ 	li	$t2,0x7
/*     1b88:	2409000f */ 	li	$t1,0xf
.L00001b8c:
/*     1b8c:	8c830000 */ 	lw	$v1,0x0($a0)
/*     1b90:	24840004 */ 	addiu	$a0,$a0,0x4
/*     1b94:	8cc50000 */ 	lw	$a1,0x0($a2)
/*     1b98:	10600011 */ 	beqz	$v1,.L00001be0
/*     1b9c:	3047000f */ 	andi	$a3,$v0,0xf
/*     1ba0:	90780020 */ 	lbu	$t8,0x20($v1)
/*     1ba4:	01277023 */ 	subu	$t6,$t1,$a3
/*     1ba8:	000e7900 */ 	sll	$t7,$t6,0x4
/*     1bac:	01e74025 */ 	or	$t0,$t7,$a3
/*     1bb0:	1518000b */ 	bne	$t0,$t8,.L00001be0
/*     1bb4:	24630020 */ 	addiu	$v1,$v1,0x20
/*     1bb8:	0065082b */ 	sltu	$at,$v1,$a1
/*     1bbc:	50200009 */ 	beqzl	$at,.L00001be4
/*     1bc0:	24420001 */ 	addiu	$v0,$v0,0x1
/*     1bc4:	90790001 */ 	lbu	$t9,0x1($v1)
.L00001bc8:
/*     1bc8:	24630001 */ 	addiu	$v1,$v1,0x1
/*     1bcc:	0065082b */ 	sltu	$at,$v1,$a1
/*     1bd0:	55190004 */ 	bnel	$t0,$t9,.L00001be4
/*     1bd4:	24420001 */ 	addiu	$v0,$v0,0x1
/*     1bd8:	5420fffb */ 	bnezl	$at,.L00001bc8
/*     1bdc:	90790001 */ 	lbu	$t9,0x1($v1)
.L00001be0:
/*     1be0:	24420001 */ 	addiu	$v0,$v0,0x1
.L00001be4:
/*     1be4:	144affe9 */ 	bne	$v0,$t2,.L00001b8c
/*     1be8:	24c60004 */ 	addiu	$a2,$a2,0x4
/*     1bec:	03e00008 */ 	jr	$ra
/*     1bf0:	00000000 */ 	nop
);
#endif

#if VERSION == VERSION_NTSC_BETA
GLOBAL_ASM(
glabel func00001bf4nb
/*     1bf4:	27bdff28 */ 	addiu	$sp,$sp,-216
/*     1bf8:	afbe0038 */ 	sw	$s8,0x38($sp)
/*     1bfc:	afb5002c */ 	sw	$s5,0x2c($sp)
/*     1c00:	afb40028 */ 	sw	$s4,0x28($sp)
/*     1c04:	afb70034 */ 	sw	$s7,0x34($sp)
/*     1c08:	afb60030 */ 	sw	$s6,0x30($sp)
/*     1c0c:	afb30024 */ 	sw	$s3,0x24($sp)
/*     1c10:	afb20020 */ 	sw	$s2,0x20($sp)
/*     1c14:	3c14dead */ 	lui	$s4,0xdead
/*     1c18:	3c157005 */ 	lui	$s5,0x7005
/*     1c1c:	3c1e8006 */ 	lui	$s8,0x8006
/*     1c20:	afbf003c */ 	sw	$ra,0x3c($sp)
/*     1c24:	afb1001c */ 	sw	$s1,0x1c($sp)
/*     1c28:	afb00018 */ 	sw	$s0,0x18($sp)
/*     1c2c:	27dee590 */ 	addiu	$s8,$s8,-6768
/*     1c30:	26b539b0 */ 	addiu	$s5,$s5,0x39b0
/*     1c34:	3694babe */ 	ori	$s4,$s4,0xbabe
/*     1c38:	27b2004c */ 	addiu	$s2,$sp,0x4c
/*     1c3c:	00009825 */ 	move	$s3,$zero
/*     1c40:	24160045 */ 	li	$s6,0x45
/*     1c44:	24170008 */ 	li	$s7,0x8
.L00001c48:
/*     1c48:	8fc20000 */ 	lw	$v0,0x0($s8)
/*     1c4c:	00008825 */ 	move	$s1,$zero
/*     1c50:	10400010 */ 	beqz	$v0,.L00001c94
/*     1c54:	00408025 */ 	move	$s0,$v0
.L00001c58:
/*     1c58:	8e0e0000 */ 	lw	$t6,0x0($s0)
/*     1c5c:	528e000b */ 	beql	$s4,$t6,.L00001c8c
/*     1c60:	26310001 */ 	addiu	$s1,$s1,0x1
/*     1c64:	0c0006dc */ 	jal	0x1b70
/*     1c68:	00000000 */ 	nop
/*     1c6c:	02402025 */ 	move	$a0,$s2
/*     1c70:	02a02825 */ 	move	$a1,$s5
/*     1c74:	0c004fc1 */ 	jal	0x13f04
/*     1c78:	02603025 */ 	move	$a2,$s3
/*     1c7c:	0c003074 */ 	jal	0xc1d0
/*     1c80:	02402025 */ 	move	$a0,$s2
/*     1c84:	a0160000 */ 	sb	$s6,0x0($zero)
/*     1c88:	26310001 */ 	addiu	$s1,$s1,0x1
.L00001c8c:
/*     1c8c:	1637fff2 */ 	bne	$s1,$s7,.L00001c58
/*     1c90:	26100004 */ 	addiu	$s0,$s0,0x4
.L00001c94:
/*     1c94:	26730001 */ 	addiu	$s3,$s3,0x1
/*     1c98:	24010007 */ 	li	$at,0x7
/*     1c9c:	1661ffea */ 	bne	$s3,$at,.L00001c48
/*     1ca0:	27de0004 */ 	addiu	$s8,$s8,0x4
/*     1ca4:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*     1ca8:	8fb00018 */ 	lw	$s0,0x18($sp)
/*     1cac:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*     1cb0:	8fb20020 */ 	lw	$s2,0x20($sp)
/*     1cb4:	8fb30024 */ 	lw	$s3,0x24($sp)
/*     1cb8:	8fb40028 */ 	lw	$s4,0x28($sp)
/*     1cbc:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*     1cc0:	8fb60030 */ 	lw	$s6,0x30($sp)
/*     1cc4:	8fb70034 */ 	lw	$s7,0x34($sp)
/*     1cc8:	8fbe0038 */ 	lw	$s8,0x38($sp)
/*     1ccc:	03e00008 */ 	jr	$ra
/*     1cd0:	27bd00d8 */ 	addiu	$sp,$sp,0xd8
/*     1cd4:	00000000 */ 	nop
/*     1cd8:	00000000 */ 	nop
/*     1cdc:	00000000 */ 	nop
);
#endif

void func00001b10(u32 value)
{
	var8005ce64 = value;
}

void func00001b1c(u32 value)
{
	var8005ce60 = value;
}

void func00001b28(u32 value)
{
	var8005ce68 = value;
}

void func00001b34(u32 value)
{
	var8005ce6c = value;
}

void func00001b40(u32 arg0)
{
	if ((var8005ce68 && var8005ce64) || var8005ce60) {
		func0000cf54(arg0);
		var8005ce70 = osGetCount();
	}
}

void func00001b98(u32 value)
{
	if ((value & 0xf) == 0 && ((var8005ce68 && var8005ce64) || var8005ce60)) {
		if (osGetCount() - var8005ce70 > var8005ce6c) {
			func0000cf54(var8009cac0);
			func0000cf54(var8009cac4);
		}
	}
}

void osCreateLog(void)
{
	var8005ce70 = osGetCount();
}

void osCreateScheduler(OSSched *sc, void *stack, u8 mode, u32 numFields)
{
	sc->curRSPTask = 0;
	sc->curRDPTask = 0;
	sc->clientList = 0;
	sc->frameCount = 0;
	sc->audioListHead = 0;
	sc->gfxListHead = 0;
	sc->audioListTail = 0;
	sc->gfxListTail = 0;
	sc->retraceMsg.type = OS_SC_RETRACE_MSG;
	sc->prenmiMsg.type = 5; // OS_SC_PRE_NMI_MSG
	sc->thread = stack;

	resetThreadCreate();

	osCreateMesgQueue(&sc->interruptQ, sc->intBuf, OS_SC_MAX_MESGS);
	osCreateMesgQueue(&sc->cmdQ, sc->cmdMsgBuf, OS_SC_MAX_MESGS);

	osCreateViManager(OS_PRIORITY_VIMGR);

	var8008de08 = osViModeTable[mode].comRegs.hStart;
	var8008de0c = osViModeTable[mode].fldRegs[0].vStart;
	var8008de10 = osViModeTable[mode].fldRegs[1].vStart;

	var8008dd60[0] = &var8008dd68;
	var8008dd60[1] = &var8008ddb8;

	var8008dd68 = osViModeTable[mode];
	var8008ddb8 = osViModeTable[mode];

	osSetEventMesg(OS_EVENT_SP, &sc->interruptQ, (OSMesg)RSP_DONE_MSG);
	osSetEventMesg(OS_EVENT_DP, &sc->interruptQ, (OSMesg)RDP_DONE_MSG);

	osViSetEvent(&sc->interruptQ, (OSMesg)VIDEO_MSG, numFields);
	osCreateLog();
	osCreateThread(sc->thread, THREAD_SCHED, &__scMain, sc, allocateStack(THREAD_SCHED, 0x400), 30);
	osStartThread(sc->thread);
}

void osScAddClient(OSSched *sc, OSScClient *c, OSMesgQueue *msgQ, OSScClient *next)
{
	OSIntMask mask;

	mask = osSetIntMask(1);

	c->msgQ = msgQ;
	c[1].next = next;
	c->next = sc->clientList;
	sc->clientList = c;

	osSetIntMask(mask);
}

#if VERSION == VERSION_NTSC_BETA
GLOBAL_ASM(
glabel osScRemoveClient
/*     205c:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*     2060:	afbf0014 */ 	sw	$ra,0x14($sp)
/*     2064:	afa40020 */ 	sw	$a0,0x20($sp)
/*     2068:	8c8300b4 */ 	lw	$v1,0xb4($a0)
/*     206c:	afa00018 */ 	sw	$zero,0x18($sp)
/*     2070:	afa50024 */ 	sw	$a1,0x24($sp)
/*     2074:	24040001 */ 	li	$a0,0x1
/*     2078:	0c012688 */ 	jal	osSetIntMask
/*     207c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*     2080:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*     2084:	8fa50024 */ 	lw	$a1,0x24($sp)
/*     2088:	8fa60018 */ 	lw	$a2,0x18($sp)
/*     208c:	1060000f */ 	beqz	$v1,.L000020cc
/*     2090:	00402025 */ 	move	$a0,$v0
.L00002094:
/*     2094:	5465000a */ 	bnel	$v1,$a1,.L000020c0
/*     2098:	00603025 */ 	move	$a2,$v1
/*     209c:	10c00004 */ 	beqz	$a2,.L000020b0
/*     20a0:	8fb90020 */ 	lw	$t9,0x20($sp)
/*     20a4:	8caf0000 */ 	lw	$t7,0x0($a1)
/*     20a8:	10000008 */ 	b	.L000020cc
/*     20ac:	accf0000 */ 	sw	$t7,0x0($a2)
.L000020b0:
/*     20b0:	8cb80000 */ 	lw	$t8,0x0($a1)
/*     20b4:	10000005 */ 	b	.L000020cc
/*     20b8:	af3800b4 */ 	sw	$t8,0xb4($t9)
/*     20bc:	00603025 */ 	move	$a2,$v1
.L000020c0:
/*     20c0:	8c630000 */ 	lw	$v1,0x0($v1)
/*     20c4:	1460fff3 */ 	bnez	$v1,.L00002094
/*     20c8:	00000000 */ 	nop
.L000020cc:
/*     20cc:	0c012688 */ 	jal	osSetIntMask
/*     20d0:	00000000 */ 	nop
/*     20d4:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*     20d8:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*     20dc:	03e00008 */ 	jr	$ra
/*     20e0:	00000000 */ 	nop
);
#endif

OSMesgQueue *osScGetCmdQ(OSSched *sc)
{
	return &sc->cmdQ;
}

void __scMain(void *arg)
{
    OSMesg msg = 0;
    OSSched *sc = (OSSched *)arg;
    int done = 0;

	func00002400();

	while (!done) {
		osRecvMesg(&sc->interruptQ, (OSMesg *)&msg, OS_MESG_BLOCK);

		switch ((int) msg) {
		case VIDEO_MSG:
			if (osViGetCurrentFramebuffer() == osViGetNextFramebuffer()) {
				osDpSetStatus(4);
			}

			func00002078(sc);
			__scHandleRetrace(sc);
			break;
		case RSP_DONE_MSG:
			__scHandleRSP(sc);
			break;
		case RDP_DONE_MSG:
			osDpSetStatus(8);
			__scHandleRDP(sc);
			__scHandleRetrace(sc);
			break;
		}
	}
}

GLOBAL_ASM(
glabel func00001fa8
/*     1fa8:	27bdffd0 */ 	addiu	$sp,$sp,-48
/*     1fac:	afb00018 */ 	sw	$s0,0x18($sp)
/*     1fb0:	00808025 */ 	or	$s0,$a0,$zero
/*     1fb4:	afbf001c */ 	sw	$ra,0x1c($sp)
/*     1fb8:	afa50034 */ 	sw	$a1,0x34($sp)
/*     1fbc:	afa00028 */ 	sw	$zero,0x28($sp)
/*     1fc0:	afa00024 */ 	sw	$zero,0x24($sp)
/*     1fc4:	0c012230 */ 	jal	osGetThreadPri
/*     1fc8:	00002025 */ 	or	$a0,$zero,$zero
/*     1fcc:	afa20020 */ 	sw	$v0,0x20($sp)
/*     1fd0:	00002025 */ 	or	$a0,$zero,$zero
/*     1fd4:	0c01210c */ 	jal	osSetThreadPri
/*     1fd8:	2405001f */ 	addiu	$a1,$zero,0x1f
/*     1fdc:	02002025 */ 	or	$a0,$s0,$zero
/*     1fe0:	0c000a87 */ 	jal	__scAppendList
/*     1fe4:	8fa50034 */ 	lw	$a1,0x34($sp)
/*     1fe8:	8e0e00d4 */ 	lw	$t6,0xd4($s0)
/*     1fec:	02002025 */ 	or	$a0,$s0,$zero
/*     1ff0:	27a50028 */ 	addiu	$a1,$sp,0x28
/*     1ff4:	51c00009 */ 	beqzl	$t6,.L0000201c
/*     1ff8:	8e1800c8 */ 	lw	$t8,0xc8($s0)
/*     1ffc:	8e0f00c8 */ 	lw	$t7,0xc8($s0)
/*     2000:	51e00006 */ 	beqzl	$t7,.L0000201c
/*     2004:	8e1800c8 */ 	lw	$t8,0xc8($s0)
/*     2008:	0c000adc */ 	jal	__scYield
/*     200c:	02002025 */ 	or	$a0,$s0,$zero
/*     2010:	10000012 */ 	b	.L0000205c
/*     2014:	00002025 */ 	or	$a0,$zero,$zero
/*     2018:	8e1800c8 */ 	lw	$t8,0xc8($s0)
.L0000201c:
/*     201c:	8e0900cc */ 	lw	$t1,0xcc($s0)
/*     2020:	27a60024 */ 	addiu	$a2,$sp,0x24
/*     2024:	2f190001 */ 	sltiu	$t9,$t8,0x1
/*     2028:	00194040 */ 	sll	$t0,$t9,0x1
/*     202c:	2d2a0001 */ 	sltiu	$t2,$t1,0x1
/*     2030:	010a3825 */ 	or	$a3,$t0,$t2
/*     2034:	0c000aeb */ 	jal	__scSchedule
/*     2038:	afa7002c */ 	sw	$a3,0x2c($sp)
/*     203c:	8fa7002c */ 	lw	$a3,0x2c($sp)
/*     2040:	02002025 */ 	or	$a0,$s0,$zero
/*     2044:	8fa50028 */ 	lw	$a1,0x28($sp)
/*     2048:	50470004 */ 	beql	$v0,$a3,.L0000205c
/*     204c:	00002025 */ 	or	$a0,$zero,$zero
/*     2050:	0c000aa1 */ 	jal	__scExec
/*     2054:	8fa60024 */ 	lw	$a2,0x24($sp)
/*     2058:	00002025 */ 	or	$a0,$zero,$zero
.L0000205c:
/*     205c:	0c01210c */ 	jal	osSetThreadPri
/*     2060:	8fa50020 */ 	lw	$a1,0x20($sp)
/*     2064:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*     2068:	8fb00018 */ 	lw	$s0,0x18($sp)
/*     206c:	27bd0030 */ 	addiu	$sp,$sp,0x30
/*     2070:	03e00008 */ 	jr	$ra
/*     2074:	00000000 */ 	nop
);

GLOBAL_ASM(
glabel func00002078
/*     2078:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*     207c:	afbf0024 */ 	sw	$ra,0x24($sp)
/*     2080:	afa40028 */ 	sw	$a0,0x28($sp)
/*     2084:	8c8f00d0 */ 	lw	$t7,0xd0($a0)
/*     2088:	3c038006 */ 	lui	$v1,%hi(var8005ced0)
/*     208c:	25f80001 */ 	addiu	$t8,$t7,0x1
/*     2090:	ac9800d0 */ 	sw	$t8,0xd0($a0)
/*     2094:	8063ced0 */ 	lb	$v1,%lo(var8005ced0)($v1)
/*     2098:	33080001 */ 	andi	$t0,$t8,0x1
/*     209c:	1460001b */ 	bnez	$v1,.L0000210c
/*     20a0:	00000000 */ 	nop
/*     20a4:	15000004 */ 	bnez	$t0,.L000020b8
/*     20a8:	3c098009 */ 	lui	$t1,%hi(g_Is4Mb)
/*     20ac:	91290af0 */ 	lbu	$t1,%lo(g_Is4Mb)($t1)
/*     20b0:	24010001 */ 	addiu	$at,$zero,0x1
/*     20b4:	15210015 */ 	bne	$t1,$at,.L0000210c
.L000020b8:
/*     20b8:	3c048009 */ 	lui	$a0,%hi(var8008de18)
/*     20bc:	0c01228c */ 	jal	osStopTimer
/*     20c0:	2484de18 */ 	addiu	$a0,$a0,%lo(var8008de18)
/*     20c4:	0c002446 */ 	jal	func00009118
/*     20c8:	00000000 */ 	nop
/*     20cc:	3c0c8006 */ 	lui	$t4,%hi(var8005cea8)
/*     20d0:	258ccea8 */ 	addiu	$t4,$t4,%lo(var8005cea8)
/*     20d4:	3c048009 */ 	lui	$a0,%hi(var8008de18)
/*     20d8:	3c070004 */ 	lui	$a3,0x4
/*     20dc:	240a0000 */ 	addiu	$t2,$zero,0x0
/*     20e0:	240b0000 */ 	addiu	$t3,$zero,0x0
/*     20e4:	afab0014 */ 	sw	$t3,0x14($sp)
/*     20e8:	afaa0010 */ 	sw	$t2,0x10($sp)
/*     20ec:	34e745c0 */ 	ori	$a3,$a3,0x45c0
/*     20f0:	2484de18 */ 	addiu	$a0,$a0,%lo(var8008de18)
/*     20f4:	afac001c */ 	sw	$t4,0x1c($sp)
/*     20f8:	24060000 */ 	addiu	$a2,$zero,0x0
/*     20fc:	0c0122c8 */ 	jal	osSetTimer
/*     2100:	afa20018 */ 	sw	$v0,0x18($sp)
/*     2104:	3c038006 */ 	lui	$v1,%hi(var8005ced0)
/*     2108:	8063ced0 */ 	lb	$v1,%lo(var8005ced0)($v1)
.L0000210c:
/*     210c:	14600003 */ 	bnez	$v1,.L0000211c
/*     2110:	00000000 */ 	nop
/*     2114:	0c0027b5 */ 	jal	func00009ed4
/*     2118:	00000000 */ 	nop
.L0000211c:
/*     211c:	0c005121 */ 	jal	contPoll
/*     2120:	00000000 */ 	nop
/*     2124:	0c003f86 */ 	jal	func0000fe18
/*     2128:	00000000 */ 	nop
/*     212c:	8fad0028 */ 	lw	$t5,0x28($sp)
/*     2130:	0c0006e6 */ 	jal	func00001b98
/*     2134:	8da400d0 */ 	lw	$a0,0xd0($t5)
/*     2138:	8fbf0024 */ 	lw	$ra,0x24($sp)
/*     213c:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*     2140:	03e00008 */ 	jr	$ra
/*     2144:	00000000 */ 	nop
);

#if VERSION >= VERSION_NTSC_1_0
void __scHandleRetrace(OSSched *sc)
{
	s32         state;
	OSScTask    *rspTask = 0;
	OSScClient  *client;
	OSScTask    *sp = 0;
	OSScTask    *dp = 0;

	func00009a88();

	while (osRecvMesg(&sc->cmdQ, (OSMesg*)&rspTask, OS_MESG_NOBLOCK) != -1) {
		__scAppendList(sc, rspTask);
	}

	if (sc->doAudio && sc->curRSPTask) {
		__scYield(sc);
	} else {
		state = ((sc->curRSPTask == 0) << 1) | (sc->curRDPTask == 0);

		if ( __scSchedule (sc, &sp, &dp, state) != state) {
			__scExec(sc, sp, dp);
		}
	}

	for (client = sc->clientList; client != 0; client = client->next) {
		if ((*((s32*)client + 2) == 0) || ((sc->frameCount & 1) == 0)) {
			osSendMesg(client->msgQ, (OSMesg) &sc->retraceMsg, OS_MESG_NOBLOCK);
		}
	}

#if PIRACYCHECKS
	{
		u32 checksum = 0;
		s32 *end = (s32 *)&allocateStack;
		s32 *ptr = (s32 *)&func000016cc;
		s32 i;

		while (ptr < end) {
			checksum ^= *ptr;
			ptr++;
		}

		if (checksum != CHECKSUM_PLACEHOLDER) {
			u8 *addr = var80095210;

			for (i = 0; i < 40; i++) {
				addr[4 + i] = 0xff;
			}
		}
	}
#endif
}
#endif

void __scHandleRSP(OSSched *sc)
{
	OSScTask *t, *sp = 0, *dp = 0;
	s32 state;

	if (!var8005ced0) {
		t = sc->curRSPTask;
		sc->curRSPTask = 0;

		func00009aa0(0x10001);

		if ((t->state & OS_SC_YIELD) && osSpTaskYielded(&t->list)) {
			t->state |= OS_SC_YIELDED;

			if ((t->flags & OS_SC_TYPE_MASK) == OS_SC_XBUS) {
				t->next = sc->gfxListHead;
				sc->gfxListHead = t;

				if (sc->gfxListTail == 0) {
					sc->gfxListTail = t;
				}
			}
		} else {
			t->state &= ~OS_SC_NEEDS_RSP;
			__scTaskComplete(sc, t);
		}

		state = ((sc->curRSPTask == 0) << 1) | (sc->curRDPTask == 0);

		if (__scSchedule(sc, &sp, &dp, state) != state) {
			__scExec(sc, sp, dp);
		}
	}
}

u32 *func000023f4(void)
{
	return &var8008de38;
}

GLOBAL_ASM(
glabel func00002400
/*     2400:	3c038009 */ 	lui	$v1,%hi(var8008de48)
/*     2404:	3c058009 */ 	lui	$a1,%hi(var8008fa68)
/*     2408:	3c078009 */ 	lui	$a3,%hi(var8008fa68+0x3)
/*     240c:	24e7fa6b */ 	addiu	$a3,$a3,%lo(var8008fa68+0x3)
/*     2410:	24a5fa68 */ 	addiu	$a1,$a1,%lo(var8008fa68)
/*     2414:	2463de48 */ 	addiu	$v1,$v1,%lo(var8008de48)
/*     2418:	24060078 */ 	addiu	$a2,$zero,0x78
.L0000241c:
/*     241c:	00001025 */ 	or	$v0,$zero,$zero
/*     2420:	00602025 */ 	or	$a0,$v1,$zero
.L00002424:
/*     2424:	24420001 */ 	addiu	$v0,$v0,0x1
/*     2428:	24840014 */ 	addiu	$a0,$a0,0x14
/*     242c:	1446fffd */ 	bne	$v0,$a2,.L00002424
/*     2430:	a480ffec */ 	sh	$zero,-0x14($a0)
/*     2434:	24a50001 */ 	addiu	$a1,$a1,0x1
/*     2438:	24630960 */ 	addiu	$v1,$v1,0x960
/*     243c:	14a7fff7 */ 	bne	$a1,$a3,.L0000241c
/*     2440:	a0a0ffff */ 	sb	$zero,-0x1($a1)
/*     2444:	03e00008 */ 	jr	$ra
/*     2448:	00000000 */ 	nop
);

struct bootbufferthing *func0000244c(void)
{
	return &var8008de48[var8008fa6c];
}

struct bootbufferthing *func00002480(void)
{
	return &var8008de48[var8008fa70];
}

struct bootbufferthing *func000024b4(void)
{
	return &var8008de48[var8008fa74];
}

void func000024e8(void)
{
	var8008fa6c = (var8008fa6c + 1) % 3;
}

void func00002510(void)
{
	var8008fa70 = (var8008fa70 + 1) % 3;
}

void func00002538(void)
{
	var8008fa74 = (var8008fa74 + 1) % 3;
}

void func00002560(void)
{
	var8008fa6c = 0;
	var8008fa70 = 1;
	var8008fa74 = 0;
}

GLOBAL_ASM(
glabel func00002580
/*     2580:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*     2584:	afbf0014 */ 	sw	$ra,0x14($sp)
/*     2588:	0c00092d */ 	jal	func000024b4
/*     258c:	00000000 */ 	nop
/*     2590:	3c0b8009 */ 	lui	$t3,%hi(var8008fa74)
/*     2594:	3c0a8009 */ 	lui	$t2,%hi(var8008fa68)
/*     2598:	254afa68 */ 	addiu	$t2,$t2,%lo(var8008fa68)
/*     259c:	256bfa74 */ 	addiu	$t3,$t3,%lo(var8008fa74)
/*     25a0:	00003825 */ 	or	$a3,$zero,$zero
/*     25a4:	00401825 */ 	or	$v1,$v0,$zero
/*     25a8:	24090960 */ 	addiu	$t1,$zero,0x960
/*     25ac:	24080001 */ 	addiu	$t0,$zero,0x1
.L000025b0:
/*     25b0:	946e0000 */ 	lhu	$t6,0x0($v1)
/*     25b4:	24e70014 */ 	addiu	$a3,$a3,0x14
/*     25b8:	11c00012 */ 	beqz	$t6,.L00002604
/*     25bc:	00000000 */ 	nop
/*     25c0:	8d6f0000 */ 	lw	$t7,0x0($t3)
/*     25c4:	8c640008 */ 	lw	$a0,0x8($v1)
/*     25c8:	014fc021 */ 	addu	$t8,$t2,$t7
/*     25cc:	93190000 */ 	lbu	$t9,0x0($t8)
/*     25d0:	94850000 */ 	lhu	$a1,0x0($a0)
/*     25d4:	5519000b */ 	bnel	$t0,$t9,.L00002604
/*     25d8:	a4650002 */ 	sh	$a1,0x2($v1)
/*     25dc:	8c64000c */ 	lw	$a0,0xc($v1)
/*     25e0:	94860000 */ 	lhu	$a2,0x0($a0)
/*     25e4:	00a6082a */ 	slt	$at,$a1,$a2
/*     25e8:	10200003 */ 	beqz	$at,.L000025f8
/*     25ec:	00000000 */ 	nop
/*     25f0:	10000004 */ 	b	.L00002604
/*     25f4:	a4650002 */ 	sh	$a1,0x2($v1)
.L000025f8:
/*     25f8:	10000002 */ 	b	.L00002604
/*     25fc:	a4660002 */ 	sh	$a2,0x2($v1)
/*     2600:	a4650002 */ 	sh	$a1,0x2($v1)
.L00002604:
/*     2604:	14e9ffea */ 	bne	$a3,$t1,.L000025b0
/*     2608:	24630014 */ 	addiu	$v1,$v1,0x14
/*     260c:	8d6c0000 */ 	lw	$t4,0x0($t3)
/*     2610:	014c6821 */ 	addu	$t5,$t2,$t4
/*     2614:	0c00094e */ 	jal	func00002538
/*     2618:	a1a00000 */ 	sb	$zero,0x0($t5)
/*     261c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*     2620:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*     2624:	03e00008 */ 	jr	$ra
/*     2628:	00000000 */ 	nop
);

void __scHandleRDP(OSSched *sc)
{
	OSScTask *t, *sp = NULL, *dp = NULL;
	s32 state;

	if (sc->curRDPTask != NULL) {
		func00002580();

		if (var8005dd18 == 0) {
			func00002d90();
		}

		func00009aa0(0x10002);
		osDpGetCounters(&var8008de38);

		t = sc->curRDPTask;
		sc->curRDPTask = NULL;
		t->state &= ~OS_SC_NEEDS_RDP;

		__scTaskComplete(sc, t);

		state = ((sc->curRSPTask == 0) << 1) | (sc->curRDPTask == 0);

		if (__scSchedule(sc, &sp, &dp, state) != state) {
			__scExec(sc, sp, dp);
		}
	}
}

OSScTask *__scTaskReady(OSScTask *t)
{
	void *a;
	void *b;

	if (t) {
		if ((func00048cd0() & 2) == 0) {
			if (osViGetCurrentFramebuffer() != osViGetNextFramebuffer()) {
				return 0;
			}
		}

		return t;
	}

	return 0;
}

GLOBAL_ASM(
glabel __scTaskComplete
/*     2768:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*     276c:	afbf0014 */ 	sw	$ra,0x14($sp)
/*     2770:	afa40018 */ 	sw	$a0,0x18($sp)
/*     2774:	afa5001c */ 	sw	$a1,0x1c($sp)
/*     2778:	8caf0004 */ 	lw	$t7,0x4($a1)
/*     277c:	00001025 */ 	or	$v0,$zero,$zero
/*     2780:	31f80003 */ 	andi	$t8,$t7,0x3
/*     2784:	170000a1 */ 	bnez	$t8,.L00002a0c
/*     2788:	00000000 */ 	nop
/*     278c:	8cb90010 */ 	lw	$t9,0x10($a1)
/*     2790:	24010001 */ 	addiu	$at,$zero,0x1
/*     2794:	57210097 */ 	bnel	$t9,$at,.L000029f4
/*     2798:	8fab001c */ 	lw	$t3,0x1c($sp)
/*     279c:	8ca20008 */ 	lw	$v0,0x8($a1)
/*     27a0:	30480040 */ 	andi	$t0,$v0,0x40
/*     27a4:	11000092 */ 	beqz	$t0,.L000029f0
/*     27a8:	30490020 */ 	andi	$t1,$v0,0x20
/*     27ac:	11200090 */ 	beqz	$t1,.L000029f0
/*     27b0:	3c0a8006 */ 	lui	$t2,%hi(var8005cec8)
/*     27b4:	8d4acec8 */ 	lw	$t2,%lo(var8005cec8)($t2)
/*     27b8:	11400005 */ 	beqz	$t2,.L000027d0
/*     27bc:	00000000 */ 	nop
/*     27c0:	0c012338 */ 	jal	func00048ce0
/*     27c4:	00002025 */ 	or	$a0,$zero,$zero
/*     27c8:	3c018006 */ 	lui	$at,%hi(var8005cec8)
/*     27cc:	ac20cec8 */ 	sw	$zero,%lo(var8005cec8)($at)
.L000027d0:
/*     27d0:	3c078006 */ 	lui	$a3,%hi(var8005ce74)
/*     27d4:	24e7ce74 */ 	addiu	$a3,$a3,%lo(var8005ce74)
/*     27d8:	8ceb0000 */ 	lw	$t3,0x0($a3)
/*     27dc:	3c198006 */ 	lui	$t9,%hi(var8005ce8c)
/*     27e0:	2739ce8c */ 	addiu	$t9,$t9,%lo(var8005ce8c)
/*     27e4:	256c0001 */ 	addiu	$t4,$t3,0x1
/*     27e8:	05810004 */ 	bgez	$t4,.L000027fc
/*     27ec:	318d0001 */ 	andi	$t5,$t4,0x1
/*     27f0:	11a00002 */ 	beqz	$t5,.L000027fc
/*     27f4:	00000000 */ 	nop
/*     27f8:	25adfffe */ 	addiu	$t5,$t5,-2
.L000027fc:
/*     27fc:	000d7880 */ 	sll	$t7,$t5,0x2
/*     2800:	000fc023 */ 	negu	$t8,$t7
/*     2804:	03193021 */ 	addu	$a2,$t8,$t9
/*     2808:	8cce0000 */ 	lw	$t6,0x0($a2)
/*     280c:	24080001 */ 	addiu	$t0,$zero,0x1
/*     2810:	aced0000 */ 	sw	$t5,0x0($a3)
/*     2814:	11c00068 */ 	beqz	$t6,.L000029b8
/*     2818:	010d1823 */ 	subu	$v1,$t0,$t5
/*     281c:	00034880 */ 	sll	$t1,$v1,0x2
/*     2820:	3c028009 */ 	lui	$v0,%hi(var8008dd60)
/*     2824:	00491021 */ 	addu	$v0,$v0,$t1
/*     2828:	00035080 */ 	sll	$t2,$v1,0x2
/*     282c:	8c42dd60 */ 	lw	$v0,%lo(var8008dd60)($v0)
/*     2830:	01435021 */ 	addu	$t2,$t2,$v1
/*     2834:	3c0b8009 */ 	lui	$t3,%hi(var8008dcc0)
/*     2838:	256bdcc0 */ 	addiu	$t3,$t3,%lo(var8008dcc0)
/*     283c:	000a5100 */ 	sll	$t2,$t2,0x4
/*     2840:	014b2021 */ 	addu	$a0,$t2,$t3
/*     2844:	8c8d0008 */ 	lw	$t5,0x8($a0)
/*     2848:	8c4c0008 */ 	lw	$t4,0x8($v0)
/*     284c:	558d0015 */ 	bnel	$t4,$t5,.L000028a4
/*     2850:	3c040008 */ 	lui	$a0,0x8
/*     2854:	8c4f0020 */ 	lw	$t7,0x20($v0)
/*     2858:	8c980020 */ 	lw	$t8,0x20($a0)
/*     285c:	55f80011 */ 	bnel	$t7,$t8,.L000028a4
/*     2860:	3c040008 */ 	lui	$a0,0x8
/*     2864:	8c59002c */ 	lw	$t9,0x2c($v0)
/*     2868:	8c8e002c */ 	lw	$t6,0x2c($a0)
/*     286c:	572e000d */ 	bnel	$t9,$t6,.L000028a4
/*     2870:	3c040008 */ 	lui	$a0,0x8
/*     2874:	8c480040 */ 	lw	$t0,0x40($v0)
/*     2878:	8c890040 */ 	lw	$t1,0x40($a0)
/*     287c:	55090009 */ 	bnel	$t0,$t1,.L000028a4
/*     2880:	3c040008 */ 	lui	$a0,0x8
/*     2884:	8c4a0028 */ 	lw	$t2,0x28($v0)
/*     2888:	8c8b0028 */ 	lw	$t3,0x28($a0)
/*     288c:	554b0005 */ 	bnel	$t2,$t3,.L000028a4
/*     2890:	3c040008 */ 	lui	$a0,0x8
/*     2894:	8c4c003c */ 	lw	$t4,0x3c($v0)
/*     2898:	8c8d003c */ 	lw	$t5,0x3c($a0)
/*     289c:	118d0045 */ 	beq	$t4,$t5,.L000029b4
/*     28a0:	3c040008 */ 	lui	$a0,0x8
.L000028a4:
/*     28a4:	0c012194 */ 	jal	osSetIntMask
/*     28a8:	34840401 */ 	ori	$a0,$a0,0x401
/*     28ac:	3c0f8006 */ 	lui	$t7,%hi(var8005ce74)
/*     28b0:	8defce74 */ 	lw	$t7,%lo(var8005ce74)($t7)
/*     28b4:	24180001 */ 	addiu	$t8,$zero,0x1
/*     28b8:	3c098009 */ 	lui	$t1,%hi(var8008dcc0)
/*     28bc:	030f1823 */ 	subu	$v1,$t8,$t7
/*     28c0:	00034080 */ 	sll	$t0,$v1,0x2
/*     28c4:	01034021 */ 	addu	$t0,$t0,$v1
/*     28c8:	00084100 */ 	sll	$t0,$t0,0x4
/*     28cc:	0003c880 */ 	sll	$t9,$v1,0x2
/*     28d0:	2529dcc0 */ 	addiu	$t1,$t1,%lo(var8008dcc0)
/*     28d4:	3c0e8009 */ 	lui	$t6,%hi(var8008dd60)
/*     28d8:	01d97021 */ 	addu	$t6,$t6,$t9
/*     28dc:	01095021 */ 	addu	$t2,$t0,$t1
/*     28e0:	254c0048 */ 	addiu	$t4,$t2,0x48
/*     28e4:	8dcedd60 */ 	lw	$t6,%lo(var8008dd60)($t6)
.L000028e8:
/*     28e8:	8d410000 */ 	lw	$at,0x0($t2)
/*     28ec:	254a000c */ 	addiu	$t2,$t2,12
/*     28f0:	25ce000c */ 	addiu	$t6,$t6,12
/*     28f4:	adc1fff4 */ 	sw	$at,-0xc($t6)
/*     28f8:	8d41fff8 */ 	lw	$at,-0x8($t2)
/*     28fc:	adc1fff8 */ 	sw	$at,-0x8($t6)
/*     2900:	8d41fffc */ 	lw	$at,-0x4($t2)
/*     2904:	154cfff8 */ 	bne	$t2,$t4,.L000028e8
/*     2908:	adc1fffc */ 	sw	$at,-0x4($t6)
/*     290c:	8d410000 */ 	lw	$at,0x0($t2)
/*     2910:	00402025 */ 	or	$a0,$v0,$zero
/*     2914:	adc10000 */ 	sw	$at,0x0($t6)
/*     2918:	8d4c0004 */ 	lw	$t4,0x4($t2)
/*     291c:	0c012194 */ 	jal	osSetIntMask
/*     2920:	adcc0004 */ 	sw	$t4,0x4($t6)
/*     2924:	3c0d8006 */ 	lui	$t5,%hi(var8005ce74)
/*     2928:	8dadce74 */ 	lw	$t5,%lo(var8005ce74)($t5)
/*     292c:	3c048009 */ 	lui	$a0,%hi(var8008dd60+0x4)
/*     2930:	000dc080 */ 	sll	$t8,$t5,0x2
/*     2934:	00187823 */ 	negu	$t7,$t8
/*     2938:	008f2021 */ 	addu	$a0,$a0,$t7
/*     293c:	0c012354 */ 	jal	func00048d50
/*     2940:	8c84dd64 */ 	lw	$a0,%lo(var8008dd60+0x4)($a0)
/*     2944:	3c048006 */ 	lui	$a0,%hi(var8005ce90+0x3)
/*     2948:	0c012338 */ 	jal	func00048ce0
/*     294c:	9084ce93 */ 	lbu	$a0,%lo(var8005ce90+0x3)($a0)
/*     2950:	3c198006 */ 	lui	$t9,%hi(var8005ce74)
/*     2954:	8f39ce74 */ 	lw	$t9,%lo(var8005ce74)($t9)
/*     2958:	3c018006 */ 	lui	$at,%hi(var8005ce7c)
/*     295c:	00194080 */ 	sll	$t0,$t9,0x2
/*     2960:	00084823 */ 	negu	$t1,$t0
/*     2964:	00290821 */ 	addu	$at,$at,$t1
/*     2968:	0c012370 */ 	jal	func00048dc0
/*     296c:	c42cce7c */ 	lwc1	$f12,%lo(var8005ce7c)($at)
/*     2970:	3c0b8006 */ 	lui	$t3,%hi(var8005ce74)
/*     2974:	8d6bce74 */ 	lw	$t3,%lo(var8005ce74)($t3)
/*     2978:	3c018006 */ 	lui	$at,%hi(var8005ce84)
/*     297c:	000b6080 */ 	sll	$t4,$t3,0x2
/*     2980:	000c5023 */ 	negu	$t2,$t4
/*     2984:	002a0821 */ 	addu	$at,$at,$t2
/*     2988:	0c0123bc */ 	jal	func00048ef0
/*     298c:	c42cce84 */ 	lwc1	$f12,%lo(var8005ce84)($at)
/*     2990:	0c0123d4 */ 	jal	func00048f50
/*     2994:	24040042 */ 	addiu	$a0,$zero,0x42
/*     2998:	3c0e8006 */ 	lui	$t6,%hi(var8005ce74)
/*     299c:	8dcece74 */ 	lw	$t6,%lo(var8005ce74)($t6)
/*     29a0:	3c0f8006 */ 	lui	$t7,%hi(var8005ce8c)
/*     29a4:	25efce8c */ 	addiu	$t7,$t7,%lo(var8005ce8c)
/*     29a8:	000e6880 */ 	sll	$t5,$t6,0x2
/*     29ac:	000dc023 */ 	negu	$t8,$t5
/*     29b0:	030f3021 */ 	addu	$a2,$t8,$t7
.L000029b4:
/*     29b4:	acc00000 */ 	sw	$zero,0x0($a2)
.L000029b8:
/*     29b8:	3c028006 */ 	lui	$v0,%hi(var8005ce90)
/*     29bc:	8c42ce90 */ 	lw	$v0,%lo(var8005ce90)($v0)
/*     29c0:	10400005 */ 	beqz	$v0,.L000029d8
/*     29c4:	28410003 */ 	slti	$at,$v0,0x3
/*     29c8:	10200003 */ 	beqz	$at,.L000029d8
/*     29cc:	2459ffff */ 	addiu	$t9,$v0,-1
/*     29d0:	3c018006 */ 	lui	$at,%hi(var8005ce90)
/*     29d4:	ac39ce90 */ 	sw	$t9,%lo(var8005ce90)($at)
.L000029d8:
/*     29d8:	8fa8001c */ 	lw	$t0,0x1c($sp)
/*     29dc:	0c0006d0 */ 	jal	func00001b40
/*     29e0:	8d04000c */ 	lw	$a0,0xc($t0)
/*     29e4:	8fa9001c */ 	lw	$t1,0x1c($sp)
/*     29e8:	0c01242c */ 	jal	func000490b0
/*     29ec:	8d24000c */ 	lw	$a0,0xc($t1)
.L000029f0:
/*     29f0:	8fab001c */ 	lw	$t3,0x1c($sp)
.L000029f4:
/*     29f4:	24060001 */ 	addiu	$a2,$zero,0x1
/*     29f8:	8d640050 */ 	lw	$a0,0x50($t3)
/*     29fc:	0c012238 */ 	jal	osSendMesg
/*     2a00:	8d650054 */ 	lw	$a1,0x54($t3)
/*     2a04:	10000001 */ 	b	.L00002a0c
/*     2a08:	24020001 */ 	addiu	$v0,$zero,0x1
.L00002a0c:
/*     2a0c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*     2a10:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*     2a14:	03e00008 */ 	jr	$ra
/*     2a18:	00000000 */ 	nop
);

void __scAppendList(OSSched *sc, OSScTask *t)
{
	long type = t->list.t.type;

	if (type == M_AUDTASK) {
		if (sc->audioListTail) {
			sc->audioListTail->next = t;
		} else {
			sc->audioListHead = t;
		}

		sc->audioListTail = t;
		sc->doAudio = 1;
	} else {
		if (sc->gfxListTail) {
			sc->gfxListTail->next = t;
		} else {
			sc->gfxListHead = t;
		}

		sc->gfxListTail = t;
	}

	t->next = NULL;
	t->state = t->flags & OS_SC_RCP_MASK;
}

void __scExec(OSSched *sc, OSScTask *sp, OSScTask *dp)
{
	if (sp) {
		if (sp->list.t.type == M_AUDTASK) {
			osWritebackDCacheAll();
		}

		if (sp->list.t.type != M_AUDTASK && (sp->state & OS_SC_YIELD) == 0) {
			osDpSetStatus(0x3c0);
		}

		if (sp->list.t.type == M_AUDTASK) {
			func00009aa0(0x30001);
		} else {
			func00009aa0(0x40001);
			func00009aa0(0x20002);
		}

		sp->state &= ~(OS_SC_YIELD | OS_SC_YIELDED);

		osSpTaskLoad(&sp->list);
		osSpTaskStartGo(&sp->list);

		sc->curRSPTask = sp;

		if (sp->list.t.type != M_AUDTASK) {
			sc->curRDPTask = dp;
		}
	}
}

#if VERSION == VERSION_NTSC_BETA
GLOBAL_ASM(
glabel func00002d68nb
/*     2d68:	8c8300c8 */ 	lw	$v1,0xc8($a0)
/*     2d6c:	00001025 */ 	move	$v0,$zero
/*     2d70:	10600005 */ 	beqz	$v1,.L00002d88
/*     2d74:	00000000 */ 	nop
/*     2d78:	8c620010 */ 	lw	$v0,0x10($v1)
/*     2d7c:	384e0002 */ 	xori	$t6,$v0,0x2
/*     2d80:	03e00008 */ 	jr	$ra
/*     2d84:	2dc20001 */ 	sltiu	$v0,$t6,0x1
.L00002d88:
/*     2d88:	03e00008 */ 	jr	$ra
/*     2d8c:	00000000 */ 	nop
);
#endif

void __scYield(OSSched *sc)
{
	if (sc->curRSPTask->list.t.type == M_GFXTASK) {
		sc->curRSPTask->state |= 0x0010;
		osSpTaskYield();
	} else {
		// empty
	}
}

s32 __scSchedule(OSSched *sc, OSScTask **sp, OSScTask **dp, s32 availRCP)
{
	s32 avail = availRCP;
	OSScTask *gfx = sc->gfxListHead;
	OSScTask *audio = sc->audioListHead;

	if (sc->doAudio && (avail & OS_SC_SP)) {
		if (gfx && (gfx->flags & OS_SC_PARALLEL_TASK)) {
			*sp = gfx;
			avail &= ~OS_SC_SP;
		} else {
			*sp = audio;
			avail &= ~OS_SC_SP;
			sc->doAudio = 0;
			sc->audioListHead = sc->audioListHead->next;

			if (sc->audioListHead == NULL) {
				sc->audioListTail = NULL;
			}
		}
	} else {
		if (__scTaskReady(gfx)) {
			switch (gfx->flags & OS_SC_TYPE_MASK) {
			case OS_SC_XBUS:
				if (gfx->state & OS_SC_YIELDED) {
					if (avail & OS_SC_SP) {
						*sp = gfx;
						avail &= ~OS_SC_SP;

						if (gfx->state & OS_SC_DP) {
							*dp = gfx;
							avail &= ~OS_SC_DP;

							if (avail & OS_SC_DP == 0) {
								assert(sc->curRDPTask == gfx);
							}
						}

						sc->gfxListHead = sc->gfxListHead->next;

						if (sc->gfxListHead == NULL) {
							sc->gfxListTail = NULL;
						}
					}
				} else {
					if (avail == (OS_SC_SP | OS_SC_DP)) {
						*sp = *dp = gfx;
						avail &= ~(OS_SC_SP | OS_SC_DP);
						sc->gfxListHead = sc->gfxListHead->next;

						if (sc->gfxListHead == NULL) {
							sc->gfxListTail = NULL;
						}
					}
				}
				break;
			case OS_SC_DRAM:
			case OS_SC_DP_DRAM:
			case OS_SC_DP_XBUS:
				if (gfx->state & OS_SC_SP) {
					if (avail & OS_SC_SP) {
						*sp = gfx;
						avail &= ~OS_SC_SP;
					}
				} else if (gfx->state & OS_SC_DP) {
					if (avail & OS_SC_DP) {
						*dp = gfx;
						avail &= ~OS_SC_DP;
						sc->gfxListHead = sc->gfxListHead->next;

						if (sc->gfxListHead == NULL) {
							sc->gfxListTail = NULL;
						}
					}
				}
				break;
			case OS_SC_SP_DRAM:
			case OS_SC_SP_XBUS:
			default:
				break;
			}
		}
	}

	if (avail != availRCP) {
		avail = __scSchedule(sc, sp, dp, avail);
	}

	return avail;
}

GLOBAL_ASM(
glabel func00002d90
/*     2d90:	3c04800a */ 	lui	$a0,%hi(g_MenuData)
/*     2d94:	248419c0 */ 	addiu	$a0,$a0,%lo(g_MenuData)
/*     2d98:	90820016 */ 	lbu	$v0,0x16($a0)
/*     2d9c:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*     2da0:	24050001 */ 	addiu	$a1,$zero,0x1
/*     2da4:	afbf0014 */ 	sw	$ra,0x14($sp)
/*     2da8:	14a2000c */ 	bne	$a1,$v0,.L00002ddc
/*     2dac:	00401825 */ 	or	$v1,$v0,$zero
/*     2db0:	3c0e8009 */ 	lui	$t6,%hi(g_Is4Mb)
/*     2db4:	91ce0af0 */ 	lbu	$t6,%lo(g_Is4Mb)($t6)
/*     2db8:	50ae0006 */ 	beql	$a1,$t6,.L00002dd4
/*     2dbc:	300200ff */ 	andi	$v0,$zero,0xff
/*     2dc0:	0fc381dc */ 	jal	func0f0e0770
/*     2dc4:	00000000 */ 	nop
/*     2dc8:	3c04800a */ 	lui	$a0,%hi(g_MenuData)
/*     2dcc:	248419c0 */ 	addiu	$a0,$a0,%lo(g_MenuData)
/*     2dd0:	300200ff */ 	andi	$v0,$zero,0xff
.L00002dd4:
/*     2dd4:	00401825 */ 	or	$v1,$v0,$zero
/*     2dd8:	a0800016 */ 	sb	$zero,0x16($a0)
.L00002ddc:
/*     2ddc:	28610002 */ 	slti	$at,$v1,0x2
/*     2de0:	14200002 */ 	bnez	$at,.L00002dec
/*     2de4:	244fffff */ 	addiu	$t7,$v0,-1
/*     2de8:	a08f0016 */ 	sb	$t7,0x16($a0)
.L00002dec:
/*     2dec:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*     2df0:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*     2df4:	03e00008 */ 	jr	$ra
/*     2df8:	00000000 */ 	nop
/*     2dfc:	00000000 */ 	nop
);
