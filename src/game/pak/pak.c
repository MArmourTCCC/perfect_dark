#include <ultra64.h>
#include "constants.h"
#include "game/camdraw.h"
#include "game/game_0f09f0.h"
#include "game/crc.h"
#include "game/gamefile.h"
#include "game/pak/pak.h"
#include "game/utils.h"
#include "bss.h"
#include "lib/args.h"
#include "lib/joy.h"
#include "lib/lib_06100.h"
#include "lib/lib_06330.h"
#include "lib/lib_06440.h"
#include "lib/lib_06550.h"
#include "lib/main.h"
#include "lib/memory.h"
#include "lib/rng.h"
#include "lib/lib_4e090.h"
#include "lib/lib_513b0.h"
#include "data.h"
#include "types.h"

/**
 * Perfect Dark supports saving to an in-cartridge EEPROM chip, as well as to
 * controller paks which can be inserted in any of the four controllers.
 *
 * This file provides an abstraction layer between a generic "pak" and the
 * backend device it uses, and also manages the structure of data within the
 * pak.
 *
 * -- EEPROM --
 *
 * The EEPROM chip is 2KB (0x800 bytes) and is rather simple: The game reads and
 * writes to it using address and length arguments to osEeprom functions.
 *
 * -- Controller Paks --
 *
 * The controller paks are accessed via osPfs functions which are provided by
 * Nintendo. Controller paks use a filesystem and can hold data from other games
 * which is why these functions must be used.
 *
 * Controller paks have a capacity of 256Kbits (32KB). Each controller pak
 * consists of 128 "pages", where each page is a block of 256 bytes. The first
 * 5 pages are reserved for the file allocation table, leaving 123 pages
 * available for game data.
 *
 * Games use osPfsAllocateFile to create a file, also known as a game note.
 * Controller paks can hold up to 16 game notes. Perfect Dark's game note uses
 * 28 pages (7168 bytes). This single game note holds all saved information
 * (game files, MP players and MP games).
 *
 * -- Data Structure --
 *
 * Regardless of whether the data is being written to EEPROM or to a controller
 * pak, the format of it is the same. The data is a list of files, with
 * different lengths based on their file type.
 *
 * Each file has a 16-byte header, followed by its variable length body.
 * The header contains a checksum of the body data, as well as its filetype,
 * size and identifiers.
 *
 * The file types are:
 *
 * BOS (length 0x70) - The "boss" file stores things global to all game files,
 *     such as the alternative title setting and chosen language if PAL.
 * GAM (length 0xb0) - Single player game files
 * MPP (length 0x60) - Multiplayer player files
 * MPG (length 0x50) - Multiplayer game setup files
 *
 * Each device can store 4 GAM, MPG and MPP files, and one BOS file. There is
 * additionally a single swap space per game type, making the total usage
 * 1984 bytes (0x7c0), which is 0x30 short of the EEPROM capacity.
 *
 * Controller paks, however, use 28 pages which is 20 pages more than necessary.
 * This is likely because they were going to hold PerfectHead photos, but when
 * the feature was removed the controller pak allocation was not adjusted.
 *
 * -- GUIDs --
 *
 * GUID is an abbreviation for globally unique identifier. GUIDs are used to
 * minimise the chance of the game overwriting a wrong file in the event that
 * a player loads a file from a controller pak, then swaps the controller pak
 * for another during gameplay. By using GUIDs, the game is very likely to
 * detect when this has happened and will prompt the player to reinsert the
 * original pak.
 *
 * When creating a game note on a controller pak, the game generates a serial
 * number for the controller pak. This serial number persists throughout the
 * life of the note. The serial number is saved into the header of every file
 * in that note.
 *
 * Additionally, when creating a file on a pak, the file is given an
 * incrementing ID number which is unique to that pak. That ID is also saved
 * into the header of that file.
 *
 * The combination of the device serial and file ID is the GUID.
 *
 * -- Terminology --
 *
 * header - is the 16 byte header which is common to all file types.
 * data   - is the byte data within each file. The data goes after the header.
 * file   - is the header + data combined, and is also what the user understands a file to be.
 * block  - is a sequence of 0x10 bytes (for game pak) or 0x20 bytes (for controller paks).
 */

#define MAX_HEADERCACHE_ENTRIES 50

const char g_N64FontCodeMap[] = "\0************** 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#'*+,-./:=?@";
const char var7f1b3ad4[] = "Pak %d -> Pak_UpdateAndGetPakNoteInfo - ERROR - ekPakErrorPakFatal\n";
const char var7f1b3b18[] = "Pak %d -> Pak_UpdateAndGetPakNoteInfo - ERROR - ekPakErrorNoPakPresent\n";

struct pak g_Paks[5];
u32 var800a317c;
OSPfs g_Pfses[4];
u32 var800a3320;
u32 var800a3324;
u32 var800a3328;
u32 var800a332c;
u32 var800a3330;
u32 var800a3334;
u32 var800a3338;
u32 var800a333c;
u32 var800a3340;
u32 var800a3344;
u32 var800a3348;
u32 var800a334c;
u32 var800a3350;
u32 var800a3354;
u32 var800a3358;
u32 var800a335c;
u32 var800a3360;
u32 var800a3364;
u32 var800a3368;
u32 var800a336c;
u32 var800a3370;
u32 var800a3374;
u32 var800a3378;
u32 var800a337c;
u32 var800a3380;
u32 var800a3384;
u32 var800a3388;
u32 var800a338c;
u32 var800a3390;
u32 var800a3394;
u32 var800a3398;
u32 var800a339c;

u16 var80075cb0 = ROM_COMPANYCODE;
char var80075cb4[] = "PerfDark";
char var80075cc0[] = "PerfDark";

u32 var80075ccc = 0x00000400;
u32 g_PakHasEeprom = false;
u32 var80075cd4 = 0x00000000;
u32 var80075cd8 = 0x00000000;
u32 var80075cdc = 0x00000000;
u32 g_PakDebugForceScrub = 0x00000000;
u32 g_PakDebugPakDump = 0x00000000;
u32 g_PakDebugPakCache = 0x00000001;
u32 g_PakDebugPakInit = 0x00000000;
u32 g_PakDebugWipeEeprom = 0x00000000;
u32 g_PakDebugCorruptMe = 0x00000000;

char g_PakNoteGameName[] = {
	N64CHAR('P'),
	N64CHAR('E'),
	N64CHAR('R'),
	N64CHAR('F'),
	N64CHAR('E'),
	N64CHAR('C'),
	N64CHAR('T'),
	N64CHAR(' '),
	N64CHAR('D'),
	N64CHAR('A'),
	N64CHAR('R'),
	N64CHAR('K'),
	0, // fill to 16 bytes
	0,
	0,
	0,
};

char g_PakNoteExtName[] = {0, 0, 0, 0};

u32 var80075d0c = 0x00000000;
u8 var80075d10 = 0;
u32 var80075d14 = 0x00000001;
u32 var80075d18 = 0x00000010;
u32 var80075d1c = 0x00000008;
u32 var80075d20 = 0x00000020;
u32 var80075d24 = 0x00000040;
u32 var80075d28 = 0x00000080;
u32 var80075d2c = 0x00000002;
u32 var80075d30 = 0x00000003;
u32 var80075d34 = 0x00000005;
u32 var80075d38 = 0x00000005;
u32 var80075d3c = 0x00000005;
u32 var80075d40 = (u32)&var7f1b423c;
u32 var80075d44 = (u32)&var7f1b4244;
u32 var80075d48 = (u32)&var7f1b424c;
u32 var80075d4c = (u32)&var7f1b4254;
u32 var80075d50 = (u32)&var7f1b425c;

u32 pakGetBlockSize(s8 device)
{
	return device == SAVEDEVICE_GAMEPAK ? 0x10 : 0x20;
}

u32 pakAlign(s8 device, u32 size)
{
	return pakGetBlockSize(device) == 0x20 ? align32(size) : align16(size);
}

void pak0f116650(void)
{
	// empty
}

s32 pakGetAlignedFileLenByBodyLen(s8 device, s32 bodylen)
{
	return pakAlign(device, sizeof(struct pakfileheader) + bodylen);
}

u32 pakGetBodyLenByFileLen(u32 filelen)
{
	return filelen - sizeof(struct pakfileheader);
}

#if VERSION >= VERSION_NTSC_1_0
u32 pakGenerateSerial(s8 device)
{
	s32 value;
	s32 rand;
	s32 count;

	if (device == SAVEDEVICE_GAMEPAK) {
		return 0xbaa;
	}

	value = g_Paks[device].unk2c8;
	rand = (random() % 496) + 16; // range 16-511
	count = osGetCount();

	return value ^ rand ^ count;
}
#endif

bool pakIsConnected(s8 device)
{
	if (g_Paks[device].unk000 == 2) {
		switch (g_Paks[device].unk010) {
		case 0x0e:
		case 0x0f:
		case 0x13:
		case 0x14:
		case 0x16:
			return false;
		}

		return true;
	}

	return false;
}

s32 pak0f1167b0(s8 device, u32 filetype, u32 *buffer1024)
{
	return pak0f118d18(device, filetype, buffer1024);
}

s32 pak0f1167d8(s8 device)
{
	return pak0f119298(device);
}

s32 pak0f116800(s8 device, s32 fileid, u8 *body, s32 arg3)
{
	return pak0f118bc8(device, fileid, body, arg3);
}

s32 pak0f116828(s8 device, s32 fileid, s32 filetype, u8 *body, s32 *outfileid, s32 arg5)
{
	return pak0f11789c(device, fileid, filetype, body, outfileid, arg5);
}

bool pakDeleteFile(s8 device, s32 fileid)
{
	return pakDeleteFile2(device, fileid);
}

s32 pakDeleteGameNote(s8 device, u16 company_code, u32 game_code, char *game_name, char *ext_name)
{
	return pakDeleteGameNote2(device, company_code, game_code, game_name, ext_name);
}

s32 pak0f1168c4(s8 device, struct pakdata **arg1)
{
	return pak0f116df0(device, arg1);
}

s32 pak0f1168ec(s8 device, u8 *olddata)
{
	return pak0f118230(device, olddata);
}

u32 pak0f116914(s8 device)
{
	return pakGetUnk000(device);
}

s32 pakGetDeviceSerial(s8 device)
{
	return pakGetSerial(device);
}

void pak0f116984(s8 arg0, u8 *arg1, u8 *arg2)
{
	pak0f116bdc(arg0, arg1, arg2);
}

void pak0f11698c(s8 device)
{
	// empty
}

#if VERSION >= VERSION_NTSC_1_0
void pak0f116994(void)
{
	if (g_Vars.stagenum == STAGE_BOOTPAKMENU) {
		g_Vars.unk0004e4 = 0xf8;
	}
}
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f1169bc(u32 arg0, u32 arg1)
{
	// empty
}
#endif

void pak0f1169c8(s8 device, s32 arg1)
{
#if VERSION >= VERSION_NTSC_1_0
	u8 prevvalue = g_Vars.paksconnected;

	g_Vars.paksconnected = 0x1f;

	if ((g_Vars.paksconnected2 | g_Vars.paksconnected) & (1 << device)) {
		var80075d10 &= ~(1 << device);

		pak0f11ca30();
		pak0f11ca30();

		if (arg1) {
			var8005eedc = 0;

			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);

			var8005eedc = 1;
		}
	}

	g_Vars.paksconnected = prevvalue;
#else
	if ((g_Vars.paksconnected2 | g_Vars.paksconnected) & (1 << device)) {
		var80075d10 &= ~(1 << device);

		pak0f11ca30();

		if (arg1) {
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
			pak0f11df94(device);
		}
	}
#endif
}

bool pak0f116aec(s8 device)
{
	if (g_Paks[device].unk010 == 11 && g_Paks[device].unk000 == 2) {
		return true;
	}

	return false;
}

bool pak0f116b5c(s8 device)
{
	if ((g_Paks[device].unk010 == 11
				|| g_Paks[device].unk010 == 16
				|| g_Paks[device].unk010 == 21)
			&& g_Paks[device].unk000 == 2) {
		return true;
	}

	return false;
}

void pak0f116bdc(s8 device, u8 *arg1, u8 *arg2)
{
	*arg1 = g_Paks[device].unk2ba;
	*arg2 = g_Paks[device].unk2bb;
}

void pak0f116c2c(s8 index)
{
	joySetPfsTemporarilyPlugged(index);
}

u16 pakGetSerial(s8 device)
{
	return g_Paks[device].serial;
}

u32 pakGetUnk000(s8 device)
{
	return g_Paks[device].unk000;
}

ubool pak0f116cd4(s8 device)
{
	pak0f11d620(device);

	return g_Paks[device].unk2b8_05 && g_Paks[device].unk2b8_03;
}

ubool pak0f116d4c(s8 device)
{
	return g_Paks[device].unk2b8_05 && g_Paks[device].unk2b8_03 == 0;
}

void pakSetUnk010(s8 device, s32 value)
{
	g_Paks[device].unk010 = value;
}

s32 pak0f116df0(s8 device, struct pakdata **pakdata)
{
	*pakdata = NULL;

	if (pak0f116b5c(device)) {
		if (pak0f11a574(device)) {
			*pakdata = &g_Paks[device].pakdata;
			return 0;
		}

		return 2;
	}

	return 1;
}

s32 pakDeleteGameNote2(s8 device, u16 company_code, u32 game_code, char *game_name, char *ext_name)
{
	s32 result;

	if (pak0f116b5c(device)) {
#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
#else
		joyGetLock(123, "pak.c");
#endif
		result = pakDeleteGameNote3(PFS(device), company_code, game_code, game_name, ext_name);
#if VERSION >= VERSION_NTSC_1_0
		joyReleaseLock();
#else
		joyReleaseLock(123, "pak.c");
#endif

		if (pakHandleResult(result, device, 1, VERSION >= VERSION_NTSC_FINAL ? 825 : 822)) {
			g_Paks[device].unk2b8_02 = 1;
			return 0;
		}

		return 2;
	}

	return 1;
}

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b3b60[] = "-> Unknown PakFileType_e - %d\n";
#else
const char var7f1b3b60[] = "-> Unknown PakFileType_e - %d";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1ad850nb[] = "Bad     ";
const char var7f1ad85cnb[] = "Blank   ";
const char var7f1ad868nb[] = "Swap    ";
const char var7f1ad874nb[] = "Camera  ";
const char var7f1ad880nb[] = "GmSetup ";
const char var7f1ad88cnb[] = "Boss    ";
const char var7f1ad898nb[] = "Multi Pl";
const char var7f1ad8a4nb[] = "Multi Gm";
const char var7f1ad8b0n0[] = "????????";
#endif

#if VERSION >= VERSION_NTSC_1_0
s32 pakDeleteFile2(s8 device, s32 fileid)
{
	struct pakfileheader header;
	s32 result = pakFindFile(device, fileid, &header);

	if (result == -1) {
		return 1;
	}

	result = pakWriteFile(device, result, header.filetype, NULL, 0, NULL, NULL, 0, header.unk0c_21 + 1);

	if (result) {
		return result;
	}

	return 0;
}
#else
s32 pakDeleteFile2(s8 device, s32 fileid)
{
	struct pakfileheader header;
	u32 result;
	u32 tmp = pakFindFile(device, fileid, &header);

	if (tmp && (!tmp || tmp >= pakGetNumBytes(device) || (pakGetBlockSize(device) - 1U & tmp))) {
		return 3;
	}

	result = pakWriteFile(device, tmp, header.filetype, NULL, 0, NULL, NULL, 0, header.unk0c_21 + 1);

	if (result) {
		return result;
	}

	return 0;
}
#endif

s32 pakGetUnk264(s8 device)
{
	return g_Paks[device].unk264;
}

u32 pakGetMaxFileSize(s8 device)
{
	if (device != SAVEDEVICE_GAMEPAK) {
		return 0x4c0;
	}

	return 0x100;
}

s32 pakGetBodyLenByType(s8 device, u32 filetype)
{
	s32 len = 0;

	switch (filetype) {
	case PAKFILETYPE_001:
	case PAKFILETYPE_BLANK:
		break;
	case PAKFILETYPE_TERMINATOR:
		len = pakGetMaxFileSize(device) - 0x10;
		break;
	case PAKFILETYPE_BOSS:
		len = 0x5b;
		break;
	case PAKFILETYPE_MPPLAYER:
		len = 0x4e;
		break;
	case PAKFILETYPE_MPSETUP:
		len = 0x31;
		break;
	case PAKFILETYPE_008:
		len = 0x4a0;
		break;
	case PAKFILETYPE_GAME:
		len = 0xa0;
		break;
	}

	return len;
}

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f1114a0nb
.late_rodata
glabel var7f1aefa8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefacnb
.word pak0f1114a0nb+0x5c
glabel var7f1aefb0nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefb4nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefb8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefbcnb
.word pak0f1114a0nb+0x5c
glabel var7f1aefc0nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefc4nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefc8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefccnb
.word pak0f1114a0nb+0x5c
glabel var7f1aefd0nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefd4nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefd8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefdcnb
.word pak0f1114a0nb+0x5c
glabel var7f1aefe0nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefe4nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefe8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aefecnb
.word pak0f1114a0nb+0x5c
glabel var7f1aeff0nb
.word pak0f1114a0nb+0x5c
glabel var7f1aeff4nb
.word pak0f1114a0nb+0x5c
glabel var7f1aeff8nb
.word pak0f1114a0nb+0x5c
glabel var7f1aeffcnb
.word pak0f1114a0nb+0x5c
glabel var7f1af000nb
.word pak0f1114a0nb+0x5c
glabel var7f1af004nb
.word pak0f1114a0nb+0x5c
glabel var7f1af008nb
.word pak0f1114a0nb+0x5c
glabel var7f1af00cnb
.word pak0f1114a0nb+0x5c
glabel var7f1af010nb
.word pak0f1114a0nb+0x5c
glabel var7f1af014nb
.word pak0f1114a0nb+0x5c
glabel var7f1af018nb
.word pak0f1114a0nb+0x5c
glabel var7f1af01cnb
.word pak0f1114a0nb+0x5c
glabel var7f1af020nb
.word pak0f1114a0nb+0x5c
glabel var7f1af024nb
.word pak0f1114a0nb+0x5c
.text
/*  f1114a0:	2c810041 */ 	sltiu	$at,$a0,0x41
/*  f1114a4:	14200005 */ 	bnez	$at,.NB0f1114bc
/*  f1114a8:	24010080 */ 	addiu	$at,$zero,0x80
/*  f1114ac:	10810013 */ 	beq	$a0,$at,.NB0f1114fc
/*  f1114b0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1114b4:	03e00008 */ 	jr	$ra
/*  f1114b8:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f1114bc:
/*  f1114bc:	2c810021 */ 	sltiu	$at,$a0,0x21
/*  f1114c0:	14200006 */ 	bnez	$at,.NB0f1114dc
/*  f1114c4:	248effff */ 	addiu	$t6,$a0,-1
/*  f1114c8:	24010040 */ 	addiu	$at,$zero,0x40
/*  f1114cc:	1081000b */ 	beq	$a0,$at,.NB0f1114fc
/*  f1114d0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1114d4:	03e00008 */ 	jr	$ra
/*  f1114d8:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f1114dc:
/*  f1114dc:	2dc10020 */ 	sltiu	$at,$t6,0x20
/*  f1114e0:	10200006 */ 	beqz	$at,.NB0f1114fc
/*  f1114e4:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f1114e8:	3c017f1b */ 	lui	$at,0x7f1b
/*  f1114ec:	002e0821 */ 	addu	$at,$at,$t6
/*  f1114f0:	8c2eefa8 */ 	lw	$t6,-0x1058($at)
/*  f1114f4:	01c00008 */ 	jr	$t6
/*  f1114f8:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f1114fc:
/*  f1114fc:	03e00008 */ 	jr	$ra
/*  f111500:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

void pak0f117150(s8 device, u8 *ptr)
{
	s32 i;

	g_Paks[device].unk2c4 = ptr;

	for (i = 0; i < 4096; i++) {
		g_Paks[device].unk2c4[i] = 0;
	}
}

void pak0f1171b4(s8 device, s32 arg1, s32 arg2)
{
	g_Paks[device].unk00c = arg1;
	g_Paks[device].unk2b8_06 = arg2;
}

s32 pakGetUnk008(s8 device)
{
	return g_Paks[device].unk008;
}

void pakSetUnk008(s8 device, s32 value)
{
	g_Paks[device].unk008 = value;
}

s32 pakGetUnk270(s8 device)
{
	return g_Paks[device].unk270;
}

s32 pakGetUnk004(s8 device)
{
	return g_Paks[device].unk004;
}

void pakSetUnk004(s8 device, s32 value)
{
	g_Paks[device].unk004 = value;
}

s32 pak0f117350(s8 device)
{
	return g_Paks[device].unk2b8_01;
}

void pak0f117398(s8 device)
{
	g_Paks[device].unk2b8_01 = 1;
}

void pak0f1173e4(s8 device)
{
	g_Paks[device].unk2b8_01 = 0;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f117430
/*  f117430:	27bdffc8 */ 	addiu	$sp,$sp,-56
/*  f117434:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f117438:	afa40038 */ 	sw	$a0,0x38($sp)
/*  f11743c:	afa5003c */ 	sw	$a1,0x3c($sp)
/*  f117440:	afa60040 */ 	sw	$a2,0x40($sp)
/*  f117444:	0fc45974 */ 	jal	pakGetBlockSize
/*  f117448:	83a4003b */ 	lb	$a0,0x3b($sp)
/*  f11744c:	83ae003b */ 	lb	$t6,0x3b($sp)
/*  f117450:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f117454:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f117458:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11745c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f117460:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f117464:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f117468:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11746c:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f117470:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f117474:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f117478:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11747c:	01f84021 */ 	addu	$t0,$t7,$t8
/*  f117480:	910502be */ 	lbu	$a1,0x2be($t0)
/*  f117484:	28a10032 */ 	slti	$at,$a1,0x32
/*  f117488:	50200021 */ 	beqzl	$at,.L0f117510
/*  f11748c:	00001025 */ 	or	$v0,$zero,$zero
/*  f117490:	18a0001e */ 	blez	$a1,.L0f11750c
/*  f117494:	00001825 */ 	or	$v1,$zero,$zero
/*  f117498:	8fb9003c */ 	lw	$t9,0x3c($sp)
/*  f11749c:	00003825 */ 	or	$a3,$zero,$zero
/*  f1174a0:	8d0402c0 */ 	lw	$a0,0x2c0($t0)
/*  f1174a4:	0322001b */ 	divu	$zero,$t9,$v0
/*  f1174a8:	00003012 */ 	mflo	$a2
/*  f1174ac:	14400002 */ 	bnez	$v0,.L0f1174b8
/*  f1174b0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1174b4:	0007000d */ 	break	0x7
.L0f1174b8:
/*  f1174b8:	8c890000 */ 	lw	$t1,0x0($a0)
/*  f1174bc:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f1174c0:	0065082a */ 	slt	$at,$v1,$a1
/*  f1174c4:	14c9000f */ 	bne	$a2,$t1,.L0f117504
/*  f1174c8:	24840024 */ 	addiu	$a0,$a0,0x24
/*  f1174cc:	83a4003b */ 	lb	$a0,0x3b($sp)
/*  f1174d0:	afa7001c */ 	sw	$a3,0x1c($sp)
/*  f1174d4:	0fc45974 */ 	jal	pakGetBlockSize
/*  f1174d8:	afa80024 */ 	sw	$t0,0x24($sp)
/*  f1174dc:	8fa80024 */ 	lw	$t0,0x24($sp)
/*  f1174e0:	8fa7001c */ 	lw	$a3,0x1c($sp)
/*  f1174e4:	8fa40040 */ 	lw	$a0,0x40($sp)
/*  f1174e8:	8d0a02c0 */ 	lw	$t2,0x2c0($t0)
/*  f1174ec:	00403025 */ 	or	$a2,$v0,$zero
/*  f1174f0:	01472821 */ 	addu	$a1,$t2,$a3
/*  f1174f4:	0c012978 */ 	jal	memcpy
/*  f1174f8:	24a50004 */ 	addiu	$a1,$a1,0x4
/*  f1174fc:	10000004 */ 	beqz	$zero,.L0f117510
/*  f117500:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f117504:
/*  f117504:	1420ffec */ 	bnez	$at,.L0f1174b8
/*  f117508:	24e70024 */ 	addiu	$a3,$a3,0x24
.L0f11750c:
/*  f11750c:	00001025 */ 	or	$v0,$zero,$zero
.L0f117510:
/*  f117510:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117514:	27bd0038 */ 	addiu	$sp,$sp,0x38
/*  f117518:	03e00008 */ 	jr	$ra
/*  f11751c:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f117430
/*  f111794:	27bdffc8 */ 	addiu	$sp,$sp,-56
/*  f111798:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11179c:	afa40038 */ 	sw	$a0,0x38($sp)
/*  f1117a0:	afa5003c */ 	sw	$a1,0x3c($sp)
/*  f1117a4:	afa60040 */ 	sw	$a2,0x40($sp)
/*  f1117a8:	0fc4428c */ 	jal	pakGetBlockSize
/*  f1117ac:	83a4003b */ 	lb	$a0,0x3b($sp)
/*  f1117b0:	83ae003b */ 	lb	$t6,0x3b($sp)
/*  f1117b4:	3c18800a */ 	lui	$t8,0x800a
/*  f1117b8:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f1117bc:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f1117c0:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f1117c4:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f1117c8:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f1117cc:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1117d0:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f1117d4:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1117d8:	01f84021 */ 	addu	$t0,$t7,$t8
/*  f1117dc:	910502be */ 	lbu	$a1,0x2be($t0)
/*  f1117e0:	28a10032 */ 	slti	$at,$a1,0x32
/*  f1117e4:	50200021 */ 	beqzl	$at,.NB0f11186c
/*  f1117e8:	00001025 */ 	or	$v0,$zero,$zero
/*  f1117ec:	18a0001e */ 	blez	$a1,.NB0f111868
/*  f1117f0:	00001825 */ 	or	$v1,$zero,$zero
/*  f1117f4:	8fb9003c */ 	lw	$t9,0x3c($sp)
/*  f1117f8:	00003825 */ 	or	$a3,$zero,$zero
/*  f1117fc:	8d0402c0 */ 	lw	$a0,0x2c0($t0)
/*  f111800:	0322001b */ 	divu	$zero,$t9,$v0
/*  f111804:	00003012 */ 	mflo	$a2
/*  f111808:	14400002 */ 	bnez	$v0,.NB0f111814
/*  f11180c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f111810:	0007000d */ 	break	0x7
.NB0f111814:
/*  f111814:	8c890000 */ 	lw	$t1,0x0($a0)
/*  f111818:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11181c:	0065082a */ 	slt	$at,$v1,$a1
/*  f111820:	14c9000f */ 	bne	$a2,$t1,.NB0f111860
/*  f111824:	24840024 */ 	addiu	$a0,$a0,0x24
/*  f111828:	83a4003b */ 	lb	$a0,0x3b($sp)
/*  f11182c:	afa7001c */ 	sw	$a3,0x1c($sp)
/*  f111830:	0fc4428c */ 	jal	pakGetBlockSize
/*  f111834:	afa80024 */ 	sw	$t0,0x24($sp)
/*  f111838:	8fa80024 */ 	lw	$t0,0x24($sp)
/*  f11183c:	8fa7001c */ 	lw	$a3,0x1c($sp)
/*  f111840:	8fa40040 */ 	lw	$a0,0x40($sp)
/*  f111844:	8d0a02c0 */ 	lw	$t2,0x2c0($t0)
/*  f111848:	00403025 */ 	or	$a2,$v0,$zero
/*  f11184c:	01472821 */ 	addu	$a1,$t2,$a3
/*  f111850:	0c012e88 */ 	jal	memcpy
/*  f111854:	24a50004 */ 	addiu	$a1,$a1,0x4
/*  f111858:	10000004 */ 	beqz	$zero,.NB0f11186c
/*  f11185c:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f111860:
/*  f111860:	1420ffec */ 	bnez	$at,.NB0f111814
/*  f111864:	24e70024 */ 	addiu	$a3,$a3,0x24
.NB0f111868:
/*  f111868:	00001025 */ 	or	$v0,$zero,$zero
.NB0f11186c:
/*  f11186c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f111870:	27bd0038 */ 	addiu	$sp,$sp,0x38
/*  f111874:	03e00008 */ 	jr	$ra
/*  f111878:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

u32 pakReadHeaderAtOffset(s8 device, u32 offset, struct pakfileheader *header)
{
	struct pakfileheader localheader;
	struct pakfileheader *headerptr;
	u32 blocknum;
	s32 result;
	u16 checksum[2];
	u8 sp38[0x20];

	headerptr = header ? header : &localheader;

	blocknum = offset / pakGetBlockSize(device);

	if (blocknum >= g_Paks[device].numblocks) {
		return 4;
	}

	if (!pakRetrieveHeaderFromCache(device, blocknum, headerptr)) {
		result = pakReadWriteBlock(device, PFS(device), g_Paks[device].noteindex, 0, offset, sizeof(sp38), sp38);

#if VERSION >= VERSION_PAL_FINAL
		if (pakHandleResult(result, device, 1, 1058) == 0)
#elif VERSION >= VERSION_NTSC_FINAL
		if (pakHandleResult(result, device, 1, 1058) == 0)
#elif VERSION >= VERSION_NTSC_1_0
		if (pakHandleResult(result, device, 1, 1055) == 0)
#else
		if (pakHandleResult(result, device, 1, 994) == 0)
#endif
		{
			if (result == 1) {
				return 1;
			}

			return 4;
		}

		memcpy(headerptr, sp38, sizeof(struct pakfileheader));
		pakCalculateChecksum(&sp38[0x08], &sp38[0x10], checksum);

		if (headerptr->headersum[0] != checksum[0] || headerptr->headersum[1] != checksum[1]) {
			return 7;
		}

		if (!headerptr->writecompleted) {
			return 15;
		}

		if ((argFindByPrefix(1, "-forceversion") ? 1 : 0) != headerptr->version) {
			return 9;
		}

		if (g_PakDebugPakCache) {
			pakSaveHeaderToCache(device, blocknum, (struct pakfileheader *) sp38);

			if (!pakRetrieveHeaderFromCache(device, blocknum, headerptr)) {
#if VERSION >= VERSION_NTSC_1_0
				osSyncPrintf("Pak %d -> Header Cache 2 - FATAL ERROR\n");
#else
				osSyncPrintf("Pak %d -> Header Cache 2 - FATAL ERROR");
#endif
				return 11;
			}
		}
	}

	if (headerptr->filelen == 0) {
		return 11;
	}

	return 0;
}

void pakDumpBuffer(u8 *buffer, u32 len, char *name)
{
	s32 i;
	char line[256];
	char tmp[256];

	osSyncPrintf(name);
	sprintf(line, "\n");

	for (i = 0; i != len; i++) {
		if ((i % 16) == 0) {
			osSyncPrintf(line);
			sprintf(line, "\nAddress = %u : ", i);
		}

		sprintf(tmp, "%2x ", buffer[i]);
		strcat(line, tmp);
	}

	strcat(line, "\n");

	osSyncPrintf(line);
}

void pakDumpEeprom(void)
{
	u8 buffer[2048];

#if VERSION >= VERSION_NTSC_1_0
	joyGetLock();
#else
	joyGetLock(1098, "pak.c");
#endif
	osEepromLongRead(&var80099e78, 0, buffer, 2048);
#if VERSION >= VERSION_NTSC_1_0
	joyReleaseLock();
#else
	joyReleaseLock(1100, "pak.c");
#endif

	pakDumpBuffer(buffer, 2048, "EEPROM DUMP");
}

const char var7f1b3be8[] = "PakSaveAtGuid: new guid = %x\n";

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11789c
/*  f11789c:	27bde780 */ 	addiu	$sp,$sp,-6272
/*  f1178a0:	afb10034 */ 	sw	$s1,0x34($sp)
/*  f1178a4:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f1178a8:	00117603 */ 	sra	$t6,$s1,0x18
/*  f1178ac:	afa41880 */ 	sw	$a0,0x1880($sp)
/*  f1178b0:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f1178b4:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f1178b8:	afb20038 */ 	sw	$s2,0x38($sp)
/*  f1178bc:	afa61888 */ 	sw	$a2,0x1888($sp)
/*  f1178c0:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f1178c4:	00a09025 */ 	or	$s2,$a1,$zero
/*  f1178c8:	01c08825 */ 	or	$s1,$t6,$zero
/*  f1178cc:	afb00030 */ 	sw	$s0,0x30($sp)
/*  f1178d0:	afa7188c */ 	sw	$a3,0x188c($sp)
/*  f1178d4:	afa0084c */ 	sw	$zero,0x84c($sp)
/*  f1178d8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1178dc:	0fc464da */ 	jal	pakFindFile
/*  f1178e0:	27a61870 */ 	addiu	$a2,$sp,0x1870
/*  f1178e4:	10400015 */ 	beqz	$v0,.L0f11793c
/*  f1178e8:	afa21858 */ 	sw	$v0,0x1858($sp)
/*  f1178ec:	10400011 */ 	beqz	$v0,.L0f117934
/*  f1178f0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1178f4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1178f8:	0fc45ff0 */ 	jal	pakGetNumBytes
/*  f1178fc:	03002025 */ 	or	$a0,$t8,$zero
/*  f117900:	8fb91858 */ 	lw	$t9,0x1858($sp)
/*  f117904:	00112600 */ 	sll	$a0,$s1,0x18
/*  f117908:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11790c:	0322082b */ 	sltu	$at,$t9,$v0
/*  f117910:	10200008 */ 	beqz	$at,.L0f117934
/*  f117914:	00000000 */ 	sll	$zero,$zero,0x0
/*  f117918:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11791c:	01002025 */ 	or	$a0,$t0,$zero
/*  f117920:	8faa1858 */ 	lw	$t2,0x1858($sp)
/*  f117924:	2449ffff */ 	addiu	$t1,$v0,-1
/*  f117928:	012a5824 */ 	and	$t3,$t1,$t2
/*  f11792c:	51600004 */ 	beqzl	$t3,.L0f117940
/*  f117930:	8fa51878 */ 	lw	$a1,0x1878($sp)
.L0f117934:
/*  f117934:	1000006d */ 	beqz	$zero,.L0f117aec
/*  f117938:	24020003 */ 	addiu	$v0,$zero,0x3
.L0f11793c:
/*  f11793c:	8fa51878 */ 	lw	$a1,0x1878($sp)
.L0f117940:
/*  f117940:	8fad1888 */ 	lw	$t5,0x1888($sp)
/*  f117944:	00112600 */ 	sll	$a0,$s1,0x18
/*  f117948:	000565c2 */ 	srl	$t4,$a1,0x17
/*  f11794c:	11ac0003 */ 	beq	$t5,$t4,.L0f11795c
/*  f117950:	01802825 */ 	or	$a1,$t4,$zero
/*  f117954:	10000065 */ 	beqz	$zero,.L0f117aec
/*  f117958:	2402000c */ 	addiu	$v0,$zero,0xc
.L0f11795c:
/*  f11795c:	00047603 */ 	sra	$t6,$a0,0x18
/*  f117960:	01c02025 */ 	or	$a0,$t6,$zero
/*  f117964:	0fc459ec */ 	jal	pak0f1167b0
/*  f117968:	27a60850 */ 	addiu	$a2,$sp,0x850
/*  f11796c:	8faf0850 */ 	lw	$t7,0x850($sp)
/*  f117970:	3c10eeee */ 	lui	$s0,0xeeee
/*  f117974:	3610eeee */ 	ori	$s0,$s0,0xeeee
/*  f117978:	11e0001b */ 	beqz	$t7,.L0f1179e8
/*  f11797c:	27a30850 */ 	addiu	$v1,$sp,0x850
/*  f117980:	8c650000 */ 	lw	$a1,0x0($v1)
/*  f117984:	00112600 */ 	sll	$a0,$s1,0x18
.L0f117988:
/*  f117988:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11798c:	03202025 */ 	or	$a0,$t9,$zero
/*  f117990:	27a61860 */ 	addiu	$a2,$sp,0x1860
/*  f117994:	0fc464da */ 	jal	pakFindFile
/*  f117998:	afa30048 */ 	sw	$v1,0x48($sp)
/*  f11799c:	2401ffff */ 	addiu	$at,$zero,-1
/*  f1179a0:	8fa30048 */ 	lw	$v1,0x48($sp)
/*  f1179a4:	14410003 */ 	bne	$v0,$at,.L0f1179b4
/*  f1179a8:	00408025 */ 	or	$s0,$v0,$zero
/*  f1179ac:	1000004f */ 	beqz	$zero,.L0f117aec
/*  f1179b0:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1179b4:
/*  f1179b4:	8fa4186c */ 	lw	$a0,0x186c($sp)
/*  f1179b8:	00044f40 */ 	sll	$t1,$a0,0x1d
/*  f1179bc:	05200006 */ 	bltz	$t1,.L0f1179d8
/*  f1179c0:	00041340 */ 	sll	$v0,$a0,0xd
/*  f1179c4:	00025642 */ 	srl	$t2,$v0,0x19
/*  f1179c8:	524a0004 */ 	beql	$s2,$t2,.L0f1179dc
/*  f1179cc:	8c650004 */ 	lw	$a1,0x4($v1)
/*  f1179d0:	10000005 */ 	beqz	$zero,.L0f1179e8
/*  f1179d4:	afaa084c */ 	sw	$t2,0x84c($sp)
.L0f1179d8:
/*  f1179d8:	8c650004 */ 	lw	$a1,0x4($v1)
.L0f1179dc:
/*  f1179dc:	24630004 */ 	addiu	$v1,$v1,0x4
/*  f1179e0:	54a0ffe9 */ 	bnezl	$a1,.L0f117988
/*  f1179e4:	00112600 */ 	sll	$a0,$s1,0x18
.L0f1179e8:
/*  f1179e8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1179ec:	16210011 */ 	bne	$s1,$at,.L0f117a34
/*  f1179f0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1179f4:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f1179f8:	01602025 */ 	or	$a0,$t3,$zero
/*  f1179fc:	8fa5084c */ 	lw	$a1,0x84c($sp)
/*  f117a00:	27a6004c */ 	addiu	$a2,$sp,0x4c
/*  f117a04:	0fc45a00 */ 	jal	pak0f116800
/*  f117a08:	2407ffff */ 	addiu	$a3,$zero,-1
/*  f117a0c:	14400003 */ 	bnez	$v0,.L0f117a1c
/*  f117a10:	27a3004c */ 	addiu	$v1,$sp,0x4c
/*  f117a14:	10000007 */ 	beqz	$zero,.L0f117a34
/*  f117a18:	afa31894 */ 	sw	$v1,0x1894($sp)
.L0f117a1c:
/*  f117a1c:	2401000a */ 	addiu	$at,$zero,0xa
/*  f117a20:	54410004 */ 	bnel	$v0,$at,.L0f117a34
/*  f117a24:	afa01894 */ 	sw	$zero,0x1894($sp)
/*  f117a28:	10000002 */ 	beqz	$zero,.L0f117a34
/*  f117a2c:	afa31894 */ 	sw	$v1,0x1894($sp)
/*  f117a30:	afa01894 */ 	sw	$zero,0x1894($sp)
.L0f117a34:
/*  f117a34:	8faf187c */ 	lw	$t7,0x187c($sp)
/*  f117a38:	8fad1890 */ 	lw	$t5,0x1890($sp)
/*  f117a3c:	8fae1894 */ 	lw	$t6,0x1894($sp)
/*  f117a40:	000fc500 */ 	sll	$t8,$t7,0x14
/*  f117a44:	0018cdc2 */ 	srl	$t9,$t8,0x17
/*  f117a48:	00112600 */ 	sll	$a0,$s1,0x18
/*  f117a4c:	00046603 */ 	sra	$t4,$a0,0x18
/*  f117a50:	27280001 */ 	addiu	$t0,$t9,0x1
/*  f117a54:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f117a58:	01802025 */ 	or	$a0,$t4,$zero
/*  f117a5c:	02002825 */ 	or	$a1,$s0,$zero
/*  f117a60:	8fa61888 */ 	lw	$a2,0x1888($sp)
/*  f117a64:	8fa7188c */ 	lw	$a3,0x188c($sp)
/*  f117a68:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f117a6c:	afb2001c */ 	sw	$s2,0x1c($sp)
/*  f117a70:	afad0014 */ 	sw	$t5,0x14($sp)
/*  f117a74:	0fc46f15 */ 	jal	pakWriteFile
/*  f117a78:	afae0018 */ 	sw	$t6,0x18($sp)
/*  f117a7c:	10400003 */ 	beqz	$v0,.L0f117a8c
/*  f117a80:	8fa51858 */ 	lw	$a1,0x1858($sp)
/*  f117a84:	10000019 */ 	beqz	$zero,.L0f117aec
/*  f117a88:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f117a8c:
/*  f117a8c:	2401ffff */ 	addiu	$at,$zero,-1
/*  f117a90:	54a10004 */ 	bnel	$a1,$at,.L0f117aa4
/*  f117a94:	3c01eeee */ 	lui	$at,0xeeee
/*  f117a98:	10000014 */ 	beqz	$zero,.L0f117aec
/*  f117a9c:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f117aa0:	3c01eeee */ 	lui	$at,0xeeee
.L0f117aa4:
/*  f117aa4:	3421eeee */ 	ori	$at,$at,0xeeee
/*  f117aa8:	10a1000f */ 	beq	$a1,$at,.L0f117ae8
/*  f117aac:	00112600 */ 	sll	$a0,$s1,0x18
/*  f117ab0:	8fab187c */ 	lw	$t3,0x187c($sp)
/*  f117ab4:	8faa084c */ 	lw	$t2,0x84c($sp)
/*  f117ab8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f117abc:	000b6500 */ 	sll	$t4,$t3,0x14
/*  f117ac0:	000c6dc2 */ 	srl	$t5,$t4,0x17
/*  f117ac4:	afad0020 */ 	sw	$t5,0x20($sp)
/*  f117ac8:	01202025 */ 	or	$a0,$t1,$zero
/*  f117acc:	8fa61888 */ 	lw	$a2,0x1888($sp)
/*  f117ad0:	00003825 */ 	or	$a3,$zero,$zero
/*  f117ad4:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f117ad8:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f117adc:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f117ae0:	0fc46f15 */ 	jal	pakWriteFile
/*  f117ae4:	afaa001c */ 	sw	$t2,0x1c($sp)
.L0f117ae8:
/*  f117ae8:	00001025 */ 	or	$v0,$zero,$zero
.L0f117aec:
/*  f117aec:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f117af0:	8fb00030 */ 	lw	$s0,0x30($sp)
/*  f117af4:	8fb10034 */ 	lw	$s1,0x34($sp)
/*  f117af8:	8fb20038 */ 	lw	$s2,0x38($sp)
/*  f117afc:	03e00008 */ 	jr	$ra
/*  f117b00:	27bd1880 */ 	addiu	$sp,$sp,0x1880
);
#else
GLOBAL_ASM(
glabel pak0f11789c
/*  f111c00:	27bde780 */ 	addiu	$sp,$sp,-6272
/*  f111c04:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f111c08:	00049600 */ 	sll	$s2,$a0,0x18
/*  f111c0c:	00127603 */ 	sra	$t6,$s2,0x18
/*  f111c10:	afa41880 */ 	sw	$a0,0x1880($sp)
/*  f111c14:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f111c18:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f111c1c:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f111c20:	afa61888 */ 	sw	$a2,0x1888($sp)
/*  f111c24:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f111c28:	00a09825 */ 	or	$s3,$a1,$zero
/*  f111c2c:	01c09025 */ 	or	$s2,$t6,$zero
/*  f111c30:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f111c34:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f111c38:	afa7188c */ 	sw	$a3,0x188c($sp)
/*  f111c3c:	afa0084c */ 	sw	$zero,0x84c($sp)
/*  f111c40:	01e02025 */ 	or	$a0,$t7,$zero
/*  f111c44:	0fc44da7 */ 	jal	pakFindFile
/*  f111c48:	27a61870 */ 	addiu	$a2,$sp,0x1870
/*  f111c4c:	10400015 */ 	beqz	$v0,.NB0f111ca4
/*  f111c50:	afa21858 */ 	sw	$v0,0x1858($sp)
/*  f111c54:	10400011 */ 	beqz	$v0,.NB0f111c9c
/*  f111c58:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111c5c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f111c60:	0fc448fb */ 	jal	pakGetNumBytes
/*  f111c64:	03002025 */ 	or	$a0,$t8,$zero
/*  f111c68:	8fb91858 */ 	lw	$t9,0x1858($sp)
/*  f111c6c:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111c70:	00044603 */ 	sra	$t0,$a0,0x18
/*  f111c74:	0322082b */ 	sltu	$at,$t9,$v0
/*  f111c78:	10200008 */ 	beqz	$at,.NB0f111c9c
/*  f111c7c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f111c80:	0fc4428c */ 	jal	pakGetBlockSize
/*  f111c84:	01002025 */ 	or	$a0,$t0,$zero
/*  f111c88:	8faa1858 */ 	lw	$t2,0x1858($sp)
/*  f111c8c:	2449ffff */ 	addiu	$t1,$v0,-1
/*  f111c90:	012a5824 */ 	and	$t3,$t1,$t2
/*  f111c94:	51600004 */ 	beqzl	$t3,.NB0f111ca8
/*  f111c98:	8fa51878 */ 	lw	$a1,0x1878($sp)
.NB0f111c9c:
/*  f111c9c:	1000005f */ 	beqz	$zero,.NB0f111e1c
/*  f111ca0:	24020003 */ 	addiu	$v0,$zero,0x3
.NB0f111ca4:
/*  f111ca4:	8fa51878 */ 	lw	$a1,0x1878($sp)
.NB0f111ca8:
/*  f111ca8:	8fad1888 */ 	lw	$t5,0x1888($sp)
/*  f111cac:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111cb0:	000565c2 */ 	srl	$t4,$a1,0x17
/*  f111cb4:	11ac0003 */ 	beq	$t5,$t4,.NB0f111cc4
/*  f111cb8:	01802825 */ 	or	$a1,$t4,$zero
/*  f111cbc:	10000057 */ 	beqz	$zero,.NB0f111e1c
/*  f111cc0:	2402000c */ 	addiu	$v0,$zero,0xc
.NB0f111cc4:
/*  f111cc4:	00047603 */ 	sra	$t6,$a0,0x18
/*  f111cc8:	01c02025 */ 	or	$a0,$t6,$zero
/*  f111ccc:	0fc442dd */ 	jal	pak0f1167b0
/*  f111cd0:	27a60850 */ 	addiu	$a2,$sp,0x850
/*  f111cd4:	8faf0850 */ 	lw	$t7,0x850($sp)
/*  f111cd8:	2411ffff */ 	addiu	$s1,$zero,-1
/*  f111cdc:	27b00850 */ 	addiu	$s0,$sp,0x850
/*  f111ce0:	51e00016 */ 	beqzl	$t7,.NB0f111d3c
/*  f111ce4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f111ce8:	8e050000 */ 	lw	$a1,0x0($s0)
/*  f111cec:	00122600 */ 	sll	$a0,$s2,0x18
.NB0f111cf0:
/*  f111cf0:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f111cf4:	03202025 */ 	or	$a0,$t9,$zero
/*  f111cf8:	0fc44da7 */ 	jal	pakFindFile
/*  f111cfc:	27a61860 */ 	addiu	$a2,$sp,0x1860
/*  f111d00:	8fa3186c */ 	lw	$v1,0x186c($sp)
/*  f111d04:	00408825 */ 	or	$s1,$v0,$zero
/*  f111d08:	00034f40 */ 	sll	$t1,$v1,0x1d
/*  f111d0c:	05200006 */ 	bltz	$t1,.NB0f111d28
/*  f111d10:	00031340 */ 	sll	$v0,$v1,0xd
/*  f111d14:	00025642 */ 	srl	$t2,$v0,0x19
/*  f111d18:	526a0004 */ 	beql	$s3,$t2,.NB0f111d2c
/*  f111d1c:	8e050004 */ 	lw	$a1,0x4($s0)
/*  f111d20:	10000005 */ 	beqz	$zero,.NB0f111d38
/*  f111d24:	afaa084c */ 	sw	$t2,0x84c($sp)
.NB0f111d28:
/*  f111d28:	8e050004 */ 	lw	$a1,0x4($s0)
.NB0f111d2c:
/*  f111d2c:	26100004 */ 	addiu	$s0,$s0,0x4
/*  f111d30:	54a0ffef */ 	bnezl	$a1,.NB0f111cf0
/*  f111d34:	00122600 */ 	sll	$a0,$s2,0x18
.NB0f111d38:
/*  f111d38:	24010004 */ 	addiu	$at,$zero,0x4
.NB0f111d3c:
/*  f111d3c:	1641000f */ 	bne	$s2,$at,.NB0f111d7c
/*  f111d40:	27b0004c */ 	addiu	$s0,$sp,0x4c
/*  f111d44:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111d48:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f111d4c:	01602025 */ 	or	$a0,$t3,$zero
/*  f111d50:	8fa5084c */ 	lw	$a1,0x84c($sp)
/*  f111d54:	02003025 */ 	or	$a2,$s0,$zero
/*  f111d58:	0fc442f1 */ 	jal	pak0f116800
/*  f111d5c:	2407ffff */ 	addiu	$a3,$zero,-1
/*  f111d60:	10400003 */ 	beqz	$v0,.NB0f111d70
/*  f111d64:	2401000a */ 	addiu	$at,$zero,0xa
/*  f111d68:	54410004 */ 	bnel	$v0,$at,.NB0f111d7c
/*  f111d6c:	afa01894 */ 	sw	$zero,0x1894($sp)
.NB0f111d70:
/*  f111d70:	10000002 */ 	beqz	$zero,.NB0f111d7c
/*  f111d74:	afb01894 */ 	sw	$s0,0x1894($sp)
/*  f111d78:	afa01894 */ 	sw	$zero,0x1894($sp)
.NB0f111d7c:
/*  f111d7c:	8fae187c */ 	lw	$t6,0x187c($sp)
/*  f111d80:	8fb01890 */ 	lw	$s0,0x1890($sp)
/*  f111d84:	8fad1894 */ 	lw	$t5,0x1894($sp)
/*  f111d88:	000e7d00 */ 	sll	$t7,$t6,0x14
/*  f111d8c:	000fc5c2 */ 	srl	$t8,$t7,0x17
/*  f111d90:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111d94:	00046603 */ 	sra	$t4,$a0,0x18
/*  f111d98:	27190001 */ 	addiu	$t9,$t8,0x1
/*  f111d9c:	afb90020 */ 	sw	$t9,0x20($sp)
/*  f111da0:	01802025 */ 	or	$a0,$t4,$zero
/*  f111da4:	02202825 */ 	or	$a1,$s1,$zero
/*  f111da8:	8fa61888 */ 	lw	$a2,0x1888($sp)
/*  f111dac:	8fa7188c */ 	lw	$a3,0x188c($sp)
/*  f111db0:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f111db4:	afb3001c */ 	sw	$s3,0x1c($sp)
/*  f111db8:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f111dbc:	0fc456f6 */ 	jal	pakWriteFile
/*  f111dc0:	afad0018 */ 	sw	$t5,0x18($sp)
/*  f111dc4:	10400003 */ 	beqz	$v0,.NB0f111dd4
/*  f111dc8:	8fa51858 */ 	lw	$a1,0x1858($sp)
/*  f111dcc:	10000013 */ 	beqz	$zero,.NB0f111e1c
/*  f111dd0:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f111dd4:
/*  f111dd4:	2401ffff */ 	addiu	$at,$zero,-1
/*  f111dd8:	10a1000f */ 	beq	$a1,$at,.NB0f111e18
/*  f111ddc:	00122600 */ 	sll	$a0,$s2,0x18
/*  f111de0:	8faa187c */ 	lw	$t2,0x187c($sp)
/*  f111de4:	8fa9084c */ 	lw	$t1,0x84c($sp)
/*  f111de8:	00044603 */ 	sra	$t0,$a0,0x18
/*  f111dec:	000a5d00 */ 	sll	$t3,$t2,0x14
/*  f111df0:	000b65c2 */ 	srl	$t4,$t3,0x17
/*  f111df4:	afac0020 */ 	sw	$t4,0x20($sp)
/*  f111df8:	01002025 */ 	or	$a0,$t0,$zero
/*  f111dfc:	8fa61888 */ 	lw	$a2,0x1888($sp)
/*  f111e00:	00003825 */ 	or	$a3,$zero,$zero
/*  f111e04:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f111e08:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f111e0c:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f111e10:	0fc456f6 */ 	jal	pakWriteFile
/*  f111e14:	afa9001c */ 	sw	$t1,0x1c($sp)
.NB0f111e18:
/*  f111e18:	00001025 */ 	or	$v0,$zero,$zero
.NB0f111e1c:
/*  f111e1c:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f111e20:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f111e24:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f111e28:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f111e2c:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f111e30:	03e00008 */ 	jr	$ra
/*  f111e34:	27bd1880 */ 	addiu	$sp,$sp,0x1880
);
#endif

#if VERSION >= VERSION_NTSC_1_0
s32 pakInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel, s32 *arg3)
#else
s32 pakInitPak(OSMesgQueue *mq, OSPfs *pfs, s32 channel)
#endif
{
	if (pfs) {
#if VERSION >= VERSION_NTSC_1_0
		return osPfsInitPak(mq, pfs, channel, arg3);
#else
		return osPfsInitPak(mq, pfs, channel);
#endif
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	return PAKERROR_OK;
}

s32 _pakReadWriteBlock(OSPfs *pfs, s32 file_no, u8 flag, u32 address, u32 len, u8 *buffer)
{
	u32 newaddress;

#if VERSION >= VERSION_NTSC_1_0
	joyCheckPfs(2);
#endif

	if (pfs) {
		return osPfsReadWriteFile(pfs, file_no, flag, address, len, buffer);
	}

	newaddress = address / 8;

	if (newaddress >= 256) {
#if VERSION < VERSION_NTSC_1_0
		func0000c1d0nb("ILLEGAL EEPROM ADDRESS (>=256)");
		*(u8 *)0 = 69;
#endif
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	if (flag == OS_WRITE) {
		return pakWriteEeprom(newaddress, buffer, len);
	}

	if (flag == OS_READ) {
		return pakReadEeprom(newaddress, buffer, len);
	}

	return PAKERROR_EEPROM_INVALIDOP;
}

s32 pak0f117c0c(s32 arg0, s32 *arg1, s32 *arg2)
{
	if (arg0) {
		s32 result;

#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
#else
		joyGetLock(1308, "pak.c");
#endif
		result = osPfsNumFiles(arg0, arg1, arg2);
#if VERSION >= VERSION_NTSC_1_0
		joyReleaseLock();
#else
		joyReleaseLock(1310, "pak.c");
#endif

		return result;
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	*arg1 = 1;
	*arg2 = 1;

	return PAKERROR_OK;
}

s32 pakFreeBlocks(OSPfs *pfs, s32 *bytes_not_used)
{
	if (pfs) {
		s32 result;

#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
#else
		joyGetLock(1337, "pak.c");
#endif

		result = osPfsFreeBlocks(pfs, bytes_not_used);

#if VERSION >= VERSION_NTSC_1_0
		joyReleaseLock();
#else
		joyReleaseLock(1339, "pak.c");
#endif

		return result;
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	*bytes_not_used = 0;

	return PAKERROR_OK;
}

s32 pakFileState(OSPfs *pfs, s32 file_no, OSPfsState *note)
{
	if (pfs) {
		s32 result;

#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
#else
		joyGetLock(1363, "pak.c");
#endif

		result = osPfsFileState(pfs, file_no, note);

#if VERSION >= VERSION_NTSC_1_0
		joyReleaseLock();
#else
		joyReleaseLock(1365, "pak.c");
#endif

		return result;
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	if (file_no) {
		return PAKERROR_EEPROM_INVALIDARG;
	}

	note->file_size = 0x800;
	note->company_code = ROM_COMPANYCODE;
	strcpy(note->game_name, g_PakNoteGameName);
	strcpy(note->ext_name, g_PakNoteExtName);

	return PAKERROR_OK;
}

const char var7f1b3c08[] = "Call to osPfsReSizeFile -> pfs=%x, cc=%u, gc=%u, gn=%s, en=%s, l=%d\n";

s32 pakAllocateFile(OSPfs *pfs, u16 company_code, u32 game_code, char *game_name, char *ext_name, s32 size, s32 *file_no)
{
	if (pfs) {
		return osPfsAllocateFile(pfs, company_code, game_code, game_name, ext_name, size, file_no);
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	*file_no = 0;

	return PAKERROR_OK;
}

u32 pakDeleteGameNote3(OSPfs *pfs, u16 company_code, u32 game_code, char *game_name, char *ext_name)
{
	if (pfs) {
		return osPfsDeleteFile(pfs, company_code, game_code, game_name, ext_name);
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	return PAKERROR_OK;
}

s32 pakFindNote(OSPfs *pfs, u16 company_code, u32 game_code, char *game_name, char *ext_name, s32 *file_no)
{
	if (pfs) {
		return osPfsFindFile(pfs, company_code, game_code, game_name, ext_name, file_no);
	}

	if (g_PakHasEeprom) {
#if VERSION >= VERSION_NTSC_FINAL
		*file_no = 0;
		return 0;
#else
		u8 sp64[8];
		u32 a;
		u16 sp56[2];
		u32 b;
		u16 sp44[4];

		*file_no = 0;
		a = pakReadWriteBlock(SAVEDEVICE_GAMEPAK, 0, 0, 0, 0, align16(0x10), (u8 *)sp56);

		if (pakHandleResult(a, SAVEDEVICE_GAMEPAK, 1, 0x60f)) {
			pakCalculateChecksum(sp64, sp64 + sizeof(sp64), sp44);

			if (sp56[0] == sp44[0] && sp56[1] == sp44[1]) {
				return 0;
			}

			return PAKERROR_EEPROM_INVALIDARG;
		}

		return PAKERROR_EEPROM_INVALIDARG;
#endif
	}

	return PAKERROR_EEPROM_MISSING;
}

s32 pak0f117ec0(OSPfs *pfs, u16 company_code, u32 game_code, u8 *game_name, u8 *ext_name, u32 numbytes)
{
	if (pfs) {
		s32 result;

#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
#else
		joyGetLock(1496, "pak.c");
#endif

		result = func00006550(pfs, company_code, game_code, game_name, ext_name, numbytes);

#if VERSION >= VERSION_NTSC_1_0
		joyReleaseLock();
#else
		joyReleaseLock(1498, "pak.c");
#endif

		return result;
	}

	if (!g_PakHasEeprom) {
		return PAKERROR_EEPROM_MISSING;
	}

	return PAKERROR_OK;
}

const char var7f1b3c50[] = "Pak %d -> Pak_AddOneCameraFile\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b3c70[] = "Pak %d -> Pak_AddOneCameraFile - Making one default camera file\n";
#else
const char var7f1b3c70[] = "Pak %d -> Pak_AddOneCameraFile - Making one default camera file";
#endif

const char var7f1b3cb4[] = "Pak %d -> Pak_AddOneCameraFile : Got Space - No need for resize\n";
const char var7f1b3cf8[] = "Pak %d -> Pak_AddOneCameraFile : No Space - Need to resize by %d pages\n";
const char var7f1b3d40[] = "Pak %d -> Pak_AddOneCameraFile - Make of one default camera files failed\n";
const char var7f1b3d8c[] = "Pak %d -> Pak_AddOneCameraFile : Error - No Room\n";
const char var7f1b3dc0[] = "Pak %d -> Pak_GameNoteResetSize : New=%u\n";
const char var7f1b3dec[] = "bDoUpdate7\n";
const char var7f1b3df8[] = "Pak_SetThisGameSetupFile -> Pak=%d, File=%d, EEPROM=%d\n";

#if VERSION < VERSION_NTSC_1_0
const char var7f1adbd8nb[] = "RWI : Pak_OneNewFile - Using a snug in a bug offset\n";
const char var7f1adc10nb[] = "pak.c";
const char var7f1adc18nb[] = "Pak_Make -> Dumping details of file types found\n";
const char var7f1adc4cnb[] = "Type %d -> ";
const char var7f1adc58nb[] = ", dSize=%u, fSize=%u\n";
const char var7f1adc70nb[] = "Pak_Make -> Checking for inserted pakz\n";
const char var7f1adc98nb[] = "Pak_Make -> Loading Boss File\n";
const char var7f1adcb8nb[] = "Pak_Make -> Boss file load failed - Try to make a new one\n";
const char var7f1adcf4nb[] = "Pak_Make -> Setting up default game file\n";
#endif

s32 pakGetNumBlocks(s8 device)
{
	return g_Paks[device].numblocks;
}

s32 pakGetNumPages(s8 device)
{
	return g_Paks[device].numpages;
}

u32 pakGetNumBytes(s8 device)
{
	return g_Paks[device].numbytes;
}

s32 pak0f118000(s8 device)
{
	s32 value;

	pakFreeBlocks(PFS(device), &value);

	return value / 256;
}

s32 pakGetNumPagesRequired(void)
{
	return 28;
}

s32 pak0f11807c(s8 device)
{
	if (device != SAVEDEVICE_GAMEPAK) {
		s32 pagesneeded;
		u32 bytesneeded = pakGetMaxFileSize(device);

		if (!pak0f1190bc(device, 8, NULL)) {
			return true;
		}

		pagesneeded = bytesneeded / 256;

		if (bytesneeded & 0xff) {
			pagesneeded++;
		}

		if (g_Paks[device].pakdata.pagesused + pagesneeded < 128) {
			return (g_Paks[device].pakdata.pagesfree >= pagesneeded);
		}
	}

	return false;
}

/**
 * Appears to returns the number of times a file can fit on the given device
 * based on the current amount of free space.
 */
s32 pak0f118148(s8 device)
{
	if (device != SAVEDEVICE_GAMEPAK) {
		s32 result = 0;
		s32 pagesneeded;
		u32 bytesneeded;

		bytesneeded = pakGetMaxFileSize(device);
		pak0f1190bc(device, 8, &result);
		pagesneeded = bytesneeded / 256;

		if (bytesneeded & 0xff) {
			pagesneeded++;
		}

		// 128 is the total number of pages on a controller pak
		result += (128 - g_Paks[device].pakdata.pagesused) / pagesneeded;

		return result;
	}

	return 0;
}

s32 pak0f118230(s8 device, u8 *olddata)
{
	if (device != SAVEDEVICE_GAMEPAK && pak0f11807c(device)) {
		s32 result;
		u32 bytesneeded = pakGetMaxFileSize(device);

		if (pak0f1190bc(device, 8, NULL)) {
			s32 pages = pakGetNumPages(device);
			s32 pagesneeded = bytesneeded / 256;

			if (bytesneeded & 0xff) {
				pagesneeded++;
			}

			pages += pagesneeded;

			if (!pak0f118334(device, pages)) {
				return 4;
			}
		}

		result = pak0f118674(device, PAKFILETYPE_008, olddata);

		if (result) {
			return result;
		}

		return 0;
	}

	return 14;
}

s32 pak0f118334(s8 device, s32 numpages)
{
	s32 stack1[2];
	s32 errno;
	struct pak *devicedata;
	s32 stack2[2];
	OSPfsState *note;
	u32 numbytes;

	pakGetNumPages(device);
	pak0f118000(device);

	numbytes = numpages * 256;
	errno = pak0f117ec0(PFS(device), ROM_COMPANYCODE, ROM_GAMECODE, g_PakNoteGameName, g_PakNoteExtName, numbytes);
	pakHandleResult(errno, device, 1, VERSION >= VERSION_NTSC_FINAL ? 1802 : 1788);

	if (errno == 0) {
		devicedata = &g_Paks[device];
		note = &devicedata->pakdata.notes[devicedata->noteindex];

		devicedata->pakdata.pagesfree -= numpages - devicedata->numpages;
		devicedata->pakdata.pagesused += numpages - devicedata->numpages;

		note->file_size = devicedata->pakdata.pagesused * 256;

		devicedata->numbytes = numbytes;
		devicedata->numblocks = devicedata->numbytes / pakGetBlockSize(device);
		devicedata->numpages = devicedata->numbytes / 256;

		return true;
	}

	return false;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f1184d8
/*  f1184d8:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*  f1184dc:	00047600 */ 	sll	$t6,$a0,0x18
/*  f1184e0:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f1184e4:	afa40018 */ 	sw	$a0,0x18($sp)
/*  f1184e8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1184ec:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1184f0:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f1184f4:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f1184f8:	11e1001d */ 	beq	$t7,$at,.L0f118570
/*  f1184fc:	00c03825 */ 	or	$a3,$a2,$zero
/*  f118500:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f118504:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f118508:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11850c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f118510:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f118514:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f118518:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11851c:	3c03800a */ 	lui	$v1,%hi(g_Paks)
/*  f118520:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f118524:	24632380 */ 	addiu	$v1,$v1,%lo(g_Paks)
/*  f118528:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11852c:	00781021 */ 	addu	$v0,$v1,$t8
/*  f118530:	8c590000 */ 	lw	$t9,0x0($v0)
/*  f118534:	24010002 */ 	addiu	$at,$zero,0x2
/*  f118538:	57210006 */ 	bnel	$t9,$at,.L0f118554
/*  f11853c:	90650ded */ 	lbu	$a1,0xded($v1)
/*  f118540:	8c480010 */ 	lw	$t0,0x10($v0)
/*  f118544:	2401000b */ 	addiu	$at,$zero,0xb
/*  f118548:	5101000a */ 	beql	$t0,$at,.L0f118574
/*  f11854c:	00045080 */ 	sll	$t2,$a0,0x2
/*  f118550:	90650ded */ 	lbu	$a1,0xded($v1)
.L0f118554:
/*  f118554:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f118558:	afa70020 */ 	sw	$a3,0x20($sp)
/*  f11855c:	30a9000f */ 	andi	$t1,$a1,0xf
/*  f118560:	0fc46178 */ 	jal	pak0f1185e0
/*  f118564:	01202825 */ 	or	$a1,$t1,$zero
/*  f118568:	8fa70020 */ 	lw	$a3,0x20($sp)
/*  f11856c:	24040004 */ 	addiu	$a0,$zero,0x4
.L0f118570:
/*  f118570:	00045080 */ 	sll	$t2,$a0,0x2
.L0f118574:
/*  f118574:	01445023 */ 	subu	$t2,$t2,$a0
/*  f118578:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11857c:	01445023 */ 	subu	$t2,$t2,$a0
/*  f118580:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118584:	01445021 */ 	addu	$t2,$t2,$a0
/*  f118588:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11858c:	3c03800a */ 	lui	$v1,%hi(g_Paks)
/*  f118590:	01445023 */ 	subu	$t2,$t2,$a0
/*  f118594:	24632380 */ 	addiu	$v1,$v1,%lo(g_Paks)
/*  f118598:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11859c:	006a1021 */ 	addu	$v0,$v1,$t2
/*  f1185a0:	904b02bd */ 	lbu	$t3,0x2bd($v0)
/*  f1185a4:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f1185a8:	316c0080 */ 	andi	$t4,$t3,0x80
/*  f1185ac:	51800004 */ 	beqzl	$t4,.L0f1185c0
/*  f1185b0:	ace00000 */ 	sw	$zero,0x0($a3)
/*  f1185b4:	10000002 */ 	beqz	$zero,.L0f1185c0
/*  f1185b8:	aced0000 */ 	sw	$t5,0x0($a3)
/*  f1185bc:	ace00000 */ 	sw	$zero,0x0($a3)
.L0f1185c0:
/*  f1185c0:	904e02bd */ 	lbu	$t6,0x2bd($v0)
/*  f1185c4:	8fb8001c */ 	lw	$t8,0x1c($sp)
/*  f1185c8:	31cf000f */ 	andi	$t7,$t6,0xf
/*  f1185cc:	af0f0000 */ 	sw	$t7,0x0($t8)
/*  f1185d0:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f1185d4:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*  f1185d8:	03e00008 */ 	jr	$ra
/*  f1185dc:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f1184d8
/*  f1128e4:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*  f1128e8:	00047600 */ 	sll	$t6,$a0,0x18
/*  f1128ec:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f1128f0:	afa40018 */ 	sw	$a0,0x18($sp)
/*  f1128f4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1128f8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1128fc:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f112900:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f112904:	11e1001b */ 	beq	$t7,$at,.NB0f112974
/*  f112908:	00c03825 */ 	or	$a3,$a2,$zero
/*  f11290c:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f112910:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f112914:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f112918:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11291c:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f112920:	3c03800a */ 	lui	$v1,0x800a
/*  f112924:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f112928:	24636870 */ 	addiu	$v1,$v1,0x6870
/*  f11292c:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f112930:	00781021 */ 	addu	$v0,$v1,$t8
/*  f112934:	8c590000 */ 	lw	$t9,0x0($v0)
/*  f112938:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11293c:	57210006 */ 	bnel	$t9,$at,.NB0f112958
/*  f112940:	90650ddd */ 	lbu	$a1,0xddd($v1)
/*  f112944:	8c480010 */ 	lw	$t0,0x10($v0)
/*  f112948:	2401000b */ 	addiu	$at,$zero,0xb
/*  f11294c:	5101000a */ 	beql	$t0,$at,.NB0f112978
/*  f112950:	00045080 */ 	sll	$t2,$a0,0x2
/*  f112954:	90650ddd */ 	lbu	$a1,0xddd($v1)
.NB0f112958:
/*  f112958:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11295c:	afa70020 */ 	sw	$a3,0x20($sp)
/*  f112960:	30a9000f */ 	andi	$t1,$a1,0xf
/*  f112964:	0fc44a77 */ 	jal	pak0f1185e0
/*  f112968:	01202825 */ 	or	$a1,$t1,$zero
/*  f11296c:	8fa70020 */ 	lw	$a3,0x20($sp)
/*  f112970:	24040004 */ 	addiu	$a0,$zero,0x4
.NB0f112974:
/*  f112974:	00045080 */ 	sll	$t2,$a0,0x2
.NB0f112978:
/*  f112978:	01445023 */ 	subu	$t2,$t2,$a0
/*  f11297c:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f112980:	01445023 */ 	subu	$t2,$t2,$a0
/*  f112984:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f112988:	3c03800a */ 	lui	$v1,0x800a
/*  f11298c:	01445021 */ 	addu	$t2,$t2,$a0
/*  f112990:	24636870 */ 	addiu	$v1,$v1,0x6870
/*  f112994:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f112998:	006a1021 */ 	addu	$v0,$v1,$t2
/*  f11299c:	904b02bd */ 	lbu	$t3,0x2bd($v0)
/*  f1129a0:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f1129a4:	316c0080 */ 	andi	$t4,$t3,0x80
/*  f1129a8:	51800004 */ 	beqzl	$t4,.NB0f1129bc
/*  f1129ac:	ace00000 */ 	sw	$zero,0x0($a3)
/*  f1129b0:	10000002 */ 	beqz	$zero,.NB0f1129bc
/*  f1129b4:	aced0000 */ 	sw	$t5,0x0($a3)
/*  f1129b8:	ace00000 */ 	sw	$zero,0x0($a3)
.NB0f1129bc:
/*  f1129bc:	904e02bd */ 	lbu	$t6,0x2bd($v0)
/*  f1129c0:	8fb8001c */ 	lw	$t8,0x1c($sp)
/*  f1129c4:	31cf000f */ 	andi	$t7,$t6,0xf
/*  f1129c8:	af0f0000 */ 	sw	$t7,0x0($t8)
/*  f1129cc:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f1129d0:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*  f1129d4:	03e00008 */ 	jr	$ra
/*  f1129d8:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

void pak0f1185e0(s8 device, s32 arg1, s32 arg2)
{
	if (arg2) {
		g_Paks[device].unk2bd = 0x80;
	} else {
		g_Paks[device].unk2bd = 0;
	}

	g_Paks[device].unk2bd += arg1;
}

#if VERSION >= VERSION_NTSC_FINAL
GLOBAL_ASM(
glabel pak0f118674
/*  f118674:	27bdff80 */ 	addiu	$sp,$sp,-128
/*  f118678:	afb20038 */ 	sw	$s2,0x38($sp)
/*  f11867c:	00049600 */ 	sll	$s2,$a0,0x18
/*  f118680:	00127603 */ 	sra	$t6,$s2,0x18
/*  f118684:	afa40080 */ 	sw	$a0,0x80($sp)
/*  f118688:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11868c:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f118690:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f118694:	01c09025 */ 	or	$s2,$t6,$zero
/*  f118698:	afb10034 */ 	sw	$s1,0x34($sp)
/*  f11869c:	afb00030 */ 	sw	$s0,0x30($sp)
/*  f1186a0:	afa50084 */ 	sw	$a1,0x84($sp)
/*  f1186a4:	afa60088 */ 	sw	$a2,0x88($sp)
/*  f1186a8:	0fc45c25 */ 	jal	pakGetBodyLenByType
/*  f1186ac:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1186b0:	00122600 */ 	sll	$a0,$s2,0x18
/*  f1186b4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1186b8:	03002025 */ 	or	$a0,$t8,$zero
/*  f1186bc:	0fc45996 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f1186c0:	00402825 */ 	or	$a1,$v0,$zero
/*  f1186c4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f1186c8:	2419ffff */ 	addiu	$t9,$zero,-1
/*  f1186cc:	00044603 */ 	sra	$t0,$a0,0x18
/*  f1186d0:	afa20064 */ 	sw	$v0,0x64($sp)
/*  f1186d4:	afb90060 */ 	sw	$t9,0x60($sp)
/*  f1186d8:	00008025 */ 	or	$s0,$zero,$zero
/*  f1186dc:	afa00058 */ 	sw	$zero,0x58($sp)
/*  f1186e0:	afa00054 */ 	sw	$zero,0x54($sp)
/*  f1186e4:	0fc459f6 */ 	jal	pak0f1167d8
/*  f1186e8:	01002025 */ 	or	$a0,$t0,$zero
/*  f1186ec:	10400007 */ 	beqz	$v0,.L0f11870c
/*  f1186f0:	00125080 */ 	sll	$t2,$s2,0x2
/*  f1186f4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f1186f8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f1186fc:	0fc459f6 */ 	jal	pak0f1167d8
/*  f118700:	01202025 */ 	or	$a0,$t1,$zero
/*  f118704:	100000ad */ 	beqz	$zero,.L0f1189bc
/*  f118708:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11870c:
/*  f11870c:	01525023 */ 	subu	$t2,$t2,$s2
/*  f118710:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118714:	01525023 */ 	subu	$t2,$t2,$s2
/*  f118718:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11871c:	01525021 */ 	addu	$t2,$t2,$s2
/*  f118720:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118724:	01525023 */ 	subu	$t2,$t2,$s2
/*  f118728:	3c0b800a */ 	lui	$t3,%hi(g_Paks)
/*  f11872c:	256b2380 */ 	addiu	$t3,$t3,%lo(g_Paks)
/*  f118730:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118734:	014b8821 */ 	addu	$s1,$t2,$t3
/*  f118738:	8e2c02a0 */ 	lw	$t4,0x2a0($s1)
/*  f11873c:	11800033 */ 	beqz	$t4,.L0f11880c
/*  f118740:	00122600 */ 	sll	$a0,$s2,0x18
.L0f118744:
/*  f118744:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f118748:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11874c:	02002825 */ 	or	$a1,$s0,$zero
/*  f118750:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f118754:	27a60070 */ 	addiu	$a2,$sp,0x70
/*  f118758:	14400020 */ 	bnez	$v0,.L0f1187dc
/*  f11875c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f118760:	8fa30078 */ 	lw	$v1,0x78($sp)
/*  f118764:	000315c2 */ 	srl	$v0,$v1,0x17
/*  f118768:	304e0004 */ 	andi	$t6,$v0,0x4
/*  f11876c:	11c0000c */ 	beqz	$t6,.L0f1187a0
/*  f118770:	30490002 */ 	andi	$t1,$v0,0x2
/*  f118774:	8faf0064 */ 	lw	$t7,0x64($sp)
/*  f118778:	8e3902a0 */ 	lw	$t9,0x2a0($s1)
/*  f11877c:	020fc021 */ 	addu	$t8,$s0,$t7
/*  f118780:	2728ffe0 */ 	addiu	$t0,$t9,-32
/*  f118784:	0118082b */ 	sltu	$at,$t0,$t8
/*  f118788:	10200003 */ 	beqz	$at,.L0f118798
/*  f11878c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118790:	10000089 */ 	beqz	$zero,.L0f1189b8
/*  f118794:	2402000e */ 	addiu	$v0,$zero,0xe
.L0f118798:
/*  f118798:	1000001c */ 	beqz	$zero,.L0f11880c
/*  f11879c:	afb00060 */ 	sw	$s0,0x60($sp)
.L0f1187a0:
/*  f1187a0:	1120000c */ 	beqz	$t1,.L0f1187d4
/*  f1187a4:	306e0fff */ 	andi	$t6,$v1,0xfff
/*  f1187a8:	8faa0064 */ 	lw	$t2,0x64($sp)
/*  f1187ac:	306b0fff */ 	andi	$t3,$v1,0xfff
/*  f1187b0:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f1187b4:	154b0004 */ 	bne	$t2,$t3,.L0f1187c8
/*  f1187b8:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f1187bc:	afac0058 */ 	sw	$t4,0x58($sp)
/*  f1187c0:	10000012 */ 	beqz	$zero,.L0f11880c
/*  f1187c4:	afb00060 */ 	sw	$s0,0x60($sp)
.L0f1187c8:
/*  f1187c8:	afad0054 */ 	sw	$t5,0x54($sp)
/*  f1187cc:	1000000f */ 	beqz	$zero,.L0f11880c
/*  f1187d0:	afb00060 */ 	sw	$s0,0x60($sp)
.L0f1187d4:
/*  f1187d4:	10000009 */ 	beqz	$zero,.L0f1187fc
/*  f1187d8:	020e8021 */ 	addu	$s0,$s0,$t6
.L0f1187dc:
/*  f1187dc:	14410003 */ 	bne	$v0,$at,.L0f1187ec
/*  f1187e0:	00122600 */ 	sll	$a0,$s2,0x18
/*  f1187e4:	10000074 */ 	beqz	$zero,.L0f1189b8
/*  f1187e8:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1187ec:
/*  f1187ec:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f1187f0:	0fc45974 */ 	jal	pakGetBlockSize
/*  f1187f4:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1187f8:	02028021 */ 	addu	$s0,$s0,$v0
.L0f1187fc:
/*  f1187fc:	8e3902a0 */ 	lw	$t9,0x2a0($s1)
/*  f118800:	0219082b */ 	sltu	$at,$s0,$t9
/*  f118804:	5420ffcf */ 	bnezl	$at,.L0f118744
/*  f118808:	00122600 */ 	sll	$a0,$s2,0x18
.L0f11880c:
/*  f11880c:	52000011 */ 	beqzl	$s0,.L0f118854
/*  f118810:	8fab0060 */ 	lw	$t3,0x60($sp)
/*  f118814:	12000057 */ 	beqz	$s0,.L0f118974
/*  f118818:	00122600 */ 	sll	$a0,$s2,0x18
/*  f11881c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f118820:	0fc45ff0 */ 	jal	pakGetNumBytes
/*  f118824:	03002025 */ 	or	$a0,$t8,$zero
/*  f118828:	0202082b */ 	sltu	$at,$s0,$v0
/*  f11882c:	10200051 */ 	beqz	$at,.L0f118974
/*  f118830:	00122600 */ 	sll	$a0,$s2,0x18
/*  f118834:	00044603 */ 	sra	$t0,$a0,0x18
/*  f118838:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11883c:	01002025 */ 	or	$a0,$t0,$zero
/*  f118840:	2449ffff */ 	addiu	$t1,$v0,-1
/*  f118844:	01305024 */ 	and	$t2,$t1,$s0
/*  f118848:	5540004b */ 	bnezl	$t2,.L0f118978
/*  f11884c:	00124080 */ 	sll	$t0,$s2,0x2
/*  f118850:	8fab0060 */ 	lw	$t3,0x60($sp)
.L0f118854:
/*  f118854:	2401ffff */ 	addiu	$at,$zero,-1
/*  f118858:	00122600 */ 	sll	$a0,$s2,0x18
/*  f11885c:	15610003 */ 	bne	$t3,$at,.L0f11886c
/*  f118860:	00046603 */ 	sra	$t4,$a0,0x18
/*  f118864:	10000054 */ 	beqz	$zero,.L0f1189b8
/*  f118868:	2402000e */ 	addiu	$v0,$zero,0xe
.L0f11886c:
/*  f11886c:	8fad0088 */ 	lw	$t5,0x88($sp)
/*  f118870:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f118874:	afae0020 */ 	sw	$t6,0x20($sp)
/*  f118878:	01802025 */ 	or	$a0,$t4,$zero
/*  f11887c:	8fa50060 */ 	lw	$a1,0x60($sp)
/*  f118880:	8fa60084 */ 	lw	$a2,0x84($sp)
/*  f118884:	00003825 */ 	or	$a3,$zero,$zero
/*  f118888:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f11888c:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f118890:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f118894:	0fc46f15 */ 	jal	pakWriteFile
/*  f118898:	afad0014 */ 	sw	$t5,0x14($sp)
/*  f11889c:	14400033 */ 	bnez	$v0,.L0f11896c
/*  f1188a0:	8faf0054 */ 	lw	$t7,0x54($sp)
/*  f1188a4:	11e0000d */ 	beqz	$t7,.L0f1188dc
/*  f1188a8:	8faa0058 */ 	lw	$t2,0x58($sp)
/*  f1188ac:	8fb90060 */ 	lw	$t9,0x60($sp)
/*  f1188b0:	8fb80064 */ 	lw	$t8,0x64($sp)
/*  f1188b4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f1188b8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f1188bc:	03384021 */ 	addu	$t0,$t9,$t8
/*  f1188c0:	afa80050 */ 	sw	$t0,0x50($sp)
/*  f1188c4:	01202025 */ 	or	$a0,$t1,$zero
/*  f1188c8:	27a50050 */ 	addiu	$a1,$sp,0x50
/*  f1188cc:	0fc46538 */ 	jal	pakRepairAsBlank
/*  f1188d0:	00003025 */ 	or	$a2,$zero,$zero
/*  f1188d4:	10000038 */ 	beqz	$zero,.L0f1189b8
/*  f1188d8:	00001025 */ 	or	$v0,$zero,$zero
.L0f1188dc:
/*  f1188dc:	15400003 */ 	bnez	$t2,.L0f1188ec
/*  f1188e0:	8fab0054 */ 	lw	$t3,0x54($sp)
/*  f1188e4:	11600003 */ 	beqz	$t3,.L0f1188f4
/*  f1188e8:	00122600 */ 	sll	$a0,$s2,0x18
.L0f1188ec:
/*  f1188ec:	10000032 */ 	beqz	$zero,.L0f1189b8
/*  f1188f0:	00001025 */ 	or	$v0,$zero,$zero
.L0f1188f4:
/*  f1188f4:	00046603 */ 	sra	$t4,$a0,0x18
/*  f1188f8:	01802025 */ 	or	$a0,$t4,$zero
/*  f1188fc:	0fc45c25 */ 	jal	pakGetBodyLenByType
/*  f118900:	8fa50084 */ 	lw	$a1,0x84($sp)
/*  f118904:	00122600 */ 	sll	$a0,$s2,0x18
/*  f118908:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11890c:	01a02025 */ 	or	$a0,$t5,$zero
/*  f118910:	0fc45996 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f118914:	00402825 */ 	or	$a1,$v0,$zero
/*  f118918:	8fae0060 */ 	lw	$t6,0x60($sp)
/*  f11891c:	00122600 */ 	sll	$a0,$s2,0x18
/*  f118920:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f118924:	24180001 */ 	addiu	$t8,$zero,0x1
/*  f118928:	01c22821 */ 	addu	$a1,$t6,$v0
/*  f11892c:	afa50060 */ 	sw	$a1,0x60($sp)
/*  f118930:	afb80020 */ 	sw	$t8,0x20($sp)
/*  f118934:	03202025 */ 	or	$a0,$t9,$zero
/*  f118938:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f11893c:	00003825 */ 	or	$a3,$zero,$zero
/*  f118940:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f118944:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f118948:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f11894c:	0fc46f15 */ 	jal	pakWriteFile
/*  f118950:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f118954:	14400003 */ 	bnez	$v0,.L0f118964
/*  f118958:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11895c:	10000016 */ 	beqz	$zero,.L0f1189b8
/*  f118960:	00001025 */ 	or	$v0,$zero,$zero
.L0f118964:
/*  f118964:	10000014 */ 	beqz	$zero,.L0f1189b8
/*  f118968:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f11896c:
/*  f11896c:	10000012 */ 	beqz	$zero,.L0f1189b8
/*  f118970:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f118974:
/*  f118974:	00124080 */ 	sll	$t0,$s2,0x2
.L0f118978:
/*  f118978:	01124023 */ 	subu	$t0,$t0,$s2
/*  f11897c:	00084080 */ 	sll	$t0,$t0,0x2
/*  f118980:	01124023 */ 	subu	$t0,$t0,$s2
/*  f118984:	00084080 */ 	sll	$t0,$t0,0x2
/*  f118988:	01124021 */ 	addu	$t0,$t0,$s2
/*  f11898c:	00084080 */ 	sll	$t0,$t0,0x2
/*  f118990:	01124023 */ 	subu	$t0,$t0,$s2
/*  f118994:	3c09800a */ 	lui	$t1,%hi(g_Paks)
/*  f118998:	25292380 */ 	addiu	$t1,$t1,%lo(g_Paks)
/*  f11899c:	00084080 */ 	sll	$t0,$t0,0x2
/*  f1189a0:	01098821 */ 	addu	$s1,$t0,$t1
/*  f1189a4:	240a0010 */ 	addiu	$t2,$zero,0x10
/*  f1189a8:	240b0002 */ 	addiu	$t3,$zero,0x2
/*  f1189ac:	ae2a0010 */ 	sw	$t2,0x10($s1)
/*  f1189b0:	ae2b0000 */ 	sw	$t3,0x0($s1)
/*  f1189b4:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f1189b8:
/*  f1189b8:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f1189bc:
/*  f1189bc:	8fb00030 */ 	lw	$s0,0x30($sp)
/*  f1189c0:	8fb10034 */ 	lw	$s1,0x34($sp)
/*  f1189c4:	8fb20038 */ 	lw	$s2,0x38($sp)
/*  f1189c8:	03e00008 */ 	jr	$ra
/*  f1189cc:	27bd0080 */ 	addiu	$sp,$sp,0x80
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f118674
/*  f118674:	27bdff88 */ 	addiu	$sp,$sp,-120
/*  f118678:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f11867c:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f118680:	00117603 */ 	sra	$t6,$s1,0x18
/*  f118684:	afa40078 */ 	sw	$a0,0x78($sp)
/*  f118688:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11868c:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f118690:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f118694:	01c08825 */ 	or	$s1,$t6,$zero
/*  f118698:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f11869c:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f1186a0:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f1186a4:	afa5007c */ 	sw	$a1,0x7c($sp)
/*  f1186a8:	afa60080 */ 	sw	$a2,0x80($sp)
/*  f1186ac:	0fc45c05 */ 	jal	pakGetBodyLenByType
/*  f1186b0:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1186b4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1186b8:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1186bc:	03002025 */ 	or	$a0,$t8,$zero
/*  f1186c0:	0fc45976 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f1186c4:	00402825 */ 	or	$a1,$v0,$zero
/*  f1186c8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1186cc:	2419ffff */ 	addiu	$t9,$zero,-1
/*  f1186d0:	00044603 */ 	sra	$t0,$a0,0x18
/*  f1186d4:	00409825 */ 	or	$s3,$v0,$zero
/*  f1186d8:	afb90058 */ 	sw	$t9,0x58($sp)
/*  f1186dc:	00008025 */ 	or	$s0,$zero,$zero
/*  f1186e0:	afa00050 */ 	sw	$zero,0x50($sp)
/*  f1186e4:	0fc459d6 */ 	jal	pak0f1167d8
/*  f1186e8:	01002025 */ 	or	$a0,$t0,$zero
/*  f1186ec:	10400007 */ 	beqz	$v0,.L0f11870c
/*  f1186f0:	00115080 */ 	sll	$t2,$s1,0x2
/*  f1186f4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1186f8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f1186fc:	0fc459d6 */ 	jal	pak0f1167d8
/*  f118700:	01202025 */ 	or	$a0,$t1,$zero
/*  f118704:	10000097 */ 	beqz	$zero,.L0f118964
/*  f118708:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11870c:
/*  f11870c:	01515023 */ 	subu	$t2,$t2,$s1
/*  f118710:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118714:	01515023 */ 	subu	$t2,$t2,$s1
/*  f118718:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11871c:	01515021 */ 	addu	$t2,$t2,$s1
/*  f118720:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118724:	01515023 */ 	subu	$t2,$t2,$s1
/*  f118728:	3c0b800a */ 	lui	$t3,%hi(g_Paks)
/*  f11872c:	256b2380 */ 	addiu	$t3,$t3,%lo(g_Paks)
/*  f118730:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118734:	014b9021 */ 	addu	$s2,$t2,$t3
/*  f118738:	8e4c02a0 */ 	lw	$t4,0x2a0($s2)
/*  f11873c:	1180002d */ 	beqz	$t4,.L0f1187f4
/*  f118740:	00112600 */ 	sll	$a0,$s1,0x18
.L0f118744:
/*  f118744:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f118748:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11874c:	02002825 */ 	or	$a1,$s0,$zero
/*  f118750:	0fc45d28 */ 	jal	pakReadHeaderAtOffset
/*  f118754:	27a60068 */ 	addiu	$a2,$sp,0x68
/*  f118758:	1440001a */ 	bnez	$v0,.L0f1187c4
/*  f11875c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f118760:	8fa30070 */ 	lw	$v1,0x70($sp)
/*  f118764:	000315c2 */ 	srl	$v0,$v1,0x17
/*  f118768:	304e0004 */ 	andi	$t6,$v0,0x4
/*  f11876c:	11c0000b */ 	beqz	$t6,.L0f11879c
/*  f118770:	30480002 */ 	andi	$t0,$v0,0x2
/*  f118774:	8e5802a0 */ 	lw	$t8,0x2a0($s2)
/*  f118778:	02137821 */ 	addu	$t7,$s0,$s3
/*  f11877c:	2719ffe0 */ 	addiu	$t9,$t8,-32
/*  f118780:	032f082b */ 	sltu	$at,$t9,$t7
/*  f118784:	10200003 */ 	beqz	$at,.L0f118794
/*  f118788:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11878c:	10000074 */ 	beqz	$zero,.L0f118960
/*  f118790:	2402000e */ 	addiu	$v0,$zero,0xe
.L0f118794:
/*  f118794:	10000017 */ 	beqz	$zero,.L0f1187f4
/*  f118798:	afb00058 */ 	sw	$s0,0x58($sp)
.L0f11879c:
/*  f11879c:	11000006 */ 	beqz	$t0,.L0f1187b8
/*  f1187a0:	30690fff */ 	andi	$t1,$v1,0xfff
/*  f1187a4:	16690004 */ 	bne	$s3,$t1,.L0f1187b8
/*  f1187a8:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f1187ac:	afaa0050 */ 	sw	$t2,0x50($sp)
/*  f1187b0:	10000010 */ 	beqz	$zero,.L0f1187f4
/*  f1187b4:	afb00058 */ 	sw	$s0,0x58($sp)
.L0f1187b8:
/*  f1187b8:	306b0fff */ 	andi	$t3,$v1,0xfff
/*  f1187bc:	10000009 */ 	beqz	$zero,.L0f1187e4
/*  f1187c0:	020b8021 */ 	addu	$s0,$s0,$t3
.L0f1187c4:
/*  f1187c4:	14410003 */ 	bne	$v0,$at,.L0f1187d4
/*  f1187c8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1187cc:	10000064 */ 	beqz	$zero,.L0f118960
/*  f1187d0:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1187d4:
/*  f1187d4:	00046603 */ 	sra	$t4,$a0,0x18
/*  f1187d8:	0fc45954 */ 	jal	pakGetBlockSize
/*  f1187dc:	01802025 */ 	or	$a0,$t4,$zero
/*  f1187e0:	02028021 */ 	addu	$s0,$s0,$v0
.L0f1187e4:
/*  f1187e4:	8e4d02a0 */ 	lw	$t5,0x2a0($s2)
/*  f1187e8:	020d082b */ 	sltu	$at,$s0,$t5
/*  f1187ec:	5420ffd5 */ 	bnezl	$at,.L0f118744
/*  f1187f0:	00112600 */ 	sll	$a0,$s1,0x18
.L0f1187f4:
/*  f1187f4:	52000011 */ 	beqzl	$s0,.L0f11883c
/*  f1187f8:	8fa80058 */ 	lw	$t0,0x58($sp)
/*  f1187fc:	12000047 */ 	beqz	$s0,.L0f11891c
/*  f118800:	00112600 */ 	sll	$a0,$s1,0x18
/*  f118804:	00047603 */ 	sra	$t6,$a0,0x18
/*  f118808:	0fc45ff0 */ 	jal	pakGetNumBytes
/*  f11880c:	01c02025 */ 	or	$a0,$t6,$zero
/*  f118810:	0202082b */ 	sltu	$at,$s0,$v0
/*  f118814:	10200041 */ 	beqz	$at,.L0f11891c
/*  f118818:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11881c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f118820:	0fc45954 */ 	jal	pakGetBlockSize
/*  f118824:	03002025 */ 	or	$a0,$t8,$zero
/*  f118828:	244fffff */ 	addiu	$t7,$v0,-1
/*  f11882c:	01f0c824 */ 	and	$t9,$t7,$s0
/*  f118830:	5720003b */ 	bnezl	$t9,.L0f118920
/*  f118834:	00114880 */ 	sll	$t1,$s1,0x2
/*  f118838:	8fa80058 */ 	lw	$t0,0x58($sp)
.L0f11883c:
/*  f11883c:	2401ffff */ 	addiu	$at,$zero,-1
/*  f118840:	00112600 */ 	sll	$a0,$s1,0x18
/*  f118844:	15010003 */ 	bne	$t0,$at,.L0f118854
/*  f118848:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11884c:	10000044 */ 	beqz	$zero,.L0f118960
/*  f118850:	2402000e */ 	addiu	$v0,$zero,0xe
.L0f118854:
/*  f118854:	8faa0080 */ 	lw	$t2,0x80($sp)
/*  f118858:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f11885c:	afab0020 */ 	sw	$t3,0x20($sp)
/*  f118860:	01202025 */ 	or	$a0,$t1,$zero
/*  f118864:	8fa50058 */ 	lw	$a1,0x58($sp)
/*  f118868:	8fa6007c */ 	lw	$a2,0x7c($sp)
/*  f11886c:	00003825 */ 	or	$a3,$zero,$zero
/*  f118870:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f118874:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f118878:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11887c:	0fc46e75 */ 	jal	pakWriteFile
/*  f118880:	afaa0014 */ 	sw	$t2,0x14($sp)
/*  f118884:	14400023 */ 	bnez	$v0,.L0f118914
/*  f118888:	8fac0050 */ 	lw	$t4,0x50($sp)
/*  f11888c:	11800003 */ 	beqz	$t4,.L0f11889c
/*  f118890:	00112600 */ 	sll	$a0,$s1,0x18
/*  f118894:	10000032 */ 	beqz	$zero,.L0f118960
/*  f118898:	00001025 */ 	or	$v0,$zero,$zero
.L0f11889c:
/*  f11889c:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f1188a0:	01a02025 */ 	or	$a0,$t5,$zero
/*  f1188a4:	0fc45c05 */ 	jal	pakGetBodyLenByType
/*  f1188a8:	8fa5007c */ 	lw	$a1,0x7c($sp)
/*  f1188ac:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1188b0:	00047603 */ 	sra	$t6,$a0,0x18
/*  f1188b4:	01c02025 */ 	or	$a0,$t6,$zero
/*  f1188b8:	0fc45976 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f1188bc:	00402825 */ 	or	$a1,$v0,$zero
/*  f1188c0:	8fb80058 */ 	lw	$t8,0x58($sp)
/*  f1188c4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1188c8:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f1188cc:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f1188d0:	03022821 */ 	addu	$a1,$t8,$v0
/*  f1188d4:	afa50058 */ 	sw	$a1,0x58($sp)
/*  f1188d8:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f1188dc:	03202025 */ 	or	$a0,$t9,$zero
/*  f1188e0:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f1188e4:	00003825 */ 	or	$a3,$zero,$zero
/*  f1188e8:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f1188ec:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f1188f0:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f1188f4:	0fc46e75 */ 	jal	pakWriteFile
/*  f1188f8:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f1188fc:	14400003 */ 	bnez	$v0,.L0f11890c
/*  f118900:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118904:	10000016 */ 	beqz	$zero,.L0f118960
/*  f118908:	00001025 */ 	or	$v0,$zero,$zero
.L0f11890c:
/*  f11890c:	10000014 */ 	beqz	$zero,.L0f118960
/*  f118910:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f118914:
/*  f118914:	10000012 */ 	beqz	$zero,.L0f118960
/*  f118918:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f11891c:
/*  f11891c:	00114880 */ 	sll	$t1,$s1,0x2
.L0f118920:
/*  f118920:	01314823 */ 	subu	$t1,$t1,$s1
/*  f118924:	00094880 */ 	sll	$t1,$t1,0x2
/*  f118928:	01314823 */ 	subu	$t1,$t1,$s1
/*  f11892c:	00094880 */ 	sll	$t1,$t1,0x2
/*  f118930:	01314821 */ 	addu	$t1,$t1,$s1
/*  f118934:	00094880 */ 	sll	$t1,$t1,0x2
/*  f118938:	01314823 */ 	subu	$t1,$t1,$s1
/*  f11893c:	3c0a800a */ 	lui	$t2,%hi(g_Paks)
/*  f118940:	254a2380 */ 	addiu	$t2,$t2,%lo(g_Paks)
/*  f118944:	00094880 */ 	sll	$t1,$t1,0x2
/*  f118948:	012a9021 */ 	addu	$s2,$t1,$t2
/*  f11894c:	240b0010 */ 	addiu	$t3,$zero,0x10
/*  f118950:	240c0002 */ 	addiu	$t4,$zero,0x2
/*  f118954:	ae4b0010 */ 	sw	$t3,0x10($s2)
/*  f118958:	ae4c0000 */ 	sw	$t4,0x0($s2)
/*  f11895c:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f118960:
/*  f118960:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f118964:
/*  f118964:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f118968:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f11896c:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f118970:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f118974:	03e00008 */ 	jr	$ra
/*  f118978:	27bd0078 */ 	addiu	$sp,$sp,0x78
);
#else
GLOBAL_ASM(
glabel pak0f118674
/*  f112a60:	27bdff88 */ 	addiu	$sp,$sp,-120
/*  f112a64:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f112a68:	00049600 */ 	sll	$s2,$a0,0x18
/*  f112a6c:	00127603 */ 	sra	$t6,$s2,0x18
/*  f112a70:	afa40078 */ 	sw	$a0,0x78($sp)
/*  f112a74:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f112a78:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f112a7c:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f112a80:	01c09025 */ 	or	$s2,$t6,$zero
/*  f112a84:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f112a88:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f112a8c:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f112a90:	afa5007c */ 	sw	$a1,0x7c($sp)
/*  f112a94:	afa60080 */ 	sw	$a2,0x80($sp)
/*  f112a98:	0fc444f9 */ 	jal	pakGetBodyLenByType
/*  f112a9c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f112aa0:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112aa4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f112aa8:	03002025 */ 	or	$a0,$t8,$zero
/*  f112aac:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f112ab0:	00402825 */ 	or	$a1,$v0,$zero
/*  f112ab4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112ab8:	2419ffff */ 	addiu	$t9,$zero,-1
/*  f112abc:	00044603 */ 	sra	$t0,$a0,0x18
/*  f112ac0:	00409825 */ 	or	$s3,$v0,$zero
/*  f112ac4:	afb90058 */ 	sw	$t9,0x58($sp)
/*  f112ac8:	00008025 */ 	or	$s0,$zero,$zero
/*  f112acc:	afa00050 */ 	sw	$zero,0x50($sp)
/*  f112ad0:	0fc442e7 */ 	jal	pak0f1167d8
/*  f112ad4:	01002025 */ 	or	$a0,$t0,$zero
/*  f112ad8:	10400007 */ 	beqz	$v0,.NB0f112af8
/*  f112adc:	00125080 */ 	sll	$t2,$s2,0x2
/*  f112ae0:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112ae4:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f112ae8:	0fc442e7 */ 	jal	pak0f1167d8
/*  f112aec:	01202025 */ 	or	$a0,$t1,$zero
/*  f112af0:	10000087 */ 	beqz	$zero,.NB0f112d10
/*  f112af4:	8fbf003c */ 	lw	$ra,0x3c($sp)
.NB0f112af8:
/*  f112af8:	01525023 */ 	subu	$t2,$t2,$s2
/*  f112afc:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f112b00:	01525023 */ 	subu	$t2,$t2,$s2
/*  f112b04:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f112b08:	01525021 */ 	addu	$t2,$t2,$s2
/*  f112b0c:	3c0b800a */ 	lui	$t3,0x800a
/*  f112b10:	256b6870 */ 	addiu	$t3,$t3,0x6870
/*  f112b14:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f112b18:	014b8821 */ 	addu	$s1,$t2,$t3
/*  f112b1c:	8e2c02a0 */ 	lw	$t4,0x2a0($s1)
/*  f112b20:	11800021 */ 	beqz	$t4,.NB0f112ba8
/*  f112b24:	00122600 */ 	sll	$a0,$s2,0x18
.NB0f112b28:
/*  f112b28:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f112b2c:	01a02025 */ 	or	$a0,$t5,$zero
/*  f112b30:	02002825 */ 	or	$a1,$s0,$zero
/*  f112b34:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f112b38:	27a60068 */ 	addiu	$a2,$sp,0x68
/*  f112b3c:	14400012 */ 	bnez	$v0,.NB0f112b88
/*  f112b40:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112b44:	8fa30070 */ 	lw	$v1,0x70($sp)
/*  f112b48:	000315c2 */ 	srl	$v0,$v1,0x17
/*  f112b4c:	304e0004 */ 	andi	$t6,$v0,0x4
/*  f112b50:	11c00003 */ 	beqz	$t6,.NB0f112b60
/*  f112b54:	304f0002 */ 	andi	$t7,$v0,0x2
/*  f112b58:	10000013 */ 	beqz	$zero,.NB0f112ba8
/*  f112b5c:	afb00058 */ 	sw	$s0,0x58($sp)
.NB0f112b60:
/*  f112b60:	11e00006 */ 	beqz	$t7,.NB0f112b7c
/*  f112b64:	30780fff */ 	andi	$t8,$v1,0xfff
/*  f112b68:	16780004 */ 	bne	$s3,$t8,.NB0f112b7c
/*  f112b6c:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f112b70:	afb90050 */ 	sw	$t9,0x50($sp)
/*  f112b74:	1000000c */ 	beqz	$zero,.NB0f112ba8
/*  f112b78:	afb00058 */ 	sw	$s0,0x58($sp)
.NB0f112b7c:
/*  f112b7c:	30680fff */ 	andi	$t0,$v1,0xfff
/*  f112b80:	10000005 */ 	beqz	$zero,.NB0f112b98
/*  f112b84:	02088021 */ 	addu	$s0,$s0,$t0
.NB0f112b88:
/*  f112b88:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f112b8c:	0fc4428c */ 	jal	pakGetBlockSize
/*  f112b90:	01202025 */ 	or	$a0,$t1,$zero
/*  f112b94:	02028021 */ 	addu	$s0,$s0,$v0
.NB0f112b98:
/*  f112b98:	8e2a02a0 */ 	lw	$t2,0x2a0($s1)
/*  f112b9c:	020a082b */ 	sltu	$at,$s0,$t2
/*  f112ba0:	5420ffe1 */ 	bnezl	$at,.NB0f112b28
/*  f112ba4:	00122600 */ 	sll	$a0,$s2,0x18
.NB0f112ba8:
/*  f112ba8:	52000011 */ 	beqzl	$s0,.NB0f112bf0
/*  f112bac:	8faf0058 */ 	lw	$t7,0x58($sp)
/*  f112bb0:	12000047 */ 	beqz	$s0,.NB0f112cd0
/*  f112bb4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112bb8:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f112bbc:	0fc448fb */ 	jal	pakGetNumBytes
/*  f112bc0:	01602025 */ 	or	$a0,$t3,$zero
/*  f112bc4:	0202082b */ 	sltu	$at,$s0,$v0
/*  f112bc8:	10200041 */ 	beqz	$at,.NB0f112cd0
/*  f112bcc:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112bd0:	00046603 */ 	sra	$t4,$a0,0x18
/*  f112bd4:	0fc4428c */ 	jal	pakGetBlockSize
/*  f112bd8:	01802025 */ 	or	$a0,$t4,$zero
/*  f112bdc:	244dffff */ 	addiu	$t5,$v0,-1
/*  f112be0:	01b07024 */ 	and	$t6,$t5,$s0
/*  f112be4:	55c0003b */ 	bnezl	$t6,.NB0f112cd4
/*  f112be8:	0012c080 */ 	sll	$t8,$s2,0x2
/*  f112bec:	8faf0058 */ 	lw	$t7,0x58($sp)
.NB0f112bf0:
/*  f112bf0:	2401ffff */ 	addiu	$at,$zero,-1
/*  f112bf4:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112bf8:	15e10003 */ 	bne	$t7,$at,.NB0f112c08
/*  f112bfc:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f112c00:	10000042 */ 	beqz	$zero,.NB0f112d0c
/*  f112c04:	2402000e */ 	addiu	$v0,$zero,0xe
.NB0f112c08:
/*  f112c08:	8fb90080 */ 	lw	$t9,0x80($sp)
/*  f112c0c:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f112c10:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f112c14:	03002025 */ 	or	$a0,$t8,$zero
/*  f112c18:	8fa50058 */ 	lw	$a1,0x58($sp)
/*  f112c1c:	8fa6007c */ 	lw	$a2,0x7c($sp)
/*  f112c20:	00003825 */ 	or	$a3,$zero,$zero
/*  f112c24:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f112c28:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f112c2c:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f112c30:	0fc456f6 */ 	jal	pakWriteFile
/*  f112c34:	afb90014 */ 	sw	$t9,0x14($sp)
/*  f112c38:	14400023 */ 	bnez	$v0,.NB0f112cc8
/*  f112c3c:	8fa90050 */ 	lw	$t1,0x50($sp)
/*  f112c40:	11200003 */ 	beqz	$t1,.NB0f112c50
/*  f112c44:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112c48:	10000030 */ 	beqz	$zero,.NB0f112d0c
/*  f112c4c:	00001025 */ 	or	$v0,$zero,$zero
.NB0f112c50:
/*  f112c50:	00045603 */ 	sra	$t2,$a0,0x18
/*  f112c54:	01402025 */ 	or	$a0,$t2,$zero
/*  f112c58:	0fc444f9 */ 	jal	pakGetBodyLenByType
/*  f112c5c:	8fa5007c */ 	lw	$a1,0x7c($sp)
/*  f112c60:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112c64:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f112c68:	01602025 */ 	or	$a0,$t3,$zero
/*  f112c6c:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f112c70:	00402825 */ 	or	$a1,$v0,$zero
/*  f112c74:	8fac0058 */ 	lw	$t4,0x58($sp)
/*  f112c78:	00122600 */ 	sll	$a0,$s2,0x18
/*  f112c7c:	00047603 */ 	sra	$t6,$a0,0x18
/*  f112c80:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f112c84:	01822821 */ 	addu	$a1,$t4,$v0
/*  f112c88:	afa50058 */ 	sw	$a1,0x58($sp)
/*  f112c8c:	afaf0020 */ 	sw	$t7,0x20($sp)
/*  f112c90:	01c02025 */ 	or	$a0,$t6,$zero
/*  f112c94:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f112c98:	00003825 */ 	or	$a3,$zero,$zero
/*  f112c9c:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f112ca0:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f112ca4:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f112ca8:	0fc456f6 */ 	jal	pakWriteFile
/*  f112cac:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f112cb0:	14400003 */ 	bnez	$v0,.NB0f112cc0
/*  f112cb4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f112cb8:	10000014 */ 	beqz	$zero,.NB0f112d0c
/*  f112cbc:	00001025 */ 	or	$v0,$zero,$zero
.NB0f112cc0:
/*  f112cc0:	10000012 */ 	beqz	$zero,.NB0f112d0c
/*  f112cc4:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f112cc8:
/*  f112cc8:	10000010 */ 	beqz	$zero,.NB0f112d0c
/*  f112ccc:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f112cd0:
/*  f112cd0:	0012c080 */ 	sll	$t8,$s2,0x2
.NB0f112cd4:
/*  f112cd4:	0312c023 */ 	subu	$t8,$t8,$s2
/*  f112cd8:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f112cdc:	0312c023 */ 	subu	$t8,$t8,$s2
/*  f112ce0:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f112ce4:	0312c021 */ 	addu	$t8,$t8,$s2
/*  f112ce8:	3c19800a */ 	lui	$t9,0x800a
/*  f112cec:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f112cf0:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f112cf4:	03198821 */ 	addu	$s1,$t8,$t9
/*  f112cf8:	24080010 */ 	addiu	$t0,$zero,0x10
/*  f112cfc:	24090002 */ 	addiu	$t1,$zero,0x2
/*  f112d00:	ae280010 */ 	sw	$t0,0x10($s1)
/*  f112d04:	ae290000 */ 	sw	$t1,0x0($s1)
/*  f112d08:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f112d0c:
/*  f112d0c:	8fbf003c */ 	lw	$ra,0x3c($sp)
.NB0f112d10:
/*  f112d10:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f112d14:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f112d18:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f112d1c:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f112d20:	03e00008 */ 	jr	$ra
/*  f112d24:	27bd0078 */ 	addiu	$sp,$sp,0x78
);
#endif

// Mismatch because goal calculates s1 (address of g_Paks[device]) twice.
// Mine also does it twice using the u32 cast but stores the second one in v1.
// Removing the cast causes mine to calculate s1 only once and reuse it.
//u32 pak0f118674(s8 device, u32 filetype, u8 *olddata)
//{
//	u32 sp112[4];
//	u32 sp108;
//	u32 value;
//	u32 sp100 = pakGetAlignedFileLenByBodyLen(device, pakGetBodyLenByType(device, filetype));
//	s32 sp96 = -1;
//	u32 s0 = 0;
//	u32 sp88 = 0;
//#if VERSION >= VERSION_NTSC_FINAL
//	u32 sp84 = 0;
//	u32 sp80;
//#endif
//
//	// 6e4
//	if (pak0f1167d8(device)) {
//		return pak0f1167d8(device);
//	}
//
//	// 73c
//	while (s0 < g_Paks[device].numbytes) {
//		value = pakReadHeaderAtOffset(device, s0, sp112);
//
//		// 758
//		if (value == 0) {
//			// 76c
//			if ((sp112[2] >> 0x17) & 4) {
//				if (g_Paks[device].numbytes - 0x20 < s0 + sp100) {
//					return 14;
//				}
//
//				sp96 = s0;
//				break;
//			}
//
//			// 7a0
//			if ((sp112[2] >> 0x17) & 2) {
//				if (sp100 == (sp112[2] & 0xfff)) {
//					sp88 = 1;
//					sp96 = s0;
//					break;
//				}
//
//#if VERSION >= VERSION_NTSC_FINAL
//				sp84 = 1;
//				sp96 = s0;
//				break;
//#endif
//			}
//
//			s0 += sp112[2] & 0xfff;
//		} else /*7dc*/ if (value == 1) {
//			return 1;
//		} else {
//			// 7ec
//			s0 += pakGetBlockSize(device);
//		}
//	}
//
//	// 80c
//	if (s0 == 0 ||
//			(s0 && s0 < pakGetNumBytes(device) && (pakGetBlockSize(device) - 1 & s0) == 0)) {
//		// 854
//		if (sp96 == -1) {
//			return 14;
//		}
//
//		// 86c
//		if (pakWriteFile(device, sp96, filetype, NULL, 0, olddata, NULL, 0, 1) == 0) {
//#if VERSION >= VERSION_NTSC_FINAL
//			// 8a4
//			if (sp84) {
//				sp80 = sp96 + sp100;
//				pakRepairAsBlank(device, &sp80, 0);
//				return 0;
//			}
//
//			// 8dc
//			if (sp88 || sp84) {
//				return 0;
//			}
//#else
//			if (sp88) {
//				return 0;
//			}
//#endif
//
//			// 8f4
//			sp96 += pakGetAlignedFileLenByBodyLen(device, pakGetBodyLenByType(device, filetype));
//
//			if (pakWriteFile(device, sp96, PAKFILETYPE_TERMINATOR, NULL, 0, NULL, NULL, 0, 1) == 0) {
//				return 0;
//			}
//
//			return 4;
//		}
//
//		return 4;
//	}
//
//	g_Paks[(u32)device].unk010 = 16;
//	g_Paks[(u32)device].unk000 = 2;
//
//	return 4;
//}

void pak0f1189d0(void)
{
	// empty
}

#if VERSION >= VERSION_NTSC_1_0
void pakInitAll(void)
{
	u8 prevvalue = g_Vars.paksconnected;
	s8 i;

	g_Vars.unk0004e4 = 0;

	for (i = 0; i < 5; i++) {
		pakInit(i);
	}

	for (i = 0; i < 5; i++) {
#if VERSION >= VERSION_PAL_FINAL
		pak0f11a32c(i, 7, 2049, "pak.c");
#elif VERSION >= VERSION_NTSC_FINAL
		pak0f11a32c(i, 7, 2049, "pak/pak.c");
#else
		pak0f11a32c(i, 7, 2016, "pak.c");
#endif
	}

	pakProbeEeprom();
	joyRecordPfsState(0x10);

	g_Vars.paksconnected = 0x10;

	pak0f1169c8(SAVEDEVICE_GAMEPAK, 1);
	bossfileLoadFull();

	gamefileLoadDefaults(&g_GameFile);
	gamefileApplyOptions(&g_GameFile);

	g_GameFileGuid.deviceserial = 0;
	g_Vars.unk0004e4 = 0xf5;
	g_Vars.paksconnected = prevvalue;
}
#else
GLOBAL_ASM(
glabel pakInitAll
/*  f112d30:	27bdffd0 */ 	addiu	$sp,$sp,-48
/*  f112d34:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f112d38:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f112d3c:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f112d40:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f112d44:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f112d48:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f112d4c:	00008025 */ 	or	$s0,$zero,$zero
/*  f112d50:	00102600 */ 	sll	$a0,$s0,0x18
.NB0f112d54:
/*  f112d54:	00047603 */ 	sra	$t6,$a0,0x18
/*  f112d58:	0fc450bb */ 	jal	pakInit
/*  f112d5c:	01c02025 */ 	or	$a0,$t6,$zero
/*  f112d60:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f112d64:	00107e00 */ 	sll	$t7,$s0,0x18
/*  f112d68:	000f8603 */ 	sra	$s0,$t7,0x18
/*  f112d6c:	2a010005 */ 	slti	$at,$s0,0x5
/*  f112d70:	5420fff8 */ 	bnezl	$at,.NB0f112d54
/*  f112d74:	00102600 */ 	sll	$a0,$s0,0x18
/*  f112d78:	2419001f */ 	addiu	$t9,$zero,0x1f
/*  f112d7c:	3c01800a */ 	lui	$at,0x800a
/*  f112d80:	3c117f1b */ 	lui	$s1,0x7f1b
/*  f112d84:	a039eb91 */ 	sb	$t9,-0x146f($at)
/*  f112d88:	2631dc10 */ 	addiu	$s1,$s1,-9200
/*  f112d8c:	00008025 */ 	or	$s0,$zero,$zero
/*  f112d90:	00102600 */ 	sll	$a0,$s0,0x18
.NB0f112d94:
/*  f112d94:	00044603 */ 	sra	$t0,$a0,0x18
/*  f112d98:	01002025 */ 	or	$a0,$t0,$zero
/*  f112d9c:	24050007 */ 	addiu	$a1,$zero,0x7
/*  f112da0:	24060789 */ 	addiu	$a2,$zero,0x789
/*  f112da4:	0fc4507d */ 	jal	pak0f11a32c
/*  f112da8:	02203825 */ 	or	$a3,$s1,$zero
/*  f112dac:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f112db0:	00104e00 */ 	sll	$t1,$s0,0x18
/*  f112db4:	00098603 */ 	sra	$s0,$t1,0x18
/*  f112db8:	2a010005 */ 	slti	$at,$s0,0x5
/*  f112dbc:	5420fff5 */ 	bnezl	$at,.NB0f112d94
/*  f112dc0:	00102600 */ 	sll	$a0,$s0,0x18
/*  f112dc4:	0fc4608d */ 	jal	pakProbeEeprom
/*  f112dc8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f112dcc:	00008825 */ 	or	$s1,$zero,$zero
/*  f112dd0:	24140009 */ 	addiu	$s4,$zero,0x9
/*  f112dd4:	240b0001 */ 	addiu	$t3,$zero,0x1
.NB0f112dd8:
/*  f112dd8:	022b9004 */ 	sllv	$s2,$t3,$s1
/*  f112ddc:	02402825 */ 	or	$a1,$s2,$zero
/*  f112de0:	0fc444f9 */ 	jal	pakGetBodyLenByType
/*  f112de4:	00002025 */ 	or	$a0,$zero,$zero
/*  f112de8:	00409825 */ 	or	$s3,$v0,$zero
/*  f112dec:	0fc44528 */ 	jal	pak0f1114a0nb
/*  f112df0:	02402025 */ 	or	$a0,$s2,$zero
/*  f112df4:	00102600 */ 	sll	$a0,$s0,0x18
/*  f112df8:	00046603 */ 	sra	$t4,$a0,0x18
/*  f112dfc:	01802025 */ 	or	$a0,$t4,$zero
/*  f112e00:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f112e04:	02602825 */ 	or	$a1,$s3,$zero
/*  f112e08:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f112e0c:	5634fff2 */ 	bnel	$s1,$s4,.NB0f112dd8
/*  f112e10:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f112e14:	0fc45920 */ 	jal	pakExecuteDebugOperations
/*  f112e18:	00000000 */ 	sll	$zero,$zero,0x0
/*  f112e1c:	0fc42ace */ 	jal	bossfileLoadFull
/*  f112e20:	00000000 */ 	sll	$zero,$zero,0x0
/*  f112e24:	3c10800a */ 	lui	$s0,0x800a
/*  f112e28:	261066f0 */ 	addiu	$s0,$s0,0x66f0
/*  f112e2c:	0fc426fe */ 	jal	gamefileLoadDefaults
/*  f112e30:	02002025 */ 	or	$a0,$s0,$zero
/*  f112e34:	0fc425d9 */ 	jal	gamefileApplyOptions
/*  f112e38:	02002025 */ 	or	$a0,$s0,$zero
/*  f112e3c:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f112e40:	3c01800a */ 	lui	$at,0x800a
/*  f112e44:	a42067b4 */ 	sh	$zero,0x67b4($at)
/*  f112e48:	3c01800a */ 	lui	$at,0x800a
/*  f112e4c:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f112e50:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f112e54:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f112e58:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f112e5c:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f112e60:	a020eb91 */ 	sb	$zero,-0x146f($at)
/*  f112e64:	03e00008 */ 	jr	$ra
/*  f112e68:	27bd0030 */ 	addiu	$sp,$sp,0x30
);
#endif

const char var7f1b3e3c[] = "Pak %d -> Pak_Dir - ERROR : Pak Not Ready\n";
const char var7f1b3e68[] = "Pak %d -> Pak_Dir - Done - Pak_GetOffsetEOF\n";
const char var7f1b3e98[] = "Pak %d -> Pak_Dir - Done - ekPakErrorHeaderCrcCheckFail\n";
const char var7f1b3ed4[] = "Pak Return Code = ekPakOk";
const char var7f1b3ef0[] = "Pak Return Code = ekPakErrorNoPakPresent";
const char var7f1b3f1c[] = "Pak Return Code = ekPakErrorPakFatal";
const char var7f1b3f44[] = "Pak Return Code = ekPakErrorFileNotFound";
const char var7f1b3f70[] = "Pak Return Code = ekPakErrorFileSystem";
const char var7f1b3f98[] = "Pak Return Code = ekPakErrorOutOfMemory";
const char var7f1b3fc0[] = "Pak Return Code = ekPakErrorPakWaitingForInit";
const char var7f1b3ff0[] = "Pak Return Code = ekPakErrorHeaderCrcCheckFail";
const char var7f1b4020[] = "Pak Return Code = ekPakErrorDataCrcCheckFail";
const char var7f1b4050[] = "Pak Return Code = ekPakErrorDataNotValid";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b407c[] = "Pak Return Code = Unknown - %d\n";
#else
const char var7f1b407c[] = "Pak Return Code = Unknown - %d";
#endif

void pakCalculateChecksum(u8 *start, u8 *end, u16 *checksum)
{
	crcCalculateU16Pair(start, end, checksum);
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f118b04
/*  f118b04:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f118b08:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f118b0c:	afa40020 */ 	sw	$a0,0x20($sp)
/*  f118b10:	afa50024 */ 	sw	$a1,0x24($sp)
/*  f118b14:	0fc459f6 */ 	jal	pak0f1167d8
/*  f118b18:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f118b1c:	14400023 */ 	bnez	$v0,.L0f118bac
/*  f118b20:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f118b24:	8fa50024 */ 	lw	$a1,0x24($sp)
/*  f118b28:	0fc464da */ 	jal	pakFindFile
/*  f118b2c:	00003025 */ 	or	$a2,$zero,$zero
/*  f118b30:	2401ffff */ 	addiu	$at,$zero,-1
/*  f118b34:	14410003 */ 	bne	$v0,$at,.L0f118b44
/*  f118b38:	00402825 */ 	or	$a1,$v0,$zero
/*  f118b3c:	1000001e */ 	beqz	$zero,.L0f118bb8
/*  f118b40:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f118b44:
/*  f118b44:	10400011 */ 	beqz	$v0,.L0f118b8c
/*  f118b48:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118b4c:	10400015 */ 	beqz	$v0,.L0f118ba4
/*  f118b50:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f118b54:	0fc45ff0 */ 	jal	pakGetNumBytes
/*  f118b58:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f118b5c:	8fa5001c */ 	lw	$a1,0x1c($sp)
/*  f118b60:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f118b64:	00a2082b */ 	sltu	$at,$a1,$v0
/*  f118b68:	1020000e */ 	beqz	$at,.L0f118ba4
/*  f118b6c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118b70:	0fc45974 */ 	jal	pakGetBlockSize
/*  f118b74:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f118b78:	8fa5001c */ 	lw	$a1,0x1c($sp)
/*  f118b7c:	244effff */ 	addiu	$t6,$v0,-1
/*  f118b80:	01c57824 */ 	and	$t7,$t6,$a1
/*  f118b84:	15e00007 */ 	bnez	$t7,.L0f118ba4
/*  f118b88:	00000000 */ 	sll	$zero,$zero,0x0
.L0f118b8c:
/*  f118b8c:	0fc46ef6 */ 	jal	pak0f11bbd8
/*  f118b90:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f118b94:	54400008 */ 	bnezl	$v0,.L0f118bb8
/*  f118b98:	00001025 */ 	or	$v0,$zero,$zero
/*  f118b9c:	10000006 */ 	beqz	$zero,.L0f118bb8
/*  f118ba0:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f118ba4:
/*  f118ba4:	10000004 */ 	beqz	$zero,.L0f118bb8
/*  f118ba8:	24020003 */ 	addiu	$v0,$zero,0x3
.L0f118bac:
/*  f118bac:	10000002 */ 	beqz	$zero,.L0f118bb8
/*  f118bb0:	24020006 */ 	addiu	$v0,$zero,0x6
/*  f118bb4:	00001025 */ 	or	$v0,$zero,$zero
.L0f118bb8:
/*  f118bb8:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f118bbc:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f118bc0:	03e00008 */ 	jr	$ra
/*  f118bc4:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f112e8c
/*  f112e8c:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f112e90:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f112e94:	afa40020 */ 	sw	$a0,0x20($sp)
/*  f112e98:	afa50024 */ 	sw	$a1,0x24($sp)
/*  f112e9c:	0fc442e7 */ 	jal	pak0f1167d8
/*  f112ea0:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f112ea4:	1440001e */ 	bnez	$v0,.NB0f112f20
/*  f112ea8:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f112eac:	8fa50024 */ 	lw	$a1,0x24($sp)
/*  f112eb0:	0fc44da7 */ 	jal	pakFindFile
/*  f112eb4:	00003025 */ 	or	$a2,$zero,$zero
/*  f112eb8:	10400011 */ 	beqz	$v0,.NB0f112f00
/*  f112ebc:	00402825 */ 	or	$a1,$v0,$zero
/*  f112ec0:	10400015 */ 	beqz	$v0,.NB0f112f18
/*  f112ec4:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f112ec8:	0fc448fb */ 	jal	pakGetNumBytes
/*  f112ecc:	afa2001c */ 	sw	$v0,0x1c($sp)
/*  f112ed0:	8fa5001c */ 	lw	$a1,0x1c($sp)
/*  f112ed4:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f112ed8:	00a2082b */ 	sltu	$at,$a1,$v0
/*  f112edc:	1020000e */ 	beqz	$at,.NB0f112f18
/*  f112ee0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f112ee4:	0fc4428c */ 	jal	pakGetBlockSize
/*  f112ee8:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f112eec:	8fa5001c */ 	lw	$a1,0x1c($sp)
/*  f112ef0:	244effff */ 	addiu	$t6,$v0,-1
/*  f112ef4:	01c57824 */ 	and	$t7,$t6,$a1
/*  f112ef8:	15e00007 */ 	bnez	$t7,.NB0f112f18
/*  f112efc:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f112f00:
/*  f112f00:	0fc456d7 */ 	jal	pak0f11bbd8
/*  f112f04:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f112f08:	54400008 */ 	bnezl	$v0,.NB0f112f2c
/*  f112f0c:	00001025 */ 	or	$v0,$zero,$zero
/*  f112f10:	10000006 */ 	beqz	$zero,.NB0f112f2c
/*  f112f14:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f112f18:
/*  f112f18:	10000004 */ 	beqz	$zero,.NB0f112f2c
/*  f112f1c:	24020003 */ 	addiu	$v0,$zero,0x3
.NB0f112f20:
/*  f112f20:	10000002 */ 	beqz	$zero,.NB0f112f2c
/*  f112f24:	24020006 */ 	addiu	$v0,$zero,0x6
/*  f112f28:	00001025 */ 	or	$v0,$zero,$zero
.NB0f112f2c:
/*  f112f2c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f112f30:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f112f34:	03e00008 */ 	jr	$ra
/*  f112f38:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

s32 pak0f118bc8(s8 device, s32 fileid, u8 *body, s32 arg3)
{
	s32 offset;
	struct pakfileheader header;
	s32 result;
	u16 checksum[2];

	if (!pak0f1167d8(device)) {
		offset = pakFindFile(device, fileid, NULL);

		if (offset == -1) {
			return 1;
		}

		if (offset == 0 || (offset && offset < pakGetNumBytes(device) && ((pakGetBlockSize(device) - 1) & offset) == 0)) {
			result = pak0f11b86c(device, offset, body, &header, arg3);

			if (result) {
				return result;
			}

			if (arg3 == -1) {
				arg3 = 0;
			}

			if (header.occupied) {
				if (!arg3) {
					pakCalculateChecksum(body, body + header.bodylen, checksum);

					if (header.bodysum[0] != checksum[0] || header.bodysum[1] != checksum[1]) {
						return 8;
					}
				}
			} else {
				return 10;
			}
		} else {
			return 3;
		}
	} else {
		return 6;
	}

	return 0;
}

s32 pak0f118d18(s8 device, u32 filetype, u32 *buffer1024)
{
	struct pakfileheader header;
	u32 offset = 0;
	u32 sp48;
	s32 len = 0;
	s32 result = pak0f119298(device);

	if (result != 0) {
		return result;
	}

	result = pak0f11b75c(device, &sp48);

	if (result != 0) {
		return result;
	}

	result = pak0f1167d8(device);

	if (result != 0) {
		return pak0f1167d8(device);
	}

	result = pakReadHeaderAtOffset(device, offset, &header);

	while (result == 0) {
		if ((filetype & PAKFILETYPE_100) || (filetype & header.filetype)) {
			buffer1024[len] = header.fileid;
			len++;
		}

		offset += header.filelen;

		if (offset >= sp48) {
			break;
		}

		result = pakReadHeaderAtOffset(device, offset, &header);
	}

	buffer1024[len] = 0;

	if (result == 7) {
		return 7;
	}

#if VERSION >= VERSION_NTSC_1_0
	if (result == 1) {
		return 1;
	}
#endif

	return 0;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f118eb0
/*  f118eb0:	27bdffa8 */ 	addiu	$sp,$sp,-88
/*  f118eb4:	afb30030 */ 	sw	$s3,0x30($sp)
/*  f118eb8:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f118ebc:	00137603 */ 	sra	$t6,$s3,0x18
/*  f118ec0:	afa40058 */ 	sw	$a0,0x58($sp)
/*  f118ec4:	f7b40010 */ 	sdc1	$f20,0x10($sp)
/*  f118ec8:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f118ecc:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f118ed0:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f118ed4:	4480a000 */ 	mtc1	$zero,$f20
/*  f118ed8:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f118edc:	01c09825 */ 	or	$s3,$t6,$zero
/*  f118ee0:	afb2002c */ 	sw	$s2,0x2c($sp)
/*  f118ee4:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f118ee8:	f7b60018 */ 	sdc1	$f22,0x18($sp)
/*  f118eec:	afa5005c */ 	sw	$a1,0x5c($sp)
/*  f118ef0:	00008025 */ 	or	$s0,$zero,$zero
/*  f118ef4:	0fc459f6 */ 	jal	pak0f1167d8
/*  f118ef8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f118efc:	10400003 */ 	beqz	$v0,.L0f118f0c
/*  f118f00:	27b20048 */ 	addiu	$s2,$sp,0x48
/*  f118f04:	10000055 */ 	beqz	$zero,.L0f11905c
/*  f118f08:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f118f0c:
/*  f118f0c:	00132600 */ 	sll	$a0,$s3,0x18
/*  f118f10:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f118f14:	03002025 */ 	or	$a0,$t8,$zero
/*  f118f18:	00002825 */ 	or	$a1,$zero,$zero
/*  f118f1c:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f118f20:	02403025 */ 	or	$a2,$s2,$zero
/*  f118f24:	14400011 */ 	bnez	$v0,.L0f118f6c
/*  f118f28:	3c013f80 */ 	lui	$at,0x3f80
/*  f118f2c:	4481b000 */ 	mtc1	$at,$f22
/*  f118f30:	24110002 */ 	addiu	$s1,$zero,0x2
/*  f118f34:	8fa20050 */ 	lw	$v0,0x50($sp)
.L0f118f38:
/*  f118f38:	00132600 */ 	sll	$a0,$s3,0x18
/*  f118f3c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f118f40:	0002cdc2 */ 	srl	$t9,$v0,0x17
/*  f118f44:	17310002 */ 	bne	$t9,$s1,.L0f118f50
/*  f118f48:	30480fff */ 	andi	$t0,$v0,0xfff
/*  f118f4c:	4616a500 */ 	add.s	$f20,$f20,$f22
.L0f118f50:
/*  f118f50:	02088021 */ 	addu	$s0,$s0,$t0
/*  f118f54:	02002825 */ 	or	$a1,$s0,$zero
/*  f118f58:	01202025 */ 	or	$a0,$t1,$zero
/*  f118f5c:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f118f60:	02403025 */ 	or	$a2,$s2,$zero
/*  f118f64:	5040fff4 */ 	beqzl	$v0,.L0f118f38
/*  f118f68:	8fa20050 */ 	lw	$v0,0x50($sp)
.L0f118f6c:
/*  f118f6c:	00135080 */ 	sll	$t2,$s3,0x2
/*  f118f70:	01535023 */ 	subu	$t2,$t2,$s3
/*  f118f74:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118f78:	01535023 */ 	subu	$t2,$t2,$s3
/*  f118f7c:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118f80:	01535021 */ 	addu	$t2,$t2,$s3
/*  f118f84:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118f88:	01535023 */ 	subu	$t2,$t2,$s3
/*  f118f8c:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f118f90:	3c0b800a */ 	lui	$t3,%hi(g_Paks+0x2a4)
/*  f118f94:	016a5821 */ 	addu	$t3,$t3,$t2
/*  f118f98:	8d6b2624 */ 	lw	$t3,%lo(g_Paks+0x2a4)($t3)
/*  f118f9c:	3c0142c8 */ 	lui	$at,0x42c8
/*  f118fa0:	44812000 */ 	mtc1	$at,$f4
/*  f118fa4:	448b3000 */ 	mtc1	$t3,$f6
/*  f118fa8:	8fae005c */ 	lw	$t6,0x5c($sp)
/*  f118fac:	05610005 */ 	bgez	$t3,.L0f118fc4
/*  f118fb0:	46803220 */ 	cvt.s.w	$f8,$f6
/*  f118fb4:	3c014f80 */ 	lui	$at,0x4f80
/*  f118fb8:	44815000 */ 	mtc1	$at,$f10
/*  f118fbc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118fc0:	460a4200 */ 	add.s	$f8,$f8,$f10
.L0f118fc4:
/*  f118fc4:	46082403 */ 	div.s	$f16,$f4,$f8
/*  f118fc8:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f118fcc:	3c014f00 */ 	lui	$at,0x4f00
/*  f118fd0:	46148482 */ 	mul.s	$f18,$f16,$f20
/*  f118fd4:	444cf800 */ 	cfc1	$t4,$31
/*  f118fd8:	44cdf800 */ 	ctc1	$t5,$31
/*  f118fdc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118fe0:	460091a4 */ 	cvt.w.s	$f6,$f18
/*  f118fe4:	444df800 */ 	cfc1	$t5,$31
/*  f118fe8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118fec:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f118ff0:	51a00013 */ 	beqzl	$t5,.L0f119040
/*  f118ff4:	440d3000 */ 	mfc1	$t5,$f6
/*  f118ff8:	44813000 */ 	mtc1	$at,$f6
/*  f118ffc:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f119000:	46069181 */ 	sub.s	$f6,$f18,$f6
/*  f119004:	44cdf800 */ 	ctc1	$t5,$31
/*  f119008:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11900c:	460031a4 */ 	cvt.w.s	$f6,$f6
/*  f119010:	444df800 */ 	cfc1	$t5,$31
/*  f119014:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119018:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f11901c:	15a00005 */ 	bnez	$t5,.L0f119034
/*  f119020:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119024:	440d3000 */ 	mfc1	$t5,$f6
/*  f119028:	3c018000 */ 	lui	$at,0x8000
/*  f11902c:	10000007 */ 	beqz	$zero,.L0f11904c
/*  f119030:	01a16825 */ 	or	$t5,$t5,$at
.L0f119034:
/*  f119034:	10000005 */ 	beqz	$zero,.L0f11904c
/*  f119038:	240dffff */ 	addiu	$t5,$zero,-1
/*  f11903c:	440d3000 */ 	mfc1	$t5,$f6
.L0f119040:
/*  f119040:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119044:	05a0fffb */ 	bltz	$t5,.L0f119034
/*  f119048:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11904c:
/*  f11904c:	44ccf800 */ 	ctc1	$t4,$31
/*  f119050:	adcd0000 */ 	sw	$t5,0x0($t6)
/*  f119054:	00001025 */ 	or	$v0,$zero,$zero
/*  f119058:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f11905c:
/*  f11905c:	d7b40010 */ 	ldc1	$f20,0x10($sp)
/*  f119060:	d7b60018 */ 	ldc1	$f22,0x18($sp)
/*  f119064:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f119068:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f11906c:	8fb2002c */ 	lw	$s2,0x2c($sp)
/*  f119070:	8fb30030 */ 	lw	$s3,0x30($sp)
/*  f119074:	03e00008 */ 	jr	$ra
/*  f119078:	27bd0058 */ 	addiu	$sp,$sp,0x58
);
#else
GLOBAL_ASM(
glabel pak0f118eb0
/*  f1131fc:	27bdffa8 */ 	addiu	$sp,$sp,-88
/*  f113200:	afb30030 */ 	sw	$s3,0x30($sp)
/*  f113204:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f113208:	00137603 */ 	sra	$t6,$s3,0x18
/*  f11320c:	afa40058 */ 	sw	$a0,0x58($sp)
/*  f113210:	f7b40010 */ 	sdc1	$f20,0x10($sp)
/*  f113214:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f113218:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f11321c:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f113220:	4480a000 */ 	mtc1	$zero,$f20
/*  f113224:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f113228:	01c09825 */ 	or	$s3,$t6,$zero
/*  f11322c:	afb2002c */ 	sw	$s2,0x2c($sp)
/*  f113230:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f113234:	f7b60018 */ 	sdc1	$f22,0x18($sp)
/*  f113238:	afa5005c */ 	sw	$a1,0x5c($sp)
/*  f11323c:	00008025 */ 	or	$s0,$zero,$zero
/*  f113240:	0fc442e7 */ 	jal	pak0f1167d8
/*  f113244:	01e02025 */ 	or	$a0,$t7,$zero
/*  f113248:	10400003 */ 	beqz	$v0,.NB0f113258
/*  f11324c:	27b20048 */ 	addiu	$s2,$sp,0x48
/*  f113250:	10000053 */ 	beqz	$zero,.NB0f1133a0
/*  f113254:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f113258:
/*  f113258:	00132600 */ 	sll	$a0,$s3,0x18
/*  f11325c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f113260:	03002025 */ 	or	$a0,$t8,$zero
/*  f113264:	00002825 */ 	or	$a1,$zero,$zero
/*  f113268:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f11326c:	02403025 */ 	or	$a2,$s2,$zero
/*  f113270:	14400011 */ 	bnez	$v0,.NB0f1132b8
/*  f113274:	3c013f80 */ 	lui	$at,0x3f80
/*  f113278:	4481b000 */ 	mtc1	$at,$f22
/*  f11327c:	24110002 */ 	addiu	$s1,$zero,0x2
/*  f113280:	8fa20050 */ 	lw	$v0,0x50($sp)
.NB0f113284:
/*  f113284:	00132600 */ 	sll	$a0,$s3,0x18
/*  f113288:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11328c:	0002cdc2 */ 	srl	$t9,$v0,0x17
/*  f113290:	17310002 */ 	bne	$t9,$s1,.NB0f11329c
/*  f113294:	30480fff */ 	andi	$t0,$v0,0xfff
/*  f113298:	4616a500 */ 	add.s	$f20,$f20,$f22
.NB0f11329c:
/*  f11329c:	02088021 */ 	addu	$s0,$s0,$t0
/*  f1132a0:	02002825 */ 	or	$a1,$s0,$zero
/*  f1132a4:	01202025 */ 	or	$a0,$t1,$zero
/*  f1132a8:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f1132ac:	02403025 */ 	or	$a2,$s2,$zero
/*  f1132b0:	5040fff4 */ 	beqzl	$v0,.NB0f113284
/*  f1132b4:	8fa20050 */ 	lw	$v0,0x50($sp)
.NB0f1132b8:
/*  f1132b8:	00135080 */ 	sll	$t2,$s3,0x2
/*  f1132bc:	01535023 */ 	subu	$t2,$t2,$s3
/*  f1132c0:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f1132c4:	01535023 */ 	subu	$t2,$t2,$s3
/*  f1132c8:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f1132cc:	01535021 */ 	addu	$t2,$t2,$s3
/*  f1132d0:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f1132d4:	3c0b800a */ 	lui	$t3,0x800a
/*  f1132d8:	016a5821 */ 	addu	$t3,$t3,$t2
/*  f1132dc:	8d6b6b14 */ 	lw	$t3,0x6b14($t3)
/*  f1132e0:	3c0142c8 */ 	lui	$at,0x42c8
/*  f1132e4:	44812000 */ 	mtc1	$at,$f4
/*  f1132e8:	448b3000 */ 	mtc1	$t3,$f6
/*  f1132ec:	8fae005c */ 	lw	$t6,0x5c($sp)
/*  f1132f0:	05610005 */ 	bgez	$t3,.NB0f113308
/*  f1132f4:	46803220 */ 	cvt.s.w	$f8,$f6
/*  f1132f8:	3c014f80 */ 	lui	$at,0x4f80
/*  f1132fc:	44815000 */ 	mtc1	$at,$f10
/*  f113300:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113304:	460a4200 */ 	add.s	$f8,$f8,$f10
.NB0f113308:
/*  f113308:	46082403 */ 	div.s	$f16,$f4,$f8
/*  f11330c:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f113310:	3c014f00 */ 	lui	$at,0x4f00
/*  f113314:	46148482 */ 	mul.s	$f18,$f16,$f20
/*  f113318:	444cf800 */ 	cfc1	$t4,$31
/*  f11331c:	44cdf800 */ 	ctc1	$t5,$31
/*  f113320:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113324:	460091a4 */ 	cvt.w.s	$f6,$f18
/*  f113328:	444df800 */ 	cfc1	$t5,$31
/*  f11332c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113330:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f113334:	51a00013 */ 	beqzl	$t5,.NB0f113384
/*  f113338:	440d3000 */ 	mfc1	$t5,$f6
/*  f11333c:	44813000 */ 	mtc1	$at,$f6
/*  f113340:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f113344:	46069181 */ 	sub.s	$f6,$f18,$f6
/*  f113348:	44cdf800 */ 	ctc1	$t5,$31
/*  f11334c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113350:	460031a4 */ 	cvt.w.s	$f6,$f6
/*  f113354:	444df800 */ 	cfc1	$t5,$31
/*  f113358:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11335c:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f113360:	15a00005 */ 	bnez	$t5,.NB0f113378
/*  f113364:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113368:	440d3000 */ 	mfc1	$t5,$f6
/*  f11336c:	3c018000 */ 	lui	$at,0x8000
/*  f113370:	10000007 */ 	beqz	$zero,.NB0f113390
/*  f113374:	01a16825 */ 	or	$t5,$t5,$at
.NB0f113378:
/*  f113378:	10000005 */ 	beqz	$zero,.NB0f113390
/*  f11337c:	240dffff */ 	addiu	$t5,$zero,-1
/*  f113380:	440d3000 */ 	mfc1	$t5,$f6
.NB0f113384:
/*  f113384:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113388:	05a0fffb */ 	bltz	$t5,.NB0f113378
/*  f11338c:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f113390:
/*  f113390:	44ccf800 */ 	ctc1	$t4,$31
/*  f113394:	adcd0000 */ 	sw	$t5,0x0($t6)
/*  f113398:	00001025 */ 	or	$v0,$zero,$zero
/*  f11339c:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f1133a0:
/*  f1133a0:	d7b40010 */ 	ldc1	$f20,0x10($sp)
/*  f1133a4:	d7b60018 */ 	ldc1	$f22,0x18($sp)
/*  f1133a8:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f1133ac:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f1133b0:	8fb2002c */ 	lw	$s2,0x2c($sp)
/*  f1133b4:	8fb30030 */ 	lw	$s3,0x30($sp)
/*  f1133b8:	03e00008 */ 	jr	$ra
/*  f1133bc:	27bd0058 */ 	addiu	$sp,$sp,0x58
);
#endif

s32 pak0f11907c(s8 device)
{
	s32 result = pak0f1167d8(device);

	if (result != 0) {
		return result;
	}

	pak0f11a1d0(device);

	return 0;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f1190bc
/*  f1190bc:	27bdffa0 */ 	addiu	$sp,$sp,-96
/*  f1190c0:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f1190c4:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f1190c8:	00137603 */ 	sra	$t6,$s3,0x18
/*  f1190cc:	afa40060 */ 	sw	$a0,0x60($sp)
/*  f1190d0:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f1190d4:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f1190d8:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f1190dc:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f1190e0:	00c08825 */ 	or	$s1,$a2,$zero
/*  f1190e4:	01c09825 */ 	or	$s3,$t6,$zero
/*  f1190e8:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f1190ec:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f1190f0:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f1190f4:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f1190f8:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f1190fc:	0fc45c25 */ 	jal	pakGetBodyLenByType
/*  f119100:	01e02025 */ 	or	$a0,$t7,$zero
/*  f119104:	00132600 */ 	sll	$a0,$s3,0x18
/*  f119108:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11910c:	03002025 */ 	or	$a0,$t8,$zero
/*  f119110:	0fc45996 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f119114:	00402825 */ 	or	$a1,$v0,$zero
/*  f119118:	00132600 */ 	sll	$a0,$s3,0x18
/*  f11911c:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f119120:	0040a825 */ 	or	$s5,$v0,$zero
/*  f119124:	0000b025 */ 	or	$s6,$zero,$zero
/*  f119128:	03202025 */ 	or	$a0,$t9,$zero
/*  f11912c:	0fc46dd7 */ 	jal	pak0f11b75c
/*  f119130:	27a50044 */ 	addiu	$a1,$sp,0x44
/*  f119134:	12200002 */ 	beqz	$s1,.L0f119140
/*  f119138:	27b40050 */ 	addiu	$s4,$sp,0x50
/*  f11913c:	ae200000 */ 	sw	$zero,0x0($s1)
.L0f119140:
/*  f119140:	00132600 */ 	sll	$a0,$s3,0x18
/*  f119144:	00044603 */ 	sra	$t0,$a0,0x18
/*  f119148:	01002025 */ 	or	$a0,$t0,$zero
/*  f11914c:	00008025 */ 	or	$s0,$zero,$zero
/*  f119150:	00002825 */ 	or	$a1,$zero,$zero
/*  f119154:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f119158:	02803025 */ 	or	$a2,$s4,$zero
/*  f11915c:	1440001f */ 	bnez	$v0,.L0f1191dc
/*  f119160:	8fa90044 */ 	lw	$t1,0x44($sp)
/*  f119164:	5120001e */ 	beqzl	$t1,.L0f1191e0
/*  f119168:	0013c880 */ 	sll	$t9,$s3,0x2
/*  f11916c:	24120002 */ 	addiu	$s2,$zero,0x2
/*  f119170:	8fa20058 */ 	lw	$v0,0x58($sp)
.L0f119174:
/*  f119174:	00132600 */ 	sll	$a0,$s3,0x18
/*  f119178:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11917c:	000255c2 */ 	srl	$t2,$v0,0x17
/*  f119180:	1552000b */ 	bne	$t2,$s2,.L0f1191b0
/*  f119184:	01e02025 */ 	or	$a0,$t7,$zero
/*  f119188:	304b0fff */ 	andi	$t3,$v0,0xfff
/*  f11918c:	0175082b */ 	sltu	$at,$t3,$s5
/*  f119190:	54200008 */ 	bnezl	$at,.L0f1191b4
/*  f119194:	304e0fff */ 	andi	$t6,$v0,0xfff
/*  f119198:	12200005 */ 	beqz	$s1,.L0f1191b0
/*  f11919c:	24160001 */ 	addiu	$s6,$zero,0x1
/*  f1191a0:	8e2c0000 */ 	lw	$t4,0x0($s1)
/*  f1191a4:	258d0001 */ 	addiu	$t5,$t4,0x1
/*  f1191a8:	ae2d0000 */ 	sw	$t5,0x0($s1)
/*  f1191ac:	8fa20058 */ 	lw	$v0,0x58($sp)
.L0f1191b0:
/*  f1191b0:	304e0fff */ 	andi	$t6,$v0,0xfff
.L0f1191b4:
/*  f1191b4:	020e8021 */ 	addu	$s0,$s0,$t6
/*  f1191b8:	02002825 */ 	or	$a1,$s0,$zero
/*  f1191bc:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f1191c0:	02803025 */ 	or	$a2,$s4,$zero
/*  f1191c4:	54400006 */ 	bnezl	$v0,.L0f1191e0
/*  f1191c8:	0013c880 */ 	sll	$t9,$s3,0x2
/*  f1191cc:	8fb80044 */ 	lw	$t8,0x44($sp)
/*  f1191d0:	0218082b */ 	sltu	$at,$s0,$t8
/*  f1191d4:	5420ffe7 */ 	bnezl	$at,.L0f119174
/*  f1191d8:	8fa20058 */ 	lw	$v0,0x58($sp)
.L0f1191dc:
/*  f1191dc:	0013c880 */ 	sll	$t9,$s3,0x2
.L0f1191e0:
/*  f1191e0:	0333c823 */ 	subu	$t9,$t9,$s3
/*  f1191e4:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f1191e8:	0333c823 */ 	subu	$t9,$t9,$s3
/*  f1191ec:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f1191f0:	0333c821 */ 	addu	$t9,$t9,$s3
/*  f1191f4:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f1191f8:	0333c823 */ 	subu	$t9,$t9,$s3
/*  f1191fc:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f119200:	3c08800a */ 	lui	$t0,%hi(g_Paks+0x2a0)
/*  f119204:	01194021 */ 	addu	$t0,$t0,$t9
/*  f119208:	8d082620 */ 	lw	$t0,%lo(g_Paks+0x2a0)($t0)
/*  f11920c:	8fa90044 */ 	lw	$t1,0x44($sp)
/*  f119210:	00132600 */ 	sll	$a0,$s3,0x18
/*  f119214:	12200009 */ 	beqz	$s1,.L0f11923c
/*  f119218:	01098023 */ 	subu	$s0,$t0,$t1
/*  f11921c:	0215001b */ 	divu	$zero,$s0,$s5
/*  f119220:	8e2a0000 */ 	lw	$t2,0x0($s1)
/*  f119224:	00005812 */ 	mflo	$t3
/*  f119228:	014b6021 */ 	addu	$t4,$t2,$t3
/*  f11922c:	ae2c0000 */ 	sw	$t4,0x0($s1)
/*  f119230:	16a00002 */ 	bnez	$s5,.L0f11923c
/*  f119234:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119238:	0007000d */ 	break	0x7
.L0f11923c:
/*  f11923c:	16c00007 */ 	bnez	$s6,.L0f11925c
/*  f119240:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f119244:	0fc45c1b */ 	jal	pakGetMaxFileSize
/*  f119248:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11924c:	0202082b */ 	sltu	$at,$s0,$v0
/*  f119250:	14200002 */ 	bnez	$at,.L0f11925c
/*  f119254:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119258:	24160001 */ 	addiu	$s6,$zero,0x1
.L0f11925c:
/*  f11925c:	12c00003 */ 	beqz	$s6,.L0f11926c
/*  f119260:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f119264:	10000002 */ 	beqz	$zero,.L0f119270
/*  f119268:	00001825 */ 	or	$v1,$zero,$zero
.L0f11926c:
/*  f11926c:	24030005 */ 	addiu	$v1,$zero,0x5
.L0f119270:
/*  f119270:	8fbf0034 */ 	lw	$ra,0x34($sp)
/*  f119274:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f119278:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f11927c:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f119280:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f119284:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f119288:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f11928c:	27bd0060 */ 	addiu	$sp,$sp,0x60
/*  f119290:	03e00008 */ 	jr	$ra
/*  f119294:	00601025 */ 	or	$v0,$v1,$zero
);
#else
GLOBAL_ASM(
glabel pak0f1190bc
/*  f113400:	27bdffa0 */ 	addiu	$sp,$sp,-96
/*  f113404:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f113408:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f11340c:	00137603 */ 	sra	$t6,$s3,0x18
/*  f113410:	afa40060 */ 	sw	$a0,0x60($sp)
/*  f113414:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f113418:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f11341c:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f113420:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f113424:	00c08825 */ 	or	$s1,$a2,$zero
/*  f113428:	01c09825 */ 	or	$s3,$t6,$zero
/*  f11342c:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f113430:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f113434:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f113438:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f11343c:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f113440:	0fc444f9 */ 	jal	pakGetBodyLenByType
/*  f113444:	01e02025 */ 	or	$a0,$t7,$zero
/*  f113448:	00132600 */ 	sll	$a0,$s3,0x18
/*  f11344c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f113450:	03002025 */ 	or	$a0,$t8,$zero
/*  f113454:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f113458:	00402825 */ 	or	$a1,$v0,$zero
/*  f11345c:	00132600 */ 	sll	$a0,$s3,0x18
/*  f113460:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f113464:	0040a825 */ 	or	$s5,$v0,$zero
/*  f113468:	0000b025 */ 	or	$s6,$zero,$zero
/*  f11346c:	03202025 */ 	or	$a0,$t9,$zero
/*  f113470:	0fc455c3 */ 	jal	pak0f11b75c
/*  f113474:	27a50044 */ 	addiu	$a1,$sp,0x44
/*  f113478:	12200002 */ 	beqz	$s1,.NB0f113484
/*  f11347c:	27b40050 */ 	addiu	$s4,$sp,0x50
/*  f113480:	ae200000 */ 	sw	$zero,0x0($s1)
.NB0f113484:
/*  f113484:	00132600 */ 	sll	$a0,$s3,0x18
/*  f113488:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11348c:	01002025 */ 	or	$a0,$t0,$zero
/*  f113490:	00008025 */ 	or	$s0,$zero,$zero
/*  f113494:	00002825 */ 	or	$a1,$zero,$zero
/*  f113498:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f11349c:	02803025 */ 	or	$a2,$s4,$zero
/*  f1134a0:	1440001f */ 	bnez	$v0,.NB0f113520
/*  f1134a4:	8fa90044 */ 	lw	$t1,0x44($sp)
/*  f1134a8:	5120001e */ 	beqzl	$t1,.NB0f113524
/*  f1134ac:	0013c880 */ 	sll	$t9,$s3,0x2
/*  f1134b0:	24120002 */ 	addiu	$s2,$zero,0x2
/*  f1134b4:	8fa20058 */ 	lw	$v0,0x58($sp)
.NB0f1134b8:
/*  f1134b8:	00132600 */ 	sll	$a0,$s3,0x18
/*  f1134bc:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f1134c0:	000255c2 */ 	srl	$t2,$v0,0x17
/*  f1134c4:	1552000b */ 	bne	$t2,$s2,.NB0f1134f4
/*  f1134c8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1134cc:	304b0fff */ 	andi	$t3,$v0,0xfff
/*  f1134d0:	0175082b */ 	sltu	$at,$t3,$s5
/*  f1134d4:	54200008 */ 	bnezl	$at,.NB0f1134f8
/*  f1134d8:	304e0fff */ 	andi	$t6,$v0,0xfff
/*  f1134dc:	12200005 */ 	beqz	$s1,.NB0f1134f4
/*  f1134e0:	24160001 */ 	addiu	$s6,$zero,0x1
/*  f1134e4:	8e2c0000 */ 	lw	$t4,0x0($s1)
/*  f1134e8:	258d0001 */ 	addiu	$t5,$t4,0x1
/*  f1134ec:	ae2d0000 */ 	sw	$t5,0x0($s1)
/*  f1134f0:	8fa20058 */ 	lw	$v0,0x58($sp)
.NB0f1134f4:
/*  f1134f4:	304e0fff */ 	andi	$t6,$v0,0xfff
.NB0f1134f8:
/*  f1134f8:	020e8021 */ 	addu	$s0,$s0,$t6
/*  f1134fc:	02002825 */ 	or	$a1,$s0,$zero
/*  f113500:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f113504:	02803025 */ 	or	$a2,$s4,$zero
/*  f113508:	54400006 */ 	bnezl	$v0,.NB0f113524
/*  f11350c:	0013c880 */ 	sll	$t9,$s3,0x2
/*  f113510:	8fb80044 */ 	lw	$t8,0x44($sp)
/*  f113514:	0218082b */ 	sltu	$at,$s0,$t8
/*  f113518:	5420ffe7 */ 	bnezl	$at,.NB0f1134b8
/*  f11351c:	8fa20058 */ 	lw	$v0,0x58($sp)
.NB0f113520:
/*  f113520:	0013c880 */ 	sll	$t9,$s3,0x2
.NB0f113524:
/*  f113524:	0333c823 */ 	subu	$t9,$t9,$s3
/*  f113528:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11352c:	0333c823 */ 	subu	$t9,$t9,$s3
/*  f113530:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f113534:	0333c821 */ 	addu	$t9,$t9,$s3
/*  f113538:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f11353c:	3c08800a */ 	lui	$t0,0x800a
/*  f113540:	01194021 */ 	addu	$t0,$t0,$t9
/*  f113544:	8d086b10 */ 	lw	$t0,0x6b10($t0)
/*  f113548:	8fa90044 */ 	lw	$t1,0x44($sp)
/*  f11354c:	00132600 */ 	sll	$a0,$s3,0x18
/*  f113550:	12200009 */ 	beqz	$s1,.NB0f113578
/*  f113554:	01098023 */ 	subu	$s0,$t0,$t1
/*  f113558:	0215001b */ 	divu	$zero,$s0,$s5
/*  f11355c:	8e2a0000 */ 	lw	$t2,0x0($s1)
/*  f113560:	00005812 */ 	mflo	$t3
/*  f113564:	014b6021 */ 	addu	$t4,$t2,$t3
/*  f113568:	ae2c0000 */ 	sw	$t4,0x0($s1)
/*  f11356c:	16a00002 */ 	bnez	$s5,.NB0f113578
/*  f113570:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113574:	0007000d */ 	break	0x7
.NB0f113578:
/*  f113578:	16c00007 */ 	bnez	$s6,.NB0f113598
/*  f11357c:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f113580:	0fc444ef */ 	jal	pakGetMaxFileSize
/*  f113584:	01a02025 */ 	or	$a0,$t5,$zero
/*  f113588:	0202082b */ 	sltu	$at,$s0,$v0
/*  f11358c:	14200002 */ 	bnez	$at,.NB0f113598
/*  f113590:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113594:	24160001 */ 	addiu	$s6,$zero,0x1
.NB0f113598:
/*  f113598:	12c00003 */ 	beqz	$s6,.NB0f1135a8
/*  f11359c:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f1135a0:	10000002 */ 	beqz	$zero,.NB0f1135ac
/*  f1135a4:	00001825 */ 	or	$v1,$zero,$zero
.NB0f1135a8:
/*  f1135a8:	24030005 */ 	addiu	$v1,$zero,0x5
.NB0f1135ac:
/*  f1135ac:	8fbf0034 */ 	lw	$ra,0x34($sp)
/*  f1135b0:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f1135b4:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f1135b8:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f1135bc:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f1135c0:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f1135c4:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f1135c8:	27bd0060 */ 	addiu	$sp,$sp,0x60
/*  f1135cc:	03e00008 */ 	jr	$ra
/*  f1135d0:	00601025 */ 	or	$v0,$v1,$zero
);
#endif

u32 pak0f119298(s8 device)
{
	if (g_Paks[device].unk000 != 2) {
		return 1;
	}

	switch (g_Paks[device].unk010) {
	case 11:
		return 0;
	case 17:
		return 14;
	case 18:
		return 4;
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		return 13;
	}

	return 1;
}

void pak0f119340(u32 arg0)
{
	switch (arg0) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
		break;
	}
}

#if VERSION >= VERSION_NTSC_1_0
// Note: ntsc-beta doesn't match the below due to stack size
s32 pakFindFile(s8 device, u32 fileid, struct pakfileheader *headerptr)
{
	struct pakfileheader header;
	s32 offset = 0;
	u32 sp30;
	s32 value;

	pak0f11b75c(device, &sp30);

	value = pakReadHeaderAtOffset(device, offset, &header);

	while (value == 0 && offset < sp30) {
		if (fileid == header.fileid) {
			if (headerptr) {
				memcpy(headerptr, &header, sizeof(struct pakfileheader));
			}

			return offset;
		}

		offset += header.filelen;

		value = pakReadHeaderAtOffset(device, offset, &header);
	}

#if VERSION >= VERSION_NTSC_1_0
	if (value == 1) {
		return -1;
	}
#endif

	return 0xffff;
}
#else
GLOBAL_ASM(
glabel pakFindFile
/*  f11369c:	27bdffc0 */ 	addiu	$sp,$sp,-64
/*  f1136a0:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f1136a4:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f1136a8:	00117603 */ 	sra	$t6,$s1,0x18
/*  f1136ac:	afa40040 */ 	sw	$a0,0x40($sp)
/*  f1136b0:	afb2001c */ 	sw	$s2,0x1c($sp)
/*  f1136b4:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f1136b8:	00a09025 */ 	or	$s2,$a1,$zero
/*  f1136bc:	afbf0024 */ 	sw	$ra,0x24($sp)
/*  f1136c0:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f1136c4:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f1136c8:	01c08825 */ 	or	$s1,$t6,$zero
/*  f1136cc:	afb30020 */ 	sw	$s3,0x20($sp)
/*  f1136d0:	afa60048 */ 	sw	$a2,0x48($sp)
/*  f1136d4:	00008025 */ 	or	$s0,$zero,$zero
/*  f1136d8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1136dc:	0fc455c3 */ 	jal	pak0f11b75c
/*  f1136e0:	27a5002c */ 	addiu	$a1,$sp,0x2c
/*  f1136e4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1136e8:	27b30030 */ 	addiu	$s3,$sp,0x30
/*  f1136ec:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1136f0:	03002025 */ 	or	$a0,$t8,$zero
/*  f1136f4:	02603025 */ 	or	$a2,$s3,$zero
/*  f1136f8:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f1136fc:	00002825 */ 	or	$a1,$zero,$zero
/*  f113700:	1440001d */ 	bnez	$v0,.NB0f113778
/*  f113704:	8fb9002c */ 	lw	$t9,0x2c($sp)
/*  f113708:	5320001c */ 	beqzl	$t9,.NB0f11377c
/*  f11370c:	3402ffff */ 	dli	$v0,0xffff
/*  f113710:	8fa8003c */ 	lw	$t0,0x3c($sp)
.NB0f113714:
/*  f113714:	8fab0038 */ 	lw	$t3,0x38($sp)
/*  f113718:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11371c:	00084b40 */ 	sll	$t1,$t0,0xd
/*  f113720:	00095642 */ 	srl	$t2,$t1,0x19
/*  f113724:	164a0009 */ 	bne	$s2,$t2,.NB0f11374c
/*  f113728:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f11372c:	8fa40048 */ 	lw	$a0,0x48($sp)
/*  f113730:	02602825 */ 	or	$a1,$s3,$zero
/*  f113734:	10800003 */ 	beqz	$a0,.NB0f113744
/*  f113738:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11373c:	0c012e88 */ 	jal	memcpy
/*  f113740:	24060010 */ 	addiu	$a2,$zero,0x10
.NB0f113744:
/*  f113744:	1000000d */ 	beqz	$zero,.NB0f11377c
/*  f113748:	02001025 */ 	or	$v0,$s0,$zero
.NB0f11374c:
/*  f11374c:	020c8021 */ 	addu	$s0,$s0,$t4
/*  f113750:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f113754:	01a02025 */ 	or	$a0,$t5,$zero
/*  f113758:	02002825 */ 	or	$a1,$s0,$zero
/*  f11375c:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f113760:	02603025 */ 	or	$a2,$s3,$zero
/*  f113764:	14400004 */ 	bnez	$v0,.NB0f113778
/*  f113768:	8fae002c */ 	lw	$t6,0x2c($sp)
/*  f11376c:	020e082b */ 	sltu	$at,$s0,$t6
/*  f113770:	5420ffe8 */ 	bnezl	$at,.NB0f113714
/*  f113774:	8fa8003c */ 	lw	$t0,0x3c($sp)
.NB0f113778:
/*  f113778:	3402ffff */ 	dli	$v0,0xffff
.NB0f11377c:
/*  f11377c:	8fbf0024 */ 	lw	$ra,0x24($sp)
/*  f113780:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f113784:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f113788:	8fb2001c */ 	lw	$s2,0x1c($sp)
/*  f11378c:	8fb30020 */ 	lw	$s3,0x20($sp)
/*  f113790:	03e00008 */ 	jr	$ra
/*  f113794:	27bd0040 */ 	addiu	$sp,$sp,0x40
);
#endif

#if VERSION >= VERSION_NTSC_FINAL
bool pakWriteBlankFile(u32 device, u32 offset, struct pakfileheader *header)
{
	if (pakWriteFile(device, offset, PAKFILETYPE_BLANK, NULL, pakGetBodyLenByFileLen(header->filelen), NULL, NULL, 0, 1) == 0) {
		return true;
	}

	return false;
}
#endif

#if VERSION >= VERSION_NTSC_FINAL
GLOBAL_ASM(
glabel pakRepairAsBlank
/*  f1194e0:	27bdff70 */ 	addiu	$sp,$sp,-144
/*  f1194e4:	afb10034 */ 	sw	$s1,0x34($sp)
/*  f1194e8:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f1194ec:	00117603 */ 	sra	$t6,$s1,0x18
/*  f1194f0:	afa40090 */ 	sw	$a0,0x90($sp)
/*  f1194f4:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f1194f8:	afbf0054 */ 	sw	$ra,0x54($sp)
/*  f1194fc:	afb20038 */ 	sw	$s2,0x38($sp)
/*  f119500:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f119504:	00c09025 */ 	or	$s2,$a2,$zero
/*  f119508:	01c08825 */ 	or	$s1,$t6,$zero
/*  f11950c:	afbe0050 */ 	sw	$s8,0x50($sp)
/*  f119510:	afb7004c */ 	sw	$s7,0x4c($sp)
/*  f119514:	afb60048 */ 	sw	$s6,0x48($sp)
/*  f119518:	afb50044 */ 	sw	$s5,0x44($sp)
/*  f11951c:	afb40040 */ 	sw	$s4,0x40($sp)
/*  f119520:	afb3003c */ 	sw	$s3,0x3c($sp)
/*  f119524:	afb00030 */ 	sw	$s0,0x30($sp)
/*  f119528:	afa50094 */ 	sw	$a1,0x94($sp)
/*  f11952c:	0fc45c1b */ 	jal	pakGetMaxFileSize
/*  f119530:	01e02025 */ 	or	$a0,$t7,$zero
/*  f119534:	8fb80094 */ 	lw	$t8,0x94($sp)
/*  f119538:	00114880 */ 	sll	$t1,$s1,0x2
/*  f11953c:	01314823 */ 	subu	$t1,$t1,$s1
/*  f119540:	8f030000 */ 	lw	$v1,0x0($t8)
/*  f119544:	0040b825 */ 	or	$s7,$v0,$zero
/*  f119548:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11954c:	0060b025 */ 	or	$s6,$v1,$zero
/*  f119550:	0060f025 */ 	or	$s8,$v1,$zero
/*  f119554:	12400004 */ 	beqz	$s2,.L0f119568
/*  f119558:	00608025 */ 	or	$s0,$v1,$zero
/*  f11955c:	8e590008 */ 	lw	$t9,0x8($s2)
/*  f119560:	33280fff */ 	andi	$t0,$t9,0xfff
/*  f119564:	00688021 */ 	addu	$s0,$v1,$t0
.L0f119568:
/*  f119568:	01314823 */ 	subu	$t1,$t1,$s1
/*  f11956c:	00094880 */ 	sll	$t1,$t1,0x2
/*  f119570:	01314821 */ 	addu	$t1,$t1,$s1
/*  f119574:	00094880 */ 	sll	$t1,$t1,0x2
/*  f119578:	01314823 */ 	subu	$t1,$t1,$s1
/*  f11957c:	3c0a800a */ 	lui	$t2,%hi(g_Paks)
/*  f119580:	254a2380 */ 	addiu	$t2,$t2,%lo(g_Paks)
/*  f119584:	00094880 */ 	sll	$t1,$t1,0x2
/*  f119588:	012aa821 */ 	addu	$s5,$t1,$t2
/*  f11958c:	8eab02a0 */ 	lw	$t3,0x2a0($s5)
/*  f119590:	24140001 */ 	addiu	$s4,$zero,0x1
/*  f119594:	24130004 */ 	addiu	$s3,$zero,0x4
/*  f119598:	020b082b */ 	sltu	$at,$s0,$t3
/*  f11959c:	10200038 */ 	beqz	$at,.L0f119680
/*  f1195a0:	27b20080 */ 	addiu	$s2,$sp,0x80
/*  f1195a4:	00112600 */ 	sll	$a0,$s1,0x18
.L0f1195a8:
/*  f1195a8:	00046603 */ 	sra	$t4,$a0,0x18
/*  f1195ac:	01802025 */ 	or	$a0,$t4,$zero
/*  f1195b0:	02002825 */ 	or	$a1,$s0,$zero
/*  f1195b4:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f1195b8:	02403025 */ 	or	$a2,$s2,$zero
/*  f1195bc:	14400009 */ 	bnez	$v0,.L0f1195e4
/*  f1195c0:	8fad0088 */ 	lw	$t5,0x88($sp)
/*  f1195c4:	000d75c2 */ 	srl	$t6,$t5,0x17
/*  f1195c8:	31cf0002 */ 	andi	$t7,$t6,0x2
/*  f1195cc:	15e00009 */ 	bnez	$t7,.L0f1195f4
/*  f1195d0:	03d0082b */ 	sltu	$at,$s8,$s0
/*  f1195d4:	50200008 */ 	beqzl	$at,.L0f1195f8
/*  f1195d8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1195dc:	10000028 */ 	beqz	$zero,.L0f119680
/*  f1195e0:	00000000 */ 	sll	$zero,$zero,0x0
.L0f1195e4:
/*  f1195e4:	54540004 */ 	bnel	$v0,$s4,.L0f1195f8
/*  f1195e8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1195ec:	1000003b */ 	beqz	$zero,.L0f1196dc
/*  f1195f0:	00001025 */ 	or	$v0,$zero,$zero
.L0f1195f4:
/*  f1195f4:	00112600 */ 	sll	$a0,$s1,0x18
.L0f1195f8:
/*  f1195f8:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1195fc:	0fc45974 */ 	jal	pakGetBlockSize
/*  f119600:	03002025 */ 	or	$a0,$t8,$zero
/*  f119604:	12330009 */ 	beq	$s1,$s3,.L0f11962c
/*  f119608:	02028021 */ 	addu	$s0,$s0,$v0
/*  f11960c:	0216c823 */ 	subu	$t9,$s0,$s6
/*  f119610:	02f9082b */ 	sltu	$at,$s7,$t9
/*  f119614:	50200006 */ 	beqzl	$at,.L0f119630
/*  f119618:	8ea202a0 */ 	lw	$v0,0x2a0($s5)
/*  f11961c:	8fa80094 */ 	lw	$t0,0x94($sp)
/*  f119620:	00001025 */ 	or	$v0,$zero,$zero
/*  f119624:	1000002d */ 	beqz	$zero,.L0f1196dc
/*  f119628:	ad100000 */ 	sw	$s0,0x0($t0)
.L0f11962c:
/*  f11962c:	8ea202a0 */ 	lw	$v0,0x2a0($s5)
.L0f119630:
/*  f119630:	0202082b */ 	sltu	$at,$s0,$v0
/*  f119634:	1420000f */ 	bnez	$at,.L0f119674
/*  f119638:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11963c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f119640:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f119644:	afaa0020 */ 	sw	$t2,0x20($sp)
/*  f119648:	01202025 */ 	or	$a0,$t1,$zero
/*  f11964c:	02c02825 */ 	or	$a1,$s6,$zero
/*  f119650:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f119654:	00003825 */ 	or	$a3,$zero,$zero
/*  f119658:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f11965c:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f119660:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f119664:	0fc46f15 */ 	jal	pakWriteFile
/*  f119668:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11966c:	1000001b */ 	beqz	$zero,.L0f1196dc
/*  f119670:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f119674:
/*  f119674:	0202082b */ 	sltu	$at,$s0,$v0
/*  f119678:	5420ffcb */ 	bnezl	$at,.L0f1195a8
/*  f11967c:	00112600 */ 	sll	$a0,$s1,0x18
.L0f119680:
/*  f119680:	0fc459a1 */ 	jal	pakGetBodyLenByFileLen
/*  f119684:	02162023 */ 	subu	$a0,$s0,$s6
/*  f119688:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11968c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f119690:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f119694:	afac0020 */ 	sw	$t4,0x20($sp)
/*  f119698:	01602025 */ 	or	$a0,$t3,$zero
/*  f11969c:	02c02825 */ 	or	$a1,$s6,$zero
/*  f1196a0:	24060002 */ 	addiu	$a2,$zero,0x2
/*  f1196a4:	00003825 */ 	or	$a3,$zero,$zero
/*  f1196a8:	afa20010 */ 	sw	$v0,0x10($sp)
/*  f1196ac:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f1196b0:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f1196b4:	0fc46f15 */ 	jal	pakWriteFile
/*  f1196b8:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f1196bc:	10400005 */ 	beqz	$v0,.L0f1196d4
/*  f1196c0:	8fae0094 */ 	lw	$t6,0x94($sp)
/*  f1196c4:	8fad0094 */ 	lw	$t5,0x94($sp)
/*  f1196c8:	00001025 */ 	or	$v0,$zero,$zero
/*  f1196cc:	10000003 */ 	beqz	$zero,.L0f1196dc
/*  f1196d0:	adb00000 */ 	sw	$s0,0x0($t5)
.L0f1196d4:
/*  f1196d4:	add00000 */ 	sw	$s0,0x0($t6)
/*  f1196d8:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1196dc:
/*  f1196dc:	8fbf0054 */ 	lw	$ra,0x54($sp)
/*  f1196e0:	8fb00030 */ 	lw	$s0,0x30($sp)
/*  f1196e4:	8fb10034 */ 	lw	$s1,0x34($sp)
/*  f1196e8:	8fb20038 */ 	lw	$s2,0x38($sp)
/*  f1196ec:	8fb3003c */ 	lw	$s3,0x3c($sp)
/*  f1196f0:	8fb40040 */ 	lw	$s4,0x40($sp)
/*  f1196f4:	8fb50044 */ 	lw	$s5,0x44($sp)
/*  f1196f8:	8fb60048 */ 	lw	$s6,0x48($sp)
/*  f1196fc:	8fb7004c */ 	lw	$s7,0x4c($sp)
/*  f119700:	8fbe0050 */ 	lw	$s8,0x50($sp)
/*  f119704:	03e00008 */ 	jr	$ra
/*  f119708:	27bd0090 */ 	addiu	$sp,$sp,0x90
);

#if VERSION < VERSION_NTSC_FINAL
const char var7f1b407c_2[] = "Pak %d -> Pak_RepairAsBlank : Repairing as Blank, Offset=%u, pH=%x\n";
const char var7f1b407c_3[] = "Pak %d -> Pak_RepairAsBlank -> Summing @ offset=%u, ret=%d\n";
const char var7f1b407c_4[] = "Pak %d -> Pak_RepairAsBlank -> Fault Speads Over More Than One File - TERMINAL";
#endif
const char var7f1b409c[] = "Pak %d -> Pak_RepairAsBlank - St=%u, Ed=%u, Gap=%u, Blank Size=%u\n";
const char var7f1b40e0[] = "Pak %d -> Pak_RepairAsBlank - Fatal Error at tOffset %u\n";
#else
/**
 * Repair the pak by writing a blank file from the given offset up until the
 * start of the next file.
 *
 * If done successfully, update the value at the offset pointer with the end
 * offset of the blank file and return true.
 *
 * For controller paks, enforce a max file size. If the blank file would exceed
 * the max file size, store the current search offset in the pointer and return
 * false.
 *
 * If the end of the note/device is reached without finding the subequent file,
 * write a terminator file at the start address and return true. A blank file
 * is not written in this case.
 *
 * If an error code is returned by called functions, the value at the offset
 * pointer is not changed and the function returns false.
 *
 * If the header argument is not NULL, it's assumed it's the header
 * corresponding to the starting offset and the function takes a shortcut by
 * starting the scan at the end of the header.
 */
// ntsc-final mismatches due to regalloc
bool pakRepairAsBlank(s8 device, u32 *offsetptr, struct pakfileheader *header)
{
	struct pakfileheader iterheader;

	u32 maxfilesize = pakGetMaxFileSize(device);
	u32 start = *offsetptr;
	u32 start2 = *offsetptr;
	u32 offset = *offsetptr;
	s32 result;
	u32 bodylen;

#if VERSION < VERSION_NTSC_FINAL
	osSyncPrintf("Pak %d -> Pak_RepairAsBlank : Repairing as Blank, Offset=%u, pH=%x\n", device, offset, header);
#endif

	// Skip past the header if given
	if (header != NULL) {
		offset += header->filelen;
	}

	while (offset < g_Paks[device].numbytes) {
		result = pakReadHeaderAtOffset(device, offset, &iterheader);

#if VERSION >= VERSION_NTSC_FINAL
		if (result == 0) {
			// Found a valid header
			if ((iterheader.filetype & PAKFILETYPE_BLANK) == 0 && offset > start2) {
				break;
			}
		} else if (result == 1) {
			// Some kind of error
			return false;
		}

		// No header at this offset
		offset += pakGetBlockSize(device);

		// For controller paks, consider giving up
		if (device != SAVEDEVICE_GAMEPAK && offset - start > maxfilesize) {
			osSyncPrintf("Pak %d -> Pak_RepairAsBlank -> Fault Speads Over More Than One File - TERMINAL", device);
			*offsetptr = offset;
			return false;
		}

		// If the end was reached, write a terminator at the starting offset
		if (offset >= g_Paks[device].numbytes) {
			pakWriteFile(device, start, PAKFILETYPE_TERMINATOR, NULL, 0, NULL, NULL, 0, 1);
			return true;
		}
#elif VERSION >= VERSION_NTSC_1_0
		osSyncPrintf("Pak %d -> Pak_RepairAsBlank -> Summing @ offset=%u, ret=%d\n", device, offset, result);

		if (result == 0) {
			// Found a valid header
			if (iterheader.filetype & PAKFILETYPE_BLANK) {
				// empty
			} else {
				break;
			}
		} else if (result == 1) {
			// Some kind of error
			return false;
		}

		// No header at this offset
		offset += pakGetBlockSize(device);

		if (offset - start > maxfilesize) {
			osSyncPrintf("Pak %d -> Pak_RepairAsBlank -> Fault Speads Over More Than One File - TERMINAL", device);
			*offsetptr = offset;
			return false;
		}
#else
		osSyncPrintf("Pak %d -> Pak_RepairAsBlank -> Summing @ offset=%u, ret=%d\n", device, offset, result);

		if (result == 0) {
			// Found a valid header
			if (iterheader.filetype & PAKFILETYPE_BLANK) {
				// empty
			} else {
				break;
			}
		}

		// No header at this offset
		offset += pakGetBlockSize(device);

		if (offset - start > maxfilesize) {
			osSyncPrintf("Pak %d -> Pak_RepairAsBlank -> Fault Speads Over More Than One File - TERMINAL", device);
			*offsetptr = offset;
			return false;
		}
#endif
	}

	bodylen = pakGetBodyLenByFileLen(offset - start);

	osSyncPrintf("Pak %d -> Pak_RepairAsBlank - St=%u, Ed=%u, Gap=%u, Blank Size=%u\n", device, start, offset, offset - start, bodylen);

	// Write the blank file ranging from to the start to the current offset
	result = pakWriteFile(device, start, PAKFILETYPE_BLANK, NULL, bodylen, NULL, NULL, 0, 1);

	if (result != 0) {
		osSyncPrintf("Pak %d -> Pak_RepairAsBlank - Fatal Error at tOffset %u\n", device, offset);
		*offsetptr = offset;
		return false;
	}

	*offsetptr = offset;
	return true;
}
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1b423c[] = "BOS";
const char var7f1b4244[] = "CAM";
const char var7f1b424c[] = "MPP";
const char var7f1b4254[] = "MPG";
const char var7f1b425c[] = "GAM";
const char var7f1ae0e4nb[] = "Pak %d -> Pak_GetCurrentSaveId - SaveID = %u\n";
#endif

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b411c[] = "Pak %d -> Pak_ValidateVersion - Start - Game File Size = %d\n";
const char var7f1b415c[] = "Pak %d -> Pak_ValidateVersion - Clearing cache 2\n";
const char var7f1b4190[] = "Pak %d -> Pak_ValidateVersion 1 - Loaded with ret=%d at offset %u\n";
const char var7f1b41d4[] = "Pak %d -> Pak_ValidateVersion 1 - Blank at %u\n";
const char var7f1b4204[] = "Pak %d -> Pak_ValidateVersion 2 - Loaded  at offset %u\n";
const char var7f1b423c[] = "BOS\n";
const char var7f1b4244[] = "CAM\n";
const char var7f1b424c[] = "MPP\n";
const char var7f1b4254[] = "MPG\n";
const char var7f1b425c[] = "GAM";
#endif

const char var7f1b4260[] = "> Pak_DefragPak_Level1 - Merge of two blanks failed";
const char var7f1b4294[] = "Pak %d - Pak_StartOne called from line %d in %s -> Flags = %0x\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b42d4[] = "\nPak_StartOne -> Pak%d, Modes -\n";
#else
const char var7f1b42d4[] = "\nPak_StartOne -> Pak%d, Modes -";
#endif

const char var7f1b42f8[] = "Memory,";
const char var7f1b4300[] = "Rumble,";
const char var7f1b4308[] = "Game Boy";
const char var7f1b4314[] = "\n";
const char var7f1b4318[] = "Pak %d -> %u Bytes of scratch for cache 2 memory at %0x\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4354[] = "\nPak%d -> Pak_EndOne - Called from line %d in %s : Modes -\n";
#else
const char var7f1b4354[] = "\nPak%d -> Pak_EndOne - Called from line %d in %s : Modes -";
#endif

const char var7f1b4390[] = "Memory,";
const char var7f1b4398[] = "Rumble,";
const char var7f1b43a0[] = "Game Boy";
const char var7f1b43ac[] = "\n";
const char var7f1b43b0[] = "Pak -> FATAL ERROR -> MEMORY INSTANCE ENDING IS NO LONGER SUPPORTED\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b43f8[] = "Pak -> Pak_MakeOne - Id=%d is finished\n";
#else
const char var7f1b43f8[] = "Pak -> Pak_MakeOne - Id=%d is finished";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae2d0nb[] = "pak.c";
const char var7f1ae2d8nb[] = "pak.c";
#endif

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4420[] = "Pak %d -> Pak_Memory_UpdateNoteInfo\n";
#else
const char var7f1b4420[] = "Pak %d -> Pak_Memory_UpdateNoteInfo";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae304nb[] = "pak.c";
const char var7f1ae30cnb[] = "pak.c";
#endif

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4448[] = "Pak %d -> Couldn't assertain the game note size\n";
const char var7f1b447c[] = "Pak %d -> Pak_AnalyseCurrentGameNote - Game note size = %uk\n";
#else
const char var7f1b4448[] = "Pak %d -> Couldn't assertain the game note size";
const char var7f1b447c[] = "Pak %d -> Pak_AnalyseCurrentGameNote - Game note size = %uk";
const char var7f1ae380nb[] = "Pak %d -> Pak_Memory_Init1";
const char var7f1ae39cnb[] = "pak.c";
const char var7f1ae3a4nb[] = "pak.c";
#endif

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b44bc[] = "Pak %d -> Searching for the game file\n";
#else
const char var7f1b44bc[] = "Pak %d -> Searching for the game file";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae3d4nb[] = "pak.c";
const char var7f1ae3dcnb[] = "pak.c";
const char var7f1ae3e4nb[] = "pak.c";
const char var7f1ae3ecnb[] = "pak.c";
const char var7f1ae3f4nb[] = "-forcewipe";
const char var7f1ae400nb[] = "-forcescrub";
const char var7f1ae40cnb[] = "Pak %d -> Initialisation - No swap file";
const char var7f1ae434nb[] = "Pak %d -> Initialisation - Found a swap file";
const char var7f1ae464nb[] = "pak.c";
const char var7f1ae46cnb[] = "pak.c";
const char var7f1ae474nb[] = "pak.c";
const char var7f1ae47cnb[] = "pak.c";
const char var7f1ae484nb[] = "pak.c";
const char var7f1ae48cnb[] = "pak.c";
const char var7f1ae494nb[] = "pak.c";
const char var7f1ae49cnb[] = "Pak %d -> About to wipe blocks %d to %d of the game file with the wipe byte %d";
#endif

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b44e4[] = "Pak %d -> Game file wipe failed\n";
#else
const char var7f1b44e4[] = "Pak %d -> Game file wipe failed";
#endif

const char var7f1b4508[] = "RWI : Warning : tOffset > gPakObj[PakNum].GameFileSize\n";

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae544nb[] = "pak.c";
const char var7f1ae54cnb[] = "pak.c";
const char var7f1ae554nb[] = "pak.c";
#endif

const char var7f1b4540[] = "Pak %d -> Pak_DeleteFile_Offset - DataSize = %u\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4574[] = "Pak %d -> Delete file offset (file id %d) failed\n";
const char var7f1b45a8[] = "Pak %d -> Delete file offset failed - Bad Offset passed\n";
#else
const char var7f1b4574[] = "Pak %d -> Delete file offset (file id %d) failed";
const char var7f1b45a8[] = "Pak %d -> Delete file offset failed - Bad Offset passed";
#endif

#if VERSION >= VERSION_PAL_FINAL
GLOBAL_ASM(
glabel pak0f11970c
/*  f11a1ec:	27bdf850 */ 	addiu	$sp,$sp,-1968
/*  f11a1f0:	afb4003c */ 	sw	$s4,0x3c($sp)
/*  f11a1f4:	0004a600 */ 	sll	$s4,$a0,0x18
/*  f11a1f8:	00147603 */ 	sra	$t6,$s4,0x18
/*  f11a1fc:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11a200:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a204:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a208:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a20c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a210:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11a214:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a218:	afa407b0 */ 	sw	$a0,0x7b0($sp)
/*  f11a21c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a220:	3c18800a */ 	lui	$t8,0x800a
/*  f11a224:	afb60044 */ 	sw	$s6,0x44($sp)
/*  f11a228:	27182920 */ 	addiu	$t8,$t8,0x2920
/*  f11a22c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a230:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11a234:	afbf004c */ 	sw	$ra,0x4c($sp)
/*  f11a238:	afb70048 */ 	sw	$s7,0x48($sp)
/*  f11a23c:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f11a240:	01f8b021 */ 	addu	$s6,$t7,$t8
/*  f11a244:	3419baba */ 	li	$t9,0xbaba
/*  f11a248:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11a24c:	01c0a025 */ 	move	$s4,$t6
/*  f11a250:	afb50040 */ 	sw	$s5,0x40($sp)
/*  f11a254:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f11a258:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f11a25c:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f11a260:	0000b825 */ 	move	$s7,$zero
/*  f11a264:	afa007a4 */ 	sw	$zero,0x7a4($sp)
/*  f11a268:	00008825 */ 	move	$s1,$zero
/*  f11a26c:	aed90260 */ 	sw	$t9,0x260($s6)
/*  f11a270:	a2c002be */ 	sb	$zero,0x2be($s6)
/*  f11a274:	0fc45cae */ 	jal	pak0f1167d8
/*  f11a278:	01202025 */ 	move	$a0,$t1
/*  f11a27c:	50400004 */ 	beqzl	$v0,.PF0f11a290
/*  f11a280:	8eca02a0 */ 	lw	$t2,0x2a0($s6)
/*  f11a284:	1000017c */ 	b	.PF0f11a878
/*  f11a288:	24020001 */ 	li	$v0,0x1
/*  f11a28c:	8eca02a0 */ 	lw	$t2,0x2a0($s6)
.PF0f11a290:
/*  f11a290:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f11a294:	27b50474 */ 	addiu	$s5,$sp,0x474
/*  f11a298:	114000a6 */ 	beqz	$t2,.PF0f11a534
/*  f11a29c:	27b30484 */ 	addiu	$s3,$sp,0x484
/*  f11a2a0:	2412ffff */ 	li	$s2,-1
/*  f11a2a4:	00142600 */ 	sll	$a0,$s4,0x18
.PF0f11a2a8:
/*  f11a2a8:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11a2ac:	01602025 */ 	move	$a0,$t3
/*  f11a2b0:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f11a2b4:	0fc46000 */ 	jal	pakReadHeaderAtOffset
/*  f11a2b8:	02a03025 */ 	move	$a2,$s5
/*  f11a2bc:	14400071 */ 	bnez	$v0,.PF0f11a484
/*  f11a2c0:	24010001 */ 	li	$at,0x1
/*  f11a2c4:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a2c8:	8fae03a4 */ 	lw	$t6,0x3a4($sp)
/*  f11a2cc:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a2d0:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f11a2d4:	304c0002 */ 	andi	$t4,$v0,0x2
/*  f11a2d8:	15800096 */ 	bnez	$t4,.PF0f11a534
/*  f11a2dc:	304d0004 */ 	andi	$t5,$v0,0x4
/*  f11a2e0:	15a00094 */ 	bnez	$t5,.PF0f11a534
/*  f11a2e4:	00001025 */ 	move	$v0,$zero
/*  f11a2e8:	8ed902a0 */ 	lw	$t9,0x2a0($s6)
/*  f11a2ec:	30cf0fff */ 	andi	$t7,$a2,0xfff
/*  f11a2f0:	01cfc021 */ 	addu	$t8,$t6,$t7
/*  f11a2f4:	0319082b */ 	sltu	$at,$t8,$t9
/*  f11a2f8:	14200010 */ 	bnez	$at,.PF0f11a33c
/*  f11a2fc:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11a300:	240a0001 */ 	li	$t2,0x1
/*  f11a304:	afaa0020 */ 	sw	$t2,0x20($sp)
/*  f11a308:	01202025 */ 	move	$a0,$t1
/*  f11a30c:	01c02825 */ 	move	$a1,$t6
/*  f11a310:	24060004 */ 	li	$a2,0x4
/*  f11a314:	00003825 */ 	move	$a3,$zero
/*  f11a318:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f11a31c:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f11a320:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f11a324:	0fc471d4 */ 	jal	pakWriteFile
/*  f11a328:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11a32c:	50400082 */ 	beqzl	$v0,.PF0f11a538
/*  f11a330:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
/*  f11a334:	10000079 */ 	b	.PF0f11a51c
/*  f11a338:	24170001 */ 	li	$s7,0x1
.PF0f11a33c:
/*  f11a33c:	1220003c */ 	beqz	$s1,.PF0f11a430
/*  f11a340:	afa0039c */ 	sw	$zero,0x39c($sp)
/*  f11a344:	27a703a8 */ 	addiu	$a3,$sp,0x3a8
/*  f11a348:	8fa80480 */ 	lw	$t0,0x480($sp)
.PF0f11a34c:
/*  f11a34c:	8ceb0000 */ 	lw	$t3,0x0($a3)
/*  f11a350:	00026100 */ 	sll	$t4,$v0,0x4
/*  f11a354:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11a358:	124b0033 */ 	beq	$s2,$t3,.PF0f11a428
/*  f11a35c:	026c3021 */ 	addu	$a2,$s3,$t4
/*  f11a360:	8cc3000c */ 	lw	$v1,0xc($a2)
/*  f11a364:	00086b40 */ 	sll	$t5,$t0,0xd
/*  f11a368:	000d7e42 */ 	srl	$t7,$t5,0x19
/*  f11a36c:	0003c340 */ 	sll	$t8,$v1,0xd
/*  f11a370:	0018ce42 */ 	srl	$t9,$t8,0x19
/*  f11a374:	15f9002c */ 	bne	$t7,$t9,.PF0f11a428
/*  f11a378:	00087500 */ 	sll	$t6,$t0,0x14
/*  f11a37c:	00035d00 */ 	sll	$t3,$v1,0x14
/*  f11a380:	000b65c2 */ 	srl	$t4,$t3,0x17
/*  f11a384:	000e55c2 */ 	srl	$t2,$t6,0x17
/*  f11a388:	24090001 */ 	li	$t1,0x1
/*  f11a38c:	014c082b */ 	sltu	$at,$t2,$t4
/*  f11a390:	10200009 */ 	beqz	$at,.PF0f11a3b8
/*  f11a394:	afa9039c */ 	sw	$t1,0x39c($sp)
/*  f11a398:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a39c:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11a3a0:	01a02025 */ 	move	$a0,$t5
/*  f11a3a4:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f11a3a8:	0fc467f0 */ 	jal	pakRepairAsBlank
/*  f11a3ac:	02a03025 */ 	move	$a2,$s5
/*  f11a3b0:	1000001f */ 	b	.PF0f11a430
/*  f11a3b4:	2c570001 */ 	sltiu	$s7,$v0,0x1
.PF0f11a3b8:
/*  f11a3b8:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a3bc:	00117900 */ 	sll	$t7,$s1,0x4
/*  f11a3c0:	026fc821 */ 	addu	$t9,$s3,$t7
/*  f11a3c4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11a3c8:	00114880 */ 	sll	$t1,$s1,0x2
/*  f11a3cc:	27ae03a8 */ 	addiu	$t6,$sp,0x3a8
/*  f11a3d0:	012e8021 */ 	addu	$s0,$t1,$t6
/*  f11a3d4:	03002025 */ 	move	$a0,$t8
/*  f11a3d8:	afb90054 */ 	sw	$t9,0x54($sp)
/*  f11a3dc:	00e02825 */ 	move	$a1,$a3
/*  f11a3e0:	0fc467f0 */ 	jal	pakRepairAsBlank
/*  f11a3e4:	afa7005c */ 	sw	$a3,0x5c($sp)
/*  f11a3e8:	8fa7005c */ 	lw	$a3,0x5c($sp)
/*  f11a3ec:	2c570001 */ 	sltiu	$s7,$v0,0x1
/*  f11a3f0:	02a02825 */ 	move	$a1,$s5
/*  f11a3f4:	acf20000 */ 	sw	$s2,0x0($a3)
/*  f11a3f8:	8fa40054 */ 	lw	$a0,0x54($sp)
/*  f11a3fc:	0c0127b8 */ 	jal	memcpy
/*  f11a400:	24060010 */ 	li	$a2,0x10
/*  f11a404:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f11a408:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a40c:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f11a410:	ae0b0000 */ 	sw	$t3,0x0($s0)
/*  f11a414:	8faa03a4 */ 	lw	$t2,0x3a4($sp)
/*  f11a418:	30cc0fff */ 	andi	$t4,$a2,0xfff
/*  f11a41c:	014c6821 */ 	addu	$t5,$t2,$t4
/*  f11a420:	10000003 */ 	b	.PF0f11a430
/*  f11a424:	afad03a4 */ 	sw	$t5,0x3a4($sp)
.PF0f11a428:
/*  f11a428:	1451ffc8 */ 	bne	$v0,$s1,.PF0f11a34c
/*  f11a42c:	24e70004 */ 	addiu	$a3,$a3,0x4
.PF0f11a430:
/*  f11a430:	8fb8039c */ 	lw	$t8,0x39c($sp)
/*  f11a434:	17000039 */ 	bnez	$t8,.PF0f11a51c
/*  f11a438:	00000000 */ 	nop
/*  f11a43c:	16e00037 */ 	bnez	$s7,.PF0f11a51c
/*  f11a440:	00117900 */ 	sll	$t7,$s1,0x4
/*  f11a444:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f11a448:	27a903a8 */ 	addiu	$t1,$sp,0x3a8
/*  f11a44c:	03298021 */ 	addu	$s0,$t9,$t1
/*  f11a450:	026f2021 */ 	addu	$a0,$s3,$t7
/*  f11a454:	02a02825 */ 	move	$a1,$s5
/*  f11a458:	0c0127b8 */ 	jal	memcpy
/*  f11a45c:	24060010 */ 	li	$a2,0x10
/*  f11a460:	8fae03a4 */ 	lw	$t6,0x3a4($sp)
/*  f11a464:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a468:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f11a46c:	ae0e0000 */ 	sw	$t6,0x0($s0)
/*  f11a470:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f11a474:	30ca0fff */ 	andi	$t2,$a2,0xfff
/*  f11a478:	016a6021 */ 	addu	$t4,$t3,$t2
/*  f11a47c:	10000027 */ 	b	.PF0f11a51c
/*  f11a480:	afac03a4 */ 	sw	$t4,0x3a4($sp)
.PF0f11a484:
/*  f11a484:	14410003 */ 	bne	$v0,$at,.PF0f11a494
/*  f11a488:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a48c:	100000fa */ 	b	.PF0f11a878
/*  f11a490:	24020001 */ 	li	$v0,0x1
.PF0f11a494:
/*  f11a494:	24010007 */ 	li	$at,0x7
/*  f11a498:	14410007 */ 	bne	$v0,$at,.PF0f11a4b8
/*  f11a49c:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11a4a0:	01a02025 */ 	move	$a0,$t5
/*  f11a4a4:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f11a4a8:	0fc467f0 */ 	jal	pakRepairAsBlank
/*  f11a4ac:	00003025 */ 	move	$a2,$zero
/*  f11a4b0:	1000001a */ 	b	.PF0f11a51c
/*  f11a4b4:	2c570001 */ 	sltiu	$s7,$v0,0x1
.PF0f11a4b8:
/*  f11a4b8:	2401000f */ 	li	$at,0xf
/*  f11a4bc:	1441000a */ 	bne	$v0,$at,.PF0f11a4e8
/*  f11a4c0:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a4c4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11a4c8:	03002025 */ 	move	$a0,$t8
/*  f11a4cc:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f11a4d0:	0fc467f0 */ 	jal	pakRepairAsBlank
/*  f11a4d4:	02a03025 */ 	move	$a2,$s5
/*  f11a4d8:	14400010 */ 	bnez	$v0,.PF0f11a51c
/*  f11a4dc:	00000000 */ 	nop
/*  f11a4e0:	10000014 */ 	b	.PF0f11a534
/*  f11a4e4:	24170001 */ 	li	$s7,0x1
.PF0f11a4e8:
/*  f11a4e8:	24100009 */ 	li	$s0,0x9
/*  f11a4ec:	14500004 */ 	bne	$v0,$s0,.PF0f11a500
/*  f11a4f0:	24010004 */ 	li	$at,0x4
/*  f11a4f4:	240f0001 */ 	li	$t7,0x1
/*  f11a4f8:	1000000e */ 	b	.PF0f11a534
/*  f11a4fc:	afaf07a4 */ 	sw	$t7,0x7a4($sp)
.PF0f11a500:
/*  f11a500:	1041000c */ 	beq	$v0,$at,.PF0f11a534
/*  f11a504:	24170001 */ 	li	$s7,0x1
/*  f11a508:	2401000b */ 	li	$at,0xb
/*  f11a50c:	5441000a */ 	bnel	$v0,$at,.PF0f11a538
/*  f11a510:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
/*  f11a514:	10000008 */ 	b	.PF0f11a538
/*  f11a518:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
.PF0f11a51c:
/*  f11a51c:	16e00005 */ 	bnez	$s7,.PF0f11a534
/*  f11a520:	8fb903a4 */ 	lw	$t9,0x3a4($sp)
/*  f11a524:	8ec902a0 */ 	lw	$t1,0x2a0($s6)
/*  f11a528:	0329082b */ 	sltu	$at,$t9,$t1
/*  f11a52c:	5420ff5e */ 	bnezl	$at,.PF0f11a2a8
/*  f11a530:	00142600 */ 	sll	$a0,$s4,0x18
.PF0f11a534:
/*  f11a534:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
.PF0f11a538:
/*  f11a538:	24100009 */ 	li	$s0,0x9
/*  f11a53c:	27b50474 */ 	addiu	$s5,$sp,0x474
/*  f11a540:	15c0003d */ 	bnez	$t6,.PF0f11a638
/*  f11a544:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f11a548:	56e0003c */ 	bnezl	$s7,.PF0f11a63c
/*  f11a54c:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f11a550:	8ecb02a0 */ 	lw	$t3,0x2a0($s6)
/*  f11a554:	11600038 */ 	beqz	$t3,.PF0f11a638
/*  f11a558:	00142600 */ 	sll	$a0,$s4,0x18
.PF0f11a55c:
/*  f11a55c:	00045603 */ 	sra	$t2,$a0,0x18
/*  f11a560:	01402025 */ 	move	$a0,$t2
/*  f11a564:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f11a568:	0fc46000 */ 	jal	pakReadHeaderAtOffset
/*  f11a56c:	02a03025 */ 	move	$a2,$s5
/*  f11a570:	14400017 */ 	bnez	$v0,.PF0f11a5d0
/*  f11a574:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a578:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f11a57c:	304c0002 */ 	andi	$t4,$v0,0x2
/*  f11a580:	11800003 */ 	beqz	$t4,.PF0f11a590
/*  f11a584:	8fad03a4 */ 	lw	$t5,0x3a4($sp)
/*  f11a588:	1000000b */ 	b	.PF0f11a5b8
/*  f11a58c:	30430004 */ 	andi	$v1,$v0,0x4
.PF0f11a590:
/*  f11a590:	11a00003 */ 	beqz	$t5,.PF0f11a5a0
/*  f11a594:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f11a598:	10000007 */ 	b	.PF0f11a5b8
/*  f11a59c:	30430004 */ 	andi	$v1,$v0,0x4
.PF0f11a5a0:
/*  f11a5a0:	0008c4c2 */ 	srl	$t8,$t0,0x13
/*  f11a5a4:	30430004 */ 	andi	$v1,$v0,0x4
/*  f11a5a8:	10600003 */ 	beqz	$v1,.PF0f11a5b8
/*  f11a5ac:	aed80260 */ 	sw	$t8,0x260($s6)
/*  f11a5b0:	100000b1 */ 	b	.PF0f11a878
/*  f11a5b4:	00001025 */ 	move	$v0,$zero
.PF0f11a5b8:
/*  f11a5b8:	1460001f */ 	bnez	$v1,.PF0f11a638
/*  f11a5bc:	8faf03a4 */ 	lw	$t7,0x3a4($sp)
/*  f11a5c0:	30d90fff */ 	andi	$t9,$a2,0xfff
/*  f11a5c4:	01f94821 */ 	addu	$t1,$t7,$t9
/*  f11a5c8:	10000011 */ 	b	.PF0f11a610
/*  f11a5cc:	afa903a4 */ 	sw	$t1,0x3a4($sp)
.PF0f11a5d0:
/*  f11a5d0:	14500008 */ 	bne	$v0,$s0,.PF0f11a5f4
/*  f11a5d4:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a5d8:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f11a5dc:	30ca0fff */ 	andi	$t2,$a2,0xfff
/*  f11a5e0:	240e0001 */ 	li	$t6,0x1
/*  f11a5e4:	016a6021 */ 	addu	$t4,$t3,$t2
/*  f11a5e8:	afae07a4 */ 	sw	$t6,0x7a4($sp)
/*  f11a5ec:	10000008 */ 	b	.PF0f11a610
/*  f11a5f0:	afac03a4 */ 	sw	$t4,0x3a4($sp)
.PF0f11a5f4:
/*  f11a5f4:	24010001 */ 	li	$at,0x1
/*  f11a5f8:	14410003 */ 	bne	$v0,$at,.PF0f11a608
/*  f11a5fc:	00000000 */ 	nop
/*  f11a600:	1000009d */ 	b	.PF0f11a878
/*  f11a604:	24020001 */ 	li	$v0,0x1
.PF0f11a608:
/*  f11a608:	1000009b */ 	b	.PF0f11a878
/*  f11a60c:	24020001 */ 	li	$v0,0x1
.PF0f11a610:
/*  f11a610:	8fad07a4 */ 	lw	$t5,0x7a4($sp)
/*  f11a614:	8fb803a4 */ 	lw	$t8,0x3a4($sp)
/*  f11a618:	55a00008 */ 	bnezl	$t5,.PF0f11a63c
/*  f11a61c:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f11a620:	56e00006 */ 	bnezl	$s7,.PF0f11a63c
/*  f11a624:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f11a628:	8ecf02a0 */ 	lw	$t7,0x2a0($s6)
/*  f11a62c:	030f082b */ 	sltu	$at,$t8,$t7
/*  f11a630:	5420ffca */ 	bnezl	$at,.PF0f11a55c
/*  f11a634:	00142600 */ 	sll	$a0,$s4,0x18
.PF0f11a638:
/*  f11a638:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
.PF0f11a63c:
/*  f11a63c:	17200079 */ 	bnez	$t9,.PF0f11a824
/*  f11a640:	00000000 */ 	nop
/*  f11a644:	16e00077 */ 	bnez	$s7,.PF0f11a824
/*  f11a648:	00000000 */ 	nop
/*  f11a64c:	8ec902a0 */ 	lw	$t1,0x2a0($s6)
/*  f11a650:	00008025 */ 	move	$s0,$zero
/*  f11a654:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f11a658:	1120002e */ 	beqz	$t1,.PF0f11a714
/*  f11a65c:	27b2007c */ 	addiu	$s2,$sp,0x7c
/*  f11a660:	24110001 */ 	li	$s1,0x1
.PF0f11a664:
/*  f11a664:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a668:	00047603 */ 	sra	$t6,$a0,0x18
/*  f11a66c:	01c02025 */ 	move	$a0,$t6
/*  f11a670:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f11a674:	0fc46000 */ 	jal	pakReadHeaderAtOffset
/*  f11a678:	02a03025 */ 	move	$a2,$s5
/*  f11a67c:	14400025 */ 	bnez	$v0,.PF0f11a714
/*  f11a680:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a684:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f11a688:	304b0002 */ 	andi	$t3,$v0,0x2
/*  f11a68c:	1560001a */ 	bnez	$t3,.PF0f11a6f8
/*  f11a690:	304a0004 */ 	andi	$t2,$v0,0x4
/*  f11a694:	1540001f */ 	bnez	$t2,.PF0f11a714
/*  f11a698:	00002825 */ 	move	$a1,$zero
/*  f11a69c:	1a00000e */ 	blez	$s0,.PF0f11a6d8
/*  f11a6a0:	00001825 */ 	move	$v1,$zero
/*  f11a6a4:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f11a6a8:	27a2007c */ 	addiu	$v0,$sp,0x7c
/*  f11a6ac:	000824c2 */ 	srl	$a0,$t0,0x13
.PF0f11a6b0:
/*  f11a6b0:	8c4c0000 */ 	lw	$t4,0x0($v0)
/*  f11a6b4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11a6b8:	148c0005 */ 	bne	$a0,$t4,.PF0f11a6d0
/*  f11a6bc:	00000000 */ 	nop
/*  f11a6c0:	8c4d0004 */ 	lw	$t5,0x4($v0)
/*  f11a6c4:	02202825 */ 	move	$a1,$s1
/*  f11a6c8:	25b80001 */ 	addiu	$t8,$t5,0x1
/*  f11a6cc:	ac580004 */ 	sw	$t8,0x4($v0)
.PF0f11a6d0:
/*  f11a6d0:	1470fff7 */ 	bne	$v1,$s0,.PF0f11a6b0
/*  f11a6d4:	24420008 */ 	addiu	$v0,$v0,0x8
.PF0f11a6d8:
/*  f11a6d8:	14a00007 */ 	bnez	$a1,.PF0f11a6f8
/*  f11a6dc:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f11a6e0:	001078c0 */ 	sll	$t7,$s0,0x3
/*  f11a6e4:	024f1021 */ 	addu	$v0,$s2,$t7
/*  f11a6e8:	0008ccc2 */ 	srl	$t9,$t0,0x13
/*  f11a6ec:	ac590000 */ 	sw	$t9,0x0($v0)
/*  f11a6f0:	ac510004 */ 	sw	$s1,0x4($v0)
/*  f11a6f4:	26100001 */ 	addiu	$s0,$s0,0x1
.PF0f11a6f8:
/*  f11a6f8:	8fa903a4 */ 	lw	$t1,0x3a4($sp)
/*  f11a6fc:	8ecc02a0 */ 	lw	$t4,0x2a0($s6)
/*  f11a700:	30ce0fff */ 	andi	$t6,$a2,0xfff
/*  f11a704:	012e5821 */ 	addu	$t3,$t1,$t6
/*  f11a708:	016c082b */ 	sltu	$at,$t3,$t4
/*  f11a70c:	1420ffd5 */ 	bnez	$at,.PF0f11a664
/*  f11a710:	afab03a4 */ 	sw	$t3,0x3a4($sp)
.PF0f11a714:
/*  f11a714:	2a010002 */ 	slti	$at,$s0,0x2
/*  f11a718:	14200040 */ 	bnez	$at,.PF0f11a81c
/*  f11a71c:	27b2007c */ 	addiu	$s2,$sp,0x7c
/*  f11a720:	2406ffff */ 	li	$a2,-1
/*  f11a724:	2405ffff */ 	li	$a1,-1
/*  f11a728:	1a00000b */ 	blez	$s0,.PF0f11a758
/*  f11a72c:	00001825 */ 	move	$v1,$zero
/*  f11a730:	27a2007c */ 	addiu	$v0,$sp,0x7c
.PF0f11a734:
/*  f11a734:	8c440004 */ 	lw	$a0,0x4($v0)
/*  f11a738:	00a4082a */ 	slt	$at,$a1,$a0
/*  f11a73c:	50200004 */ 	beqzl	$at,.PF0f11a750
/*  f11a740:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11a744:	00603025 */ 	move	$a2,$v1
/*  f11a748:	00802825 */ 	move	$a1,$a0
/*  f11a74c:	24630001 */ 	addiu	$v1,$v1,0x1
.PF0f11a750:
/*  f11a750:	1470fff8 */ 	bne	$v1,$s0,.PF0f11a734
/*  f11a754:	24420008 */ 	addiu	$v0,$v0,0x8
.PF0f11a758:
/*  f11a758:	2401ffff */ 	li	$at,-1
/*  f11a75c:	10c10031 */ 	beq	$a2,$at,.PF0f11a824
/*  f11a760:	000668c0 */ 	sll	$t5,$a2,0x3
/*  f11a764:	024dc021 */ 	addu	$t8,$s2,$t5
/*  f11a768:	8f020000 */ 	lw	$v0,0x0($t8)
/*  f11a76c:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a770:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11a774:	2c410010 */ 	sltiu	$at,$v0,0x10
/*  f11a778:	10200004 */ 	beqz	$at,.PF0f11a78c
/*  f11a77c:	aec20260 */ 	sw	$v0,0x260($s6)
/*  f11a780:	0fc45c5b */ 	jal	pakGenerateSerial
/*  f11a784:	01e02025 */ 	move	$a0,$t7
/*  f11a788:	aec20260 */ 	sw	$v0,0x260($s6)
.PF0f11a78c:
/*  f11a78c:	8ed902a0 */ 	lw	$t9,0x2a0($s6)
/*  f11a790:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f11a794:	13200023 */ 	beqz	$t9,.PF0f11a824
.PF0f11a798:
/*  f11a798:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a79c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11a7a0:	01202025 */ 	move	$a0,$t1
/*  f11a7a4:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f11a7a8:	0fc46000 */ 	jal	pakReadHeaderAtOffset
/*  f11a7ac:	02a03025 */ 	move	$a2,$s5
/*  f11a7b0:	1440001c */ 	bnez	$v0,.PF0f11a824
/*  f11a7b4:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11a7b8:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f11a7bc:	304e0002 */ 	andi	$t6,$v0,0x2
/*  f11a7c0:	15c0000d */ 	bnez	$t6,.PF0f11a7f8
/*  f11a7c4:	304b0004 */ 	andi	$t3,$v0,0x4
/*  f11a7c8:	15600016 */ 	bnez	$t3,.PF0f11a824
/*  f11a7cc:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f11a7d0:	8ecc0260 */ 	lw	$t4,0x260($s6)
/*  f11a7d4:	000854c2 */ 	srl	$t2,$t0,0x13
/*  f11a7d8:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a7dc:	114c0006 */ 	beq	$t2,$t4,.PF0f11a7f8
/*  f11a7e0:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11a7e4:	01a02025 */ 	move	$a0,$t5
/*  f11a7e8:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f11a7ec:	0fc467d6 */ 	jal	pakWriteBlankFile
/*  f11a7f0:	02a03025 */ 	move	$a2,$s5
/*  f11a7f4:	8fa6047c */ 	lw	$a2,0x47c($sp)
.PF0f11a7f8:
/*  f11a7f8:	8fb803a4 */ 	lw	$t8,0x3a4($sp)
/*  f11a7fc:	8ece02a0 */ 	lw	$t6,0x2a0($s6)
/*  f11a800:	30cf0fff */ 	andi	$t7,$a2,0xfff
/*  f11a804:	030fc821 */ 	addu	$t9,$t8,$t7
/*  f11a808:	032e082b */ 	sltu	$at,$t9,$t6
/*  f11a80c:	1420ffe2 */ 	bnez	$at,.PF0f11a798
/*  f11a810:	afb903a4 */ 	sw	$t9,0x3a4($sp)
/*  f11a814:	10000003 */ 	b	.PF0f11a824
/*  f11a818:	00000000 */ 	nop
.PF0f11a81c:
/*  f11a81c:	8fab007c */ 	lw	$t3,0x7c($sp)
/*  f11a820:	aecb0260 */ 	sw	$t3,0x260($s6)
.PF0f11a824:
/*  f11a824:	12e00003 */ 	beqz	$s7,.PF0f11a834
/*  f11a828:	8faa07a4 */ 	lw	$t2,0x7a4($sp)
/*  f11a82c:	10000012 */ 	b	.PF0f11a878
/*  f11a830:	2402ffff */ 	li	$v0,-1
.PF0f11a834:
/*  f11a834:	11400003 */ 	beqz	$t2,.PF0f11a844
/*  f11a838:	24010004 */ 	li	$at,0x4
/*  f11a83c:	1000000e */ 	b	.PF0f11a878
/*  f11a840:	2402ffff */ 	li	$v0,-1
.PF0f11a844:
/*  f11a844:	5281000c */ 	beql	$s4,$at,.PF0f11a878
/*  f11a848:	00001025 */ 	move	$v0,$zero
/*  f11a84c:	8ecc0260 */ 	lw	$t4,0x260($s6)
/*  f11a850:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a854:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11a858:	55800007 */ 	bnezl	$t4,.PF0f11a878
/*  f11a85c:	00001025 */ 	move	$v0,$zero
/*  f11a860:	0fc45c5b */ 	jal	pakGenerateSerial
/*  f11a864:	01a02025 */ 	move	$a0,$t5
/*  f11a868:	aec20260 */ 	sw	$v0,0x260($s6)
/*  f11a86c:	10000002 */ 	b	.PF0f11a878
/*  f11a870:	2402ffff */ 	li	$v0,-1
/*  f11a874:	00001025 */ 	move	$v0,$zero
.PF0f11a878:
/*  f11a878:	8fbf004c */ 	lw	$ra,0x4c($sp)
/*  f11a87c:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f11a880:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f11a884:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f11a888:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f11a88c:	8fb4003c */ 	lw	$s4,0x3c($sp)
/*  f11a890:	8fb50040 */ 	lw	$s5,0x40($sp)
/*  f11a894:	8fb60044 */ 	lw	$s6,0x44($sp)
/*  f11a898:	8fb70048 */ 	lw	$s7,0x48($sp)
/*  f11a89c:	03e00008 */ 	jr	$ra
/*  f11a8a0:	27bd07b0 */ 	addiu	$sp,$sp,0x7b0
);
#elif VERSION >= VERSION_NTSC_FINAL
GLOBAL_ASM(
glabel pak0f11970c
/*  f11970c:	27bdf850 */ 	addiu	$sp,$sp,-1968
/*  f119710:	afb50040 */ 	sw	$s5,0x40($sp)
/*  f119714:	0004ae00 */ 	sll	$s5,$a0,0x18
/*  f119718:	00157603 */ 	sra	$t6,$s5,0x18
/*  f11971c:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f119720:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f119724:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119728:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11972c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119730:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f119734:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119738:	afa407b0 */ 	sw	$a0,0x7b0($sp)
/*  f11973c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f119740:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f119744:	afb60044 */ 	sw	$s6,0x44($sp)
/*  f119748:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11974c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119750:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f119754:	afbf004c */ 	sw	$ra,0x4c($sp)
/*  f119758:	afb70048 */ 	sw	$s7,0x48($sp)
/*  f11975c:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f119760:	01f8b021 */ 	addu	$s6,$t7,$t8
/*  f119764:	3419baba */ 	dli	$t9,0xbaba
/*  f119768:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11976c:	01c0a825 */ 	or	$s5,$t6,$zero
/*  f119770:	afb4003c */ 	sw	$s4,0x3c($sp)
/*  f119774:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f119778:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f11977c:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f119780:	0000b825 */ 	or	$s7,$zero,$zero
/*  f119784:	afa007a4 */ 	sw	$zero,0x7a4($sp)
/*  f119788:	00008825 */ 	or	$s1,$zero,$zero
/*  f11978c:	aed90260 */ 	sw	$t9,0x260($s6)
/*  f119790:	a2c002be */ 	sb	$zero,0x2be($s6)
/*  f119794:	0fc459f6 */ 	jal	pak0f1167d8
/*  f119798:	01202025 */ 	or	$a0,$t1,$zero
/*  f11979c:	50400004 */ 	beqzl	$v0,.L0f1197b0
/*  f1197a0:	8eca02a0 */ 	lw	$t2,0x2a0($s6)
/*  f1197a4:	10000175 */ 	beqz	$zero,.L0f119d7c
/*  f1197a8:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f1197ac:	8eca02a0 */ 	lw	$t2,0x2a0($s6)
.L0f1197b0:
/*  f1197b0:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f1197b4:	27b40474 */ 	addiu	$s4,$sp,0x474
/*  f1197b8:	114000a6 */ 	beqz	$t2,.L0f119a54
/*  f1197bc:	27b30484 */ 	addiu	$s3,$sp,0x484
/*  f1197c0:	2412ffff */ 	addiu	$s2,$zero,-1
/*  f1197c4:	00152600 */ 	sll	$a0,$s5,0x18
.L0f1197c8:
/*  f1197c8:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f1197cc:	01602025 */ 	or	$a0,$t3,$zero
/*  f1197d0:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f1197d4:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f1197d8:	02803025 */ 	or	$a2,$s4,$zero
/*  f1197dc:	14400071 */ 	bnez	$v0,.L0f1199a4
/*  f1197e0:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1197e4:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f1197e8:	8fae03a4 */ 	lw	$t6,0x3a4($sp)
/*  f1197ec:	00152600 */ 	sll	$a0,$s5,0x18
/*  f1197f0:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f1197f4:	304c0002 */ 	andi	$t4,$v0,0x2
/*  f1197f8:	15800096 */ 	bnez	$t4,.L0f119a54
/*  f1197fc:	304d0004 */ 	andi	$t5,$v0,0x4
/*  f119800:	15a00094 */ 	bnez	$t5,.L0f119a54
/*  f119804:	00001025 */ 	or	$v0,$zero,$zero
/*  f119808:	8ed902a0 */ 	lw	$t9,0x2a0($s6)
/*  f11980c:	30cf0fff */ 	andi	$t7,$a2,0xfff
/*  f119810:	01cfc021 */ 	addu	$t8,$t6,$t7
/*  f119814:	0319082b */ 	sltu	$at,$t8,$t9
/*  f119818:	14200010 */ 	bnez	$at,.L0f11985c
/*  f11981c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f119820:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f119824:	afaa0020 */ 	sw	$t2,0x20($sp)
/*  f119828:	01202025 */ 	or	$a0,$t1,$zero
/*  f11982c:	01c02825 */ 	or	$a1,$t6,$zero
/*  f119830:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f119834:	00003825 */ 	or	$a3,$zero,$zero
/*  f119838:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f11983c:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f119840:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f119844:	0fc46f15 */ 	jal	pakWriteFile
/*  f119848:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11984c:	50400082 */ 	beqzl	$v0,.L0f119a58
/*  f119850:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
/*  f119854:	10000079 */ 	beqz	$zero,.L0f119a3c
/*  f119858:	24170001 */ 	addiu	$s7,$zero,0x1
.L0f11985c:
/*  f11985c:	1220003c */ 	beqz	$s1,.L0f119950
/*  f119860:	afa0039c */ 	sw	$zero,0x39c($sp)
/*  f119864:	27a703a8 */ 	addiu	$a3,$sp,0x3a8
/*  f119868:	8fa80480 */ 	lw	$t0,0x480($sp)
.L0f11986c:
/*  f11986c:	8ceb0000 */ 	lw	$t3,0x0($a3)
/*  f119870:	00026100 */ 	sll	$t4,$v0,0x4
/*  f119874:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f119878:	124b0033 */ 	beq	$s2,$t3,.L0f119948
/*  f11987c:	026c3021 */ 	addu	$a2,$s3,$t4
/*  f119880:	8cc3000c */ 	lw	$v1,0xc($a2)
/*  f119884:	00086b40 */ 	sll	$t5,$t0,0xd
/*  f119888:	000d7e42 */ 	srl	$t7,$t5,0x19
/*  f11988c:	0003c340 */ 	sll	$t8,$v1,0xd
/*  f119890:	0018ce42 */ 	srl	$t9,$t8,0x19
/*  f119894:	15f9002c */ 	bne	$t7,$t9,.L0f119948
/*  f119898:	00087500 */ 	sll	$t6,$t0,0x14
/*  f11989c:	00035d00 */ 	sll	$t3,$v1,0x14
/*  f1198a0:	000b65c2 */ 	srl	$t4,$t3,0x17
/*  f1198a4:	000e55c2 */ 	srl	$t2,$t6,0x17
/*  f1198a8:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f1198ac:	014c082b */ 	sltu	$at,$t2,$t4
/*  f1198b0:	10200009 */ 	beqz	$at,.L0f1198d8
/*  f1198b4:	afa9039c */ 	sw	$t1,0x39c($sp)
/*  f1198b8:	00152600 */ 	sll	$a0,$s5,0x18
/*  f1198bc:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f1198c0:	01a02025 */ 	or	$a0,$t5,$zero
/*  f1198c4:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f1198c8:	0fc46538 */ 	jal	pakRepairAsBlank
/*  f1198cc:	02803025 */ 	or	$a2,$s4,$zero
/*  f1198d0:	1000001f */ 	beqz	$zero,.L0f119950
/*  f1198d4:	2c570001 */ 	sltiu	$s7,$v0,0x1
.L0f1198d8:
/*  f1198d8:	00152600 */ 	sll	$a0,$s5,0x18
/*  f1198dc:	00117900 */ 	sll	$t7,$s1,0x4
/*  f1198e0:	026fc821 */ 	addu	$t9,$s3,$t7
/*  f1198e4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1198e8:	00114880 */ 	sll	$t1,$s1,0x2
/*  f1198ec:	27ae03a8 */ 	addiu	$t6,$sp,0x3a8
/*  f1198f0:	012e8021 */ 	addu	$s0,$t1,$t6
/*  f1198f4:	03002025 */ 	or	$a0,$t8,$zero
/*  f1198f8:	afb90054 */ 	sw	$t9,0x54($sp)
/*  f1198fc:	00e02825 */ 	or	$a1,$a3,$zero
/*  f119900:	0fc46538 */ 	jal	pakRepairAsBlank
/*  f119904:	afa7005c */ 	sw	$a3,0x5c($sp)
/*  f119908:	8fa7005c */ 	lw	$a3,0x5c($sp)
/*  f11990c:	2c570001 */ 	sltiu	$s7,$v0,0x1
/*  f119910:	02802825 */ 	or	$a1,$s4,$zero
/*  f119914:	acf20000 */ 	sw	$s2,0x0($a3)
/*  f119918:	8fa40054 */ 	lw	$a0,0x54($sp)
/*  f11991c:	0c012978 */ 	jal	memcpy
/*  f119920:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f119924:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f119928:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f11992c:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f119930:	ae0b0000 */ 	sw	$t3,0x0($s0)
/*  f119934:	8faa03a4 */ 	lw	$t2,0x3a4($sp)
/*  f119938:	30cc0fff */ 	andi	$t4,$a2,0xfff
/*  f11993c:	014c6821 */ 	addu	$t5,$t2,$t4
/*  f119940:	10000003 */ 	beqz	$zero,.L0f119950
/*  f119944:	afad03a4 */ 	sw	$t5,0x3a4($sp)
.L0f119948:
/*  f119948:	1451ffc8 */ 	bne	$v0,$s1,.L0f11986c
/*  f11994c:	24e70004 */ 	addiu	$a3,$a3,0x4
.L0f119950:
/*  f119950:	8fb8039c */ 	lw	$t8,0x39c($sp)
/*  f119954:	17000039 */ 	bnez	$t8,.L0f119a3c
/*  f119958:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11995c:	16e00037 */ 	bnez	$s7,.L0f119a3c
/*  f119960:	00117900 */ 	sll	$t7,$s1,0x4
/*  f119964:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f119968:	27a903a8 */ 	addiu	$t1,$sp,0x3a8
/*  f11996c:	03298021 */ 	addu	$s0,$t9,$t1
/*  f119970:	026f2021 */ 	addu	$a0,$s3,$t7
/*  f119974:	02802825 */ 	or	$a1,$s4,$zero
/*  f119978:	0c012978 */ 	jal	memcpy
/*  f11997c:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f119980:	8fae03a4 */ 	lw	$t6,0x3a4($sp)
/*  f119984:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f119988:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f11998c:	ae0e0000 */ 	sw	$t6,0x0($s0)
/*  f119990:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f119994:	30ca0fff */ 	andi	$t2,$a2,0xfff
/*  f119998:	016a6021 */ 	addu	$t4,$t3,$t2
/*  f11999c:	10000027 */ 	beqz	$zero,.L0f119a3c
/*  f1199a0:	afac03a4 */ 	sw	$t4,0x3a4($sp)
.L0f1199a4:
/*  f1199a4:	14410003 */ 	bne	$v0,$at,.L0f1199b4
/*  f1199a8:	00152600 */ 	sll	$a0,$s5,0x18
/*  f1199ac:	100000f3 */ 	beqz	$zero,.L0f119d7c
/*  f1199b0:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1199b4:
/*  f1199b4:	24010007 */ 	addiu	$at,$zero,0x7
/*  f1199b8:	14410007 */ 	bne	$v0,$at,.L0f1199d8
/*  f1199bc:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f1199c0:	01a02025 */ 	or	$a0,$t5,$zero
/*  f1199c4:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f1199c8:	0fc46538 */ 	jal	pakRepairAsBlank
/*  f1199cc:	00003025 */ 	or	$a2,$zero,$zero
/*  f1199d0:	1000001a */ 	beqz	$zero,.L0f119a3c
/*  f1199d4:	2c570001 */ 	sltiu	$s7,$v0,0x1
.L0f1199d8:
/*  f1199d8:	2401000f */ 	addiu	$at,$zero,0xf
/*  f1199dc:	1441000a */ 	bne	$v0,$at,.L0f119a08
/*  f1199e0:	00152600 */ 	sll	$a0,$s5,0x18
/*  f1199e4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f1199e8:	03002025 */ 	or	$a0,$t8,$zero
/*  f1199ec:	27a503a4 */ 	addiu	$a1,$sp,0x3a4
/*  f1199f0:	0fc46538 */ 	jal	pakRepairAsBlank
/*  f1199f4:	02803025 */ 	or	$a2,$s4,$zero
/*  f1199f8:	14400010 */ 	bnez	$v0,.L0f119a3c
/*  f1199fc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119a00:	10000014 */ 	beqz	$zero,.L0f119a54
/*  f119a04:	24170001 */ 	addiu	$s7,$zero,0x1
.L0f119a08:
/*  f119a08:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f119a0c:	14500004 */ 	bne	$v0,$s0,.L0f119a20
/*  f119a10:	24010004 */ 	addiu	$at,$zero,0x4
/*  f119a14:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f119a18:	1000000e */ 	beqz	$zero,.L0f119a54
/*  f119a1c:	afaf07a4 */ 	sw	$t7,0x7a4($sp)
.L0f119a20:
/*  f119a20:	1041000c */ 	beq	$v0,$at,.L0f119a54
/*  f119a24:	24170001 */ 	addiu	$s7,$zero,0x1
/*  f119a28:	2401000b */ 	addiu	$at,$zero,0xb
/*  f119a2c:	5441000a */ 	bnel	$v0,$at,.L0f119a58
/*  f119a30:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
/*  f119a34:	10000008 */ 	beqz	$zero,.L0f119a58
/*  f119a38:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
.L0f119a3c:
/*  f119a3c:	16e00005 */ 	bnez	$s7,.L0f119a54
/*  f119a40:	8fb903a4 */ 	lw	$t9,0x3a4($sp)
/*  f119a44:	8ec902a0 */ 	lw	$t1,0x2a0($s6)
/*  f119a48:	0329082b */ 	sltu	$at,$t9,$t1
/*  f119a4c:	5420ff5e */ 	bnezl	$at,.L0f1197c8
/*  f119a50:	00152600 */ 	sll	$a0,$s5,0x18
.L0f119a54:
/*  f119a54:	8fae07a4 */ 	lw	$t6,0x7a4($sp)
.L0f119a58:
/*  f119a58:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f119a5c:	27b40474 */ 	addiu	$s4,$sp,0x474
/*  f119a60:	15c0003d */ 	bnez	$t6,.L0f119b58
/*  f119a64:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f119a68:	56e0003c */ 	bnezl	$s7,.L0f119b5c
/*  f119a6c:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f119a70:	8ecb02a0 */ 	lw	$t3,0x2a0($s6)
/*  f119a74:	11600038 */ 	beqz	$t3,.L0f119b58
/*  f119a78:	00152600 */ 	sll	$a0,$s5,0x18
.L0f119a7c:
/*  f119a7c:	00045603 */ 	sra	$t2,$a0,0x18
/*  f119a80:	01402025 */ 	or	$a0,$t2,$zero
/*  f119a84:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f119a88:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f119a8c:	02803025 */ 	or	$a2,$s4,$zero
/*  f119a90:	14400017 */ 	bnez	$v0,.L0f119af0
/*  f119a94:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f119a98:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f119a9c:	304c0002 */ 	andi	$t4,$v0,0x2
/*  f119aa0:	11800003 */ 	beqz	$t4,.L0f119ab0
/*  f119aa4:	8fad03a4 */ 	lw	$t5,0x3a4($sp)
/*  f119aa8:	1000000b */ 	beqz	$zero,.L0f119ad8
/*  f119aac:	30430004 */ 	andi	$v1,$v0,0x4
.L0f119ab0:
/*  f119ab0:	11a00003 */ 	beqz	$t5,.L0f119ac0
/*  f119ab4:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f119ab8:	10000007 */ 	beqz	$zero,.L0f119ad8
/*  f119abc:	30430004 */ 	andi	$v1,$v0,0x4
.L0f119ac0:
/*  f119ac0:	0008c4c2 */ 	srl	$t8,$t0,0x13
/*  f119ac4:	30430004 */ 	andi	$v1,$v0,0x4
/*  f119ac8:	10600003 */ 	beqz	$v1,.L0f119ad8
/*  f119acc:	aed80260 */ 	sw	$t8,0x260($s6)
/*  f119ad0:	100000aa */ 	beqz	$zero,.L0f119d7c
/*  f119ad4:	00001025 */ 	or	$v0,$zero,$zero
.L0f119ad8:
/*  f119ad8:	1460001f */ 	bnez	$v1,.L0f119b58
/*  f119adc:	8faf03a4 */ 	lw	$t7,0x3a4($sp)
/*  f119ae0:	30d90fff */ 	andi	$t9,$a2,0xfff
/*  f119ae4:	01f94821 */ 	addu	$t1,$t7,$t9
/*  f119ae8:	10000011 */ 	beqz	$zero,.L0f119b30
/*  f119aec:	afa903a4 */ 	sw	$t1,0x3a4($sp)
.L0f119af0:
/*  f119af0:	14500008 */ 	bne	$v0,$s0,.L0f119b14
/*  f119af4:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f119af8:	8fab03a4 */ 	lw	$t3,0x3a4($sp)
/*  f119afc:	30ca0fff */ 	andi	$t2,$a2,0xfff
/*  f119b00:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f119b04:	016a6021 */ 	addu	$t4,$t3,$t2
/*  f119b08:	afae07a4 */ 	sw	$t6,0x7a4($sp)
/*  f119b0c:	10000008 */ 	beqz	$zero,.L0f119b30
/*  f119b10:	afac03a4 */ 	sw	$t4,0x3a4($sp)
.L0f119b14:
/*  f119b14:	24010001 */ 	addiu	$at,$zero,0x1
/*  f119b18:	14410003 */ 	bne	$v0,$at,.L0f119b28
/*  f119b1c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119b20:	10000096 */ 	beqz	$zero,.L0f119d7c
/*  f119b24:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f119b28:
/*  f119b28:	10000094 */ 	beqz	$zero,.L0f119d7c
/*  f119b2c:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f119b30:
/*  f119b30:	8fad07a4 */ 	lw	$t5,0x7a4($sp)
/*  f119b34:	8fb803a4 */ 	lw	$t8,0x3a4($sp)
/*  f119b38:	55a00008 */ 	bnezl	$t5,.L0f119b5c
/*  f119b3c:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f119b40:	56e00006 */ 	bnezl	$s7,.L0f119b5c
/*  f119b44:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
/*  f119b48:	8ecf02a0 */ 	lw	$t7,0x2a0($s6)
/*  f119b4c:	030f082b */ 	sltu	$at,$t8,$t7
/*  f119b50:	5420ffca */ 	bnezl	$at,.L0f119a7c
/*  f119b54:	00152600 */ 	sll	$a0,$s5,0x18
.L0f119b58:
/*  f119b58:	8fb907a4 */ 	lw	$t9,0x7a4($sp)
.L0f119b5c:
/*  f119b5c:	17200072 */ 	bnez	$t9,.L0f119d28
/*  f119b60:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119b64:	16e00070 */ 	bnez	$s7,.L0f119d28
/*  f119b68:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119b6c:	8ec902a0 */ 	lw	$t1,0x2a0($s6)
/*  f119b70:	00008025 */ 	or	$s0,$zero,$zero
/*  f119b74:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f119b78:	1120002e */ 	beqz	$t1,.L0f119c34
/*  f119b7c:	27b2007c */ 	addiu	$s2,$sp,0x7c
/*  f119b80:	24110001 */ 	addiu	$s1,$zero,0x1
.L0f119b84:
/*  f119b84:	00152600 */ 	sll	$a0,$s5,0x18
/*  f119b88:	00047603 */ 	sra	$t6,$a0,0x18
/*  f119b8c:	01c02025 */ 	or	$a0,$t6,$zero
/*  f119b90:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f119b94:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f119b98:	02803025 */ 	or	$a2,$s4,$zero
/*  f119b9c:	14400025 */ 	bnez	$v0,.L0f119c34
/*  f119ba0:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f119ba4:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f119ba8:	304b0002 */ 	andi	$t3,$v0,0x2
/*  f119bac:	1560001a */ 	bnez	$t3,.L0f119c18
/*  f119bb0:	304a0004 */ 	andi	$t2,$v0,0x4
/*  f119bb4:	1540001f */ 	bnez	$t2,.L0f119c34
/*  f119bb8:	00002825 */ 	or	$a1,$zero,$zero
/*  f119bbc:	1a00000e */ 	blez	$s0,.L0f119bf8
/*  f119bc0:	00001825 */ 	or	$v1,$zero,$zero
/*  f119bc4:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f119bc8:	27a2007c */ 	addiu	$v0,$sp,0x7c
/*  f119bcc:	000824c2 */ 	srl	$a0,$t0,0x13
.L0f119bd0:
/*  f119bd0:	8c4c0000 */ 	lw	$t4,0x0($v0)
/*  f119bd4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f119bd8:	148c0005 */ 	bne	$a0,$t4,.L0f119bf0
/*  f119bdc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119be0:	8c4d0004 */ 	lw	$t5,0x4($v0)
/*  f119be4:	02202825 */ 	or	$a1,$s1,$zero
/*  f119be8:	25b80001 */ 	addiu	$t8,$t5,0x1
/*  f119bec:	ac580004 */ 	sw	$t8,0x4($v0)
.L0f119bf0:
/*  f119bf0:	1470fff7 */ 	bne	$v1,$s0,.L0f119bd0
/*  f119bf4:	24420008 */ 	addiu	$v0,$v0,0x8
.L0f119bf8:
/*  f119bf8:	14a00007 */ 	bnez	$a1,.L0f119c18
/*  f119bfc:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f119c00:	001078c0 */ 	sll	$t7,$s0,0x3
/*  f119c04:	024f1021 */ 	addu	$v0,$s2,$t7
/*  f119c08:	0008ccc2 */ 	srl	$t9,$t0,0x13
/*  f119c0c:	ac590000 */ 	sw	$t9,0x0($v0)
/*  f119c10:	ac510004 */ 	sw	$s1,0x4($v0)
/*  f119c14:	26100001 */ 	addiu	$s0,$s0,0x1
.L0f119c18:
/*  f119c18:	8fa903a4 */ 	lw	$t1,0x3a4($sp)
/*  f119c1c:	8ecc02a0 */ 	lw	$t4,0x2a0($s6)
/*  f119c20:	30ce0fff */ 	andi	$t6,$a2,0xfff
/*  f119c24:	012e5821 */ 	addu	$t3,$t1,$t6
/*  f119c28:	016c082b */ 	sltu	$at,$t3,$t4
/*  f119c2c:	1420ffd5 */ 	bnez	$at,.L0f119b84
/*  f119c30:	afab03a4 */ 	sw	$t3,0x3a4($sp)
.L0f119c34:
/*  f119c34:	2a010002 */ 	slti	$at,$s0,0x2
/*  f119c38:	14200039 */ 	bnez	$at,.L0f119d20
/*  f119c3c:	27b2007c */ 	addiu	$s2,$sp,0x7c
/*  f119c40:	2406ffff */ 	addiu	$a2,$zero,-1
/*  f119c44:	2405ffff */ 	addiu	$a1,$zero,-1
/*  f119c48:	1a00000b */ 	blez	$s0,.L0f119c78
/*  f119c4c:	00001825 */ 	or	$v1,$zero,$zero
/*  f119c50:	27a2007c */ 	addiu	$v0,$sp,0x7c
.L0f119c54:
/*  f119c54:	8c440004 */ 	lw	$a0,0x4($v0)
/*  f119c58:	00a4082a */ 	slt	$at,$a1,$a0
/*  f119c5c:	50200004 */ 	beqzl	$at,.L0f119c70
/*  f119c60:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f119c64:	00603025 */ 	or	$a2,$v1,$zero
/*  f119c68:	00802825 */ 	or	$a1,$a0,$zero
/*  f119c6c:	24630001 */ 	addiu	$v1,$v1,0x1
.L0f119c70:
/*  f119c70:	1470fff8 */ 	bne	$v1,$s0,.L0f119c54
/*  f119c74:	24420008 */ 	addiu	$v0,$v0,0x8
.L0f119c78:
/*  f119c78:	2401ffff */ 	addiu	$at,$zero,-1
/*  f119c7c:	10c1002a */ 	beq	$a2,$at,.L0f119d28
/*  f119c80:	000668c0 */ 	sll	$t5,$a2,0x3
/*  f119c84:	8ed902a0 */ 	lw	$t9,0x2a0($s6)
/*  f119c88:	024dc021 */ 	addu	$t8,$s2,$t5
/*  f119c8c:	8f0f0000 */ 	lw	$t7,0x0($t8)
/*  f119c90:	afa003a4 */ 	sw	$zero,0x3a4($sp)
/*  f119c94:	13200024 */ 	beqz	$t9,.L0f119d28
/*  f119c98:	aecf0260 */ 	sw	$t7,0x260($s6)
.L0f119c9c:
/*  f119c9c:	00152600 */ 	sll	$a0,$s5,0x18
/*  f119ca0:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f119ca4:	01202025 */ 	or	$a0,$t1,$zero
/*  f119ca8:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f119cac:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f119cb0:	02803025 */ 	or	$a2,$s4,$zero
/*  f119cb4:	1440001c */ 	bnez	$v0,.L0f119d28
/*  f119cb8:	8fa6047c */ 	lw	$a2,0x47c($sp)
/*  f119cbc:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f119cc0:	304e0002 */ 	andi	$t6,$v0,0x2
/*  f119cc4:	15c0000d */ 	bnez	$t6,.L0f119cfc
/*  f119cc8:	304b0004 */ 	andi	$t3,$v0,0x4
/*  f119ccc:	15600016 */ 	bnez	$t3,.L0f119d28
/*  f119cd0:	8fa80480 */ 	lw	$t0,0x480($sp)
/*  f119cd4:	8ecc0260 */ 	lw	$t4,0x260($s6)
/*  f119cd8:	000854c2 */ 	srl	$t2,$t0,0x13
/*  f119cdc:	00152600 */ 	sll	$a0,$s5,0x18
/*  f119ce0:	114c0006 */ 	beq	$t2,$t4,.L0f119cfc
/*  f119ce4:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f119ce8:	01a02025 */ 	or	$a0,$t5,$zero
/*  f119cec:	8fa503a4 */ 	lw	$a1,0x3a4($sp)
/*  f119cf0:	0fc4651e */ 	jal	pakWriteBlankFile
/*  f119cf4:	02803025 */ 	or	$a2,$s4,$zero
/*  f119cf8:	8fa6047c */ 	lw	$a2,0x47c($sp)
.L0f119cfc:
/*  f119cfc:	8fb803a4 */ 	lw	$t8,0x3a4($sp)
/*  f119d00:	8ece02a0 */ 	lw	$t6,0x2a0($s6)
/*  f119d04:	30cf0fff */ 	andi	$t7,$a2,0xfff
/*  f119d08:	030fc821 */ 	addu	$t9,$t8,$t7
/*  f119d0c:	032e082b */ 	sltu	$at,$t9,$t6
/*  f119d10:	1420ffe2 */ 	bnez	$at,.L0f119c9c
/*  f119d14:	afb903a4 */ 	sw	$t9,0x3a4($sp)
/*  f119d18:	10000003 */ 	beqz	$zero,.L0f119d28
/*  f119d1c:	00000000 */ 	sll	$zero,$zero,0x0
.L0f119d20:
/*  f119d20:	8fab007c */ 	lw	$t3,0x7c($sp)
/*  f119d24:	aecb0260 */ 	sw	$t3,0x260($s6)
.L0f119d28:
/*  f119d28:	12e00003 */ 	beqz	$s7,.L0f119d38
/*  f119d2c:	8faa07a4 */ 	lw	$t2,0x7a4($sp)
/*  f119d30:	10000012 */ 	beqz	$zero,.L0f119d7c
/*  f119d34:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f119d38:
/*  f119d38:	11400003 */ 	beqz	$t2,.L0f119d48
/*  f119d3c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f119d40:	1000000e */ 	beqz	$zero,.L0f119d7c
/*  f119d44:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f119d48:
/*  f119d48:	52a1000c */ 	beql	$s5,$at,.L0f119d7c
/*  f119d4c:	00001025 */ 	or	$v0,$zero,$zero
/*  f119d50:	8ecc0260 */ 	lw	$t4,0x260($s6)
/*  f119d54:	00152600 */ 	sll	$a0,$s5,0x18
/*  f119d58:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f119d5c:	55800007 */ 	bnezl	$t4,.L0f119d7c
/*  f119d60:	00001025 */ 	or	$v0,$zero,$zero
/*  f119d64:	0fc459a3 */ 	jal	pakGenerateSerial
/*  f119d68:	01a02025 */ 	or	$a0,$t5,$zero
/*  f119d6c:	aec20260 */ 	sw	$v0,0x260($s6)
/*  f119d70:	10000002 */ 	beqz	$zero,.L0f119d7c
/*  f119d74:	2402ffff */ 	addiu	$v0,$zero,-1
/*  f119d78:	00001025 */ 	or	$v0,$zero,$zero
.L0f119d7c:
/*  f119d7c:	8fbf004c */ 	lw	$ra,0x4c($sp)
/*  f119d80:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f119d84:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f119d88:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f119d8c:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f119d90:	8fb4003c */ 	lw	$s4,0x3c($sp)
/*  f119d94:	8fb50040 */ 	lw	$s5,0x40($sp)
/*  f119d98:	8fb60044 */ 	lw	$s6,0x44($sp)
/*  f119d9c:	8fb70048 */ 	lw	$s7,0x48($sp)
/*  f119da0:	03e00008 */ 	jr	$ra
/*  f119da4:	27bd07b0 */ 	addiu	$sp,$sp,0x7b0
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11970c
/*  f1195e4:	27bdfb78 */ 	addiu	$sp,$sp,-1160
/*  f1195e8:	afb60048 */ 	sw	$s6,0x48($sp)
/*  f1195ec:	0004b600 */ 	sll	$s6,$a0,0x18
/*  f1195f0:	00167603 */ 	sra	$t6,$s6,0x18
/*  f1195f4:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f1195f8:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f1195fc:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119600:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f119604:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119608:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11960c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119610:	afa40488 */ 	sw	$a0,0x488($sp)
/*  f119614:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f119618:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11961c:	afbe0050 */ 	sw	$s8,0x50($sp)
/*  f119620:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f119624:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f119628:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11962c:	afbf0054 */ 	sw	$ra,0x54($sp)
/*  f119630:	afb50044 */ 	sw	$s5,0x44($sp)
/*  f119634:	afb10034 */ 	sw	$s1,0x34($sp)
/*  f119638:	01f8f021 */ 	addu	$s8,$t7,$t8
/*  f11963c:	3419baba */ 	dli	$t9,0xbaba
/*  f119640:	00044603 */ 	sra	$t0,$a0,0x18
/*  f119644:	01c0b025 */ 	or	$s6,$t6,$zero
/*  f119648:	afb7004c */ 	sw	$s7,0x4c($sp)
/*  f11964c:	afb40040 */ 	sw	$s4,0x40($sp)
/*  f119650:	afb3003c */ 	sw	$s3,0x3c($sp)
/*  f119654:	afb20038 */ 	sw	$s2,0x38($sp)
/*  f119658:	afb00030 */ 	sw	$s0,0x30($sp)
/*  f11965c:	0000a825 */ 	or	$s5,$zero,$zero
/*  f119660:	afa0047c */ 	sw	$zero,0x47c($sp)
/*  f119664:	00008825 */ 	or	$s1,$zero,$zero
/*  f119668:	afd90260 */ 	sw	$t9,0x260($s8)
/*  f11966c:	a3c002be */ 	sb	$zero,0x2be($s8)
/*  f119670:	0fc459d6 */ 	jal	pak0f1167d8
/*  f119674:	01002025 */ 	or	$a0,$t0,$zero
/*  f119678:	50400004 */ 	beqzl	$v0,.L0f11968c
/*  f11967c:	8fc902a0 */ 	lw	$t1,0x2a0($s8)
/*  f119680:	1000011d */ 	beqz	$zero,.L0f119af8
/*  f119684:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f119688:	8fc902a0 */ 	lw	$t1,0x2a0($s8)
.L0f11968c:
/*  f11968c:	afa0007c */ 	sw	$zero,0x7c($sp)
/*  f119690:	27b7014c */ 	addiu	$s7,$sp,0x14c
/*  f119694:	112000ac */ 	beqz	$t1,.L0f119948
/*  f119698:	27b3015c */ 	addiu	$s3,$sp,0x15c
/*  f11969c:	2412ffff */ 	addiu	$s2,$zero,-1
/*  f1196a0:	00162600 */ 	sll	$a0,$s6,0x18
.L0f1196a4:
/*  f1196a4:	00045603 */ 	sra	$t2,$a0,0x18
/*  f1196a8:	01402025 */ 	or	$a0,$t2,$zero
/*  f1196ac:	8fa5007c */ 	lw	$a1,0x7c($sp)
/*  f1196b0:	0fc45d28 */ 	jal	pakReadHeaderAtOffset
/*  f1196b4:	02e03025 */ 	or	$a2,$s7,$zero
/*  f1196b8:	14400077 */ 	bnez	$v0,.L0f119898
/*  f1196bc:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1196c0:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f1196c4:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1196c8:	00046603 */ 	sra	$t4,$a0,0x18
/*  f1196cc:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f1196d0:	304b0002 */ 	andi	$t3,$v0,0x2
/*  f1196d4:	11600007 */ 	beqz	$t3,.L0f1196f4
/*  f1196d8:	304d0004 */ 	andi	$t5,$v0,0x4
/*  f1196dc:	01802025 */ 	or	$a0,$t4,$zero
/*  f1196e0:	27a5007c */ 	addiu	$a1,$sp,0x7c
/*  f1196e4:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1196e8:	00003025 */ 	or	$a2,$zero,$zero
/*  f1196ec:	10000090 */ 	beqz	$zero,.L0f119930
/*  f1196f0:	2c550001 */ 	sltiu	$s5,$v0,0x1
.L0f1196f4:
/*  f1196f4:	15a00094 */ 	bnez	$t5,.L0f119948
/*  f1196f8:	8fae007c */ 	lw	$t6,0x7c($sp)
/*  f1196fc:	8fd902a0 */ 	lw	$t9,0x2a0($s8)
/*  f119700:	30cf0fff */ 	andi	$t7,$a2,0xfff
/*  f119704:	01cfc021 */ 	addu	$t8,$t6,$t7
/*  f119708:	0319082b */ 	sltu	$at,$t8,$t9
/*  f11970c:	14200012 */ 	bnez	$at,.L0f119758
/*  f119710:	0000a025 */ 	or	$s4,$zero,$zero
/*  f119714:	00162600 */ 	sll	$a0,$s6,0x18
/*  f119718:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11971c:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f119720:	afa90020 */ 	sw	$t1,0x20($sp)
/*  f119724:	01002025 */ 	or	$a0,$t0,$zero
/*  f119728:	01c02825 */ 	or	$a1,$t6,$zero
/*  f11972c:	24060004 */ 	addiu	$a2,$zero,0x4
/*  f119730:	00003825 */ 	or	$a3,$zero,$zero
/*  f119734:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f119738:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f11973c:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f119740:	0fc46e75 */ 	jal	pakWriteFile
/*  f119744:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f119748:	50400080 */ 	beqzl	$v0,.L0f11994c
/*  f11974c:	8fac047c */ 	lw	$t4,0x47c($sp)
/*  f119750:	10000077 */ 	beqz	$zero,.L0f119930
/*  f119754:	24150001 */ 	addiu	$s5,$zero,0x1
.L0f119758:
/*  f119758:	1220003b */ 	beqz	$s1,.L0f119848
/*  f11975c:	00001025 */ 	or	$v0,$zero,$zero
/*  f119760:	27a70080 */ 	addiu	$a3,$sp,0x80
/*  f119764:	8fa40158 */ 	lw	$a0,0x158($sp)
.L0f119768:
/*  f119768:	8cea0000 */ 	lw	$t2,0x0($a3)
/*  f11976c:	00025900 */ 	sll	$t3,$v0,0x4
/*  f119770:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f119774:	124a0032 */ 	beq	$s2,$t2,.L0f119840
/*  f119778:	026b3021 */ 	addu	$a2,$s3,$t3
/*  f11977c:	8cc3000c */ 	lw	$v1,0xc($a2)
/*  f119780:	00046340 */ 	sll	$t4,$a0,0xd
/*  f119784:	000c6e42 */ 	srl	$t5,$t4,0x19
/*  f119788:	00037b40 */ 	sll	$t7,$v1,0xd
/*  f11978c:	000fc642 */ 	srl	$t8,$t7,0x19
/*  f119790:	15b8002b */ 	bne	$t5,$t8,.L0f119840
/*  f119794:	0004cd00 */ 	sll	$t9,$a0,0x14
/*  f119798:	00037500 */ 	sll	$t6,$v1,0x14
/*  f11979c:	000e4dc2 */ 	srl	$t1,$t6,0x17
/*  f1197a0:	001945c2 */ 	srl	$t0,$t9,0x17
/*  f1197a4:	0109082b */ 	sltu	$at,$t0,$t1
/*  f1197a8:	10200009 */ 	beqz	$at,.L0f1197d0
/*  f1197ac:	24140001 */ 	addiu	$s4,$zero,0x1
/*  f1197b0:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1197b4:	00045603 */ 	sra	$t2,$a0,0x18
/*  f1197b8:	01402025 */ 	or	$a0,$t2,$zero
/*  f1197bc:	27a5007c */ 	addiu	$a1,$sp,0x7c
/*  f1197c0:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1197c4:	02e03025 */ 	or	$a2,$s7,$zero
/*  f1197c8:	1000001f */ 	beqz	$zero,.L0f119848
/*  f1197cc:	2c550001 */ 	sltiu	$s5,$v0,0x1
.L0f1197d0:
/*  f1197d0:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1197d4:	00116100 */ 	sll	$t4,$s1,0x4
/*  f1197d8:	026c7821 */ 	addu	$t7,$s3,$t4
/*  f1197dc:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f1197e0:	00116880 */ 	sll	$t5,$s1,0x2
/*  f1197e4:	27b80080 */ 	addiu	$t8,$sp,0x80
/*  f1197e8:	01b88021 */ 	addu	$s0,$t5,$t8
/*  f1197ec:	01602025 */ 	or	$a0,$t3,$zero
/*  f1197f0:	afaf0060 */ 	sw	$t7,0x60($sp)
/*  f1197f4:	00e02825 */ 	or	$a1,$a3,$zero
/*  f1197f8:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1197fc:	afa70068 */ 	sw	$a3,0x68($sp)
/*  f119800:	8fa70068 */ 	lw	$a3,0x68($sp)
/*  f119804:	2c550001 */ 	sltiu	$s5,$v0,0x1
/*  f119808:	02e02825 */ 	or	$a1,$s7,$zero
/*  f11980c:	acf20000 */ 	sw	$s2,0x0($a3)
/*  f119810:	8fa40060 */ 	lw	$a0,0x60($sp)
/*  f119814:	0c012978 */ 	jal	memcpy
/*  f119818:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f11981c:	8fb9007c */ 	lw	$t9,0x7c($sp)
/*  f119820:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f119824:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f119828:	ae190000 */ 	sw	$t9,0x0($s0)
/*  f11982c:	8fae007c */ 	lw	$t6,0x7c($sp)
/*  f119830:	30c80fff */ 	andi	$t0,$a2,0xfff
/*  f119834:	01c84821 */ 	addu	$t1,$t6,$t0
/*  f119838:	10000003 */ 	beqz	$zero,.L0f119848
/*  f11983c:	afa9007c */ 	sw	$t1,0x7c($sp)
.L0f119840:
/*  f119840:	1451ffc9 */ 	bne	$v0,$s1,.L0f119768
/*  f119844:	24e70004 */ 	addiu	$a3,$a3,0x4
.L0f119848:
/*  f119848:	16800039 */ 	bnez	$s4,.L0f119930
/*  f11984c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119850:	16a00037 */ 	bnez	$s5,.L0f119930
/*  f119854:	00115100 */ 	sll	$t2,$s1,0x4
/*  f119858:	00115880 */ 	sll	$t3,$s1,0x2
/*  f11985c:	27ac0080 */ 	addiu	$t4,$sp,0x80
/*  f119860:	016c8021 */ 	addu	$s0,$t3,$t4
/*  f119864:	026a2021 */ 	addu	$a0,$s3,$t2
/*  f119868:	02e02825 */ 	or	$a1,$s7,$zero
/*  f11986c:	0c012978 */ 	jal	memcpy
/*  f119870:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f119874:	8faf007c */ 	lw	$t7,0x7c($sp)
/*  f119878:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f11987c:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f119880:	ae0f0000 */ 	sw	$t7,0x0($s0)
/*  f119884:	8fad007c */ 	lw	$t5,0x7c($sp)
/*  f119888:	30d80fff */ 	andi	$t8,$a2,0xfff
/*  f11988c:	01b8c821 */ 	addu	$t9,$t5,$t8
/*  f119890:	10000027 */ 	beqz	$zero,.L0f119930
/*  f119894:	afb9007c */ 	sw	$t9,0x7c($sp)
.L0f119898:
/*  f119898:	14410003 */ 	bne	$v0,$at,.L0f1198a8
/*  f11989c:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1198a0:	10000095 */ 	beqz	$zero,.L0f119af8
/*  f1198a4:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f1198a8:
/*  f1198a8:	24010007 */ 	addiu	$at,$zero,0x7
/*  f1198ac:	14410007 */ 	bne	$v0,$at,.L0f1198cc
/*  f1198b0:	00047603 */ 	sra	$t6,$a0,0x18
/*  f1198b4:	01c02025 */ 	or	$a0,$t6,$zero
/*  f1198b8:	27a5007c */ 	addiu	$a1,$sp,0x7c
/*  f1198bc:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1198c0:	00003025 */ 	or	$a2,$zero,$zero
/*  f1198c4:	1000001a */ 	beqz	$zero,.L0f119930
/*  f1198c8:	2c550001 */ 	sltiu	$s5,$v0,0x1
.L0f1198cc:
/*  f1198cc:	2401000f */ 	addiu	$at,$zero,0xf
/*  f1198d0:	1441000a */ 	bne	$v0,$at,.L0f1198fc
/*  f1198d4:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1198d8:	00044603 */ 	sra	$t0,$a0,0x18
/*  f1198dc:	01002025 */ 	or	$a0,$t0,$zero
/*  f1198e0:	27a5007c */ 	addiu	$a1,$sp,0x7c
/*  f1198e4:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1198e8:	02e03025 */ 	or	$a2,$s7,$zero
/*  f1198ec:	14400010 */ 	bnez	$v0,.L0f119930
/*  f1198f0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1198f4:	10000014 */ 	beqz	$zero,.L0f119948
/*  f1198f8:	24150001 */ 	addiu	$s5,$zero,0x1
.L0f1198fc:
/*  f1198fc:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f119900:	14500004 */ 	bne	$v0,$s0,.L0f119914
/*  f119904:	24010004 */ 	addiu	$at,$zero,0x4
/*  f119908:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11990c:	1000000e */ 	beqz	$zero,.L0f119948
/*  f119910:	afa9047c */ 	sw	$t1,0x47c($sp)
.L0f119914:
/*  f119914:	1041000c */ 	beq	$v0,$at,.L0f119948
/*  f119918:	24150001 */ 	addiu	$s5,$zero,0x1
/*  f11991c:	2401000b */ 	addiu	$at,$zero,0xb
/*  f119920:	5441000a */ 	bnel	$v0,$at,.L0f11994c
/*  f119924:	8fac047c */ 	lw	$t4,0x47c($sp)
/*  f119928:	10000008 */ 	beqz	$zero,.L0f11994c
/*  f11992c:	8fac047c */ 	lw	$t4,0x47c($sp)
.L0f119930:
/*  f119930:	16a00005 */ 	bnez	$s5,.L0f119948
/*  f119934:	8faa007c */ 	lw	$t2,0x7c($sp)
/*  f119938:	8fcb02a0 */ 	lw	$t3,0x2a0($s8)
/*  f11993c:	014b082b */ 	sltu	$at,$t2,$t3
/*  f119940:	5420ff58 */ 	bnezl	$at,.L0f1196a4
/*  f119944:	00162600 */ 	sll	$a0,$s6,0x18
.L0f119948:
/*  f119948:	8fac047c */ 	lw	$t4,0x47c($sp)
.L0f11994c:
/*  f11994c:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f119950:	27b7014c */ 	addiu	$s7,$sp,0x14c
/*  f119954:	15800053 */ 	bnez	$t4,.L0f119aa4
/*  f119958:	afa0007c */ 	sw	$zero,0x7c($sp)
/*  f11995c:	16a00051 */ 	bnez	$s5,.L0f119aa4
/*  f119960:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119964:	8fcf02a0 */ 	lw	$t7,0x2a0($s8)
/*  f119968:	11e0004e */ 	beqz	$t7,.L0f119aa4
/*  f11996c:	00162600 */ 	sll	$a0,$s6,0x18
.L0f119970:
/*  f119970:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f119974:	01a02025 */ 	or	$a0,$t5,$zero
/*  f119978:	8fa5007c */ 	lw	$a1,0x7c($sp)
/*  f11997c:	0fc45d28 */ 	jal	pakReadHeaderAtOffset
/*  f119980:	02e03025 */ 	or	$a2,$s7,$zero
/*  f119984:	1440002d */ 	bnez	$v0,.L0f119a3c
/*  f119988:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f11998c:	000615c2 */ 	srl	$v0,$a2,0x17
/*  f119990:	30580002 */ 	andi	$t8,$v0,0x2
/*  f119994:	13000003 */ 	beqz	$t8,.L0f1199a4
/*  f119998:	8fb9007c */ 	lw	$t9,0x7c($sp)
/*  f11999c:	10000021 */ 	beqz	$zero,.L0f119a24
/*  f1199a0:	30430004 */ 	andi	$v1,$v0,0x4
.L0f1199a4:
/*  f1199a4:	13200019 */ 	beqz	$t9,.L0f119a0c
/*  f1199a8:	8fa40158 */ 	lw	$a0,0x158($sp)
/*  f1199ac:	8fa40158 */ 	lw	$a0,0x158($sp)
/*  f1199b0:	8fce0260 */ 	lw	$t6,0x260($s8)
/*  f1199b4:	00042cc2 */ 	srl	$a1,$a0,0x13
/*  f1199b8:	10ae0011 */ 	beq	$a1,$t6,.L0f119a00
/*  f1199bc:	00162600 */ 	sll	$a0,$s6,0x18
/*  f1199c0:	00044603 */ 	sra	$t0,$a0,0x18
/*  f1199c4:	01002025 */ 	or	$a0,$t0,$zero
/*  f1199c8:	27a5007c */ 	addiu	$a1,$sp,0x7c
/*  f1199cc:	0fc46509 */ 	jal	pakRepairAsBlank
/*  f1199d0:	02e03025 */ 	or	$a2,$s7,$zero
/*  f1199d4:	10400006 */ 	beqz	$v0,.L0f1199f0
/*  f1199d8:	8fa40158 */ 	lw	$a0,0x158($sp)
/*  f1199dc:	8fa40158 */ 	lw	$a0,0x158($sp)
/*  f1199e0:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f1199e4:	00042cc2 */ 	srl	$a1,$a0,0x13
/*  f1199e8:	10000005 */ 	beqz	$zero,.L0f119a00
/*  f1199ec:	000615c2 */ 	srl	$v0,$a2,0x17
.L0f1199f0:
/*  f1199f0:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f1199f4:	24150001 */ 	addiu	$s5,$zero,0x1
/*  f1199f8:	00042cc2 */ 	srl	$a1,$a0,0x13
/*  f1199fc:	000615c2 */ 	srl	$v0,$a2,0x17
.L0f119a00:
/*  f119a00:	afc50260 */ 	sw	$a1,0x260($s8)
/*  f119a04:	10000007 */ 	beqz	$zero,.L0f119a24
/*  f119a08:	30430004 */ 	andi	$v1,$v0,0x4
.L0f119a0c:
/*  f119a0c:	00044cc2 */ 	srl	$t1,$a0,0x13
/*  f119a10:	30430004 */ 	andi	$v1,$v0,0x4
/*  f119a14:	10600003 */ 	beqz	$v1,.L0f119a24
/*  f119a18:	afc90260 */ 	sw	$t1,0x260($s8)
/*  f119a1c:	10000036 */ 	beqz	$zero,.L0f119af8
/*  f119a20:	00001025 */ 	or	$v0,$zero,$zero
.L0f119a24:
/*  f119a24:	1460001f */ 	bnez	$v1,.L0f119aa4
/*  f119a28:	8faa007c */ 	lw	$t2,0x7c($sp)
/*  f119a2c:	30cb0fff */ 	andi	$t3,$a2,0xfff
/*  f119a30:	014b6021 */ 	addu	$t4,$t2,$t3
/*  f119a34:	10000011 */ 	beqz	$zero,.L0f119a7c
/*  f119a38:	afac007c */ 	sw	$t4,0x7c($sp)
.L0f119a3c:
/*  f119a3c:	14500008 */ 	bne	$v0,$s0,.L0f119a60
/*  f119a40:	8fa60154 */ 	lw	$a2,0x154($sp)
/*  f119a44:	8fad007c */ 	lw	$t5,0x7c($sp)
/*  f119a48:	30d80fff */ 	andi	$t8,$a2,0xfff
/*  f119a4c:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f119a50:	01b8c821 */ 	addu	$t9,$t5,$t8
/*  f119a54:	afaf047c */ 	sw	$t7,0x47c($sp)
/*  f119a58:	10000008 */ 	beqz	$zero,.L0f119a7c
/*  f119a5c:	afb9007c */ 	sw	$t9,0x7c($sp)
.L0f119a60:
/*  f119a60:	24010001 */ 	addiu	$at,$zero,0x1
/*  f119a64:	14410003 */ 	bne	$v0,$at,.L0f119a74
/*  f119a68:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119a6c:	10000022 */ 	beqz	$zero,.L0f119af8
/*  f119a70:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f119a74:
/*  f119a74:	10000020 */ 	beqz	$zero,.L0f119af8
/*  f119a78:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f119a7c:
/*  f119a7c:	8fae047c */ 	lw	$t6,0x47c($sp)
/*  f119a80:	8fa8007c */ 	lw	$t0,0x7c($sp)
/*  f119a84:	15c00007 */ 	bnez	$t6,.L0f119aa4
/*  f119a88:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119a8c:	16a00005 */ 	bnez	$s5,.L0f119aa4
/*  f119a90:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119a94:	8fc902a0 */ 	lw	$t1,0x2a0($s8)
/*  f119a98:	0109082b */ 	sltu	$at,$t0,$t1
/*  f119a9c:	5420ffb4 */ 	bnezl	$at,.L0f119970
/*  f119aa0:	00162600 */ 	sll	$a0,$s6,0x18
.L0f119aa4:
/*  f119aa4:	12a00003 */ 	beqz	$s5,.L0f119ab4
/*  f119aa8:	8faa047c */ 	lw	$t2,0x47c($sp)
/*  f119aac:	10000012 */ 	beqz	$zero,.L0f119af8
/*  f119ab0:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f119ab4:
/*  f119ab4:	11400003 */ 	beqz	$t2,.L0f119ac4
/*  f119ab8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f119abc:	1000000e */ 	beqz	$zero,.L0f119af8
/*  f119ac0:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f119ac4:
/*  f119ac4:	52c1000c */ 	beql	$s6,$at,.L0f119af8
/*  f119ac8:	00001025 */ 	or	$v0,$zero,$zero
/*  f119acc:	8fcb0260 */ 	lw	$t3,0x260($s8)
/*  f119ad0:	00162600 */ 	sll	$a0,$s6,0x18
/*  f119ad4:	00046603 */ 	sra	$t4,$a0,0x18
/*  f119ad8:	55600007 */ 	bnezl	$t3,.L0f119af8
/*  f119adc:	00001025 */ 	or	$v0,$zero,$zero
/*  f119ae0:	0fc45983 */ 	jal	pakGenerateSerial
/*  f119ae4:	01802025 */ 	or	$a0,$t4,$zero
/*  f119ae8:	afc20260 */ 	sw	$v0,0x260($s8)
/*  f119aec:	10000002 */ 	beqz	$zero,.L0f119af8
/*  f119af0:	2402ffff */ 	addiu	$v0,$zero,-1
/*  f119af4:	00001025 */ 	or	$v0,$zero,$zero
.L0f119af8:
/*  f119af8:	8fbf0054 */ 	lw	$ra,0x54($sp)
/*  f119afc:	8fb00030 */ 	lw	$s0,0x30($sp)
/*  f119b00:	8fb10034 */ 	lw	$s1,0x34($sp)
/*  f119b04:	8fb20038 */ 	lw	$s2,0x38($sp)
/*  f119b08:	8fb3003c */ 	lw	$s3,0x3c($sp)
/*  f119b0c:	8fb40040 */ 	lw	$s4,0x40($sp)
/*  f119b10:	8fb50044 */ 	lw	$s5,0x44($sp)
/*  f119b14:	8fb60048 */ 	lw	$s6,0x48($sp)
/*  f119b18:	8fb7004c */ 	lw	$s7,0x4c($sp)
/*  f119b1c:	8fbe0050 */ 	lw	$s8,0x50($sp)
/*  f119b20:	03e00008 */ 	jr	$ra
/*  f119b24:	27bd0488 */ 	addiu	$sp,$sp,0x488
);
#else
GLOBAL_ASM(
glabel pak0f11970c
/*  f11392c:	27bdfb90 */ 	addiu	$sp,$sp,-1136
/*  f113930:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f113934:	0004b600 */ 	sll	$s6,$a0,0x18
/*  f113938:	00167603 */ 	sra	$t6,$s6,0x18
/*  f11393c:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f113940:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f113944:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f113948:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11394c:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f113950:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f113954:	3c18800a */ 	lui	$t8,0x800a
/*  f113958:	afa40470 */ 	sw	$a0,0x470($sp)
/*  f11395c:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f113960:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f113964:	01f81021 */ 	addu	$v0,$t7,$t8
/*  f113968:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11396c:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f113970:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f113974:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f113978:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11397c:	01c0b025 */ 	or	$s6,$t6,$zero
/*  f113980:	afbe0038 */ 	sw	$s8,0x38($sp)
/*  f113984:	afb70034 */ 	sw	$s7,0x34($sp)
/*  f113988:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f11398c:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f113990:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f113994:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f113998:	0000a825 */ 	or	$s5,$zero,$zero
/*  f11399c:	afa00464 */ 	sw	$zero,0x464($sp)
/*  f1139a0:	00008825 */ 	or	$s1,$zero,$zero
/*  f1139a4:	ac400260 */ 	sw	$zero,0x260($v0)
/*  f1139a8:	a04002be */ 	sb	$zero,0x2be($v0)
/*  f1139ac:	03202025 */ 	or	$a0,$t9,$zero
/*  f1139b0:	0fc442e7 */ 	jal	pak0f1167d8
/*  f1139b4:	afa20054 */ 	sw	$v0,0x54($sp)
/*  f1139b8:	50400004 */ 	beqzl	$v0,.NB0f1139cc
/*  f1139bc:	8fa20054 */ 	lw	$v0,0x54($sp)
/*  f1139c0:	100000f1 */ 	beqz	$zero,.NB0f113d88
/*  f1139c4:	00001025 */ 	or	$v0,$zero,$zero
/*  f1139c8:	8fa20054 */ 	lw	$v0,0x54($sp)
.NB0f1139cc:
/*  f1139cc:	afa00064 */ 	sw	$zero,0x64($sp)
/*  f1139d0:	27be0134 */ 	addiu	$s8,$sp,0x134
/*  f1139d4:	8c4802a0 */ 	lw	$t0,0x2a0($v0)
/*  f1139d8:	27b70064 */ 	addiu	$s7,$sp,0x64
/*  f1139dc:	27b30144 */ 	addiu	$s3,$sp,0x144
/*  f1139e0:	11000092 */ 	beqz	$t0,.NB0f113c2c
/*  f1139e4:	2412ffff */ 	addiu	$s2,$zero,-1
/*  f1139e8:	00162600 */ 	sll	$a0,$s6,0x18
.NB0f1139ec:
/*  f1139ec:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f1139f0:	01202025 */ 	or	$a0,$t1,$zero
/*  f1139f4:	8fa50064 */ 	lw	$a1,0x64($sp)
/*  f1139f8:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f1139fc:	03c03025 */ 	or	$a2,$s8,$zero
/*  f113a00:	14400060 */ 	bnez	$v0,.NB0f113b84
/*  f113a04:	24010007 */ 	addiu	$at,$zero,0x7
/*  f113a08:	8fa3013c */ 	lw	$v1,0x13c($sp)
/*  f113a0c:	00162600 */ 	sll	$a0,$s6,0x18
/*  f113a10:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f113a14:	000315c2 */ 	srl	$v0,$v1,0x17
/*  f113a18:	304a0002 */ 	andi	$t2,$v0,0x2
/*  f113a1c:	11400007 */ 	beqz	$t2,.NB0f113a3c
/*  f113a20:	304c0004 */ 	andi	$t4,$v0,0x4
/*  f113a24:	01602025 */ 	or	$a0,$t3,$zero
/*  f113a28:	02e02825 */ 	or	$a1,$s7,$zero
/*  f113a2c:	0fc44de6 */ 	jal	pakRepairAsBlank
/*  f113a30:	00003025 */ 	or	$a2,$zero,$zero
/*  f113a34:	10000076 */ 	beqz	$zero,.NB0f113c10
/*  f113a38:	2c550001 */ 	sltiu	$s5,$v0,0x1
.NB0f113a3c:
/*  f113a3c:	1580007b */ 	bnez	$t4,.NB0f113c2c
/*  f113a40:	0000a025 */ 	or	$s4,$zero,$zero
/*  f113a44:	1220003b */ 	beqz	$s1,.NB0f113b34
/*  f113a48:	00001025 */ 	or	$v0,$zero,$zero
/*  f113a4c:	27a70068 */ 	addiu	$a3,$sp,0x68
/*  f113a50:	8fa40140 */ 	lw	$a0,0x140($sp)
.NB0f113a54:
/*  f113a54:	8ced0000 */ 	lw	$t5,0x0($a3)
/*  f113a58:	00027100 */ 	sll	$t6,$v0,0x4
/*  f113a5c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f113a60:	124d0032 */ 	beq	$s2,$t5,.NB0f113b2c
/*  f113a64:	026e3021 */ 	addu	$a2,$s3,$t6
/*  f113a68:	8cc3000c */ 	lw	$v1,0xc($a2)
/*  f113a6c:	00047b40 */ 	sll	$t7,$a0,0xd
/*  f113a70:	000fc642 */ 	srl	$t8,$t7,0x19
/*  f113a74:	0003cb40 */ 	sll	$t9,$v1,0xd
/*  f113a78:	00194642 */ 	srl	$t0,$t9,0x19
/*  f113a7c:	1708002b */ 	bne	$t8,$t0,.NB0f113b2c
/*  f113a80:	00044d00 */ 	sll	$t1,$a0,0x14
/*  f113a84:	00035d00 */ 	sll	$t3,$v1,0x14
/*  f113a88:	000b65c2 */ 	srl	$t4,$t3,0x17
/*  f113a8c:	000955c2 */ 	srl	$t2,$t1,0x17
/*  f113a90:	014c082b */ 	sltu	$at,$t2,$t4
/*  f113a94:	10200009 */ 	beqz	$at,.NB0f113abc
/*  f113a98:	24140001 */ 	addiu	$s4,$zero,0x1
/*  f113a9c:	00162600 */ 	sll	$a0,$s6,0x18
/*  f113aa0:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f113aa4:	01a02025 */ 	or	$a0,$t5,$zero
/*  f113aa8:	02e02825 */ 	or	$a1,$s7,$zero
/*  f113aac:	0fc44de6 */ 	jal	pakRepairAsBlank
/*  f113ab0:	03c03025 */ 	or	$a2,$s8,$zero
/*  f113ab4:	1000001f */ 	beqz	$zero,.NB0f113b34
/*  f113ab8:	2c550001 */ 	sltiu	$s5,$v0,0x1
.NB0f113abc:
/*  f113abc:	00162600 */ 	sll	$a0,$s6,0x18
/*  f113ac0:	00117900 */ 	sll	$t7,$s1,0x4
/*  f113ac4:	026fc821 */ 	addu	$t9,$s3,$t7
/*  f113ac8:	00047603 */ 	sra	$t6,$a0,0x18
/*  f113acc:	0011c080 */ 	sll	$t8,$s1,0x2
/*  f113ad0:	27a80068 */ 	addiu	$t0,$sp,0x68
/*  f113ad4:	03088021 */ 	addu	$s0,$t8,$t0
/*  f113ad8:	01c02025 */ 	or	$a0,$t6,$zero
/*  f113adc:	afb90048 */ 	sw	$t9,0x48($sp)
/*  f113ae0:	00e02825 */ 	or	$a1,$a3,$zero
/*  f113ae4:	0fc44de6 */ 	jal	pakRepairAsBlank
/*  f113ae8:	afa70050 */ 	sw	$a3,0x50($sp)
/*  f113aec:	8fa70050 */ 	lw	$a3,0x50($sp)
/*  f113af0:	2c550001 */ 	sltiu	$s5,$v0,0x1
/*  f113af4:	03c02825 */ 	or	$a1,$s8,$zero
/*  f113af8:	acf20000 */ 	sw	$s2,0x0($a3)
/*  f113afc:	8fa40048 */ 	lw	$a0,0x48($sp)
/*  f113b00:	0c012e88 */ 	jal	memcpy
/*  f113b04:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f113b08:	8fa90064 */ 	lw	$t1,0x64($sp)
/*  f113b0c:	8fa3013c */ 	lw	$v1,0x13c($sp)
/*  f113b10:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f113b14:	ae090000 */ 	sw	$t1,0x0($s0)
/*  f113b18:	8fab0064 */ 	lw	$t3,0x64($sp)
/*  f113b1c:	306a0fff */ 	andi	$t2,$v1,0xfff
/*  f113b20:	016a6021 */ 	addu	$t4,$t3,$t2
/*  f113b24:	10000003 */ 	beqz	$zero,.NB0f113b34
/*  f113b28:	afac0064 */ 	sw	$t4,0x64($sp)
.NB0f113b2c:
/*  f113b2c:	1451ffc9 */ 	bne	$v0,$s1,.NB0f113a54
/*  f113b30:	24e70004 */ 	addiu	$a3,$a3,0x4
.NB0f113b34:
/*  f113b34:	16800036 */ 	bnez	$s4,.NB0f113c10
/*  f113b38:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113b3c:	16a00034 */ 	bnez	$s5,.NB0f113c10
/*  f113b40:	00116900 */ 	sll	$t5,$s1,0x4
/*  f113b44:	00117080 */ 	sll	$t6,$s1,0x2
/*  f113b48:	27af0068 */ 	addiu	$t7,$sp,0x68
/*  f113b4c:	01cf8021 */ 	addu	$s0,$t6,$t7
/*  f113b50:	026d2021 */ 	addu	$a0,$s3,$t5
/*  f113b54:	03c02825 */ 	or	$a1,$s8,$zero
/*  f113b58:	0c012e88 */ 	jal	memcpy
/*  f113b5c:	24060010 */ 	addiu	$a2,$zero,0x10
/*  f113b60:	8fb90064 */ 	lw	$t9,0x64($sp)
/*  f113b64:	8fa3013c */ 	lw	$v1,0x13c($sp)
/*  f113b68:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f113b6c:	ae190000 */ 	sw	$t9,0x0($s0)
/*  f113b70:	8fb80064 */ 	lw	$t8,0x64($sp)
/*  f113b74:	30680fff */ 	andi	$t0,$v1,0xfff
/*  f113b78:	03084821 */ 	addu	$t1,$t8,$t0
/*  f113b7c:	10000024 */ 	beqz	$zero,.NB0f113c10
/*  f113b80:	afa90064 */ 	sw	$t1,0x64($sp)
.NB0f113b84:
/*  f113b84:	14410008 */ 	bne	$v0,$at,.NB0f113ba8
/*  f113b88:	00162600 */ 	sll	$a0,$s6,0x18
/*  f113b8c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f113b90:	01602025 */ 	or	$a0,$t3,$zero
/*  f113b94:	02e02825 */ 	or	$a1,$s7,$zero
/*  f113b98:	0fc44de6 */ 	jal	pakRepairAsBlank
/*  f113b9c:	00003025 */ 	or	$a2,$zero,$zero
/*  f113ba0:	1000001b */ 	beqz	$zero,.NB0f113c10
/*  f113ba4:	2c550001 */ 	sltiu	$s5,$v0,0x1
.NB0f113ba8:
/*  f113ba8:	2401000f */ 	addiu	$at,$zero,0xf
/*  f113bac:	14410008 */ 	bne	$v0,$at,.NB0f113bd0
/*  f113bb0:	00162600 */ 	sll	$a0,$s6,0x18
/*  f113bb4:	00045603 */ 	sra	$t2,$a0,0x18
/*  f113bb8:	01402025 */ 	or	$a0,$t2,$zero
/*  f113bbc:	02e02825 */ 	or	$a1,$s7,$zero
/*  f113bc0:	0fc44de6 */ 	jal	pakRepairAsBlank
/*  f113bc4:	03c03025 */ 	or	$a2,$s8,$zero
/*  f113bc8:	10000011 */ 	beqz	$zero,.NB0f113c10
/*  f113bcc:	2c550001 */ 	sltiu	$s5,$v0,0x1
.NB0f113bd0:
/*  f113bd0:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f113bd4:	14500004 */ 	bne	$v0,$s0,.NB0f113be8
/*  f113bd8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f113bdc:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f113be0:	10000012 */ 	beqz	$zero,.NB0f113c2c
/*  f113be4:	afac0464 */ 	sw	$t4,0x464($sp)
.NB0f113be8:
/*  f113be8:	10410005 */ 	beq	$v0,$at,.NB0f113c00
/*  f113bec:	24010001 */ 	addiu	$at,$zero,0x1
/*  f113bf0:	10410003 */ 	beq	$v0,$at,.NB0f113c00
/*  f113bf4:	2401000b */ 	addiu	$at,$zero,0xb
/*  f113bf8:	14410003 */ 	bne	$v0,$at,.NB0f113c08
/*  f113bfc:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f113c00:
/*  f113c00:	10000061 */ 	beqz	$zero,.NB0f113d88
/*  f113c04:	00001025 */ 	or	$v0,$zero,$zero
.NB0f113c08:
/*  f113c08:	1000005f */ 	beqz	$zero,.NB0f113d88
/*  f113c0c:	00001025 */ 	or	$v0,$zero,$zero
.NB0f113c10:
/*  f113c10:	16a00006 */ 	bnez	$s5,.NB0f113c2c
/*  f113c14:	8fae0054 */ 	lw	$t6,0x54($sp)
/*  f113c18:	8fad0064 */ 	lw	$t5,0x64($sp)
/*  f113c1c:	8dcf02a0 */ 	lw	$t7,0x2a0($t6)
/*  f113c20:	01af082b */ 	sltu	$at,$t5,$t7
/*  f113c24:	5420ff71 */ 	bnezl	$at,.NB0f1139ec
/*  f113c28:	00162600 */ 	sll	$a0,$s6,0x18
.NB0f113c2c:
/*  f113c2c:	8fb90464 */ 	lw	$t9,0x464($sp)
/*  f113c30:	8fa20054 */ 	lw	$v0,0x54($sp)
/*  f113c34:	24100009 */ 	addiu	$s0,$zero,0x9
/*  f113c38:	27be0134 */ 	addiu	$s8,$sp,0x134
/*  f113c3c:	17200037 */ 	bnez	$t9,.NB0f113d1c
/*  f113c40:	afa00064 */ 	sw	$zero,0x64($sp)
/*  f113c44:	16a00035 */ 	bnez	$s5,.NB0f113d1c
/*  f113c48:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113c4c:	8c5802a0 */ 	lw	$t8,0x2a0($v0)
/*  f113c50:	13000032 */ 	beqz	$t8,.NB0f113d1c
/*  f113c54:	00162600 */ 	sll	$a0,$s6,0x18
.NB0f113c58:
/*  f113c58:	00044603 */ 	sra	$t0,$a0,0x18
/*  f113c5c:	01002025 */ 	or	$a0,$t0,$zero
/*  f113c60:	8fa50064 */ 	lw	$a1,0x64($sp)
/*  f113c64:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f113c68:	03c03025 */ 	or	$a2,$s8,$zero
/*  f113c6c:	14400016 */ 	bnez	$v0,.NB0f113cc8
/*  f113c70:	8fa3013c */ 	lw	$v1,0x13c($sp)
/*  f113c74:	000315c2 */ 	srl	$v0,$v1,0x17
/*  f113c78:	30490004 */ 	andi	$t1,$v0,0x4
/*  f113c7c:	15200027 */ 	bnez	$t1,.NB0f113d1c
/*  f113c80:	304b0002 */ 	andi	$t3,$v0,0x2
/*  f113c84:	1560000b */ 	bnez	$t3,.NB0f113cb4
/*  f113c88:	8faa0064 */ 	lw	$t2,0x64($sp)
/*  f113c8c:	11400006 */ 	beqz	$t2,.NB0f113ca8
/*  f113c90:	8fa40140 */ 	lw	$a0,0x140($sp)
/*  f113c94:	8fa40140 */ 	lw	$a0,0x140($sp)
/*  f113c98:	8fac0054 */ 	lw	$t4,0x54($sp)
/*  f113c9c:	000414c2 */ 	srl	$v0,$a0,0x13
/*  f113ca0:	10000004 */ 	beqz	$zero,.NB0f113cb4
/*  f113ca4:	ad820260 */ 	sw	$v0,0x260($t4)
.NB0f113ca8:
/*  f113ca8:	8fad0054 */ 	lw	$t5,0x54($sp)
/*  f113cac:	000474c2 */ 	srl	$t6,$a0,0x13
/*  f113cb0:	adae0260 */ 	sw	$t6,0x260($t5)
.NB0f113cb4:
/*  f113cb4:	8faf0064 */ 	lw	$t7,0x64($sp)
/*  f113cb8:	30790fff */ 	andi	$t9,$v1,0xfff
/*  f113cbc:	01f9c021 */ 	addu	$t8,$t7,$t9
/*  f113cc0:	1000000c */ 	beqz	$zero,.NB0f113cf4
/*  f113cc4:	afb80064 */ 	sw	$t8,0x64($sp)
.NB0f113cc8:
/*  f113cc8:	14500008 */ 	bne	$v0,$s0,.NB0f113cec
/*  f113ccc:	8fa3013c */ 	lw	$v1,0x13c($sp)
/*  f113cd0:	8fa90064 */ 	lw	$t1,0x64($sp)
/*  f113cd4:	306b0fff */ 	andi	$t3,$v1,0xfff
/*  f113cd8:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f113cdc:	012b5021 */ 	addu	$t2,$t1,$t3
/*  f113ce0:	afa80464 */ 	sw	$t0,0x464($sp)
/*  f113ce4:	10000003 */ 	beqz	$zero,.NB0f113cf4
/*  f113ce8:	afaa0064 */ 	sw	$t2,0x64($sp)
.NB0f113cec:
/*  f113cec:	10000026 */ 	beqz	$zero,.NB0f113d88
/*  f113cf0:	00001025 */ 	or	$v0,$zero,$zero
.NB0f113cf4:
/*  f113cf4:	8fac0464 */ 	lw	$t4,0x464($sp)
/*  f113cf8:	8fad0054 */ 	lw	$t5,0x54($sp)
/*  f113cfc:	15800007 */ 	bnez	$t4,.NB0f113d1c
/*  f113d00:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113d04:	16a00005 */ 	bnez	$s5,.NB0f113d1c
/*  f113d08:	8fae0064 */ 	lw	$t6,0x64($sp)
/*  f113d0c:	8daf02a0 */ 	lw	$t7,0x2a0($t5)
/*  f113d10:	01cf082b */ 	sltu	$at,$t6,$t7
/*  f113d14:	5420ffd0 */ 	bnezl	$at,.NB0f113c58
/*  f113d18:	00162600 */ 	sll	$a0,$s6,0x18
.NB0f113d1c:
/*  f113d1c:	12a00003 */ 	beqz	$s5,.NB0f113d2c
/*  f113d20:	8fa20054 */ 	lw	$v0,0x54($sp)
/*  f113d24:	10000018 */ 	beqz	$zero,.NB0f113d88
/*  f113d28:	00001025 */ 	or	$v0,$zero,$zero
.NB0f113d2c:
/*  f113d2c:	8fb90464 */ 	lw	$t9,0x464($sp)
/*  f113d30:	3c18800a */ 	lui	$t8,0x800a
/*  f113d34:	27187390 */ 	addiu	$t8,$t8,0x7390
/*  f113d38:	13200003 */ 	beqz	$t9,.NB0f113d48
/*  f113d3c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113d40:	10000011 */ 	beqz	$zero,.NB0f113d88
/*  f113d44:	00001025 */ 	or	$v0,$zero,$zero
.NB0f113d48:
/*  f113d48:	5058000f */ 	beql	$v0,$t8,.NB0f113d88
/*  f113d4c:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f113d50:	8c480260 */ 	lw	$t0,0x260($v0)
/*  f113d54:	5500000c */ 	bnezl	$t0,.NB0f113d88
/*  f113d58:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f113d5c:	0c004d84 */ 	jal	random
/*  f113d60:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113d64:	24011ff0 */ 	addiu	$at,$zero,0x1ff0
/*  f113d68:	0041001b */ 	divu	$zero,$v0,$at
/*  f113d6c:	8faa0054 */ 	lw	$t2,0x54($sp)
/*  f113d70:	00004810 */ 	mfhi	$t1
/*  f113d74:	252b0010 */ 	addiu	$t3,$t1,0x10
/*  f113d78:	00001025 */ 	or	$v0,$zero,$zero
/*  f113d7c:	10000002 */ 	beqz	$zero,.NB0f113d88
/*  f113d80:	ad4b0260 */ 	sw	$t3,0x260($t2)
/*  f113d84:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f113d88:
/*  f113d88:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f113d8c:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f113d90:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f113d94:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f113d98:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f113d9c:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f113da0:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f113da4:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f113da8:	8fb70034 */ 	lw	$s7,0x34($sp)
/*  f113dac:	8fbe0038 */ 	lw	$s8,0x38($sp)
/*  f113db0:	03e00008 */ 	jr	$ra
/*  f113db4:	27bd0470 */ 	addiu	$sp,$sp,0x470
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pakCorrupt
/*  f119da8:	27bdef90 */ 	addiu	$sp,$sp,-4208
/*  f119dac:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f119db0:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f119db4:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f119db8:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f119dbc:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f119dc0:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f119dc4:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f119dc8:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f119dcc:	27a6005c */ 	addiu	$a2,$sp,0x5c
/*  f119dd0:	24040004 */ 	addiu	$a0,$zero,0x4
/*  f119dd4:	0fc459ec */ 	jal	pak0f1167b0
/*  f119dd8:	24050080 */ 	addiu	$a1,$zero,0x80
/*  f119ddc:	8fae005c */ 	lw	$t6,0x5c($sp)
/*  f119de0:	27b2005c */ 	addiu	$s2,$sp,0x5c
/*  f119de4:	00009825 */ 	or	$s3,$zero,$zero
/*  f119de8:	11c0001e */ 	beqz	$t6,.L0f119e64
/*  f119dec:	27b60048 */ 	addiu	$s6,$sp,0x48
/*  f119df0:	3c15800a */ 	lui	$s5,%hi(var80099e78)
/*  f119df4:	26b59e78 */ 	addiu	$s5,$s5,%lo(var80099e78)
/*  f119df8:	27b41060 */ 	addiu	$s4,$sp,0x1060
/*  f119dfc:	27b10050 */ 	addiu	$s1,$sp,0x50
/*  f119e00:	27b00048 */ 	addiu	$s0,$sp,0x48
.L0f119e04:
/*  f119e04:	0c004b70 */ 	jal	random
/*  f119e08:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119e0c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f119e10:	1611fffc */ 	bne	$s0,$s1,.L0f119e04
/*  f119e14:	a202ffff */ 	sb	$v0,-0x1($s0)
/*  f119e18:	24040004 */ 	addiu	$a0,$zero,0x4
/*  f119e1c:	8e450000 */ 	lw	$a1,0x0($s2)
/*  f119e20:	0fc464da */ 	jal	pakFindFile
/*  f119e24:	02803025 */ 	or	$a2,$s4,$zero
/*  f119e28:	00538021 */ 	addu	$s0,$v0,$s3
/*  f119e2c:	0c00543a */ 	jal	joyGetLock
/*  f119e30:	26100030 */ 	addiu	$s0,$s0,0x30
/*  f119e34:	02a02025 */ 	or	$a0,$s5,$zero
/*  f119e38:	320500ff */ 	andi	$a1,$s0,0xff
/*  f119e3c:	02c03025 */ 	or	$a2,$s6,$zero
/*  f119e40:	0c001910 */ 	jal	osEepromLongWrite
/*  f119e44:	24070008 */ 	addiu	$a3,$zero,0x8
/*  f119e48:	0c005451 */ 	jal	joyReleaseLock
/*  f119e4c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119e50:	8e580004 */ 	lw	$t8,0x4($s2)
/*  f119e54:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f119e58:	26730008 */ 	addiu	$s3,$s3,0x8
/*  f119e5c:	5700ffe9 */ 	bnezl	$t8,.L0f119e04
/*  f119e60:	27b00048 */ 	addiu	$s0,$sp,0x48
.L0f119e64:
/*  f119e64:	8fbf0034 */ 	lw	$ra,0x34($sp)
/*  f119e68:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f119e6c:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f119e70:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f119e74:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f119e78:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f119e7c:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f119e80:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f119e84:	03e00008 */ 	jr	$ra
/*  f119e88:	27bd1070 */ 	addiu	$sp,$sp,0x1070
);

// Mismatch due to regalloc when calculating buffer1024[i] at end of the loop.
// Suspect the return value of osEepromLongWrite is being stored into result
// and printed in an indeffed block, but still can't get a match.
//void pakCorrupt(void)
//{
//	u8 buffer16[16];
//	s32 result;
//	u32 buffer1024[1024];
//	u32 address;
//	s32 i;
//	s32 j;
//	u8 buffer8[8];
//
//	pak0f1167b0(SAVEDEVICE_GAMEPAK, PAKFILETYPE_GAME, buffer1024);
//
//	for (i = 0; buffer1024[i] != 0; i++) {
//		for (j = 0; j < 8; j++) {
//			buffer8[j] = random();
//		}
//
//		address = pakFindFile(SAVEDEVICE_GAMEPAK, buffer1024[i], buffer16);
//		address += i * 8;
//		address += 0x30;
//
//		joyGetLock();
//		osEepromLongWrite(&var80099e78, address, buffer8, 8);
//		joyReleaseLock();
//	}
//}
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f119e8c
/*  f119e8c:	27bdef58 */ 	addiu	$sp,$sp,-4264
/*  f119e90:	3c188007 */ 	lui	$t8,%hi(var80075d18)
/*  f119e94:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f119e98:	afb70030 */ 	sw	$s7,0x30($sp)
/*  f119e9c:	afb6002c */ 	sw	$s6,0x2c($sp)
/*  f119ea0:	afb50028 */ 	sw	$s5,0x28($sp)
/*  f119ea4:	afb40024 */ 	sw	$s4,0x24($sp)
/*  f119ea8:	afb30020 */ 	sw	$s3,0x20($sp)
/*  f119eac:	afb2001c */ 	sw	$s2,0x1c($sp)
/*  f119eb0:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f119eb4:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f119eb8:	afa410a8 */ 	sw	$a0,0x10a8($sp)
/*  f119ebc:	27185d18 */ 	addiu	$t8,$t8,%lo(var80075d18)
/*  f119ec0:	8f010000 */ 	lw	$at,0x0($t8)
/*  f119ec4:	27af0074 */ 	addiu	$t7,$sp,0x74
/*  f119ec8:	8f090004 */ 	lw	$t1,0x4($t8)
/*  f119ecc:	ade10000 */ 	sw	$at,0x0($t7)
/*  f119ed0:	8f010008 */ 	lw	$at,0x8($t8)
/*  f119ed4:	ade90004 */ 	sw	$t1,0x4($t7)
/*  f119ed8:	8f09000c */ 	lw	$t1,0xc($t8)
/*  f119edc:	ade10008 */ 	sw	$at,0x8($t7)
/*  f119ee0:	8f010010 */ 	lw	$at,0x10($t8)
/*  f119ee4:	3c0a8007 */ 	lui	$t2,%hi(var80075d2c)
/*  f119ee8:	254a5d2c */ 	addiu	$t2,$t2,%lo(var80075d2c)
/*  f119eec:	ade9000c */ 	sw	$t1,0xc($t7)
/*  f119ef0:	ade10010 */ 	sw	$at,0x10($t7)
/*  f119ef4:	8d410000 */ 	lw	$at,0x0($t2)
/*  f119ef8:	27b60060 */ 	addiu	$s6,$sp,0x60
/*  f119efc:	3c088007 */ 	lui	$t0,%hi(var80075d40)
/*  f119f00:	aec10000 */ 	sw	$at,0x0($s6)
/*  f119f04:	8d4d0004 */ 	lw	$t5,0x4($t2)
/*  f119f08:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f119f0c:	25085d40 */ 	addiu	$t0,$t0,%lo(var80075d40)
/*  f119f10:	aecd0004 */ 	sw	$t5,0x4($s6)
/*  f119f14:	8d410008 */ 	lw	$at,0x8($t2)
/*  f119f18:	00137603 */ 	sra	$t6,$s3,0x18
/*  f119f1c:	01c09825 */ 	or	$s3,$t6,$zero
/*  f119f20:	aec10008 */ 	sw	$at,0x8($s6)
/*  f119f24:	8d4d000c */ 	lw	$t5,0xc($t2)
/*  f119f28:	27ae004c */ 	addiu	$t6,$sp,0x4c
/*  f119f2c:	00132600 */ 	sll	$a0,$s3,0x18
/*  f119f30:	aecd000c */ 	sw	$t5,0xc($s6)
/*  f119f34:	8d410010 */ 	lw	$at,0x10($t2)
/*  f119f38:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f119f3c:	01202025 */ 	or	$a0,$t1,$zero
/*  f119f40:	aec10010 */ 	sw	$at,0x10($s6)
/*  f119f44:	8d010000 */ 	lw	$at,0x0($t0)
/*  f119f48:	8d180004 */ 	lw	$t8,0x4($t0)
/*  f119f4c:	27a60094 */ 	addiu	$a2,$sp,0x94
/*  f119f50:	adc10000 */ 	sw	$at,0x0($t6)
/*  f119f54:	8d010008 */ 	lw	$at,0x8($t0)
/*  f119f58:	add80004 */ 	sw	$t8,0x4($t6)
/*  f119f5c:	8d18000c */ 	lw	$t8,0xc($t0)
/*  f119f60:	adc10008 */ 	sw	$at,0x8($t6)
/*  f119f64:	8d010010 */ 	lw	$at,0x10($t0)
/*  f119f68:	24050100 */ 	addiu	$a1,$zero,0x100
/*  f119f6c:	add8000c */ 	sw	$t8,0xc($t6)
/*  f119f70:	0fc459ec */ 	jal	pak0f1167b0
/*  f119f74:	adc10010 */ 	sw	$at,0x10($t6)
/*  f119f78:	10400003 */ 	beqz	$v0,.L0f119f88
/*  f119f7c:	8fac0094 */ 	lw	$t4,0x94($sp)
/*  f119f80:	1000004e */ 	beqz	$zero,.L0f11a0bc
/*  f119f84:	00001025 */ 	or	$v0,$zero,$zero
.L0f119f88:
/*  f119f88:	11800022 */ 	beqz	$t4,.L0f11a014
/*  f119f8c:	27b20094 */ 	addiu	$s2,$sp,0x94
/*  f119f90:	8e450000 */ 	lw	$a1,0x0($s2)
/*  f119f94:	2415ffff */ 	addiu	$s5,$zero,-1
/*  f119f98:	27b41098 */ 	addiu	$s4,$sp,0x1098
/*  f119f9c:	27b10088 */ 	addiu	$s1,$sp,0x88
/*  f119fa0:	00132600 */ 	sll	$a0,$s3,0x18
.L0f119fa4:
/*  f119fa4:	00045603 */ 	sra	$t2,$a0,0x18
/*  f119fa8:	01402025 */ 	or	$a0,$t2,$zero
/*  f119fac:	0fc464da */ 	jal	pakFindFile
/*  f119fb0:	02803025 */ 	or	$a2,$s4,$zero
/*  f119fb4:	14550003 */ 	bne	$v0,$s5,.L0f119fc4
/*  f119fb8:	8fa410a0 */ 	lw	$a0,0x10a0($sp)
/*  f119fbc:	1000003f */ 	beqz	$zero,.L0f11a0bc
/*  f119fc0:	00001025 */ 	or	$v0,$zero,$zero
.L0f119fc4:
/*  f119fc4:	00046dc2 */ 	srl	$t5,$a0,0x17
/*  f119fc8:	01a02025 */ 	or	$a0,$t5,$zero
/*  f119fcc:	00001825 */ 	or	$v1,$zero,$zero
/*  f119fd0:	27a20074 */ 	addiu	$v0,$sp,0x74
.L0f119fd4:
/*  f119fd4:	8c4f0000 */ 	lw	$t7,0x0($v0)
/*  f119fd8:	24420004 */ 	addiu	$v0,$v0,0x4
/*  f119fdc:	148f0007 */ 	bne	$a0,$t7,.L0f119ffc
/*  f119fe0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f119fe4:	02c31021 */ 	addu	$v0,$s6,$v1
/*  f119fe8:	8c440000 */ 	lw	$a0,0x0($v0)
/*  f119fec:	10800005 */ 	beqz	$a0,.L0f11a004
/*  f119ff0:	2499ffff */ 	addiu	$t9,$a0,-1
/*  f119ff4:	10000003 */ 	beqz	$zero,.L0f11a004
/*  f119ff8:	ac590000 */ 	sw	$t9,0x0($v0)
.L0f119ffc:
/*  f119ffc:	1451fff5 */ 	bne	$v0,$s1,.L0f119fd4
/*  f11a000:	24630004 */ 	addiu	$v1,$v1,0x4
.L0f11a004:
/*  f11a004:	8e450004 */ 	lw	$a1,0x4($s2)
/*  f11a008:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f11a00c:	54a0ffe5 */ 	bnezl	$a1,.L0f119fa4
/*  f11a010:	00132600 */ 	sll	$a0,$s3,0x18
.L0f11a014:
/*  f11a014:	0000a025 */ 	or	$s4,$zero,$zero
/*  f11a018:	27b20060 */ 	addiu	$s2,$sp,0x60
/*  f11a01c:	27b70074 */ 	addiu	$s7,$sp,0x74
/*  f11a020:	27b60064 */ 	addiu	$s6,$sp,0x64
/*  f11a024:	24150004 */ 	addiu	$s5,$zero,0x4
.L0f11a028:
/*  f11a028:	8e420000 */ 	lw	$v0,0x0($s2)
/*  f11a02c:	50400020 */ 	beqzl	$v0,.L0f11a0b0
/*  f11a030:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f11a034:	16750003 */ 	bne	$s3,$s5,.L0f11a044
/*  f11a038:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a03c:	5256001c */ 	beql	$s2,$s6,.L0f11a0b0
/*  f11a040:	26520004 */ 	addiu	$s2,$s2,0x4
.L0f11a044:
/*  f11a044:	10400019 */ 	beqz	$v0,.L0f11a0ac
/*  f11a048:	00008025 */ 	or	$s0,$zero,$zero
/*  f11a04c:	27ae0074 */ 	addiu	$t6,$sp,0x74
/*  f11a050:	028e8821 */ 	addu	$s1,$s4,$t6
/*  f11a054:	00132600 */ 	sll	$a0,$s3,0x18
.L0f11a058:
/*  f11a058:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11a05c:	01002025 */ 	or	$a0,$t0,$zero
/*  f11a060:	8e250000 */ 	lw	$a1,0x0($s1)
/*  f11a064:	0fc4619d */ 	jal	pak0f118674
/*  f11a068:	00003025 */ 	or	$a2,$zero,$zero
/*  f11a06c:	1040000a */ 	beqz	$v0,.L0f11a098
/*  f11a070:	00402025 */ 	or	$a0,$v0,$zero
/*  f11a074:	2401000e */ 	addiu	$at,$zero,0xe
/*  f11a078:	14410003 */ 	bne	$v0,$at,.L0f11a088
/*  f11a07c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a080:	1000000e */ 	beqz	$zero,.L0f11a0bc
/*  f11a084:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11a088:
/*  f11a088:	0fc464d0 */ 	jal	pak0f119340
/*  f11a08c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a090:	1000000a */ 	beqz	$zero,.L0f11a0bc
/*  f11a094:	00001025 */ 	or	$v0,$zero,$zero
.L0f11a098:
/*  f11a098:	8e580000 */ 	lw	$t8,0x0($s2)
/*  f11a09c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11a0a0:	0218082b */ 	sltu	$at,$s0,$t8
/*  f11a0a4:	5420ffec */ 	bnezl	$at,.L0f11a058
/*  f11a0a8:	00132600 */ 	sll	$a0,$s3,0x18
.L0f11a0ac:
/*  f11a0ac:	26520004 */ 	addiu	$s2,$s2,0x4
.L0f11a0b0:
/*  f11a0b0:	1657ffdd */ 	bne	$s2,$s7,.L0f11a028
/*  f11a0b4:	26940004 */ 	addiu	$s4,$s4,0x4
/*  f11a0b8:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11a0bc:
/*  f11a0bc:	8fbf0034 */ 	lw	$ra,0x34($sp)
/*  f11a0c0:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f11a0c4:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f11a0c8:	8fb2001c */ 	lw	$s2,0x1c($sp)
/*  f11a0cc:	8fb30020 */ 	lw	$s3,0x20($sp)
/*  f11a0d0:	8fb40024 */ 	lw	$s4,0x24($sp)
/*  f11a0d4:	8fb50028 */ 	lw	$s5,0x28($sp)
/*  f11a0d8:	8fb6002c */ 	lw	$s6,0x2c($sp)
/*  f11a0dc:	8fb70030 */ 	lw	$s7,0x30($sp)
/*  f11a0e0:	03e00008 */ 	jr	$ra
/*  f11a0e4:	27bd10a8 */ 	addiu	$sp,$sp,0x10a8
);
#else
GLOBAL_ASM(
glabel pak0f119e8c
/*  f113db8:	27bdef58 */ 	addiu	$sp,$sp,-4264
/*  f113dbc:	3c188008 */ 	lui	$t8,0x8008
/*  f113dc0:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f113dc4:	afb70030 */ 	sw	$s7,0x30($sp)
/*  f113dc8:	afb6002c */ 	sw	$s6,0x2c($sp)
/*  f113dcc:	afb50028 */ 	sw	$s5,0x28($sp)
/*  f113dd0:	afb40024 */ 	sw	$s4,0x24($sp)
/*  f113dd4:	afb30020 */ 	sw	$s3,0x20($sp)
/*  f113dd8:	afb2001c */ 	sw	$s2,0x1c($sp)
/*  f113ddc:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f113de0:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f113de4:	afa410a8 */ 	sw	$a0,0x10a8($sp)
/*  f113de8:	271880d4 */ 	addiu	$t8,$t8,-32556
/*  f113dec:	8f010000 */ 	lw	$at,0x0($t8)
/*  f113df0:	27af0074 */ 	addiu	$t7,$sp,0x74
/*  f113df4:	8f090004 */ 	lw	$t1,0x4($t8)
/*  f113df8:	ade10000 */ 	sw	$at,0x0($t7)
/*  f113dfc:	8f010008 */ 	lw	$at,0x8($t8)
/*  f113e00:	ade90004 */ 	sw	$t1,0x4($t7)
/*  f113e04:	8f09000c */ 	lw	$t1,0xc($t8)
/*  f113e08:	ade10008 */ 	sw	$at,0x8($t7)
/*  f113e0c:	8f010010 */ 	lw	$at,0x10($t8)
/*  f113e10:	3c0a8008 */ 	lui	$t2,0x8008
/*  f113e14:	254a80e8 */ 	addiu	$t2,$t2,-32536
/*  f113e18:	ade9000c */ 	sw	$t1,0xc($t7)
/*  f113e1c:	ade10010 */ 	sw	$at,0x10($t7)
/*  f113e20:	8d410000 */ 	lw	$at,0x0($t2)
/*  f113e24:	27b50060 */ 	addiu	$s5,$sp,0x60
/*  f113e28:	3c088008 */ 	lui	$t0,0x8008
/*  f113e2c:	aea10000 */ 	sw	$at,0x0($s5)
/*  f113e30:	8d4d0004 */ 	lw	$t5,0x4($t2)
/*  f113e34:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f113e38:	250880fc */ 	addiu	$t0,$t0,-32516
/*  f113e3c:	aead0004 */ 	sw	$t5,0x4($s5)
/*  f113e40:	8d410008 */ 	lw	$at,0x8($t2)
/*  f113e44:	00137603 */ 	sra	$t6,$s3,0x18
/*  f113e48:	01c09825 */ 	or	$s3,$t6,$zero
/*  f113e4c:	aea10008 */ 	sw	$at,0x8($s5)
/*  f113e50:	8d4d000c */ 	lw	$t5,0xc($t2)
/*  f113e54:	27ae004c */ 	addiu	$t6,$sp,0x4c
/*  f113e58:	00132600 */ 	sll	$a0,$s3,0x18
/*  f113e5c:	aead000c */ 	sw	$t5,0xc($s5)
/*  f113e60:	8d410010 */ 	lw	$at,0x10($t2)
/*  f113e64:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f113e68:	01202025 */ 	or	$a0,$t1,$zero
/*  f113e6c:	aea10010 */ 	sw	$at,0x10($s5)
/*  f113e70:	8d010000 */ 	lw	$at,0x0($t0)
/*  f113e74:	8d180004 */ 	lw	$t8,0x4($t0)
/*  f113e78:	27a60094 */ 	addiu	$a2,$sp,0x94
/*  f113e7c:	adc10000 */ 	sw	$at,0x0($t6)
/*  f113e80:	8d010008 */ 	lw	$at,0x8($t0)
/*  f113e84:	add80004 */ 	sw	$t8,0x4($t6)
/*  f113e88:	8d18000c */ 	lw	$t8,0xc($t0)
/*  f113e8c:	adc10008 */ 	sw	$at,0x8($t6)
/*  f113e90:	8d010010 */ 	lw	$at,0x10($t0)
/*  f113e94:	24050100 */ 	addiu	$a1,$zero,0x100
/*  f113e98:	add8000c */ 	sw	$t8,0xc($t6)
/*  f113e9c:	0fc442dd */ 	jal	pak0f1167b0
/*  f113ea0:	adc10010 */ 	sw	$at,0x10($t6)
/*  f113ea4:	14400044 */ 	bnez	$v0,.NB0f113fb8
/*  f113ea8:	8fac0094 */ 	lw	$t4,0x94($sp)
/*  f113eac:	1180001e */ 	beqz	$t4,.NB0f113f28
/*  f113eb0:	27b20094 */ 	addiu	$s2,$sp,0x94
/*  f113eb4:	8e450000 */ 	lw	$a1,0x0($s2)
/*  f113eb8:	27b41098 */ 	addiu	$s4,$sp,0x1098
/*  f113ebc:	27b10088 */ 	addiu	$s1,$sp,0x88
/*  f113ec0:	00132600 */ 	sll	$a0,$s3,0x18
.NB0f113ec4:
/*  f113ec4:	00045603 */ 	sra	$t2,$a0,0x18
/*  f113ec8:	01402025 */ 	or	$a0,$t2,$zero
/*  f113ecc:	02803025 */ 	or	$a2,$s4,$zero
/*  f113ed0:	0fc44da7 */ 	jal	pakFindFile
/*  f113ed4:	00008025 */ 	or	$s0,$zero,$zero
/*  f113ed8:	8fa510a0 */ 	lw	$a1,0x10a0($sp)
/*  f113edc:	00002025 */ 	or	$a0,$zero,$zero
/*  f113ee0:	27a30074 */ 	addiu	$v1,$sp,0x74
/*  f113ee4:	00056dc2 */ 	srl	$t5,$a1,0x17
/*  f113ee8:	01a02825 */ 	or	$a1,$t5,$zero
.NB0f113eec:
/*  f113eec:	8c6f0000 */ 	lw	$t7,0x0($v1)
/*  f113ef0:	24630004 */ 	addiu	$v1,$v1,0x4
/*  f113ef4:	14af0006 */ 	bne	$a1,$t7,.NB0f113f10
/*  f113ef8:	02a41021 */ 	addu	$v0,$s5,$a0
/*  f113efc:	8c430000 */ 	lw	$v1,0x0($v0)
/*  f113f00:	10600005 */ 	beqz	$v1,.NB0f113f18
/*  f113f04:	2479ffff */ 	addiu	$t9,$v1,-1
/*  f113f08:	10000003 */ 	beqz	$zero,.NB0f113f18
/*  f113f0c:	ac590000 */ 	sw	$t9,0x0($v0)
.NB0f113f10:
/*  f113f10:	1471fff6 */ 	bne	$v1,$s1,.NB0f113eec
/*  f113f14:	24840004 */ 	addiu	$a0,$a0,0x4
.NB0f113f18:
/*  f113f18:	8e450004 */ 	lw	$a1,0x4($s2)
/*  f113f1c:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f113f20:	54a0ffe8 */ 	bnezl	$a1,.NB0f113ec4
/*  f113f24:	00132600 */ 	sll	$a0,$s3,0x18
.NB0f113f28:
/*  f113f28:	0000a025 */ 	or	$s4,$zero,$zero
/*  f113f2c:	27b20060 */ 	addiu	$s2,$sp,0x60
/*  f113f30:	27b70074 */ 	addiu	$s7,$sp,0x74
/*  f113f34:	27b60064 */ 	addiu	$s6,$sp,0x64
/*  f113f38:	24150004 */ 	addiu	$s5,$zero,0x4
.NB0f113f3c:
/*  f113f3c:	8e420000 */ 	lw	$v0,0x0($s2)
/*  f113f40:	5040001b */ 	beqzl	$v0,.NB0f113fb0
/*  f113f44:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f113f48:	16750003 */ 	bne	$s3,$s5,.NB0f113f58
/*  f113f4c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113f50:	52560017 */ 	beql	$s2,$s6,.NB0f113fb0
/*  f113f54:	26520004 */ 	addiu	$s2,$s2,0x4
.NB0f113f58:
/*  f113f58:	10400014 */ 	beqz	$v0,.NB0f113fac
/*  f113f5c:	00008025 */ 	or	$s0,$zero,$zero
/*  f113f60:	27ae0074 */ 	addiu	$t6,$sp,0x74
/*  f113f64:	028e8821 */ 	addu	$s1,$s4,$t6
/*  f113f68:	00132600 */ 	sll	$a0,$s3,0x18
.NB0f113f6c:
/*  f113f6c:	00044603 */ 	sra	$t0,$a0,0x18
/*  f113f70:	01002025 */ 	or	$a0,$t0,$zero
/*  f113f74:	8e250000 */ 	lw	$a1,0x0($s1)
/*  f113f78:	0fc44a98 */ 	jal	pak0f118674
/*  f113f7c:	00003025 */ 	or	$a2,$zero,$zero
/*  f113f80:	10400005 */ 	beqz	$v0,.NB0f113f98
/*  f113f84:	00402025 */ 	or	$a0,$v0,$zero
/*  f113f88:	0fc44d9d */ 	jal	pak0f119340
/*  f113f8c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f113f90:	1000000a */ 	beqz	$zero,.NB0f113fbc
/*  f113f94:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f113f98:
/*  f113f98:	8e580000 */ 	lw	$t8,0x0($s2)
/*  f113f9c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f113fa0:	0218082b */ 	sltu	$at,$s0,$t8
/*  f113fa4:	5420fff1 */ 	bnezl	$at,.NB0f113f6c
/*  f113fa8:	00132600 */ 	sll	$a0,$s3,0x18
.NB0f113fac:
/*  f113fac:	26520004 */ 	addiu	$s2,$s2,0x4
.NB0f113fb0:
/*  f113fb0:	1657ffe2 */ 	bne	$s2,$s7,.NB0f113f3c
/*  f113fb4:	26940004 */ 	addiu	$s4,$s4,0x4
.NB0f113fb8:
/*  f113fb8:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f113fbc:
/*  f113fbc:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f113fc0:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f113fc4:	8fb2001c */ 	lw	$s2,0x1c($sp)
/*  f113fc8:	8fb30020 */ 	lw	$s3,0x20($sp)
/*  f113fcc:	8fb40024 */ 	lw	$s4,0x24($sp)
/*  f113fd0:	8fb50028 */ 	lw	$s5,0x28($sp)
/*  f113fd4:	8fb6002c */ 	lw	$s6,0x2c($sp)
/*  f113fd8:	8fb70030 */ 	lw	$s7,0x30($sp)
/*  f113fdc:	03e00008 */ 	jr	$ra
/*  f113fe0:	27bd10a8 */ 	addiu	$sp,$sp,0x10a8
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11a0e8
/*  f11a0e8:	27bdefa0 */ 	addiu	$sp,$sp,-4192
/*  f11a0ec:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f11a0f0:	0004a600 */ 	sll	$s4,$a0,0x18
/*  f11a0f4:	00147603 */ 	sra	$t6,$s4,0x18
/*  f11a0f8:	afa41060 */ 	sw	$a0,0x1060($sp)
/*  f11a0fc:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11a100:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f11a104:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f11a108:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11a10c:	01c0a025 */ 	or	$s4,$t6,$zero
/*  f11a110:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f11a114:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f11a118:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11a11c:	00008825 */ 	or	$s1,$zero,$zero
/*  f11a120:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11a124:	27a6004c */ 	addiu	$a2,$sp,0x4c
/*  f11a128:	0fc459ec */ 	jal	pak0f1167b0
/*  f11a12c:	24050100 */ 	addiu	$a1,$zero,0x100
/*  f11a130:	1440001c */ 	bnez	$v0,.L0f11a1a4
/*  f11a134:	8fb8004c */ 	lw	$t8,0x4c($sp)
/*  f11a138:	1300001c */ 	beqz	$t8,.L0f11a1ac
/*  f11a13c:	27b0004c */ 	addiu	$s0,$sp,0x4c
/*  f11a140:	8e050000 */ 	lw	$a1,0x0($s0)
/*  f11a144:	2413ffff */ 	addiu	$s3,$zero,-1
/*  f11a148:	27b21050 */ 	addiu	$s2,$sp,0x1050
/*  f11a14c:	00142600 */ 	sll	$a0,$s4,0x18
.L0f11a150:
/*  f11a150:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11a154:	01002025 */ 	or	$a0,$t0,$zero
/*  f11a158:	0fc464da */ 	jal	pakFindFile
/*  f11a15c:	02403025 */ 	or	$a2,$s2,$zero
/*  f11a160:	54530004 */ 	bnel	$v0,$s3,.L0f11a174
/*  f11a164:	8fa2105c */ 	lw	$v0,0x105c($sp)
/*  f11a168:	10000011 */ 	beqz	$zero,.L0f11a1b0
/*  f11a16c:	2402ffff */ 	addiu	$v0,$zero,-1
/*  f11a170:	8fa2105c */ 	lw	$v0,0x105c($sp)
.L0f11a174:
/*  f11a174:	00024b40 */ 	sll	$t1,$v0,0xd
/*  f11a178:	00095642 */ 	srl	$t2,$t1,0x19
/*  f11a17c:	022a082b */ 	sltu	$at,$s1,$t2
/*  f11a180:	50200003 */ 	beqzl	$at,.L0f11a190
/*  f11a184:	8e050004 */ 	lw	$a1,0x4($s0)
/*  f11a188:	01408825 */ 	or	$s1,$t2,$zero
/*  f11a18c:	8e050004 */ 	lw	$a1,0x4($s0)
.L0f11a190:
/*  f11a190:	26100004 */ 	addiu	$s0,$s0,0x4
/*  f11a194:	54a0ffee */ 	bnezl	$a1,.L0f11a150
/*  f11a198:	00142600 */ 	sll	$a0,$s4,0x18
/*  f11a19c:	10000004 */ 	beqz	$zero,.L0f11a1b0
/*  f11a1a0:	02201025 */ 	or	$v0,$s1,$zero
.L0f11a1a4:
/*  f11a1a4:	10000002 */ 	beqz	$zero,.L0f11a1b0
/*  f11a1a8:	2402ffff */ 	addiu	$v0,$zero,-1
.L0f11a1ac:
/*  f11a1ac:	02201025 */ 	or	$v0,$s1,$zero
.L0f11a1b0:
/*  f11a1b0:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f11a1b4:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11a1b8:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f11a1bc:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f11a1c0:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f11a1c4:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f11a1c8:	03e00008 */ 	jr	$ra
/*  f11a1cc:	27bd1060 */ 	addiu	$sp,$sp,0x1060
);
#else
GLOBAL_ASM(
glabel pak0f11a0e8
/*  f113fe4:	27bdefa8 */ 	addiu	$sp,$sp,-4184
/*  f113fe8:	afb30020 */ 	sw	$s3,0x20($sp)
/*  f113fec:	00049e00 */ 	sll	$s3,$a0,0x18
/*  f113ff0:	00137603 */ 	sra	$t6,$s3,0x18
/*  f113ff4:	afa41058 */ 	sw	$a0,0x1058($sp)
/*  f113ff8:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f113ffc:	afbf0024 */ 	sw	$ra,0x24($sp)
/*  f114000:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f114004:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f114008:	01c09825 */ 	or	$s3,$t6,$zero
/*  f11400c:	afb2001c */ 	sw	$s2,0x1c($sp)
/*  f114010:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f114014:	00008825 */ 	or	$s1,$zero,$zero
/*  f114018:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11401c:	27a60044 */ 	addiu	$a2,$sp,0x44
/*  f114020:	0fc442dd */ 	jal	pak0f1167b0
/*  f114024:	24050100 */ 	addiu	$a1,$zero,0x100
/*  f114028:	8fb80044 */ 	lw	$t8,0x44($sp)
/*  f11402c:	27b00044 */ 	addiu	$s0,$sp,0x44
/*  f114030:	27b21048 */ 	addiu	$s2,$sp,0x1048
/*  f114034:	53000013 */ 	beqzl	$t8,.NB0f114084
/*  f114038:	8fbf0024 */ 	lw	$ra,0x24($sp)
/*  f11403c:	8e050000 */ 	lw	$a1,0x0($s0)
/*  f114040:	00132600 */ 	sll	$a0,$s3,0x18
.NB0f114044:
/*  f114044:	00044603 */ 	sra	$t0,$a0,0x18
/*  f114048:	01002025 */ 	or	$a0,$t0,$zero
/*  f11404c:	0fc44da7 */ 	jal	pakFindFile
/*  f114050:	02403025 */ 	or	$a2,$s2,$zero
/*  f114054:	8fa31054 */ 	lw	$v1,0x1054($sp)
/*  f114058:	00034b40 */ 	sll	$t1,$v1,0xd
/*  f11405c:	00095642 */ 	srl	$t2,$t1,0x19
/*  f114060:	022a082b */ 	sltu	$at,$s1,$t2
/*  f114064:	50200003 */ 	beqzl	$at,.NB0f114074
/*  f114068:	8e050004 */ 	lw	$a1,0x4($s0)
/*  f11406c:	01408825 */ 	or	$s1,$t2,$zero
/*  f114070:	8e050004 */ 	lw	$a1,0x4($s0)
.NB0f114074:
/*  f114074:	26100004 */ 	addiu	$s0,$s0,0x4
/*  f114078:	54a0fff2 */ 	bnezl	$a1,.NB0f114044
/*  f11407c:	00132600 */ 	sll	$a0,$s3,0x18
/*  f114080:	8fbf0024 */ 	lw	$ra,0x24($sp)
.NB0f114084:
/*  f114084:	02201025 */ 	or	$v0,$s1,$zero
/*  f114088:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f11408c:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f114090:	8fb2001c */ 	lw	$s2,0x1c($sp)
/*  f114094:	8fb30020 */ 	lw	$s3,0x20($sp)
/*  f114098:	03e00008 */ 	jr	$ra
/*  f11409c:	27bd1058 */ 	addiu	$sp,$sp,0x1058
);
#endif

GLOBAL_ASM(
glabel pak0f11a1d0
/*  f11a1d0:	27bdff90 */ 	addiu	$sp,$sp,-112
/*  f11a1d4:	afb20034 */ 	sw	$s2,0x34($sp)
/*  f11a1d8:	00049600 */ 	sll	$s2,$a0,0x18
/*  f11a1dc:	00127603 */ 	sra	$t6,$s2,0x18
/*  f11a1e0:	afa40070 */ 	sw	$a0,0x70($sp)
/*  f11a1e4:	afb50040 */ 	sw	$s5,0x40($sp)
/*  f11a1e8:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11a1ec:	27b50060 */ 	addiu	$s5,$sp,0x60
/*  f11a1f0:	afbf0044 */ 	sw	$ra,0x44($sp)
/*  f11a1f4:	afb10030 */ 	sw	$s1,0x30($sp)
/*  f11a1f8:	afb0002c */ 	sw	$s0,0x2c($sp)
/*  f11a1fc:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11a200:	01c09025 */ 	or	$s2,$t6,$zero
/*  f11a204:	afb4003c */ 	sw	$s4,0x3c($sp)
/*  f11a208:	afb30038 */ 	sw	$s3,0x38($sp)
/*  f11a20c:	00008025 */ 	or	$s0,$zero,$zero
/*  f11a210:	3411ffff */ 	dli	$s1,0xffff
/*  f11a214:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11a218:	02a03025 */ 	or	$a2,$s5,$zero
/*  f11a21c:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f11a220:	00002825 */ 	or	$a1,$zero,$zero
/*  f11a224:	14400026 */ 	bnez	$v0,.L0f11a2c0
/*  f11a228:	3414ffff */ 	dli	$s4,0xffff
/*  f11a22c:	24130002 */ 	addiu	$s3,$zero,0x2
/*  f11a230:	8fa30068 */ 	lw	$v1,0x68($sp)
.L0f11a234:
/*  f11a234:	30620fff */ 	andi	$v0,$v1,0xfff
/*  f11a238:	0003c5c2 */ 	srl	$t8,$v1,0x17
/*  f11a23c:	17130017 */ 	bne	$t8,$s3,.L0f11a29c
/*  f11a240:	02022821 */ 	addu	$a1,$s0,$v0
/*  f11a244:	12340013 */ 	beq	$s1,$s4,.L0f11a294
/*  f11a248:	00122600 */ 	sll	$a0,$s2,0x18
/*  f11a24c:	02114023 */ 	subu	$t0,$s0,$s1
/*  f11a250:	01024821 */ 	addu	$t1,$t0,$v0
/*  f11a254:	252afff0 */ 	addiu	$t2,$t1,-16
/*  f11a258:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11a25c:	02202825 */ 	or	$a1,$s1,$zero
/*  f11a260:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f11a264:	afab0020 */ 	sw	$t3,0x20($sp)
/*  f11a268:	3411ffff */ 	dli	$s1,0xffff
/*  f11a26c:	03202025 */ 	or	$a0,$t9,$zero
/*  f11a270:	afaa0010 */ 	sw	$t2,0x10($sp)
/*  f11a274:	02603025 */ 	or	$a2,$s3,$zero
/*  f11a278:	00003825 */ 	or	$a3,$zero,$zero
/*  f11a27c:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f11a280:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f11a284:	0fc46f15 */ 	jal	pakWriteFile
/*  f11a288:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11a28c:	10000004 */ 	beqz	$zero,.L0f11a2a0
/*  f11a290:	00002825 */ 	or	$a1,$zero,$zero
.L0f11a294:
/*  f11a294:	10000002 */ 	beqz	$zero,.L0f11a2a0
/*  f11a298:	02008825 */ 	or	$s1,$s0,$zero
.L0f11a29c:
/*  f11a29c:	02808825 */ 	or	$s1,$s4,$zero
.L0f11a2a0:
/*  f11a2a0:	00122600 */ 	sll	$a0,$s2,0x18
/*  f11a2a4:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11a2a8:	01802025 */ 	or	$a0,$t4,$zero
/*  f11a2ac:	00a08025 */ 	or	$s0,$a1,$zero
/*  f11a2b0:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f11a2b4:	02a03025 */ 	or	$a2,$s5,$zero
/*  f11a2b8:	5040ffde */ 	beqzl	$v0,.L0f11a234
/*  f11a2bc:	8fa30068 */ 	lw	$v1,0x68($sp)
.L0f11a2c0:
/*  f11a2c0:	8fbf0044 */ 	lw	$ra,0x44($sp)
/*  f11a2c4:	8fb0002c */ 	lw	$s0,0x2c($sp)
/*  f11a2c8:	8fb10030 */ 	lw	$s1,0x30($sp)
/*  f11a2cc:	8fb20034 */ 	lw	$s2,0x34($sp)
/*  f11a2d0:	8fb30038 */ 	lw	$s3,0x38($sp)
/*  f11a2d4:	8fb4003c */ 	lw	$s4,0x3c($sp)
/*  f11a2d8:	8fb50040 */ 	lw	$s5,0x40($sp)
/*  f11a2dc:	03e00008 */ 	jr	$ra
/*  f11a2e0:	27bd0070 */ 	addiu	$sp,$sp,0x70
);

void pak0f11a2e4(void)
{
	// empty
}

s32 pakGetUnk014(s8 device)
{
	return g_Paks[device].unk014;
}

void pak0f11a32c(s8 device, u8 arg1, u32 line, char *file)
{
	if (g_Paks[device].unk014 == 0) {
		g_Paks[device].unk014 = arg1;

		if ((g_Paks[device].unk014 & 1) && g_Paks[device].headercache == NULL) {
			g_Paks[device].headercachecount = 0;
			g_Paks[device].headercache = malloc(align32(sizeof(struct pakheadercache) * MAX_HEADERCACHE_ENTRIES), MEMPOOL_PERMANENT);

			// This would have been used in an osSyncPrintf call.
			// Perhaps using the strings at var7f1b4318 through var7f1b43ac?
			align32(sizeof(struct pakheadercache) * MAX_HEADERCACHE_ENTRIES);
		}
	}
}

void pak0f11a3dc(s8 device, u32 arg1, u32 arg2)
{
	if (g_Paks[device].unk014) {
		g_Paks[device].unk014 = 0;
	}

	if (g_Paks[device].unk014) {
		// empty
	}
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pakInit
/*  f11a434:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11a438:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11a43c:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11a440:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11a444:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11a448:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11a44c:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11a450:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11a454:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11a458:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11a45c:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11a460:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11a464:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11a468:	03191021 */ 	addu	$v0,$t8,$t9
/*  f11a46c:	904902b8 */ 	lbu	$t1,0x2b8($v0)
/*  f11a470:	2408ffff */ 	addiu	$t0,$zero,-1
/*  f11a474:	ac48029c */ 	sw	$t0,0x29c($v0)
/*  f11a478:	312bff7f */ 	andi	$t3,$t1,0xff7f
/*  f11a47c:	316d00f7 */ 	andi	$t5,$t3,0xf7
/*  f11a480:	a04b02b8 */ 	sb	$t3,0x2b8($v0)
/*  f11a484:	31af00df */ 	andi	$t7,$t5,0xdf
/*  f11a488:	a04d02b8 */ 	sb	$t5,0x2b8($v0)
/*  f11a48c:	31e800bf */ 	andi	$t0,$t7,0xbf
/*  f11a490:	a04f02b8 */ 	sb	$t7,0x2b8($v0)
/*  f11a494:	310a00fb */ 	andi	$t2,$t0,0xfb
/*  f11a498:	3c01bf80 */ 	lui	$at,0xbf80
/*  f11a49c:	44812000 */ 	mtc1	$at,$f4
/*  f11a4a0:	a04802b8 */ 	sb	$t0,0x2b8($v0)
/*  f11a4a4:	24030003 */ 	addiu	$v1,$zero,0x3
/*  f11a4a8:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11a4ac:	24190080 */ 	addiu	$t9,$zero,0x80
/*  f11a4b0:	a04a02b8 */ 	sb	$t2,0x2b8($v0)
/*  f11a4b4:	314b00fd */ 	andi	$t3,$t2,0xfd
/*  f11a4b8:	240c0008 */ 	addiu	$t4,$zero,0x8
/*  f11a4bc:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11a4c0:	ac430274 */ 	sw	$v1,0x274($v0)
/*  f11a4c4:	a0400014 */ 	sb	$zero,0x14($v0)
/*  f11a4c8:	ac400000 */ 	sw	$zero,0x0($v0)
/*  f11a4cc:	ac450008 */ 	sw	$a1,0x8($v0)
/*  f11a4d0:	ac450004 */ 	sw	$a1,0x4($v0)
/*  f11a4d4:	ac43000c */ 	sw	$v1,0xc($v0)
/*  f11a4d8:	ac400010 */ 	sw	$zero,0x10($v0)
/*  f11a4dc:	a05902bd */ 	sb	$t9,0x2bd($v0)
/*  f11a4e0:	ac400264 */ 	sw	$zero,0x264($v0)
/*  f11a4e4:	a04b02b8 */ 	sb	$t3,0x2b8($v0)
/*  f11a4e8:	ac4002c0 */ 	sw	$zero,0x2c0($v0)
/*  f11a4ec:	ac4002c4 */ 	sw	$zero,0x2c4($v0)
/*  f11a4f0:	ac4c025c */ 	sw	$t4,0x25c($v0)
/*  f11a4f4:	ac400260 */ 	sw	$zero,0x260($v0)
/*  f11a4f8:	ac4002c8 */ 	sw	$zero,0x2c8($v0)
/*  f11a4fc:	03e00008 */ 	jr	$ra
/*  f11a500:	e44402b4 */ 	swc1	$f4,0x2b4($v0)
);
#else
GLOBAL_ASM(
glabel pakInit
/*  f1142ec:	00047600 */ 	sll	$t6,$a0,0x18
/*  f1142f0:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f1142f4:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f1142f8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f1142fc:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f114300:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f114304:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f114308:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11430c:	3c19800a */ 	lui	$t9,0x800a
/*  f114310:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f114314:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f114318:	03191021 */ 	addu	$v0,$t8,$t9
/*  f11431c:	904902b8 */ 	lbu	$t1,0x2b8($v0)
/*  f114320:	2408ffff */ 	addiu	$t0,$zero,-1
/*  f114324:	ac48029c */ 	sw	$t0,0x29c($v0)
/*  f114328:	312bff7f */ 	andi	$t3,$t1,0xff7f
/*  f11432c:	316d00f7 */ 	andi	$t5,$t3,0xf7
/*  f114330:	a04b02b8 */ 	sb	$t3,0x2b8($v0)
/*  f114334:	31af00df */ 	andi	$t7,$t5,0xdf
/*  f114338:	a04d02b8 */ 	sb	$t5,0x2b8($v0)
/*  f11433c:	31e800bf */ 	andi	$t0,$t7,0xbf
/*  f114340:	3c01bf80 */ 	lui	$at,0xbf80
/*  f114344:	44812000 */ 	mtc1	$at,$f4
/*  f114348:	a04f02b8 */ 	sb	$t7,0x2b8($v0)
/*  f11434c:	24030003 */ 	addiu	$v1,$zero,0x3
/*  f114350:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f114354:	a04802b8 */ 	sb	$t0,0x2b8($v0)
/*  f114358:	24190080 */ 	addiu	$t9,$zero,0x80
/*  f11435c:	310900fb */ 	andi	$t1,$t0,0xfb
/*  f114360:	240a0008 */ 	addiu	$t2,$zero,0x8
/*  f114364:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f114368:	ac430274 */ 	sw	$v1,0x274($v0)
/*  f11436c:	a0400014 */ 	sb	$zero,0x14($v0)
/*  f114370:	ac400000 */ 	sw	$zero,0x0($v0)
/*  f114374:	ac450008 */ 	sw	$a1,0x8($v0)
/*  f114378:	ac450004 */ 	sw	$a1,0x4($v0)
/*  f11437c:	ac43000c */ 	sw	$v1,0xc($v0)
/*  f114380:	ac400010 */ 	sw	$zero,0x10($v0)
/*  f114384:	a05902bd */ 	sb	$t9,0x2bd($v0)
/*  f114388:	ac400264 */ 	sw	$zero,0x264($v0)
/*  f11438c:	a04902b8 */ 	sb	$t1,0x2b8($v0)
/*  f114390:	ac4002c0 */ 	sw	$zero,0x2c0($v0)
/*  f114394:	ac4002c4 */ 	sw	$zero,0x2c4($v0)
/*  f114398:	ac4a025c */ 	sw	$t2,0x25c($v0)
/*  f11439c:	ac400260 */ 	sw	$zero,0x260($v0)
/*  f1143a0:	03e00008 */ 	jr	$ra
/*  f1143a4:	e44402b4 */ 	swc1	$f4,0x2b4($v0)
);
#endif

// Mismatch due to regalloc
//void pakInit(s8 device)
//{
//	g_Paks[device].unk274 = 3;
//	g_Paks[device].unk014 = 0;
//	g_Paks[device].unk000 = 0;
//	g_Paks[device].unk008 = 1;
//	g_Paks[device].unk004 = 1;
//	g_Paks[device].unk00c = 3;
//	g_Paks[device].unk010 = 0;
//	g_Paks[device].noteindex = -1;
//	g_Paks[device].unk2bd = 128;
//	g_Paks[device].unk264 = 0;
//	g_Paks[device].unk2b8_01 = 0;
//	g_Paks[device].unk2b8_05 = 0;
//	g_Paks[device].unk2b8_03 = 0;
//	g_Paks[device].unk2b8_02 = 0;
//	g_Paks[device].unk2b8_06 = 0;
//	g_Paks[device].unk2b8_07 = 0;
//	g_Paks[device].headercache = NULL;
//	g_Paks[device].unk2c4 = NULL;
//	g_Paks[device].nextfileid = 8;
//	g_Paks[device].serial = 0;
//	g_Paks[device].unk2c8 = 0;
//	g_Paks[device].unk2b4 = -1;
//}

s32 pakReadWriteBlock(s8 device, OSPfs *pfs, s32 file_no, u8 flag, u32 address, u32 len, u8 *buffer)
{
	s32 result;
	len = pakAlign(device, len);

#if VERSION >= VERSION_NTSC_1_0
	joyGetLock();
#else
	joyGetLock(3096, "pak.c");
#endif

	result = _pakReadWriteBlock(pfs, file_no, flag, address, len, buffer);

#if VERSION >= VERSION_NTSC_1_0
	joyReleaseLock();
#else
	joyReleaseLock(3098, "pak.c");
#endif

	return result;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11a574
/*  f11a574:	27bdff58 */ 	addiu	$sp,$sp,-168
/*  f11a578:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f11a57c:	0004ae00 */ 	sll	$s5,$a0,0x18
/*  f11a580:	00157603 */ 	sra	$t6,$s5,0x18
/*  f11a584:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11a588:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a58c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a590:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a594:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a598:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11a59c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a5a0:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a5a4:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11a5a8:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f11a5ac:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11a5b0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a5b4:	01f8a021 */ 	addu	$s4,$t7,$t8
/*  f11a5b8:	8e9902b8 */ 	lw	$t9,0x2b8($s4)
/*  f11a5bc:	01c0a825 */ 	or	$s5,$t6,$zero
/*  f11a5c0:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f11a5c4:	00194840 */ 	sll	$t1,$t9,0x1
/*  f11a5c8:	afbe0038 */ 	sw	$s8,0x38($sp)
/*  f11a5cc:	afb70034 */ 	sw	$s7,0x34($sp)
/*  f11a5d0:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f11a5d4:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f11a5d8:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f11a5dc:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f11a5e0:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11a5e4:	05200003 */ 	bltz	$t1,.L0f11a5f4
/*  f11a5e8:	afa400a8 */ 	sw	$a0,0xa8($sp)
/*  f11a5ec:	1000006f */ 	beqz	$zero,.L0f11a7ac
/*  f11a5f0:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11a5f4:
/*  f11a5f4:	24170004 */ 	addiu	$s7,$zero,0x4
/*  f11a5f8:	16b70005 */ 	bne	$s5,$s7,.L0f11a610
/*  f11a5fc:	26850290 */ 	addiu	$a1,$s4,0x290
/*  f11a600:	3c1e800a */ 	lui	$s8,%hi(g_Pfses)
/*  f11a604:	27de3180 */ 	addiu	$s8,$s8,%lo(g_Pfses)
/*  f11a608:	10000009 */ 	beqz	$zero,.L0f11a630
/*  f11a60c:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a610:
/*  f11a610:	00155080 */ 	sll	$t2,$s5,0x2
/*  f11a614:	01555023 */ 	subu	$t2,$t2,$s5
/*  f11a618:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f11a61c:	01555021 */ 	addu	$t2,$t2,$s5
/*  f11a620:	3c1e800a */ 	lui	$s8,%hi(g_Pfses)
/*  f11a624:	27de3180 */ 	addiu	$s8,$s8,%lo(g_Pfses)
/*  f11a628:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f11a62c:	03ca2021 */ 	addu	$a0,$s8,$t2
.L0f11a630:
/*  f11a630:	0fc45f03 */ 	jal	pak0f117c0c
/*  f11a634:	26860294 */ 	addiu	$a2,$s4,0x294
/*  f11a638:	00152e00 */ 	sll	$a1,$s5,0x18
/*  f11a63c:	00055e03 */ 	sra	$t3,$a1,0x18
/*  f11a640:	01602825 */ 	or	$a1,$t3,$zero
/*  f11a644:	00402025 */ 	or	$a0,$v0,$zero
/*  f11a648:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11a64c:	0fc470e7 */ 	jal	pakHandleResult
/*  f11a650:	24070d9e */ 	addiu	$a3,$zero,_val7f11a650
/*  f11a654:	14400006 */ 	bnez	$v0,.L0f11a670
/*  f11a658:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a65c:	928c02b8 */ 	lbu	$t4,0x2b8($s4)
/*  f11a660:	00001025 */ 	or	$v0,$zero,$zero
/*  f11a664:	318dffbf */ 	andi	$t5,$t4,0xffbf
/*  f11a668:	10000050 */ 	beqz	$zero,.L0f11a7ac
/*  f11a66c:	a28d02b8 */ 	sb	$t5,0x2b8($s4)
.L0f11a670:
/*  f11a670:	16b70003 */ 	bne	$s5,$s7,.L0f11a680
/*  f11a674:	00157080 */ 	sll	$t6,$s5,0x2
/*  f11a678:	10000006 */ 	beqz	$zero,.L0f11a694
/*  f11a67c:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a680:
/*  f11a680:	01d57023 */ 	subu	$t6,$t6,$s5
/*  f11a684:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11a688:	01d57021 */ 	addu	$t6,$t6,$s5
/*  f11a68c:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f11a690:	03ce2021 */ 	addu	$a0,$s8,$t6
.L0f11a694:
/*  f11a694:	0fc45f20 */ 	jal	pakFreeBlocks
/*  f11a698:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f11a69c:	8faf0060 */ 	lw	$t7,0x60($sp)
/*  f11a6a0:	00152e00 */ 	sll	$a1,$s5,0x18
/*  f11a6a4:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f11a6a8:	25f800ff */ 	addiu	$t8,$t7,0xff
/*  f11a6ac:	3319ffff */ 	andi	$t9,$t8,0xffff
/*  f11a6b0:	00194203 */ 	sra	$t0,$t9,0x8
/*  f11a6b4:	a688025a */ 	sh	$t0,0x25a($s4)
/*  f11a6b8:	01202825 */ 	or	$a1,$t1,$zero
/*  f11a6bc:	00402025 */ 	or	$a0,$v0,$zero
/*  f11a6c0:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11a6c4:	0fc470e7 */ 	jal	pakHandleResult
/*  f11a6c8:	24070da7 */ 	addiu	$a3,$zero,_val7f11a6c8
/*  f11a6cc:	14400006 */ 	bnez	$v0,.L0f11a6e8
/*  f11a6d0:	00008025 */ 	or	$s0,$zero,$zero
/*  f11a6d4:	928a02b8 */ 	lbu	$t2,0x2b8($s4)
/*  f11a6d8:	00001025 */ 	or	$v0,$zero,$zero
/*  f11a6dc:	314bffbf */ 	andi	$t3,$t2,0xffbf
/*  f11a6e0:	10000032 */ 	beqz	$zero,.L0f11a7ac
/*  f11a6e4:	a28b02b8 */ 	sb	$t3,0x2b8($s4)
.L0f11a6e8:
/*  f11a6e8:	27b10064 */ 	addiu	$s1,$sp,0x64
/*  f11a6ec:	02809025 */ 	or	$s2,$s4,$zero
/*  f11a6f0:	26930018 */ 	addiu	$s3,$s4,0x18
/*  f11a6f4:	24160001 */ 	addiu	$s6,$zero,0x1
.L0f11a6f8:
/*  f11a6f8:	16b70003 */ 	bne	$s5,$s7,.L0f11a708
/*  f11a6fc:	02002825 */ 	or	$a1,$s0,$zero
/*  f11a700:	10000007 */ 	beqz	$zero,.L0f11a720
/*  f11a704:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a708:
/*  f11a708:	00156080 */ 	sll	$t4,$s5,0x2
/*  f11a70c:	01956023 */ 	subu	$t4,$t4,$s5
/*  f11a710:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f11a714:	01956021 */ 	addu	$t4,$t4,$s5
/*  f11a718:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f11a71c:	03cc2021 */ 	addu	$a0,$s8,$t4
.L0f11a720:
/*  f11a720:	0fc45f39 */ 	jal	pakFileState
/*  f11a724:	02603025 */ 	or	$a2,$s3,$zero
/*  f11a728:	10400003 */ 	beqz	$v0,.L0f11a738
/*  f11a72c:	ae220000 */ 	sw	$v0,0x0($s1)
/*  f11a730:	10000002 */ 	beqz	$zero,.L0f11a73c
/*  f11a734:	ae400218 */ 	sw	$zero,0x218($s2)
.L0f11a738:
/*  f11a738:	ae560218 */ 	sw	$s6,0x218($s2)
.L0f11a73c:
/*  f11a73c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11a740:	2a010010 */ 	slti	$at,$s0,0x10
/*  f11a744:	26310004 */ 	addiu	$s1,$s1,0x4
/*  f11a748:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f11a74c:	1420ffea */ 	bnez	$at,.L0f11a6f8
/*  f11a750:	26730020 */ 	addiu	$s3,$s3,0x20
/*  f11a754:	00008025 */ 	or	$s0,$zero,$zero
/*  f11a758:	a6800258 */ 	sh	$zero,0x258($s4)
/*  f11a75c:	27b10064 */ 	addiu	$s1,$sp,0x64
/*  f11a760:	24020010 */ 	addiu	$v0,$zero,0x10
.L0f11a764:
/*  f11a764:	8e2d0000 */ 	lw	$t5,0x0($s1)
/*  f11a768:	00107940 */ 	sll	$t7,$s0,0x5
/*  f11a76c:	028fc021 */ 	addu	$t8,$s4,$t7
/*  f11a770:	55a00008 */ 	bnezl	$t5,.L0f11a794
/*  f11a774:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11a778:	8f190018 */ 	lw	$t9,0x18($t8)
/*  f11a77c:	968e0258 */ 	lhu	$t6,0x258($s4)
/*  f11a780:	272800ff */ 	addiu	$t0,$t9,0xff
/*  f11a784:	00084a02 */ 	srl	$t1,$t0,0x8
/*  f11a788:	01c95021 */ 	addu	$t2,$t6,$t1
/*  f11a78c:	a68a0258 */ 	sh	$t2,0x258($s4)
/*  f11a790:	26100001 */ 	addiu	$s0,$s0,0x1
.L0f11a794:
/*  f11a794:	1602fff3 */ 	bne	$s0,$v0,.L0f11a764
/*  f11a798:	26310004 */ 	addiu	$s1,$s1,0x4
/*  f11a79c:	928b02b8 */ 	lbu	$t3,0x2b8($s4)
/*  f11a7a0:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f11a7a4:	316cffbf */ 	andi	$t4,$t3,0xffbf
/*  f11a7a8:	a28c02b8 */ 	sb	$t4,0x2b8($s4)
.L0f11a7ac:
/*  f11a7ac:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f11a7b0:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11a7b4:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f11a7b8:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f11a7bc:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f11a7c0:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f11a7c4:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f11a7c8:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f11a7cc:	8fb70034 */ 	lw	$s7,0x34($sp)
/*  f11a7d0:	8fbe0038 */ 	lw	$s8,0x38($sp)
/*  f11a7d4:	03e00008 */ 	jr	$ra
/*  f11a7d8:	27bd00a8 */ 	addiu	$sp,$sp,0xa8
);
#else
GLOBAL_ASM(
glabel pak0f11a574
/*  f114430:	27bdff58 */ 	addiu	$sp,$sp,-168
/*  f114434:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f114438:	0004ae00 */ 	sll	$s5,$a0,0x18
/*  f11443c:	00157603 */ 	sra	$t6,$s5,0x18
/*  f114440:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f114444:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f114448:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11444c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f114450:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114454:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f114458:	3c18800a */ 	lui	$t8,0x800a
/*  f11445c:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f114460:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f114464:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114468:	01f8a021 */ 	addu	$s4,$t7,$t8
/*  f11446c:	8e9902b8 */ 	lw	$t9,0x2b8($s4)
/*  f114470:	01c0a825 */ 	or	$s5,$t6,$zero
/*  f114474:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f114478:	00194840 */ 	sll	$t1,$t9,0x1
/*  f11447c:	afbe0038 */ 	sw	$s8,0x38($sp)
/*  f114480:	afb70034 */ 	sw	$s7,0x34($sp)
/*  f114484:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f114488:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f11448c:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f114490:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f114494:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f114498:	05200003 */ 	bltz	$t1,.NB0f1144a8
/*  f11449c:	afa400a8 */ 	sw	$a0,0xa8($sp)
/*  f1144a0:	1000006f */ 	beqz	$zero,.NB0f114660
/*  f1144a4:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f1144a8:
/*  f1144a8:	24170004 */ 	addiu	$s7,$zero,0x4
/*  f1144ac:	16b70005 */ 	bne	$s5,$s7,.NB0f1144c4
/*  f1144b0:	26850290 */ 	addiu	$a1,$s4,0x290
/*  f1144b4:	3c1e800a */ 	lui	$s8,0x800a
/*  f1144b8:	27de7658 */ 	addiu	$s8,$s8,0x7658
/*  f1144bc:	10000009 */ 	beqz	$zero,.NB0f1144e4
/*  f1144c0:	00002025 */ 	or	$a0,$zero,$zero
.NB0f1144c4:
/*  f1144c4:	00155080 */ 	sll	$t2,$s5,0x2
/*  f1144c8:	01555023 */ 	subu	$t2,$t2,$s5
/*  f1144cc:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f1144d0:	01555021 */ 	addu	$t2,$t2,$s5
/*  f1144d4:	3c1e800a */ 	lui	$s8,0x800a
/*  f1144d8:	27de7658 */ 	addiu	$s8,$s8,0x7658
/*  f1144dc:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f1144e0:	03ca2021 */ 	addu	$a0,$s8,$t2
.NB0f1144e4:
/*  f1144e4:	0fc447d4 */ 	jal	pak0f117c0c
/*  f1144e8:	26860294 */ 	addiu	$a2,$s4,0x294
/*  f1144ec:	00152e00 */ 	sll	$a1,$s5,0x18
/*  f1144f0:	00055e03 */ 	sra	$t3,$a1,0x18
/*  f1144f4:	01602825 */ 	or	$a1,$t3,$zero
/*  f1144f8:	00402025 */ 	or	$a0,$v0,$zero
/*  f1144fc:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f114500:	0fc458cb */ 	jal	pakHandleResult
/*  f114504:	24070c3d */ 	addiu	$a3,$zero,0xc3d
/*  f114508:	14400006 */ 	bnez	$v0,.NB0f114524
/*  f11450c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f114510:	928c02b8 */ 	lbu	$t4,0x2b8($s4)
/*  f114514:	00001025 */ 	or	$v0,$zero,$zero
/*  f114518:	318dffbf */ 	andi	$t5,$t4,0xffbf
/*  f11451c:	10000050 */ 	beqz	$zero,.NB0f114660
/*  f114520:	a28d02b8 */ 	sb	$t5,0x2b8($s4)
.NB0f114524:
/*  f114524:	16b70003 */ 	bne	$s5,$s7,.NB0f114534
/*  f114528:	00157080 */ 	sll	$t6,$s5,0x2
/*  f11452c:	10000006 */ 	beqz	$zero,.NB0f114548
/*  f114530:	00002025 */ 	or	$a0,$zero,$zero
.NB0f114534:
/*  f114534:	01d57023 */ 	subu	$t6,$t6,$s5
/*  f114538:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11453c:	01d57021 */ 	addu	$t6,$t6,$s5
/*  f114540:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f114544:	03ce2021 */ 	addu	$a0,$s8,$t6
.NB0f114548:
/*  f114548:	0fc447f9 */ 	jal	pakFreeBlocks
/*  f11454c:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f114550:	8faf0060 */ 	lw	$t7,0x60($sp)
/*  f114554:	00152e00 */ 	sll	$a1,$s5,0x18
/*  f114558:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f11455c:	25f800ff */ 	addiu	$t8,$t7,0xff
/*  f114560:	3319ffff */ 	andi	$t9,$t8,0xffff
/*  f114564:	00194203 */ 	sra	$t0,$t9,0x8
/*  f114568:	a688025a */ 	sh	$t0,0x25a($s4)
/*  f11456c:	01202825 */ 	or	$a1,$t1,$zero
/*  f114570:	00402025 */ 	or	$a0,$v0,$zero
/*  f114574:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f114578:	0fc458cb */ 	jal	pakHandleResult
/*  f11457c:	24070c46 */ 	addiu	$a3,$zero,0xc46
/*  f114580:	14400006 */ 	bnez	$v0,.NB0f11459c
/*  f114584:	00008025 */ 	or	$s0,$zero,$zero
/*  f114588:	928a02b8 */ 	lbu	$t2,0x2b8($s4)
/*  f11458c:	00001025 */ 	or	$v0,$zero,$zero
/*  f114590:	314bffbf */ 	andi	$t3,$t2,0xffbf
/*  f114594:	10000032 */ 	beqz	$zero,.NB0f114660
/*  f114598:	a28b02b8 */ 	sb	$t3,0x2b8($s4)
.NB0f11459c:
/*  f11459c:	27b10064 */ 	addiu	$s1,$sp,0x64
/*  f1145a0:	02809025 */ 	or	$s2,$s4,$zero
/*  f1145a4:	26930018 */ 	addiu	$s3,$s4,0x18
/*  f1145a8:	24160001 */ 	addiu	$s6,$zero,0x1
.NB0f1145ac:
/*  f1145ac:	16b70003 */ 	bne	$s5,$s7,.NB0f1145bc
/*  f1145b0:	02002825 */ 	or	$a1,$s0,$zero
/*  f1145b4:	10000007 */ 	beqz	$zero,.NB0f1145d4
/*  f1145b8:	00002025 */ 	or	$a0,$zero,$zero
.NB0f1145bc:
/*  f1145bc:	00156080 */ 	sll	$t4,$s5,0x2
/*  f1145c0:	01956023 */ 	subu	$t4,$t4,$s5
/*  f1145c4:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f1145c8:	01956021 */ 	addu	$t4,$t4,$s5
/*  f1145cc:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f1145d0:	03cc2021 */ 	addu	$a0,$s8,$t4
.NB0f1145d4:
/*  f1145d4:	0fc4481a */ 	jal	pakFileState
/*  f1145d8:	02603025 */ 	or	$a2,$s3,$zero
/*  f1145dc:	10400003 */ 	beqz	$v0,.NB0f1145ec
/*  f1145e0:	ae220000 */ 	sw	$v0,0x0($s1)
/*  f1145e4:	10000002 */ 	beqz	$zero,.NB0f1145f0
/*  f1145e8:	ae400218 */ 	sw	$zero,0x218($s2)
.NB0f1145ec:
/*  f1145ec:	ae560218 */ 	sw	$s6,0x218($s2)
.NB0f1145f0:
/*  f1145f0:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f1145f4:	2a010010 */ 	slti	$at,$s0,0x10
/*  f1145f8:	26310004 */ 	addiu	$s1,$s1,0x4
/*  f1145fc:	26520004 */ 	addiu	$s2,$s2,0x4
/*  f114600:	1420ffea */ 	bnez	$at,.NB0f1145ac
/*  f114604:	26730020 */ 	addiu	$s3,$s3,0x20
/*  f114608:	00008025 */ 	or	$s0,$zero,$zero
/*  f11460c:	a6800258 */ 	sh	$zero,0x258($s4)
/*  f114610:	27b10064 */ 	addiu	$s1,$sp,0x64
/*  f114614:	24020010 */ 	addiu	$v0,$zero,0x10
.NB0f114618:
/*  f114618:	8e2d0000 */ 	lw	$t5,0x0($s1)
/*  f11461c:	00107940 */ 	sll	$t7,$s0,0x5
/*  f114620:	028fc021 */ 	addu	$t8,$s4,$t7
/*  f114624:	55a00008 */ 	bnezl	$t5,.NB0f114648
/*  f114628:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11462c:	8f190018 */ 	lw	$t9,0x18($t8)
/*  f114630:	968e0258 */ 	lhu	$t6,0x258($s4)
/*  f114634:	272800ff */ 	addiu	$t0,$t9,0xff
/*  f114638:	00084a02 */ 	srl	$t1,$t0,0x8
/*  f11463c:	01c95021 */ 	addu	$t2,$t6,$t1
/*  f114640:	a68a0258 */ 	sh	$t2,0x258($s4)
/*  f114644:	26100001 */ 	addiu	$s0,$s0,0x1
.NB0f114648:
/*  f114648:	1602fff3 */ 	bne	$s0,$v0,.NB0f114618
/*  f11464c:	26310004 */ 	addiu	$s1,$s1,0x4
/*  f114650:	928b02b8 */ 	lbu	$t3,0x2b8($s4)
/*  f114654:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f114658:	316cffbf */ 	andi	$t4,$t3,0xffbf
/*  f11465c:	a28c02b8 */ 	sb	$t4,0x2b8($s4)
.NB0f114660:
/*  f114660:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f114664:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f114668:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f11466c:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f114670:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f114674:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f114678:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f11467c:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f114680:	8fb70034 */ 	lw	$s7,0x34($sp)
/*  f114684:	8fbe0038 */ 	lw	$s8,0x38($sp)
/*  f114688:	03e00008 */ 	jr	$ra
/*  f11468c:	27bd00a8 */ 	addiu	$sp,$sp,0xa8
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11a7dc
/*  f11a7dc:	27bdffb0 */ 	addiu	$sp,$sp,-80
/*  f11a7e0:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f11a7e4:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11a7e8:	00117603 */ 	sra	$t6,$s1,0x18
/*  f11a7ec:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11a7f0:	01c08825 */ 	or	$s1,$t6,$zero
/*  f11a7f4:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f11a7f8:	0c00543a */ 	jal	joyGetLock
/*  f11a7fc:	afa40050 */ 	sw	$a0,0x50($sp)
/*  f11a800:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a804:	16210003 */ 	bne	$s1,$at,.L0f11a814
/*  f11a808:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f11a80c:	10000009 */ 	beqz	$zero,.L0f11a834
/*  f11a810:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a814:
/*  f11a814:	00117880 */ 	sll	$t7,$s1,0x2
/*  f11a818:	01f17823 */ 	subu	$t7,$t7,$s1
/*  f11a81c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a820:	01f17821 */ 	addu	$t7,$t7,$s1
/*  f11a824:	3c18800a */ 	lui	$t8,%hi(g_Pfses)
/*  f11a828:	27183180 */ 	addiu	$t8,$t8,%lo(g_Pfses)
/*  f11a82c:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f11a830:	01f82021 */ 	addu	$a0,$t7,$t8
.L0f11a834:
/*  f11a834:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11a838:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11a83c:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11a840:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11a844:	0331c821 */ 	addu	$t9,$t9,$s1
/*  f11a848:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11a84c:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11a850:	3c08800a */ 	lui	$t0,%hi(g_Paks)
/*  f11a854:	25082380 */ 	addiu	$t0,$t0,%lo(g_Paks)
/*  f11a858:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11a85c:	03288021 */ 	addu	$s0,$t9,$t0
/*  f11a860:	8e05029c */ 	lw	$a1,0x29c($s0)
/*  f11a864:	0fc45f39 */ 	jal	pakFileState
/*  f11a868:	27a6002c */ 	addiu	$a2,$sp,0x2c
/*  f11a86c:	0c005451 */ 	jal	joyReleaseLock
/*  f11a870:	afa20028 */ 	sw	$v0,0x28($sp)
/*  f11a874:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11a878:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f11a87c:	01202825 */ 	or	$a1,$t1,$zero
/*  f11a880:	8fa40028 */ 	lw	$a0,0x28($sp)
/*  f11a884:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11a888:	0fc470e7 */ 	jal	pakHandleResult
/*  f11a88c:	24070e0f */ 	addiu	$a3,$zero,_val7f11a88c
/*  f11a890:	10400013 */ 	beqz	$v0,.L0f11a8e0
/*  f11a894:	8faa002c */ 	lw	$t2,0x2c($sp)
/*  f11a898:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a89c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11a8a0:	ae0a02a0 */ 	sw	$t2,0x2a0($s0)
/*  f11a8a4:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11a8a8:	01602025 */ 	or	$a0,$t3,$zero
/*  f11a8ac:	8e0302a0 */ 	lw	$v1,0x2a0($s0)
/*  f11a8b0:	24011c00 */ 	addiu	$at,$zero,0x1c00
/*  f11a8b4:	0062001b */ 	divu	$zero,$v1,$v0
/*  f11a8b8:	00006012 */ 	mflo	$t4
/*  f11a8bc:	00036a02 */ 	srl	$t5,$v1,0x8
/*  f11a8c0:	ae0c02a4 */ 	sw	$t4,0x2a4($s0)
/*  f11a8c4:	0061001b */ 	divu	$zero,$v1,$at
/*  f11a8c8:	00007012 */ 	mflo	$t6
/*  f11a8cc:	ae0d02a8 */ 	sw	$t5,0x2a8($s0)
/*  f11a8d0:	14400002 */ 	bnez	$v0,.L0f11a8dc
/*  f11a8d4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a8d8:	0007000d */ 	break	0x7
.L0f11a8dc:
/*  f11a8dc:	a20e02bc */ 	sb	$t6,0x2bc($s0)
.L0f11a8e0:
/*  f11a8e0:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11a8e4:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f11a8e8:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f11a8ec:	03e00008 */ 	jr	$ra
/*  f11a8f0:	27bd0050 */ 	addiu	$sp,$sp,0x50
);
#else
GLOBAL_ASM(
glabel pak0f11a7dc
/*  f114690:	27bdffb0 */ 	addiu	$sp,$sp,-80
/*  f114694:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f114698:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11469c:	00117603 */ 	sra	$t6,$s1,0x18
/*  f1146a0:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f1146a4:	afa40050 */ 	sw	$a0,0x50($sp)
/*  f1146a8:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1146ac:	01c08825 */ 	or	$s1,$t6,$zero
/*  f1146b0:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f1146b4:	24a5e304 */ 	addiu	$a1,$a1,-7420
/*  f1146b8:	0c00581b */ 	jal	joyGetLock
/*  f1146bc:	24040caa */ 	addiu	$a0,$zero,0xcaa
/*  f1146c0:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1146c4:	16210003 */ 	bne	$s1,$at,.NB0f1146d4
/*  f1146c8:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f1146cc:	10000009 */ 	beqz	$zero,.NB0f1146f4
/*  f1146d0:	00002025 */ 	or	$a0,$zero,$zero
.NB0f1146d4:
/*  f1146d4:	00117880 */ 	sll	$t7,$s1,0x2
/*  f1146d8:	01f17823 */ 	subu	$t7,$t7,$s1
/*  f1146dc:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f1146e0:	01f17821 */ 	addu	$t7,$t7,$s1
/*  f1146e4:	3c18800a */ 	lui	$t8,0x800a
/*  f1146e8:	27187658 */ 	addiu	$t8,$t8,0x7658
/*  f1146ec:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1146f0:	01f82021 */ 	addu	$a0,$t7,$t8
.NB0f1146f4:
/*  f1146f4:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f1146f8:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f1146fc:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f114700:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f114704:	0331c821 */ 	addu	$t9,$t9,$s1
/*  f114708:	3c08800a */ 	lui	$t0,0x800a
/*  f11470c:	25086870 */ 	addiu	$t0,$t0,0x6870
/*  f114710:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f114714:	03288021 */ 	addu	$s0,$t9,$t0
/*  f114718:	8e05029c */ 	lw	$a1,0x29c($s0)
/*  f11471c:	0fc4481a */ 	jal	pakFileState
/*  f114720:	27a6002c */ 	addiu	$a2,$sp,0x2c
/*  f114724:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114728:	afa20028 */ 	sw	$v0,0x28($sp)
/*  f11472c:	24a5e30c */ 	addiu	$a1,$a1,-7412
/*  f114730:	0c005834 */ 	jal	joyReleaseLock
/*  f114734:	24040cac */ 	addiu	$a0,$zero,0xcac
/*  f114738:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11473c:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f114740:	01202825 */ 	or	$a1,$t1,$zero
/*  f114744:	8fa40028 */ 	lw	$a0,0x28($sp)
/*  f114748:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11474c:	0fc458cb */ 	jal	pakHandleResult
/*  f114750:	24070cae */ 	addiu	$a3,$zero,0xcae
/*  f114754:	10400013 */ 	beqz	$v0,.NB0f1147a4
/*  f114758:	8faa002c */ 	lw	$t2,0x2c($sp)
/*  f11475c:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114760:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f114764:	ae0a02a0 */ 	sw	$t2,0x2a0($s0)
/*  f114768:	0fc4428c */ 	jal	pakGetBlockSize
/*  f11476c:	01602025 */ 	or	$a0,$t3,$zero
/*  f114770:	8e0302a0 */ 	lw	$v1,0x2a0($s0)
/*  f114774:	24011c00 */ 	addiu	$at,$zero,0x1c00
/*  f114778:	0062001b */ 	divu	$zero,$v1,$v0
/*  f11477c:	00006012 */ 	mflo	$t4
/*  f114780:	00036a02 */ 	srl	$t5,$v1,0x8
/*  f114784:	ae0c02a4 */ 	sw	$t4,0x2a4($s0)
/*  f114788:	0061001b */ 	divu	$zero,$v1,$at
/*  f11478c:	00007012 */ 	mflo	$t6
/*  f114790:	ae0d02a8 */ 	sw	$t5,0x2a8($s0)
/*  f114794:	14400002 */ 	bnez	$v0,.NB0f1147a0
/*  f114798:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11479c:	0007000d */ 	break	0x7
.NB0f1147a0:
/*  f1147a0:	a20e02bc */ 	sb	$t6,0x2bc($s0)
.NB0f1147a4:
/*  f1147a4:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f1147a8:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f1147ac:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f1147b0:	03e00008 */ 	jr	$ra
/*  f1147b4:	27bd0050 */ 	addiu	$sp,$sp,0x50
);
#endif

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f1147b8nb
/*  f1147b8:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f1147bc:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f1147c0:	00048600 */ 	sll	$s0,$a0,0x18
/*  f1147c4:	00107603 */ 	sra	$t6,$s0,0x18
/*  f1147c8:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f1147cc:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f1147d0:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1147d4:	01c08025 */ 	or	$s0,$t6,$zero
/*  f1147d8:	24a5e39c */ 	addiu	$a1,$a1,-7268
/*  f1147dc:	0c00581b */ 	jal	joyGetLock
/*  f1147e0:	24040cc8 */ 	addiu	$a0,$zero,0xcc8
/*  f1147e4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1147e8:	16010003 */ 	bne	$s0,$at,.NB0f1147f8
/*  f1147ec:	3c04800a */ 	lui	$a0,0x800a
/*  f1147f0:	10000009 */ 	beqz	$zero,.NB0f114818
/*  f1147f4:	00002825 */ 	or	$a1,$zero,$zero
.NB0f1147f8:
/*  f1147f8:	00107880 */ 	sll	$t7,$s0,0x2
/*  f1147fc:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f114800:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f114804:	01f07821 */ 	addu	$t7,$t7,$s0
/*  f114808:	3c18800a */ 	lui	$t8,0x800a
/*  f11480c:	27187658 */ 	addiu	$t8,$t8,0x7658
/*  f114810:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114814:	01f82821 */ 	addu	$a1,$t7,$t8
.NB0f114818:
/*  f114818:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f11481c:	0fc4478e */ 	jal	pakInitPak
/*  f114820:	02003025 */ 	or	$a2,$s0,$zero
/*  f114824:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114828:	afa20020 */ 	sw	$v0,0x20($sp)
/*  f11482c:	24a5e3a4 */ 	addiu	$a1,$a1,-7260
/*  f114830:	0c005834 */ 	jal	joyReleaseLock
/*  f114834:	24040cca */ 	addiu	$a0,$zero,0xcca
/*  f114838:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f11483c:	0005ce03 */ 	sra	$t9,$a1,0x18
/*  f114840:	03202825 */ 	or	$a1,$t9,$zero
/*  f114844:	8fa40020 */ 	lw	$a0,0x20($sp)
/*  f114848:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11484c:	0fc458cb */ 	jal	pakHandleResult
/*  f114850:	24070ccc */ 	addiu	$a3,$zero,0xccc
/*  f114854:	14400003 */ 	bnez	$v0,.NB0f114864
/*  f114858:	24080003 */ 	addiu	$t0,$zero,0x3
/*  f11485c:	10000015 */ 	beqz	$zero,.NB0f1148b4
/*  f114860:	00001025 */ 	or	$v0,$zero,$zero
.NB0f114864:
/*  f114864:	00104880 */ 	sll	$t1,$s0,0x2
/*  f114868:	01304823 */ 	subu	$t1,$t1,$s0
/*  f11486c:	00094880 */ 	sll	$t1,$t1,0x2
/*  f114870:	01304823 */ 	subu	$t1,$t1,$s0
/*  f114874:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f114878:	01304821 */ 	addu	$t1,$t1,$s0
/*  f11487c:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f114880:	3c01800a */ 	lui	$at,0x800a
/*  f114884:	00290821 */ 	addu	$at,$at,$t1
/*  f114888:	ac286880 */ 	sw	$t0,0x6880($at)
/*  f11488c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114890:	56010008 */ 	bnel	$s0,$at,.NB0f1148b4
/*  f114894:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f114898:	0fc45920 */ 	jal	pakExecuteDebugOperations
/*  f11489c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1148a0:	0fc45920 */ 	jal	pakExecuteDebugOperations
/*  f1148a4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1148a8:	0fc45920 */ 	jal	pakExecuteDebugOperations
/*  f1148ac:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1148b0:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f1148b4:
/*  f1148b4:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f1148b8:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f1148bc:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f1148c0:	03e00008 */ 	jr	$ra
/*  f1148c4:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_FINAL
GLOBAL_ASM(
glabel pak0f11a8f4
/*  f11a8f4:	27bdefa0 */ 	addiu	$sp,$sp,-4192
/*  f11a8f8:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f11a8fc:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11a900:	00117603 */ 	sra	$t6,$s1,0x18
/*  f11a904:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11a908:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a90c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a910:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a914:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a918:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11a91c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a920:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a924:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11a928:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f11a92c:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11a930:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a934:	01f88021 */ 	addu	$s0,$t7,$t8
/*  f11a938:	920902b8 */ 	lbu	$t1,0x2b8($s0)
/*  f11a93c:	afa41060 */ 	sw	$a0,0x1060($sp)
/*  f11a940:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11a944:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f11a948:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f11a94c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11a950:	352a0040 */ 	ori	$t2,$t1,0x40
/*  f11a954:	01c08825 */ 	or	$s1,$t6,$zero
/*  f11a958:	afa01054 */ 	sw	$zero,0x1054($sp)
/*  f11a95c:	afa01050 */ 	sw	$zero,0x1050($sp)
/*  f11a960:	ae190000 */ 	sw	$t9,0x0($s0)
/*  f11a964:	a20a02b8 */ 	sb	$t2,0x2b8($s0)
/*  f11a968:	0fc4695d */ 	jal	pak0f11a574
/*  f11a96c:	01602025 */ 	or	$a0,$t3,$zero
/*  f11a970:	8e0c0010 */ 	lw	$t4,0x10($s0)
/*  f11a974:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11a978:	15810003 */ 	bne	$t4,$at,.L0f11a988
/*  f11a97c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a980:	100000b9 */ 	beqz	$zero,.L0f11ac68
/*  f11a984:	00001025 */ 	or	$v0,$zero,$zero
.L0f11a988:
/*  f11a988:	0c00543a */ 	jal	joyGetLock
/*  f11a98c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a990:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a994:	16210003 */ 	bne	$s1,$at,.L0f11a9a4
/*  f11a998:	24053459 */ 	addiu	$a1,$zero,0x3459
/*  f11a99c:	10000009 */ 	beqz	$zero,.L0f11a9c4
/*  f11a9a0:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a9a4:
/*  f11a9a4:	00116880 */ 	sll	$t5,$s1,0x2
/*  f11a9a8:	01b16823 */ 	subu	$t5,$t5,$s1
/*  f11a9ac:	000d6880 */ 	sll	$t5,$t5,0x2
/*  f11a9b0:	01b16821 */ 	addu	$t5,$t5,$s1
/*  f11a9b4:	3c0e800a */ 	lui	$t6,%hi(g_Pfses)
/*  f11a9b8:	25ce3180 */ 	addiu	$t6,$t6,%lo(g_Pfses)
/*  f11a9bc:	000d68c0 */ 	sll	$t5,$t5,0x3
/*  f11a9c0:	01ae2021 */ 	addu	$a0,$t5,$t6
.L0f11a9c4:
/*  f11a9c4:	3c0f8007 */ 	lui	$t7,%hi(g_PakNoteExtName)
/*  f11a9c8:	25ef5d08 */ 	addiu	$t7,$t7,%lo(g_PakNoteExtName)
/*  f11a9cc:	3c064e50 */ 	lui	$a2,0x4e50
/*  f11a9d0:	3c078007 */ 	lui	$a3,%hi(g_PakNoteGameName)
/*  f11a9d4:	2602029c */ 	addiu	$v0,$s0,0x29c
/*  f11a9d8:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f11a9dc:	afa20030 */ 	sw	$v0,0x30($sp)
/*  f11a9e0:	24e75cf8 */ 	addiu	$a3,$a3,%lo(g_PakNoteGameName)
/*  f11a9e4:	34c64445 */ 	ori	$a2,$a2,_gamecode
/*  f11a9e8:	0fc45f96 */ 	jal	pakFindNote
/*  f11a9ec:	afaf0010 */ 	sw	$t7,0x10($sp)
/*  f11a9f0:	0c005451 */ 	jal	joyReleaseLock
/*  f11a9f4:	afa20048 */ 	sw	$v0,0x48($sp)
/*  f11a9f8:	8fa40048 */ 	lw	$a0,0x48($sp)
/*  f11a9fc:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11aa00:	0005c603 */ 	sra	$t8,$a1,0x18
/*  f11aa04:	1080003e */ 	beqz	$a0,.L0f11ab00
/*  f11aa08:	03002825 */ 	or	$a1,$t8,$zero
/*  f11aa0c:	00003025 */ 	or	$a2,$zero,$zero
/*  f11aa10:	0fc470e7 */ 	jal	pakHandleResult
/*  f11aa14:	24070e46 */ 	addiu	$a3,$zero,_val7f11aa14
/*  f11aa18:	9619025a */ 	lhu	$t9,0x25a($s0)
/*  f11aa1c:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11aa20:	24080002 */ 	addiu	$t0,$zero,0x2
/*  f11aa24:	2b210081 */ 	slti	$at,$t9,0x81
/*  f11aa28:	54200004 */ 	bnezl	$at,.L0f11aa3c
/*  f11aa2c:	a20902bc */ 	sb	$t1,0x2bc($s0)
/*  f11aa30:	10000002 */ 	beqz	$zero,.L0f11aa3c
/*  f11aa34:	a20802bc */ 	sb	$t0,0x2bc($s0)
/*  f11aa38:	a20902bc */ 	sb	$t1,0x2bc($s0)
.L0f11aa3c:
/*  f11aa3c:	920a02bc */ 	lbu	$t2,0x2bc($s0)
/*  f11aa40:	000a58c0 */ 	sll	$t3,$t2,0x3
/*  f11aa44:	016a5823 */ 	subu	$t3,$t3,$t2
/*  f11aa48:	000b5a80 */ 	sll	$t3,$t3,0xa
/*  f11aa4c:	0c00543a */ 	jal	joyGetLock
/*  f11aa50:	afab0044 */ 	sw	$t3,0x44($sp)
/*  f11aa54:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11aa58:	16210003 */ 	bne	$s1,$at,.L0f11aa68
/*  f11aa5c:	24053459 */ 	addiu	$a1,$zero,0x3459
/*  f11aa60:	10000009 */ 	beqz	$zero,.L0f11aa88
/*  f11aa64:	00002025 */ 	or	$a0,$zero,$zero
.L0f11aa68:
/*  f11aa68:	00116080 */ 	sll	$t4,$s1,0x2
/*  f11aa6c:	01916023 */ 	subu	$t4,$t4,$s1
/*  f11aa70:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f11aa74:	01916021 */ 	addu	$t4,$t4,$s1
/*  f11aa78:	3c0d800a */ 	lui	$t5,%hi(g_Pfses)
/*  f11aa7c:	25ad3180 */ 	addiu	$t5,$t5,%lo(g_Pfses)
/*  f11aa80:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f11aa84:	018d2021 */ 	addu	$a0,$t4,$t5
.L0f11aa88:
/*  f11aa88:	8faf0044 */ 	lw	$t7,0x44($sp)
/*  f11aa8c:	8fb80030 */ 	lw	$t8,0x30($sp)
/*  f11aa90:	3c0e8007 */ 	lui	$t6,%hi(g_PakNoteExtName)
/*  f11aa94:	25ce5d08 */ 	addiu	$t6,$t6,%lo(g_PakNoteExtName)
/*  f11aa98:	3c064e50 */ 	lui	$a2,0x4e50
/*  f11aa9c:	3c078007 */ 	lui	$a3,%hi(g_PakNoteGameName)
/*  f11aaa0:	24e75cf8 */ 	addiu	$a3,$a3,%lo(g_PakNoteGameName)
/*  f11aaa4:	34c64445 */ 	ori	$a2,$a2,_gamecode
/*  f11aaa8:	afae0010 */ 	sw	$t6,0x10($sp)
/*  f11aaac:	afaf0014 */ 	sw	$t7,0x14($sp)
/*  f11aab0:	0fc45f64 */ 	jal	pakAllocateFile
/*  f11aab4:	afb80018 */ 	sw	$t8,0x18($sp)
/*  f11aab8:	0c005451 */ 	jal	joyReleaseLock
/*  f11aabc:	afa20048 */ 	sw	$v0,0x48($sp)
/*  f11aac0:	920802b8 */ 	lbu	$t0,0x2b8($s0)
/*  f11aac4:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11aac8:	8fa40048 */ 	lw	$a0,0x48($sp)
/*  f11aacc:	00055603 */ 	sra	$t2,$a1,0x18
/*  f11aad0:	35090040 */ 	ori	$t1,$t0,0x40
/*  f11aad4:	a20902b8 */ 	sb	$t1,0x2b8($s0)
/*  f11aad8:	01402825 */ 	or	$a1,$t2,$zero
/*  f11aadc:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11aae0:	0fc470e7 */ 	jal	pakHandleResult
/*  f11aae4:	24070e54 */ 	addiu	$a3,$zero,_val7f11aae4
/*  f11aae8:	10400003 */ 	beqz	$v0,.L0f11aaf8
/*  f11aaec:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f11aaf0:	10000003 */ 	beqz	$zero,.L0f11ab00
/*  f11aaf4:	afab1054 */ 	sw	$t3,0x1054($sp)
.L0f11aaf8:
/*  f11aaf8:	1000005b */ 	beqz	$zero,.L0f11ac68
/*  f11aafc:	00001025 */ 	or	$v0,$zero,$zero
.L0f11ab00:
/*  f11ab00:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ab04:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11ab08:	0fc4695d */ 	jal	pak0f11a574
/*  f11ab0c:	01802025 */ 	or	$a0,$t4,$zero
/*  f11ab10:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ab14:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11ab18:	0fc469f7 */ 	jal	pak0f11a7dc
/*  f11ab1c:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11ab20:	920e02b8 */ 	lbu	$t6,0x2b8($s0)
/*  f11ab24:	2418000b */ 	addiu	$t8,$zero,0xb
/*  f11ab28:	a20002be */ 	sb	$zero,0x2be($s0)
/*  f11ab2c:	31cffffd */ 	andi	$t7,$t6,0xfffd
/*  f11ab30:	a20f02b8 */ 	sb	$t7,0x2b8($s0)
/*  f11ab34:	ae180010 */ 	sw	$t8,0x10($s0)
/*  f11ab38:	8fb91054 */ 	lw	$t9,0x1054($sp)
/*  f11ab3c:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ab40:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11ab44:	5320000a */ 	beqzl	$t9,.L0f11ab70
/*  f11ab48:	8faa1050 */ 	lw	$t2,0x1050($sp)
/*  f11ab4c:	0fc46d52 */ 	jal	pakScrub
/*  f11ab50:	01002025 */ 	or	$a0,$t0,$zero
/*  f11ab54:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11ab58:	10410003 */ 	beq	$v0,$at,.L0f11ab68
/*  f11ab5c:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11ab60:	10000002 */ 	beqz	$zero,.L0f11ab6c
/*  f11ab64:	ae020260 */ 	sw	$v0,0x260($s0)
.L0f11ab68:
/*  f11ab68:	afa91050 */ 	sw	$t1,0x1050($sp)
.L0f11ab6c:
/*  f11ab6c:	8faa1050 */ 	lw	$t2,0x1050($sp)
.L0f11ab70:
/*  f11ab70:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ab74:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11ab78:	55400016 */ 	bnezl	$t2,.L0f11abd4
/*  f11ab7c:	8fb91050 */ 	lw	$t9,0x1050($sp)
/*  f11ab80:	0fc465c3 */ 	jal	pak0f11970c
/*  f11ab84:	01602025 */ 	or	$a0,$t3,$zero
/*  f11ab88:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11ab8c:	14410010 */ 	bne	$v0,$at,.L0f11abd0
/*  f11ab90:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ab94:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11ab98:	0fc46d52 */ 	jal	pakScrub
/*  f11ab9c:	01802025 */ 	or	$a0,$t4,$zero
/*  f11aba0:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11aba4:	10410003 */ 	beq	$v0,$at,.L0f11abb4
/*  f11aba8:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11abac:	10000002 */ 	beqz	$zero,.L0f11abb8
/*  f11abb0:	ae020260 */ 	sw	$v0,0x260($s0)
.L0f11abb4:
/*  f11abb4:	afad1050 */ 	sw	$t5,0x1050($sp)
.L0f11abb8:
/*  f11abb8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11abbc:	52210005 */ 	beql	$s1,$at,.L0f11abd4
/*  f11abc0:	8fb91050 */ 	lw	$t9,0x1050($sp)
/*  f11abc4:	920f02b8 */ 	lbu	$t7,0x2b8($s0)
/*  f11abc8:	35f80002 */ 	ori	$t8,$t7,0x2
/*  f11abcc:	a21802b8 */ 	sb	$t8,0x2b8($s0)
.L0f11abd0:
/*  f11abd0:	8fb91050 */ 	lw	$t9,0x1050($sp)
.L0f11abd4:
/*  f11abd4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11abd8:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11abdc:	5720001e */ 	bnezl	$t9,.L0f11ac58
/*  f11abe0:	240d0016 */ 	addiu	$t5,$zero,0x16
/*  f11abe4:	0fc4683a */ 	jal	pak0f11a0e8
/*  f11abe8:	01002025 */ 	or	$a0,$t0,$zero
/*  f11abec:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11abf0:	10410018 */ 	beq	$v0,$at,.L0f11ac54
/*  f11abf4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11abf8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11abfc:	ae02025c */ 	sw	$v0,0x25c($s0)
/*  f11ac00:	01202025 */ 	or	$a0,$t1,$zero
/*  f11ac04:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11ac08:	0fc459ec */ 	jal	pak0f1167b0
/*  f11ac0c:	27a60050 */ 	addiu	$a2,$sp,0x50
/*  f11ac10:	14400010 */ 	bnez	$v0,.L0f11ac54
/*  f11ac14:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ac18:	00045603 */ 	sra	$t2,$a0,0x18
/*  f11ac1c:	0fc467a3 */ 	jal	pak0f119e8c
/*  f11ac20:	01402025 */ 	or	$a0,$t2,$zero
/*  f11ac24:	1040000b */ 	beqz	$v0,.L0f11ac54
/*  f11ac28:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11ac2c:	16210004 */ 	bne	$s1,$at,.L0f11ac40
/*  f11ac30:	240c0006 */ 	addiu	$t4,$zero,0x6
/*  f11ac34:	240b000b */ 	addiu	$t3,$zero,0xb
/*  f11ac38:	10000002 */ 	beqz	$zero,.L0f11ac44
/*  f11ac3c:	ae0b0010 */ 	sw	$t3,0x10($s0)
.L0f11ac40:
/*  f11ac40:	ae0c0010 */ 	sw	$t4,0x10($s0)
.L0f11ac44:
/*  f11ac44:	0fc44364 */ 	jal	func0f110d90
/*  f11ac48:	02202025 */ 	or	$a0,$s1,$zero
/*  f11ac4c:	10000006 */ 	beqz	$zero,.L0f11ac68
/*  f11ac50:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11ac54:
/*  f11ac54:	240d0016 */ 	addiu	$t5,$zero,0x16
.L0f11ac58:
/*  f11ac58:	ae0d0010 */ 	sw	$t5,0x10($s0)
/*  f11ac5c:	0fc44364 */ 	jal	func0f110d90
/*  f11ac60:	02202025 */ 	or	$a0,$s1,$zero
/*  f11ac64:	00001025 */ 	or	$v0,$zero,$zero
.L0f11ac68:
/*  f11ac68:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f11ac6c:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f11ac70:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f11ac74:	03e00008 */ 	jr	$ra
/*  f11ac78:	27bd1060 */ 	addiu	$sp,$sp,0x1060
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11a8f4
/*  f11a674:	27bdef98 */ 	addiu	$sp,$sp,-4200
/*  f11a678:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f11a67c:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11a680:	00117603 */ 	sra	$t6,$s1,0x18
/*  f11a684:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11a688:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a68c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a690:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a694:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a698:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11a69c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a6a0:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11a6a4:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11a6a8:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f11a6ac:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11a6b0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11a6b4:	01f88021 */ 	addu	$s0,$t7,$t8
/*  f11a6b8:	920902b8 */ 	lbu	$t1,0x2b8($s0)
/*  f11a6bc:	afa41068 */ 	sw	$a0,0x1068($sp)
/*  f11a6c0:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11a6c4:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f11a6c8:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f11a6cc:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11a6d0:	352a0040 */ 	ori	$t2,$t1,0x40
/*  f11a6d4:	01c08825 */ 	or	$s1,$t6,$zero
/*  f11a6d8:	afa0105c */ 	sw	$zero,0x105c($sp)
/*  f11a6dc:	afa01058 */ 	sw	$zero,0x1058($sp)
/*  f11a6e0:	ae190000 */ 	sw	$t9,0x0($s0)
/*  f11a6e4:	a20a02b8 */ 	sb	$t2,0x2b8($s0)
/*  f11a6e8:	0fc468bd */ 	jal	pak0f11a574
/*  f11a6ec:	01602025 */ 	or	$a0,$t3,$zero
/*  f11a6f0:	8e0c0010 */ 	lw	$t4,0x10($s0)
/*  f11a6f4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11a6f8:	15810003 */ 	bne	$t4,$at,.L0f11a708_2
/*  f11a6fc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a700:	100000b9 */ 	beqz	$zero,.L0f11a9e8_2
/*  f11a704:	00001025 */ 	or	$v0,$zero,$zero
.L0f11a708_2:
/*  f11a708:	0c00543a */ 	jal	joyGetLock
/*  f11a70c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11a710:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a714:	16210003 */ 	bne	$s1,$at,.L0f11a724_2
/*  f11a718:	24053459 */ 	addiu	$a1,$zero,0x3459
/*  f11a71c:	10000009 */ 	beqz	$zero,.L0f11a744_2
/*  f11a720:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a724_2:
/*  f11a724:	00116880 */ 	sll	$t5,$s1,0x2
/*  f11a728:	01b16823 */ 	subu	$t5,$t5,$s1
/*  f11a72c:	000d6880 */ 	sll	$t5,$t5,0x2
/*  f11a730:	01b16821 */ 	addu	$t5,$t5,$s1
/*  f11a734:	3c0e800a */ 	lui	$t6,%hi(g_Pfses)
/*  f11a738:	25ce3180 */ 	addiu	$t6,$t6,%lo(g_Pfses)
/*  f11a73c:	000d68c0 */ 	sll	$t5,$t5,0x3
/*  f11a740:	01ae2021 */ 	addu	$a0,$t5,$t6
.L0f11a744_2:
/*  f11a744:	3c0f8007 */ 	lui	$t7,%hi(g_PakNoteExtName)
/*  f11a748:	25ef5d08 */ 	addiu	$t7,$t7,%lo(g_PakNoteExtName)
/*  f11a74c:	3c064e50 */ 	lui	$a2,0x4e50
/*  f11a750:	3c078007 */ 	lui	$a3,%hi(g_PakNoteGameName)
/*  f11a754:	2602029c */ 	addiu	$v0,$s0,0x29c
/*  f11a758:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f11a75c:	afa20034 */ 	sw	$v0,0x34($sp)
/*  f11a760:	24e75cf8 */ 	addiu	$a3,$a3,%lo(g_PakNoteGameName)
/*  f11a764:	34c64445 */ 	ori	$a2,$a2,_gamecode
/*  f11a768:	0fc45f76 */ 	jal	pakFindNote
/*  f11a76c:	afaf0010 */ 	sw	$t7,0x10($sp)
/*  f11a770:	0c005451 */ 	jal	joyReleaseLock
/*  f11a774:	afa2004c */ 	sw	$v0,0x4c($sp)
/*  f11a778:	8fa4004c */ 	lw	$a0,0x4c($sp)
/*  f11a77c:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11a780:	0005c603 */ 	sra	$t8,$a1,0x18
/*  f11a784:	1080003e */ 	beqz	$a0,.L0f11a880_2
/*  f11a788:	03002825 */ 	or	$a1,$t8,$zero
/*  f11a78c:	00003025 */ 	or	$a2,$zero,$zero
/*  f11a790:	0fc47047 */ 	jal	pakHandleResult
/*  f11a794:	24070d83 */ 	addiu	$a3,$zero,0xd83
/*  f11a798:	9619025a */ 	lhu	$t9,0x25a($s0)
/*  f11a79c:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11a7a0:	24080002 */ 	addiu	$t0,$zero,0x2
/*  f11a7a4:	2b210081 */ 	slti	$at,$t9,0x81
/*  f11a7a8:	54200004 */ 	bnezl	$at,.L0f11a7bc_2
/*  f11a7ac:	a20902bc */ 	sb	$t1,0x2bc($s0)
/*  f11a7b0:	10000002 */ 	beqz	$zero,.L0f11a7bc_2
/*  f11a7b4:	a20802bc */ 	sb	$t0,0x2bc($s0)
/*  f11a7b8:	a20902bc */ 	sb	$t1,0x2bc($s0)
.L0f11a7bc_2:
/*  f11a7bc:	920a02bc */ 	lbu	$t2,0x2bc($s0)
/*  f11a7c0:	000a58c0 */ 	sll	$t3,$t2,0x3
/*  f11a7c4:	016a5823 */ 	subu	$t3,$t3,$t2
/*  f11a7c8:	000b5a80 */ 	sll	$t3,$t3,0xa
/*  f11a7cc:	0c00543a */ 	jal	joyGetLock
/*  f11a7d0:	afab0048 */ 	sw	$t3,0x48($sp)
/*  f11a7d4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a7d8:	16210003 */ 	bne	$s1,$at,.L0f11a7e8_2
/*  f11a7dc:	24053459 */ 	addiu	$a1,$zero,0x3459
/*  f11a7e0:	10000009 */ 	beqz	$zero,.L0f11a808_2
/*  f11a7e4:	00002025 */ 	or	$a0,$zero,$zero
.L0f11a7e8_2:
/*  f11a7e8:	00116080 */ 	sll	$t4,$s1,0x2
/*  f11a7ec:	01916023 */ 	subu	$t4,$t4,$s1
/*  f11a7f0:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f11a7f4:	01916021 */ 	addu	$t4,$t4,$s1
/*  f11a7f8:	3c0d800a */ 	lui	$t5,%hi(g_Pfses)
/*  f11a7fc:	25ad3180 */ 	addiu	$t5,$t5,%lo(g_Pfses)
/*  f11a800:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f11a804:	018d2021 */ 	addu	$a0,$t4,$t5
.L0f11a808_2:
/*  f11a808:	8faf0048 */ 	lw	$t7,0x48($sp)
/*  f11a80c:	8fb80034 */ 	lw	$t8,0x34($sp)
/*  f11a810:	3c0e8007 */ 	lui	$t6,%hi(g_PakNoteExtName)
/*  f11a814:	25ce5d08 */ 	addiu	$t6,$t6,%lo(g_PakNoteExtName)
/*  f11a818:	3c064e50 */ 	lui	$a2,0x4e50
/*  f11a81c:	3c078007 */ 	lui	$a3,%hi(g_PakNoteGameName)
/*  f11a820:	24e75cf8 */ 	addiu	$a3,$a3,%lo(g_PakNoteGameName)
/*  f11a824:	34c64445 */ 	ori	$a2,$a2,_gamecode
/*  f11a828:	afae0010 */ 	sw	$t6,0x10($sp)
/*  f11a82c:	afaf0014 */ 	sw	$t7,0x14($sp)
/*  f11a830:	0fc45f44 */ 	jal	pakAllocateFile
/*  f11a834:	afb80018 */ 	sw	$t8,0x18($sp)
/*  f11a838:	0c005451 */ 	jal	joyReleaseLock
/*  f11a83c:	afa2004c */ 	sw	$v0,0x4c($sp)
/*  f11a840:	920802b8 */ 	lbu	$t0,0x2b8($s0)
/*  f11a844:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11a848:	8fa4004c */ 	lw	$a0,0x4c($sp)
/*  f11a84c:	00055603 */ 	sra	$t2,$a1,0x18
/*  f11a850:	35090040 */ 	ori	$t1,$t0,0x40
/*  f11a854:	a20902b8 */ 	sb	$t1,0x2b8($s0)
/*  f11a858:	01402825 */ 	or	$a1,$t2,$zero
/*  f11a85c:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11a860:	0fc47047 */ 	jal	pakHandleResult
/*  f11a864:	24070d91 */ 	addiu	$a3,$zero,0xd91
/*  f11a868:	10400003 */ 	beqz	$v0,.L0f11a878_2
/*  f11a86c:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f11a870:	10000003 */ 	beqz	$zero,.L0f11a880_2
/*  f11a874:	afab105c */ 	sw	$t3,0x105c($sp)
.L0f11a878_2:
/*  f11a878:	1000005b */ 	beqz	$zero,.L0f11a9e8_2
/*  f11a87c:	00001025 */ 	or	$v0,$zero,$zero
.L0f11a880_2:
/*  f11a880:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a884:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11a888:	0fc468bd */ 	jal	pak0f11a574
/*  f11a88c:	01802025 */ 	or	$a0,$t4,$zero
/*  f11a890:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a894:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f11a898:	0fc46957 */ 	jal	pak0f11a7dc
/*  f11a89c:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11a8a0:	920e02b8 */ 	lbu	$t6,0x2b8($s0)
/*  f11a8a4:	2418000b */ 	addiu	$t8,$zero,0xb
/*  f11a8a8:	a20002be */ 	sb	$zero,0x2be($s0)
/*  f11a8ac:	31cffffd */ 	andi	$t7,$t6,0xfffd
/*  f11a8b0:	a20f02b8 */ 	sb	$t7,0x2b8($s0)
/*  f11a8b4:	ae180010 */ 	sw	$t8,0x10($s0)
/*  f11a8b8:	8fb9105c */ 	lw	$t9,0x105c($sp)
/*  f11a8bc:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a8c0:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11a8c4:	5320000a */ 	beqzl	$t9,.L0f11a8f0_2
/*  f11a8c8:	8faa1058 */ 	lw	$t2,0x1058($sp)
/*  f11a8cc:	0fc46cb2 */ 	jal	pakScrub
/*  f11a8d0:	01002025 */ 	or	$a0,$t0,$zero
/*  f11a8d4:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11a8d8:	10410003 */ 	beq	$v0,$at,.L0f11a8e8_2
/*  f11a8dc:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11a8e0:	10000002 */ 	beqz	$zero,.L0f11a8ec_2
/*  f11a8e4:	ae020260 */ 	sw	$v0,0x260($s0)
.L0f11a8e8_2:
/*  f11a8e8:	afa91058 */ 	sw	$t1,0x1058($sp)
.L0f11a8ec_2:
/*  f11a8ec:	8faa1058 */ 	lw	$t2,0x1058($sp)
.L0f11a8f0_2:
/*  f11a8f0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a8f4:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11a8f8:	55400016 */ 	bnezl	$t2,.L0f11a954_2
/*  f11a8fc:	8fb91058 */ 	lw	$t9,0x1058($sp)
/*  f11a900:	0fc46579 */ 	jal	pak0f11970c
/*  f11a904:	01602025 */ 	or	$a0,$t3,$zero
/*  f11a908:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11a90c:	14410010 */ 	bne	$v0,$at,.L0f11a950_2
/*  f11a910:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a914:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11a918:	0fc46cb2 */ 	jal	pakScrub
/*  f11a91c:	01802025 */ 	or	$a0,$t4,$zero
/*  f11a920:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11a924:	10410003 */ 	beq	$v0,$at,.L0f11a934_2
/*  f11a928:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11a92c:	10000002 */ 	beqz	$zero,.L0f11a938_2
/*  f11a930:	ae020260 */ 	sw	$v0,0x260($s0)
.L0f11a934_2:
/*  f11a934:	afad1058 */ 	sw	$t5,0x1058($sp)
.L0f11a938_2:
/*  f11a938:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a93c:	52210005 */ 	beql	$s1,$at,.L0f11a954_2
/*  f11a940:	8fb91058 */ 	lw	$t9,0x1058($sp)
/*  f11a944:	920f02b8 */ 	lbu	$t7,0x2b8($s0)
/*  f11a948:	35f80002 */ 	ori	$t8,$t7,0x2
/*  f11a94c:	a21802b8 */ 	sb	$t8,0x2b8($s0)
.L0f11a950_2:
/*  f11a950:	8fb91058 */ 	lw	$t9,0x1058($sp)
.L0f11a954_2:
/*  f11a954:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a958:	00044603 */ 	sra	$t0,$a0,0x18
/*  f11a95c:	5720001e */ 	bnezl	$t9,.L0f11a9d8_2
/*  f11a960:	240d0016 */ 	addiu	$t5,$zero,0x16
/*  f11a964:	0fc4679a */ 	jal	pak0f11a0e8
/*  f11a968:	01002025 */ 	or	$a0,$t0,$zero
/*  f11a96c:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11a970:	10410018 */ 	beq	$v0,$at,.L0f11a9d4_2
/*  f11a974:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a978:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11a97c:	ae02025c */ 	sw	$v0,0x25c($s0)
/*  f11a980:	01202025 */ 	or	$a0,$t1,$zero
/*  f11a984:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11a988:	0fc459cc */ 	jal	pak0f1167b0
/*  f11a98c:	27a60054 */ 	addiu	$a2,$sp,0x54
/*  f11a990:	14400010 */ 	bnez	$v0,.L0f11a9d4_2
/*  f11a994:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11a998:	00045603 */ 	sra	$t2,$a0,0x18
/*  f11a99c:	0fc46703 */ 	jal	pak0f119e8c
/*  f11a9a0:	01402025 */ 	or	$a0,$t2,$zero
/*  f11a9a4:	1040000b */ 	beqz	$v0,.L0f11a9d4_2
/*  f11a9a8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11a9ac:	16210004 */ 	bne	$s1,$at,.L0f11a9c0_2
/*  f11a9b0:	240c0006 */ 	addiu	$t4,$zero,0x6
/*  f11a9b4:	240b000b */ 	addiu	$t3,$zero,0xb
/*  f11a9b8:	10000002 */ 	beqz	$zero,.L0f11a9c4_2
/*  f11a9bc:	ae0b0010 */ 	sw	$t3,0x10($s0)
.L0f11a9c0_2:
/*  f11a9c0:	ae0c0010 */ 	sw	$t4,0x10($s0)
.L0f11a9c4_2:
/*  f11a9c4:	0fc44344 */ 	jal	func0f110d90
/*  f11a9c8:	02202025 */ 	or	$a0,$s1,$zero
/*  f11a9cc:	10000006 */ 	beqz	$zero,.L0f11a9e8_2
/*  f11a9d0:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11a9d4_2:
/*  f11a9d4:	240d0016 */ 	addiu	$t5,$zero,0x16
.L0f11a9d8_2:
/*  f11a9d8:	ae0d0010 */ 	sw	$t5,0x10($s0)
/*  f11a9dc:	0fc44344 */ 	jal	func0f110d90
/*  f11a9e0:	02202025 */ 	or	$a0,$s1,$zero
/*  f11a9e4:	00001025 */ 	or	$v0,$zero,$zero
.L0f11a9e8_2:
/*  f11a9e8:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f11a9ec:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f11a9f0:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f11a9f4:	03e00008 */ 	jr	$ra
/*  f11a9f8:	27bd1068 */ 	addiu	$sp,$sp,0x1068
);
#else
GLOBAL_ASM(
glabel pak0f11a8f4
/*  f1148c8:	27bdefb0 */ 	addiu	$sp,$sp,-4176
/*  f1148cc:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f1148d0:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f1148d4:	00117603 */ 	sra	$t6,$s1,0x18
/*  f1148d8:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f1148dc:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f1148e0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f1148e4:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f1148e8:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1148ec:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f1148f0:	3c18800a */ 	lui	$t8,0x800a
/*  f1148f4:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f1148f8:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f1148fc:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114900:	01f88021 */ 	addu	$s0,$t7,$t8
/*  f114904:	920902b8 */ 	lbu	$t1,0x2b8($s0)
/*  f114908:	afa41050 */ 	sw	$a0,0x1050($sp)
/*  f11490c:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f114910:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f114914:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f114918:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11491c:	352a0040 */ 	ori	$t2,$t1,0x40
/*  f114920:	01c08825 */ 	or	$s1,$t6,$zero
/*  f114924:	afa01044 */ 	sw	$zero,0x1044($sp)
/*  f114928:	ae190000 */ 	sw	$t9,0x0($s0)
/*  f11492c:	a20a02b8 */ 	sb	$t2,0x2b8($s0)
/*  f114930:	0fc4510c */ 	jal	pak0f11a574
/*  f114934:	01602025 */ 	or	$a0,$t3,$zero
/*  f114938:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f11493c:	24a5e3d4 */ 	addiu	$a1,$a1,-7212
/*  f114940:	0c00581b */ 	jal	joyGetLock
/*  f114944:	24040cf7 */ 	addiu	$a0,$zero,0xcf7
/*  f114948:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11494c:	16210003 */ 	bne	$s1,$at,.NB0f11495c
/*  f114950:	24053031 */ 	addiu	$a1,$zero,0x3031
/*  f114954:	10000009 */ 	beqz	$zero,.NB0f11497c
/*  f114958:	00002025 */ 	or	$a0,$zero,$zero
.NB0f11495c:
/*  f11495c:	00116080 */ 	sll	$t4,$s1,0x2
/*  f114960:	01916023 */ 	subu	$t4,$t4,$s1
/*  f114964:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f114968:	01916021 */ 	addu	$t4,$t4,$s1
/*  f11496c:	3c0d800a */ 	lui	$t5,0x800a
/*  f114970:	25ad7658 */ 	addiu	$t5,$t5,0x7658
/*  f114974:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f114978:	018d2021 */ 	addu	$a0,$t4,$t5
.NB0f11497c:
/*  f11497c:	3c0e8008 */ 	lui	$t6,0x8008
/*  f114980:	25ce80c8 */ 	addiu	$t6,$t6,-32568
/*  f114984:	3c064e50 */ 	lui	$a2,0x4e50
/*  f114988:	3c078008 */ 	lui	$a3,0x8008
/*  f11498c:	2602029c */ 	addiu	$v0,$s0,0x29c
/*  f114990:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f114994:	afa20030 */ 	sw	$v0,0x30($sp)
/*  f114998:	24e780b8 */ 	addiu	$a3,$a3,-32584
/*  f11499c:	34c64445 */ 	ori	$a2,$a2,0x4445
/*  f1149a0:	0fc4487f */ 	jal	pakFindNote
/*  f1149a4:	afae0010 */ 	sw	$t6,0x10($sp)
/*  f1149a8:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1149ac:	afa20040 */ 	sw	$v0,0x40($sp)
/*  f1149b0:	24a5e3dc */ 	addiu	$a1,$a1,-7204
/*  f1149b4:	0c005834 */ 	jal	joyReleaseLock
/*  f1149b8:	24040cf9 */ 	addiu	$a0,$zero,0xcf9
/*  f1149bc:	8fa40040 */ 	lw	$a0,0x40($sp)
/*  f1149c0:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f1149c4:	00057e03 */ 	sra	$t7,$a1,0x18
/*  f1149c8:	10800043 */ 	beqz	$a0,.NB0f114ad8
/*  f1149cc:	01e02825 */ 	or	$a1,$t7,$zero
/*  f1149d0:	00003025 */ 	or	$a2,$zero,$zero
/*  f1149d4:	0fc458cb */ 	jal	pakHandleResult
/*  f1149d8:	24070d00 */ 	addiu	$a3,$zero,0xd00
/*  f1149dc:	9618025a */ 	lhu	$t8,0x25a($s0)
/*  f1149e0:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f1149e4:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f1149e8:	2b010081 */ 	slti	$at,$t8,0x81
/*  f1149ec:	14200003 */ 	bnez	$at,.NB0f1149fc
/*  f1149f0:	24040d08 */ 	addiu	$a0,$zero,0xd08
/*  f1149f4:	10000002 */ 	beqz	$zero,.NB0f114a00
/*  f1149f8:	a21902bc */ 	sb	$t9,0x2bc($s0)
.NB0f1149fc:
/*  f1149fc:	a20802bc */ 	sb	$t0,0x2bc($s0)
.NB0f114a00:
/*  f114a00:	920902bc */ 	lbu	$t1,0x2bc($s0)
/*  f114a04:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114a08:	24a5e3e4 */ 	addiu	$a1,$a1,-7196
/*  f114a0c:	000950c0 */ 	sll	$t2,$t1,0x3
/*  f114a10:	01495023 */ 	subu	$t2,$t2,$t1
/*  f114a14:	000a5280 */ 	sll	$t2,$t2,0xa
/*  f114a18:	0c00581b */ 	jal	joyGetLock
/*  f114a1c:	afaa003c */ 	sw	$t2,0x3c($sp)
/*  f114a20:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114a24:	16210003 */ 	bne	$s1,$at,.NB0f114a34
/*  f114a28:	24053031 */ 	addiu	$a1,$zero,0x3031
/*  f114a2c:	10000009 */ 	beqz	$zero,.NB0f114a54
/*  f114a30:	00002025 */ 	or	$a0,$zero,$zero
.NB0f114a34:
/*  f114a34:	00115880 */ 	sll	$t3,$s1,0x2
/*  f114a38:	01715823 */ 	subu	$t3,$t3,$s1
/*  f114a3c:	000b5880 */ 	sll	$t3,$t3,0x2
/*  f114a40:	01715821 */ 	addu	$t3,$t3,$s1
/*  f114a44:	3c0c800a */ 	lui	$t4,0x800a
/*  f114a48:	258c7658 */ 	addiu	$t4,$t4,0x7658
/*  f114a4c:	000b58c0 */ 	sll	$t3,$t3,0x3
/*  f114a50:	016c2021 */ 	addu	$a0,$t3,$t4
.NB0f114a54:
/*  f114a54:	8fae003c */ 	lw	$t6,0x3c($sp)
/*  f114a58:	8faf0030 */ 	lw	$t7,0x30($sp)
/*  f114a5c:	3c0d8008 */ 	lui	$t5,0x8008
/*  f114a60:	25ad80c8 */ 	addiu	$t5,$t5,-32568
/*  f114a64:	3c064e50 */ 	lui	$a2,0x4e50
/*  f114a68:	3c078008 */ 	lui	$a3,0x8008
/*  f114a6c:	24e780b8 */ 	addiu	$a3,$a3,-32584
/*  f114a70:	34c64445 */ 	ori	$a2,$a2,0x4445
/*  f114a74:	afad0010 */ 	sw	$t5,0x10($sp)
/*  f114a78:	afae0014 */ 	sw	$t6,0x14($sp)
/*  f114a7c:	0fc4484d */ 	jal	pakAllocateFile
/*  f114a80:	afaf0018 */ 	sw	$t7,0x18($sp)
/*  f114a84:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114a88:	afa20040 */ 	sw	$v0,0x40($sp)
/*  f114a8c:	24a5e3ec */ 	addiu	$a1,$a1,-7188
/*  f114a90:	0c005834 */ 	jal	joyReleaseLock
/*  f114a94:	24040d0a */ 	addiu	$a0,$zero,0xd0a
/*  f114a98:	921902b8 */ 	lbu	$t9,0x2b8($s0)
/*  f114a9c:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f114aa0:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f114aa4:	37280040 */ 	ori	$t0,$t9,0x40
/*  f114aa8:	8fa40040 */ 	lw	$a0,0x40($sp)
/*  f114aac:	a20802b8 */ 	sb	$t0,0x2b8($s0)
/*  f114ab0:	01202825 */ 	or	$a1,$t1,$zero
/*  f114ab4:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f114ab8:	0fc458cb */ 	jal	pakHandleResult
/*  f114abc:	24070d0e */ 	addiu	$a3,$zero,0xd0e
/*  f114ac0:	10400003 */ 	beqz	$v0,.NB0f114ad0
/*  f114ac4:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f114ac8:	10000003 */ 	beqz	$zero,.NB0f114ad8
/*  f114acc:	afaa1044 */ 	sw	$t2,0x1044($sp)
.NB0f114ad0:
/*  f114ad0:	10000048 */ 	beqz	$zero,.NB0f114bf4
/*  f114ad4:	00001025 */ 	or	$v0,$zero,$zero
.NB0f114ad8:
/*  f114ad8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114adc:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f114ae0:	0fc4510c */ 	jal	pak0f11a574
/*  f114ae4:	01602025 */ 	or	$a0,$t3,$zero
/*  f114ae8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114aec:	00046603 */ 	sra	$t4,$a0,0x18
/*  f114af0:	0fc451a4 */ 	jal	pak0f11a7dc
/*  f114af4:	01802025 */ 	or	$a0,$t4,$zero
/*  f114af8:	240d000b */ 	addiu	$t5,$zero,0xb
/*  f114afc:	a20002be */ 	sb	$zero,0x2be($s0)
/*  f114b00:	ae0d0010 */ 	sw	$t5,0x10($s0)
/*  f114b04:	8fae1044 */ 	lw	$t6,0x1044($sp)
/*  f114b08:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114b0c:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f114b10:	15c00006 */ 	bnez	$t6,.NB0f114b2c
/*  f114b14:	00000000 */ 	sll	$zero,$zero,0x0
/*  f114b18:	0fc44e4b */ 	jal	pak0f11970c
/*  f114b1c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f114b20:	14400002 */ 	bnez	$v0,.NB0f114b2c
/*  f114b24:	24180001 */ 	addiu	$t8,$zero,0x1
/*  f114b28:	afb81044 */ 	sw	$t8,0x1044($sp)
.NB0f114b2c:
/*  f114b2c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114b30:	24a5e3f4 */ 	addiu	$a1,$a1,-7180
/*  f114b34:	0c004e18 */ 	jal	argFindByPrefix
/*  f114b38:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f114b3c:	10400009 */ 	beqz	$v0,.NB0f114b64
/*  f114b40:	8fa81044 */ 	lw	$t0,0x1044($sp)
/*  f114b44:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114b48:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f114b4c:	03202025 */ 	or	$a0,$t9,$zero
/*  f114b50:	00002825 */ 	or	$a1,$zero,$zero
/*  f114b54:	0fc453e4 */ 	jal	pakWipe
/*  f114b58:	8e0602a4 */ 	lw	$a2,0x2a4($s0)
/*  f114b5c:	1000000d */ 	beqz	$zero,.NB0f114b94
/*  f114b60:	00112600 */ 	sll	$a0,$s1,0x18
.NB0f114b64:
/*  f114b64:	15000005 */ 	bnez	$t0,.NB0f114b7c
/*  f114b68:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f114b6c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114b70:	0c004e18 */ 	jal	argFindByPrefix
/*  f114b74:	24a5e400 */ 	addiu	$a1,$a1,-7168
/*  f114b78:	10400005 */ 	beqz	$v0,.NB0f114b90
.NB0f114b7c:
/*  f114b7c:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114b80:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f114b84:	0fc45544 */ 	jal	pakScrub
/*  f114b88:	01202025 */ 	or	$a0,$t1,$zero
/*  f114b8c:	ae020260 */ 	sw	$v0,0x260($s0)
.NB0f114b90:
/*  f114b90:	00112600 */ 	sll	$a0,$s1,0x18
.NB0f114b94:
/*  f114b94:	00045603 */ 	sra	$t2,$a0,0x18
/*  f114b98:	0fc44ff9 */ 	jal	pak0f11a0e8
/*  f114b9c:	01402025 */ 	or	$a0,$t2,$zero
/*  f114ba0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114ba4:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f114ba8:	ae02025c */ 	sw	$v0,0x25c($s0)
/*  f114bac:	01602025 */ 	or	$a0,$t3,$zero
/*  f114bb0:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f114bb4:	0fc442dd */ 	jal	pak0f1167b0
/*  f114bb8:	27a60044 */ 	addiu	$a2,$sp,0x44
/*  f114bbc:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114bc0:	00046603 */ 	sra	$t4,$a0,0x18
/*  f114bc4:	0fc44f6e */ 	jal	pak0f119e8c
/*  f114bc8:	01802025 */ 	or	$a0,$t4,$zero
/*  f114bcc:	8fad0030 */ 	lw	$t5,0x30($sp)
/*  f114bd0:	3c0e800a */ 	lui	$t6,0x800a
/*  f114bd4:	25ce762c */ 	addiu	$t6,$t6,0x762c
/*  f114bd8:	15ae0004 */ 	bne	$t5,$t6,.NB0f114bec
/*  f114bdc:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f114be0:	240f000b */ 	addiu	$t7,$zero,0xb
/*  f114be4:	10000003 */ 	beqz	$zero,.NB0f114bf4
/*  f114be8:	ae0f0010 */ 	sw	$t7,0x10($s0)
.NB0f114bec:
/*  f114bec:	24180006 */ 	addiu	$t8,$zero,0x6
/*  f114bf0:	ae180010 */ 	sw	$t8,0x10($s0)
.NB0f114bf4:
/*  f114bf4:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f114bf8:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f114bfc:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f114c00:	03e00008 */ 	jr	$ra
/*  f114c04:	27bd1050 */ 	addiu	$sp,$sp,0x1050
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11ac7c
/*  f11ac7c:	27bdffd0 */ 	addiu	$sp,$sp,-48
/*  f11ac80:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11ac84:	00048600 */ 	sll	$s0,$a0,0x18
/*  f11ac88:	00107603 */ 	sra	$t6,$s0,0x18
/*  f11ac8c:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11ac90:	01c08025 */ 	or	$s0,$t6,$zero
/*  f11ac94:	afa40030 */ 	sw	$a0,0x30($sp)
/*  f11ac98:	afa0002c */ 	sw	$zero,0x2c($sp)
/*  f11ac9c:	0c00543a */ 	jal	joyGetLock
/*  f11aca0:	afa00024 */ 	sw	$zero,0x24($sp)
/*  f11aca4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11aca8:	16010003 */ 	bne	$s0,$at,.L0f11acb8
/*  f11acac:	8fa30024 */ 	lw	$v1,0x24($sp)
/*  f11acb0:	10000009 */ 	beqz	$zero,.L0f11acd8
/*  f11acb4:	00002825 */ 	or	$a1,$zero,$zero
.L0f11acb8:
/*  f11acb8:	00107880 */ 	sll	$t7,$s0,0x2
/*  f11acbc:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f11acc0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11acc4:	01f07821 */ 	addu	$t7,$t7,$s0
/*  f11acc8:	3c18800a */ 	lui	$t8,%hi(g_Pfses)
/*  f11accc:	27183180 */ 	addiu	$t8,$t8,%lo(g_Pfses)
/*  f11acd0:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f11acd4:	01f82821 */ 	addu	$a1,$t7,$t8
.L0f11acd8:
/*  f11acd8:	3c04800a */ 	lui	$a0,%hi(var80099e78)
/*  f11acdc:	24849e78 */ 	addiu	$a0,$a0,%lo(var80099e78)
/*  f11ace0:	02003025 */ 	or	$a2,$s0,$zero
/*  f11ace4:	00003825 */ 	or	$a3,$zero,$zero
/*  f11ace8:	0fc45ec1 */ 	jal	pakInitPak
/*  f11acec:	afa30024 */ 	sw	$v1,0x24($sp)
/*  f11acf0:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f11acf4:	0005ce03 */ 	sra	$t9,$a1,0x18
/*  f11acf8:	afa20028 */ 	sw	$v0,0x28($sp)
/*  f11acfc:	03202825 */ 	or	$a1,$t9,$zero
/*  f11ad00:	00402025 */ 	or	$a0,$v0,$zero
/*  f11ad04:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11ad08:	0fc470e7 */ 	jal	pakHandleResult
/*  f11ad0c:	24070ef5 */ 	addiu	$a3,$zero,_val7f11ad0c
/*  f11ad10:	1040001b */ 	beqz	$v0,.L0f11ad80
/*  f11ad14:	8fa30024 */ 	lw	$v1,0x24($sp)
/*  f11ad18:	00104880 */ 	sll	$t1,$s0,0x2
/*  f11ad1c:	01304823 */ 	subu	$t1,$t1,$s0
/*  f11ad20:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11ad24:	01304823 */ 	subu	$t1,$t1,$s0
/*  f11ad28:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11ad2c:	01304821 */ 	addu	$t1,$t1,$s0
/*  f11ad30:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11ad34:	01304823 */ 	subu	$t1,$t1,$s0
/*  f11ad38:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11ad3c:	3c01800a */ 	lui	$at,%hi(g_Paks+0x10)
/*  f11ad40:	00290821 */ 	addu	$at,$at,$t1
/*  f11ad44:	24080003 */ 	addiu	$t0,$zero,0x3
/*  f11ad48:	ac282390 */ 	sw	$t0,%lo(g_Paks+0x10)($at)
/*  f11ad4c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11ad50:	56010008 */ 	bnel	$s0,$at,.L0f11ad74
/*  f11ad54:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f11ad58:	0fc471e8 */ 	jal	pakExecuteDebugOperations
/*  f11ad5c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ad60:	0fc471e8 */ 	jal	pakExecuteDebugOperations
/*  f11ad64:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ad68:	0fc471e8 */ 	jal	pakExecuteDebugOperations
/*  f11ad6c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ad70:	240a0001 */ 	addiu	$t2,$zero,0x1
.L0f11ad74:
/*  f11ad74:	afaa002c */ 	sw	$t2,0x2c($sp)
/*  f11ad78:	10000006 */ 	beqz	$zero,.L0f11ad94
/*  f11ad7c:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11ad80:
/*  f11ad80:	8fab0028 */ 	lw	$t3,0x28($sp)
/*  f11ad84:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11ad88:	15610002 */ 	bne	$t3,$at,.L0f11ad94
/*  f11ad8c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ad90:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11ad94:
/*  f11ad94:	1460007f */ 	bnez	$v1,.L0f11af94
/*  f11ad98:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11ad9c:	16010003 */ 	bne	$s0,$at,.L0f11adac
/*  f11ada0:	3c04800a */ 	lui	$a0,%hi(var80099e78)
/*  f11ada4:	afa0002c */ 	sw	$zero,0x2c($sp)
/*  f11ada8:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11adac:
/*  f11adac:	14600079 */ 	bnez	$v1,.L0f11af94
/*  f11adb0:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11adb4:	16010003 */ 	bne	$s0,$at,.L0f11adc4
/*  f11adb8:	24849e78 */ 	addiu	$a0,$a0,%lo(var80099e78)
/*  f11adbc:	10000009 */ 	beqz	$zero,.L0f11ade4
/*  f11adc0:	00002825 */ 	or	$a1,$zero,$zero
.L0f11adc4:
/*  f11adc4:	00106080 */ 	sll	$t4,$s0,0x2
/*  f11adc8:	01906023 */ 	subu	$t4,$t4,$s0
/*  f11adcc:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f11add0:	01906021 */ 	addu	$t4,$t4,$s0
/*  f11add4:	3c0d800a */ 	lui	$t5,%hi(g_Pfses)
/*  f11add8:	25ad3180 */ 	addiu	$t5,$t5,%lo(g_Pfses)
/*  f11addc:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f11ade0:	018d2821 */ 	addu	$a1,$t4,$t5
.L0f11ade4:
/*  f11ade4:	02003025 */ 	or	$a2,$s0,$zero
/*  f11ade8:	0c013e15 */ 	jal	osMotorProbe
/*  f11adec:	afa30024 */ 	sw	$v1,0x24($sp)
/*  f11adf0:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f11adf4:	00057603 */ 	sra	$t6,$a1,0x18
/*  f11adf8:	afa20028 */ 	sw	$v0,0x28($sp)
/*  f11adfc:	01c02825 */ 	or	$a1,$t6,$zero
/*  f11ae00:	00402025 */ 	or	$a0,$v0,$zero
/*  f11ae04:	00003025 */ 	or	$a2,$zero,$zero
/*  f11ae08:	0fc470e7 */ 	jal	pakHandleResult
/*  f11ae0c:	24070f19 */ 	addiu	$a3,$zero,_val7f11ae0c
/*  f11ae10:	1040001a */ 	beqz	$v0,.L0f11ae7c
/*  f11ae14:	8fa30024 */ 	lw	$v1,0x24($sp)
/*  f11ae18:	00107880 */ 	sll	$t7,$s0,0x2
/*  f11ae1c:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f11ae20:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11ae24:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f11ae28:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11ae2c:	01f07821 */ 	addu	$t7,$t7,$s0
/*  f11ae30:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11ae34:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f11ae38:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11ae3c:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11ae40:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11ae44:	01f81021 */ 	addu	$v0,$t7,$t8
/*  f11ae48:	8c4a0264 */ 	lw	$t2,0x264($v0)
/*  f11ae4c:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f11ae50:	2408000b */ 	addiu	$t0,$zero,0xb
/*  f11ae54:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11ae58:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f11ae5c:	254b0001 */ 	addiu	$t3,$t2,0x1
/*  f11ae60:	ac590000 */ 	sw	$t9,0x0($v0)
/*  f11ae64:	ac480010 */ 	sw	$t0,0x10($v0)
/*  f11ae68:	ac490004 */ 	sw	$t1,0x4($v0)
/*  f11ae6c:	ac4b0264 */ 	sw	$t3,0x264($v0)
/*  f11ae70:	afac002c */ 	sw	$t4,0x2c($sp)
/*  f11ae74:	10000007 */ 	beqz	$zero,.L0f11ae94
/*  f11ae78:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11ae7c:
/*  f11ae7c:	8fad0028 */ 	lw	$t5,0x28($sp)
/*  f11ae80:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11ae84:	15a10003 */ 	bne	$t5,$at,.L0f11ae94
/*  f11ae88:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ae8c:	afa0002c */ 	sw	$zero,0x2c($sp)
/*  f11ae90:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11ae94:
/*  f11ae94:	1460003f */ 	bnez	$v1,.L0f11af94
/*  f11ae98:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11ae9c:	16010003 */ 	bne	$s0,$at,.L0f11aeac
/*  f11aea0:	3c04800a */ 	lui	$a0,%hi(var80099e78)
/*  f11aea4:	10000009 */ 	beqz	$zero,.L0f11aecc
/*  f11aea8:	00002825 */ 	or	$a1,$zero,$zero
.L0f11aeac:
/*  f11aeac:	00107080 */ 	sll	$t6,$s0,0x2
/*  f11aeb0:	01d07023 */ 	subu	$t6,$t6,$s0
/*  f11aeb4:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11aeb8:	01d07021 */ 	addu	$t6,$t6,$s0
/*  f11aebc:	3c0f800a */ 	lui	$t7,%hi(g_Pfses)
/*  f11aec0:	25ef3180 */ 	addiu	$t7,$t7,%lo(g_Pfses)
/*  f11aec4:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f11aec8:	01cf2821 */ 	addu	$a1,$t6,$t7
.L0f11aecc:
/*  f11aecc:	24849e78 */ 	addiu	$a0,$a0,%lo(var80099e78)
/*  f11aed0:	0c001840 */ 	jal	func00006100
/*  f11aed4:	02003025 */ 	or	$a2,$s0,$zero
/*  f11aed8:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f11aedc:	0005c603 */ 	sra	$t8,$a1,0x18
/*  f11aee0:	afa20028 */ 	sw	$v0,0x28($sp)
/*  f11aee4:	03002825 */ 	or	$a1,$t8,$zero
/*  f11aee8:	00402025 */ 	or	$a0,$v0,$zero
/*  f11aeec:	00003025 */ 	or	$a2,$zero,$zero
/*  f11aef0:	0fc470e7 */ 	jal	pakHandleResult
/*  f11aef4:	24070f31 */ 	addiu	$a3,$zero,_val7f11aef4
/*  f11aef8:	10400022 */ 	beqz	$v0,.L0f11af84
/*  f11aefc:	8fa80028 */ 	lw	$t0,0x28($sp)
/*  f11af00:	0010c880 */ 	sll	$t9,$s0,0x2
/*  f11af04:	0330c823 */ 	subu	$t9,$t9,$s0
/*  f11af08:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11af0c:	0330c823 */ 	subu	$t9,$t9,$s0
/*  f11af10:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11af14:	0330c821 */ 	addu	$t9,$t9,$s0
/*  f11af18:	3c0a8009 */ 	lui	$t2,%hi(g_Is4Mb)
/*  f11af1c:	914a0af0 */ 	lbu	$t2,%lo(g_Is4Mb)($t2)
/*  f11af20:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11af24:	0330c823 */ 	subu	$t9,$t9,$s0
/*  f11af28:	3c08800a */ 	lui	$t0,%hi(g_Paks)
/*  f11af2c:	25082380 */ 	addiu	$t0,$t0,%lo(g_Paks)
/*  f11af30:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11af34:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11af38:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11af3c:	afa9002c */ 	sw	$t1,0x2c($sp)
/*  f11af40:	15410005 */ 	bne	$t2,$at,.L0f11af58
/*  f11af44:	03281021 */ 	addu	$v0,$t9,$t0
/*  f11af48:	240b0016 */ 	addiu	$t3,$zero,0x16
/*  f11af4c:	ac400000 */ 	sw	$zero,0x0($v0)
/*  f11af50:	10000010 */ 	beqz	$zero,.L0f11af94
/*  f11af54:	ac4b0010 */ 	sw	$t3,0x10($v0)
.L0f11af58:
/*  f11af58:	904e02b8 */ 	lbu	$t6,0x2b8($v0)
/*  f11af5c:	8c580264 */ 	lw	$t8,0x264($v0)
/*  f11af60:	240c0003 */ 	addiu	$t4,$zero,0x3
/*  f11af64:	240d0008 */ 	addiu	$t5,$zero,0x8
/*  f11af68:	31cfff7f */ 	andi	$t7,$t6,0xff7f
/*  f11af6c:	27190001 */ 	addiu	$t9,$t8,0x1
/*  f11af70:	ac4c0000 */ 	sw	$t4,0x0($v0)
/*  f11af74:	ac4d0010 */ 	sw	$t5,0x10($v0)
/*  f11af78:	a04f02b8 */ 	sb	$t7,0x2b8($v0)
/*  f11af7c:	10000005 */ 	beqz	$zero,.L0f11af94
/*  f11af80:	ac590264 */ 	sw	$t9,0x264($v0)
.L0f11af84:
/*  f11af84:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11af88:	15010002 */ 	bne	$t0,$at,.L0f11af94
/*  f11af8c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11af90:	afa0002c */ 	sw	$zero,0x2c($sp)
.L0f11af94:
/*  f11af94:	0c005451 */ 	jal	joyReleaseLock
/*  f11af98:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11af9c:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11afa0:	8fa2002c */ 	lw	$v0,0x2c($sp)
/*  f11afa4:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11afa8:	03e00008 */ 	jr	$ra
/*  f11afac:	27bd0030 */ 	addiu	$sp,$sp,0x30
);
#endif

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f114c08nb
/*  f114c08:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f114c0c:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f114c10:	00048600 */ 	sll	$s0,$a0,0x18
/*  f114c14:	00107603 */ 	sra	$t6,$s0,0x18
/*  f114c18:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114c1c:	01c08025 */ 	or	$s0,$t6,$zero
/*  f114c20:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f114c24:	15c10003 */ 	bne	$t6,$at,.NB0f114c34
/*  f114c28:	afa40020 */ 	sw	$a0,0x20($sp)
/*  f114c2c:	10000064 */ 	beqz	$zero,.NB0f114dc0
/*  f114c30:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f114c34:
/*  f114c34:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114c38:	24a5e464 */ 	addiu	$a1,$a1,-7068
/*  f114c3c:	0c00581b */ 	jal	joyGetLock
/*  f114c40:	24040d6a */ 	addiu	$a0,$zero,0xd6a
/*  f114c44:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114c48:	16010003 */ 	bne	$s0,$at,.NB0f114c58
/*  f114c4c:	3c04800a */ 	lui	$a0,0x800a
/*  f114c50:	10000009 */ 	beqz	$zero,.NB0f114c78
/*  f114c54:	00002825 */ 	or	$a1,$zero,$zero
.NB0f114c58:
/*  f114c58:	00107880 */ 	sll	$t7,$s0,0x2
/*  f114c5c:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f114c60:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f114c64:	01f07821 */ 	addu	$t7,$t7,$s0
/*  f114c68:	3c18800a */ 	lui	$t8,0x800a
/*  f114c6c:	27187658 */ 	addiu	$t8,$t8,0x7658
/*  f114c70:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114c74:	01f82821 */ 	addu	$a1,$t7,$t8
.NB0f114c78:
/*  f114c78:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f114c7c:	0fc4478e */ 	jal	pakInitPak
/*  f114c80:	02003025 */ 	or	$a2,$s0,$zero
/*  f114c84:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f114c88:	0005ce03 */ 	sra	$t9,$a1,0x18
/*  f114c8c:	03202825 */ 	or	$a1,$t9,$zero
/*  f114c90:	00402025 */ 	or	$a0,$v0,$zero
/*  f114c94:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f114c98:	0fc458cb */ 	jal	pakHandleResult
/*  f114c9c:	24070d6d */ 	addiu	$a3,$zero,0xd6d
/*  f114ca0:	10400007 */ 	beqz	$v0,.NB0f114cc0
/*  f114ca4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114ca8:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114cac:	24a5e46c */ 	addiu	$a1,$a1,-7060
/*  f114cb0:	0c005834 */ 	jal	joyReleaseLock
/*  f114cb4:	24040d6f */ 	addiu	$a0,$zero,0xd6f
/*  f114cb8:	10000041 */ 	beqz	$zero,.NB0f114dc0
/*  f114cbc:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f114cc0:
/*  f114cc0:	16010003 */ 	bne	$s0,$at,.NB0f114cd0
/*  f114cc4:	3c04800a */ 	lui	$a0,0x800a
/*  f114cc8:	10000009 */ 	beqz	$zero,.NB0f114cf0
/*  f114ccc:	00002825 */ 	or	$a1,$zero,$zero
.NB0f114cd0:
/*  f114cd0:	00104080 */ 	sll	$t0,$s0,0x2
/*  f114cd4:	01104023 */ 	subu	$t0,$t0,$s0
/*  f114cd8:	00084080 */ 	sll	$t0,$t0,0x2
/*  f114cdc:	01104021 */ 	addu	$t0,$t0,$s0
/*  f114ce0:	3c09800a */ 	lui	$t1,0x800a
/*  f114ce4:	25297658 */ 	addiu	$t1,$t1,0x7658
/*  f114ce8:	000840c0 */ 	sll	$t0,$t0,0x3
/*  f114cec:	01092821 */ 	addu	$a1,$t0,$t1
.NB0f114cf0:
/*  f114cf0:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f114cf4:	0c01440d */ 	jal	osMotorProbe
/*  f114cf8:	02003025 */ 	or	$a2,$s0,$zero
/*  f114cfc:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f114d00:	00055603 */ 	sra	$t2,$a1,0x18
/*  f114d04:	01402825 */ 	or	$a1,$t2,$zero
/*  f114d08:	00402025 */ 	or	$a0,$v0,$zero
/*  f114d0c:	00003025 */ 	or	$a2,$zero,$zero
/*  f114d10:	0fc458cb */ 	jal	pakHandleResult
/*  f114d14:	24070d76 */ 	addiu	$a3,$zero,0xd76
/*  f114d18:	10400007 */ 	beqz	$v0,.NB0f114d38
/*  f114d1c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114d20:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114d24:	24a5e474 */ 	addiu	$a1,$a1,-7052
/*  f114d28:	0c005834 */ 	jal	joyReleaseLock
/*  f114d2c:	24040d78 */ 	addiu	$a0,$zero,0xd78
/*  f114d30:	10000023 */ 	beqz	$zero,.NB0f114dc0
/*  f114d34:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f114d38:
/*  f114d38:	16010003 */ 	bne	$s0,$at,.NB0f114d48
/*  f114d3c:	3c04800a */ 	lui	$a0,0x800a
/*  f114d40:	10000009 */ 	beqz	$zero,.NB0f114d68
/*  f114d44:	00002825 */ 	or	$a1,$zero,$zero
.NB0f114d48:
/*  f114d48:	00105880 */ 	sll	$t3,$s0,0x2
/*  f114d4c:	01705823 */ 	subu	$t3,$t3,$s0
/*  f114d50:	000b5880 */ 	sll	$t3,$t3,0x2
/*  f114d54:	01705821 */ 	addu	$t3,$t3,$s0
/*  f114d58:	3c0c800a */ 	lui	$t4,0x800a
/*  f114d5c:	258c7658 */ 	addiu	$t4,$t4,0x7658
/*  f114d60:	000b58c0 */ 	sll	$t3,$t3,0x3
/*  f114d64:	016c2821 */ 	addu	$a1,$t3,$t4
.NB0f114d68:
/*  f114d68:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f114d6c:	0c001890 */ 	jal	func00006100
/*  f114d70:	02003025 */ 	or	$a2,$s0,$zero
/*  f114d74:	00102e00 */ 	sll	$a1,$s0,0x18
/*  f114d78:	00056e03 */ 	sra	$t5,$a1,0x18
/*  f114d7c:	01a02825 */ 	or	$a1,$t5,$zero
/*  f114d80:	00402025 */ 	or	$a0,$v0,$zero
/*  f114d84:	00003025 */ 	or	$a2,$zero,$zero
/*  f114d88:	0fc458cb */ 	jal	pakHandleResult
/*  f114d8c:	24070d7f */ 	addiu	$a3,$zero,0xd7f
/*  f114d90:	10400007 */ 	beqz	$v0,.NB0f114db0
/*  f114d94:	24040d86 */ 	addiu	$a0,$zero,0xd86
/*  f114d98:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114d9c:	24a5e47c */ 	addiu	$a1,$a1,-7044
/*  f114da0:	0c005834 */ 	jal	joyReleaseLock
/*  f114da4:	24040d81 */ 	addiu	$a0,$zero,0xd81
/*  f114da8:	10000005 */ 	beqz	$zero,.NB0f114dc0
/*  f114dac:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f114db0:
/*  f114db0:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114db4:	0c005834 */ 	jal	joyReleaseLock
/*  f114db8:	24a5e484 */ 	addiu	$a1,$a1,-7036
/*  f114dbc:	00001025 */ 	or	$v0,$zero,$zero
.NB0f114dc0:
/*  f114dc0:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f114dc4:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f114dc8:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f114dcc:	03e00008 */ 	jr	$ra
/*  f114dd0:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f114dd4nb
/*  f114dd4:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f114dd8:	afb10018 */ 	sw	$s1,0x18($sp)
/*  f114ddc:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f114de0:	00117603 */ 	sra	$t6,$s1,0x18
/*  f114de4:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f114de8:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f114dec:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f114df0:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f114df4:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114df8:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f114dfc:	3c18800a */ 	lui	$t8,0x800a
/*  f114e00:	afb00014 */ 	sw	$s0,0x14($sp)
/*  f114e04:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f114e08:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f114e0c:	01f88021 */ 	addu	$s0,$t7,$t8
/*  f114e10:	920802b8 */ 	lbu	$t0,0x2b8($s0)
/*  f114e14:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f114e18:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f114e1c:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f114e20:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f114e24:	2419ffff */ 	addiu	$t9,$zero,-1
/*  f114e28:	240a0003 */ 	addiu	$t2,$zero,0x3
/*  f114e2c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f114e30:	3109ff7f */ 	andi	$t1,$t0,0xff7f
/*  f114e34:	01c08825 */ 	or	$s1,$t6,$zero
/*  f114e38:	ae19029c */ 	sw	$t9,0x29c($s0)
/*  f114e3c:	a20902b8 */ 	sb	$t1,0x2b8($s0)
/*  f114e40:	ae000000 */ 	sw	$zero,0x0($s0)
/*  f114e44:	ae020008 */ 	sw	$v0,0x8($s0)
/*  f114e48:	ae020004 */ 	sw	$v0,0x4($s0)
/*  f114e4c:	ae0a000c */ 	sw	$t2,0xc($s0)
/*  f114e50:	0fc45302 */ 	jal	pak0f114c08nb
/*  f114e54:	01602025 */ 	or	$a0,$t3,$zero
/*  f114e58:	50400049 */ 	beqzl	$v0,.NB0f114f80
/*  f114e5c:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f114e60:	92020014 */ 	lbu	$v0,0x14($s0)
/*  f114e64:	00112600 */ 	sll	$a0,$s1,0x18
/*  f114e68:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f114e6c:	304c0001 */ 	andi	$t4,$v0,0x1
/*  f114e70:	5180000b */ 	beqzl	$t4,.NB0f114ea0
/*  f114e74:	30580002 */ 	andi	$t8,$v0,0x2
/*  f114e78:	0fc451ee */ 	jal	pak0f1147b8nb
/*  f114e7c:	01a02025 */ 	or	$a0,$t5,$zero
/*  f114e80:	50400006 */ 	beqzl	$v0,.NB0f114e9c
/*  f114e84:	92020014 */ 	lbu	$v0,0x14($s0)
/*  f114e88:	8e0e0264 */ 	lw	$t6,0x264($s0)
/*  f114e8c:	25cf0001 */ 	addiu	$t7,$t6,0x1
/*  f114e90:	1000003a */ 	beqz	$zero,.NB0f114f7c
/*  f114e94:	ae0f0264 */ 	sw	$t7,0x264($s0)
/*  f114e98:	92020014 */ 	lbu	$v0,0x14($s0)
.NB0f114e9c:
/*  f114e9c:	30580002 */ 	andi	$t8,$v0,0x2
.NB0f114ea0:
/*  f114ea0:	1300002c */ 	beqz	$t8,.NB0f114f54
/*  f114ea4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114ea8:	12210034 */ 	beq	$s1,$at,.NB0f114f7c
/*  f114eac:	24040dba */ 	addiu	$a0,$zero,0xdba
/*  f114eb0:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114eb4:	0c00581b */ 	jal	joyGetLock
/*  f114eb8:	24a5e48c */ 	addiu	$a1,$a1,-7028
/*  f114ebc:	24010004 */ 	addiu	$at,$zero,0x4
/*  f114ec0:	16210003 */ 	bne	$s1,$at,.NB0f114ed0
/*  f114ec4:	3c04800a */ 	lui	$a0,0x800a
/*  f114ec8:	10000009 */ 	beqz	$zero,.NB0f114ef0
/*  f114ecc:	00002825 */ 	or	$a1,$zero,$zero
.NB0f114ed0:
/*  f114ed0:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f114ed4:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f114ed8:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f114edc:	0331c821 */ 	addu	$t9,$t9,$s1
/*  f114ee0:	3c08800a */ 	lui	$t0,0x800a
/*  f114ee4:	25087658 */ 	addiu	$t0,$t0,0x7658
/*  f114ee8:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f114eec:	03282821 */ 	addu	$a1,$t9,$t0
.NB0f114ef0:
/*  f114ef0:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f114ef4:	0c01440d */ 	jal	osMotorProbe
/*  f114ef8:	02203025 */ 	or	$a2,$s1,$zero
/*  f114efc:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f114f00:	afa20020 */ 	sw	$v0,0x20($sp)
/*  f114f04:	24a5e494 */ 	addiu	$a1,$a1,-7020
/*  f114f08:	0c005834 */ 	jal	joyReleaseLock
/*  f114f0c:	24040dbc */ 	addiu	$a0,$zero,0xdbc
/*  f114f10:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f114f14:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f114f18:	01202825 */ 	or	$a1,$t1,$zero
/*  f114f1c:	8fa40020 */ 	lw	$a0,0x20($sp)
/*  f114f20:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f114f24:	0fc458cb */ 	jal	pakHandleResult
/*  f114f28:	24070dbe */ 	addiu	$a3,$zero,0xdbe
/*  f114f2c:	10400008 */ 	beqz	$v0,.NB0f114f50
/*  f114f30:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f114f34:	8e0c0264 */ 	lw	$t4,0x264($s0)
/*  f114f38:	240b000b */ 	addiu	$t3,$zero,0xb
/*  f114f3c:	ae0a0000 */ 	sw	$t2,0x0($s0)
/*  f114f40:	258d0001 */ 	addiu	$t5,$t4,0x1
/*  f114f44:	ae0b0010 */ 	sw	$t3,0x10($s0)
/*  f114f48:	1000000c */ 	beqz	$zero,.NB0f114f7c
/*  f114f4c:	ae0d0264 */ 	sw	$t5,0x264($s0)
.NB0f114f50:
/*  f114f50:	92020014 */ 	lbu	$v0,0x14($s0)
.NB0f114f54:
/*  f114f54:	304e0004 */ 	andi	$t6,$v0,0x4
/*  f114f58:	11c00008 */ 	beqz	$t6,.NB0f114f7c
/*  f114f5c:	240f0008 */ 	addiu	$t7,$zero,0x8
/*  f114f60:	921802b8 */ 	lbu	$t8,0x2b8($s0)
/*  f114f64:	8e080264 */ 	lw	$t0,0x264($s0)
/*  f114f68:	ae0f0010 */ 	sw	$t7,0x10($s0)
/*  f114f6c:	3319ff7f */ 	andi	$t9,$t8,0xff7f
/*  f114f70:	25090001 */ 	addiu	$t1,$t0,0x1
/*  f114f74:	a21902b8 */ 	sb	$t9,0x2b8($s0)
/*  f114f78:	ae090264 */ 	sw	$t1,0x264($s0)
.NB0f114f7c:
/*  f114f7c:	8fbf001c */ 	lw	$ra,0x1c($sp)
.NB0f114f80:
/*  f114f80:	8fb00014 */ 	lw	$s0,0x14($sp)
/*  f114f84:	8fb10018 */ 	lw	$s1,0x18($sp)
/*  f114f88:	03e00008 */ 	jr	$ra
/*  f114f8c:	27bd0028 */ 	addiu	$sp,$sp,0x28
);
#endif

void pakWipe(s8 device, u32 start, u32 end)
{
	u8 buffer[128];
	u32 i;

	for (i = 0; i < pakGetBlockSize(device); i++) {
		buffer[i] = '!';
	}

	for (i = start; i < end; i++) {
		s32 result = pakReadWriteBlock(device, PFS(device), g_Paks[device].noteindex, PFS_WRITE, i * pakGetBlockSize(device), pakGetBlockSize(device), buffer);

		g_Paks[device].headercachecount = 0;

#if VERSION >= VERSION_PAL_FINAL
		if (!pakHandleResult(result, device, 1, 3955))
#elif VERSION >= VERSION_NTSC_FINAL
		if (!pakHandleResult(result, device, 1, 3948))
#elif VERSION >= VERSION_NTSC_1_0
		if (!pakHandleResult(result, device, 1, 3753))
#else
		if (!pakHandleResult(result, device, 1, 3573))
#endif
		{
			g_Paks[device].noteindex = -1;
			break;
		}
	}
}

void pakSaveHeaderToCache(s8 device, s32 blocknum, struct pakfileheader *header)
{
	struct pak *pak = &g_Paks[device];
	s32 count;
	s32 overview[1024];
	u32 stack[2];
	s32 j;
	s32 k;
	s32 i;
	s32 endblocknum = header->filelen / pakGetBlockSize(device) + blocknum;

	for (i = 0; i < ARRAYCOUNT(overview); i++) {
		overview[i] = -1;
	}

	// Iterate existing cache items and write their indexes into the overview array,
	// where the index in the overview array is determined by the cache item's blocknum.
	for (i = 0; i < pak->headercachecount; i++) {
		struct pakfileheader *tmp = (struct pakfileheader *) pak->headercache[i].payload;

		for (j = 0; j < tmp->filelen / pakGetBlockSize(device); j++) {
			overview[pak->headercache[i].blocknum + j] = i;
		}
	}

	// Examine the overview range where the new cache entry is going to go.
	// If any cache entries are there then they're likely an older version of
	// the cache header that's being inserted, so invalidate them.
	for (k = blocknum; k < endblocknum; k++) {
		if (overview[k] != -1) {
			pak->headercache[overview[k]].blocknum = -1;
		}
	}

	// Save the header into the cache
	pak->headercache[pak->headercachecount].blocknum = blocknum;
	memcpy(pak->headercache[pak->headercachecount].payload, header, pakGetBlockSize(device));

	pak->headercachecount++;

	// Close any gaps in the cache list and recount for good measure
	count = 0;

	for (i = 0; i < pak->headercachecount; i++) {
		if (pak->headercache[i].blocknum != -1) {
			pak->headercache[count].blocknum = pak->headercache[i].blocknum;
			memcpy(&pak->headercache[count].payload, &pak->headercache[i].payload, pakGetBlockSize(device));
			count++;
		}
	}

	pak->headercachecount = count;
}

bool pakRetrieveHeaderFromCache(s8 device, s32 blocknum, struct pakfileheader *dst)
{
	struct pak *pak = &g_Paks[device];
	s32 i;

	if (pak->headercachecount < MAX_HEADERCACHE_ENTRIES) {
		for (i = 0; i < pak->headercachecount; i++) {
			if (blocknum == pak->headercache[i].blocknum) {
				memcpy(dst, &pak->headercache[i].payload, sizeof(struct pakfileheader));
				return true;
			}
		}
	}

	return false;
}

s32 pakScrub(s8 device)
{
	u8 data[32];
	s32 address;
	s32 result;
	s32 i;

	for (i = 0; i < 32; i++) {
		data[i] = random() & 0xff;
	}

	address = pakGetAlignedFileLenByBodyLen(device, pakGetBodyLenByType(device, PAKFILETYPE_TERMINATOR));

	g_Paks[device].nextfileid = 0x10;
#if VERSION >= VERSION_NTSC_1_0
	g_Paks[device].serial = pakGenerateSerial(device);
#else
	g_Paks[device].serial = 0x10 + random() % 0x1ff0;
#endif
	g_Paks[device].headercachecount = 0;

	pakWriteFile(device, 0, PAKFILETYPE_TERMINATOR, NULL, 0, NULL, NULL, 0, 1);

	result = pakReadWriteBlock(device, PFS(device), g_Paks[device].noteindex, PFS_WRITE, address, pakGetBlockSize(device), data);

#if VERSION >= VERSION_PAL_FINAL
	if (pakHandleResult(result, device, 1, 4147) == 0) {
		return -1;
	}
#elif VERSION >= VERSION_NTSC_FINAL
	if (pakHandleResult(result, device, 1, 4140) == 0) {
		return -1;
	}
#elif VERSION >= VERSION_NTSC_1_0
	if (pakHandleResult(result, device, 1, 3945) == 0) {
		return -1;
	}
#else
	pakHandleResult(result, device, 1, 3779);
#endif

	return g_Paks[device].serial;
}

s32 pak0f11b6ec(s8 device)
{
	if (g_Paks[device].unk010 == 0xb && g_Paks[device].unk000 == 2) {
		return 3;
	}

	return 0;
}

bool pak0f11b75c(s8 device, u32 *arg1)
{
	struct pakfileheader header;
	s32 offset = 0;
	u32 stack[2];

	for (offset = 0; offset < g_Paks[device].numbytes;) {
		s32 value = pakReadHeaderAtOffset(device, offset, &header);
		offset += header.filelen;

#if VERSION >= VERSION_NTSC_1_0
		if (value == 1) {
			return true;
		}
#endif

		if (PAKFILETYPE_TERMINATOR == header.filetype) {
			*arg1 = offset;
			return false;
		}

		if (value) {
			return false;
		}
	}

	return false;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11b86c
/*  f11b86c:	27bdfed0 */ 	addiu	$sp,$sp,-304
/*  f11b870:	afb2002c */ 	sw	$s2,0x2c($sp)
/*  f11b874:	8fb20140 */ 	lw	$s2,0x140($sp)
/*  f11b878:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f11b87c:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11b880:	00117603 */ 	sra	$t6,$s1,0x18
/*  f11b884:	afb50038 */ 	sw	$s5,0x38($sp)
/*  f11b888:	afb30030 */ 	sw	$s3,0x30($sp)
/*  f11b88c:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11b890:	00e09825 */ 	or	$s3,$a3,$zero
/*  f11b894:	00c0a825 */ 	or	$s5,$a2,$zero
/*  f11b898:	01c08825 */ 	or	$s1,$t6,$zero
/*  f11b89c:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f11b8a0:	afb40034 */ 	sw	$s4,0x34($sp)
/*  f11b8a4:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f11b8a8:	afa40130 */ 	sw	$a0,0x130($sp)
/*  f11b8ac:	16410006 */ 	bne	$s2,$at,.L0f11b8c8
/*  f11b8b0:	afa50134 */ 	sw	$a1,0x134($sp)
/*  f11b8b4:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f11b8b8:	afaf0114 */ 	sw	$t7,0x114($sp)
/*  f11b8bc:	24140001 */ 	addiu	$s4,$zero,0x1
/*  f11b8c0:	10000003 */ 	beqz	$zero,.L0f11b8d0
/*  f11b8c4:	00009025 */ 	or	$s2,$zero,$zero
.L0f11b8c8:
/*  f11b8c8:	afa00114 */ 	sw	$zero,0x114($sp)
/*  f11b8cc:	0000a025 */ 	or	$s4,$zero,$zero
.L0f11b8d0:
/*  f11b8d0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11b8d4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11b8d8:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11b8dc:	03002025 */ 	or	$a0,$t8,$zero
/*  f11b8e0:	0012802b */ 	sltu	$s0,$zero,$s2
/*  f11b8e4:	12000008 */ 	beqz	$s0,.L0f11b908
/*  f11b8e8:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11b8ec:	0015802b */ 	sltu	$s0,$zero,$s5
/*  f11b8f0:	12000005 */ 	beqz	$s0,.L0f11b908
/*  f11b8f4:	26590010 */ 	addiu	$t9,$s2,0x10
/*  f11b8f8:	0059802b */ 	sltu	$s0,$v0,$t9
/*  f11b8fc:	3a100001 */ 	xori	$s0,$s0,0x1
/*  f11b900:	0010482b */ 	sltu	$t1,$zero,$s0
/*  f11b904:	01208025 */ 	or	$s0,$t1,$zero
.L0f11b908:
/*  f11b908:	16600002 */ 	bnez	$s3,.L0f11b914
/*  f11b90c:	00045603 */ 	sra	$t2,$a0,0x18
/*  f11b910:	27b30120 */ 	addiu	$s3,$sp,0x120
.L0f11b914:
/*  f11b914:	01402025 */ 	or	$a0,$t2,$zero
/*  f11b918:	8fa50134 */ 	lw	$a1,0x134($sp)
/*  f11b91c:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f11b920:	02603025 */ 	or	$a2,$s3,$zero
/*  f11b924:	10400003 */ 	beqz	$v0,.L0f11b934
/*  f11b928:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11b92c:	100000a2 */ 	beqz	$zero,.L0f11bbb8
/*  f11b930:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11b934:
/*  f11b934:	16800007 */ 	bnez	$s4,.L0f11b954
/*  f11b938:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11b93c:	8e6b000c */ 	lw	$t3,0xc($s3)
/*  f11b940:	000b6f40 */ 	sll	$t5,$t3,0x1d
/*  f11b944:	05a00003 */ 	bltz	$t5,.L0f11b954
/*  f11b948:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11b94c:	10000099 */ 	beqz	$zero,.L0f11bbb4
/*  f11b950:	2402000a */ 	addiu	$v0,$zero,0xa
.L0f11b954:
/*  f11b954:	12000014 */ 	beqz	$s0,.L0f11b9a8
/*  f11b958:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11b95c:	00047603 */ 	sra	$t6,$a0,0x18
/*  f11b960:	01c02025 */ 	or	$a0,$t6,$zero
/*  f11b964:	8fa50134 */ 	lw	$a1,0x134($sp)
/*  f11b968:	0fc45d0c */ 	jal	pak0f117430
/*  f11b96c:	27a600e0 */ 	addiu	$a2,$sp,0xe0
/*  f11b970:	5040000e */ 	beqzl	$v0,.L0f11b9ac
/*  f11b974:	8e650008 */ 	lw	$a1,0x8($s3)
/*  f11b978:	12400009 */ 	beqz	$s2,.L0f11b9a0
/*  f11b97c:	00008025 */ 	or	$s0,$zero,$zero
/*  f11b980:	02a01025 */ 	or	$v0,$s5,$zero
/*  f11b984:	27a300e0 */ 	addiu	$v1,$sp,0xe0
.L0f11b988:
/*  f11b988:	906f0010 */ 	lbu	$t7,0x10($v1)
/*  f11b98c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11b990:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11b994:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11b998:	1612fffb */ 	bne	$s0,$s2,.L0f11b988
/*  f11b99c:	a04fffff */ 	sb	$t7,-0x1($v0)
.L0f11b9a0:
/*  f11b9a0:	10000084 */ 	beqz	$zero,.L0f11bbb4
/*  f11b9a4:	00001025 */ 	or	$v0,$zero,$zero
.L0f11b9a8:
/*  f11b9a8:	8e650008 */ 	lw	$a1,0x8($s3)
.L0f11b9ac:
/*  f11b9ac:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11b9b0:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11b9b4:	0005c240 */ 	sll	$t8,$a1,0x9
/*  f11b9b8:	00182d42 */ 	srl	$a1,$t8,0x15
/*  f11b9bc:	16450002 */ 	bne	$s2,$a1,.L0f11b9c8
/*  f11b9c0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11b9c4:	00009025 */ 	or	$s2,$zero,$zero
.L0f11b9c8:
/*  f11b9c8:	0fc45996 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f11b9cc:	01202025 */ 	or	$a0,$t1,$zero
/*  f11b9d0:	16400005 */ 	bnez	$s2,.L0f11b9e8
/*  f11b9d4:	8fac0114 */ 	lw	$t4,0x114($sp)
/*  f11b9d8:	8e700008 */ 	lw	$s0,0x8($s3)
/*  f11b9dc:	00105240 */ 	sll	$t2,$s0,0x9
/*  f11b9e0:	10000002 */ 	beqz	$zero,.L0f11b9ec
/*  f11b9e4:	000a8542 */ 	srl	$s0,$t2,0x15
.L0f11b9e8:
/*  f11b9e8:	02408025 */ 	or	$s0,$s2,$zero
.L0f11b9ec:
/*  f11b9ec:	11800002 */ 	beqz	$t4,.L0f11b9f8
/*  f11b9f0:	26030010 */ 	addiu	$v1,$s0,0x10
/*  f11b9f4:	00401825 */ 	or	$v1,$v0,$zero
.L0f11b9f8:
/*  f11b9f8:	0c00543a */ 	jal	joyGetLock
/*  f11b9fc:	afa30104 */ 	sw	$v1,0x104($sp)
/*  f11ba00:	8fad0104 */ 	lw	$t5,0x104($sp)
/*  f11ba04:	00008025 */ 	or	$s0,$zero,$zero
/*  f11ba08:	27b40058 */ 	addiu	$s4,$sp,0x58
/*  f11ba0c:	11a00066 */ 	beqz	$t5,.L0f11bba8
/*  f11ba10:	00112600 */ 	sll	$a0,$s1,0x18
.L0f11ba14:
/*  f11ba14:	00047603 */ 	sra	$t6,$a0,0x18
/*  f11ba18:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11ba1c:	01c02025 */ 	or	$a0,$t6,$zero
/*  f11ba20:	0202001b */ 	divu	$zero,$s0,$v0
/*  f11ba24:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ba28:	00009010 */ 	mfhi	$s2
/*  f11ba2c:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11ba30:	14400002 */ 	bnez	$v0,.L0f11ba3c
/*  f11ba34:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ba38:	0007000d */ 	break	0x7
.L0f11ba3c:
/*  f11ba3c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11ba40:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11ba44:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ba48:	0202001b */ 	divu	$zero,$s0,$v0
/*  f11ba4c:	00001812 */ 	mflo	$v1
/*  f11ba50:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11ba54:	14400002 */ 	bnez	$v0,.L0f11ba60
/*  f11ba58:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ba5c:	0007000d */ 	break	0x7
.L0f11ba60:
/*  f11ba60:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11ba64:	16400044 */ 	bnez	$s2,.L0f11bb78
/*  f11ba68:	0011c880 */ 	sll	$t9,$s1,0x2
/*  f11ba6c:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11ba70:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11ba74:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11ba78:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11ba7c:	0331c821 */ 	addu	$t9,$t9,$s1
/*  f11ba80:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11ba84:	0331c823 */ 	subu	$t9,$t9,$s1
/*  f11ba88:	3c09800a */ 	lui	$t1,%hi(g_Paks)
/*  f11ba8c:	25292380 */ 	addiu	$t1,$t1,%lo(g_Paks)
/*  f11ba90:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11ba94:	03299821 */ 	addu	$s3,$t9,$t1
/*  f11ba98:	03002025 */ 	or	$a0,$t8,$zero
/*  f11ba9c:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11baa0:	afa300d8 */ 	sw	$v1,0xd8($sp)
/*  f11baa4:	8fa300d8 */ 	lw	$v1,0xd8($sp)
/*  f11baa8:	00116080 */ 	sll	$t4,$s1,0x2
/*  f11baac:	01916023 */ 	subu	$t4,$t4,$s1
/*  f11bab0:	00430019 */ 	multu	$v0,$v1
/*  f11bab4:	8fab0134 */ 	lw	$t3,0x134($sp)
/*  f11bab8:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f11babc:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11bac0:	00047603 */ 	sra	$t6,$a0,0x18
/*  f11bac4:	01916021 */ 	addu	$t4,$t4,$s1
/*  f11bac8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11bacc:	3c0d800a */ 	lui	$t5,%hi(g_Pfses)
/*  f11bad0:	25ad3180 */ 	addiu	$t5,$t5,%lo(g_Pfses)
/*  f11bad4:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f11bad8:	00005012 */ 	mflo	$t2
/*  f11badc:	01c02025 */ 	or	$a0,$t6,$zero
/*  f11bae0:	16210003 */ 	bne	$s1,$at,.L0f11baf0
/*  f11bae4:	014b4021 */ 	addu	$t0,$t2,$t3
/*  f11bae8:	10000002 */ 	beqz	$zero,.L0f11baf4
/*  f11baec:	00002825 */ 	or	$a1,$zero,$zero
.L0f11baf0:
/*  f11baf0:	018d2821 */ 	addu	$a1,$t4,$t5
.L0f11baf4:
/*  f11baf4:	afa50048 */ 	sw	$a1,0x48($sp)
/*  f11baf8:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11bafc:	afa80054 */ 	sw	$t0,0x54($sp)
/*  f11bb00:	8fa80054 */ 	lw	$t0,0x54($sp)
/*  f11bb04:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11bb08:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11bb0c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11bb10:	8fa50048 */ 	lw	$a1,0x48($sp)
/*  f11bb14:	8e66029c */ 	lw	$a2,0x29c($s3)
/*  f11bb18:	00003825 */ 	or	$a3,$zero,$zero
/*  f11bb1c:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f11bb20:	afb40018 */ 	sw	$s4,0x18($sp)
/*  f11bb24:	0fc46941 */ 	jal	pakReadWriteBlock
/*  f11bb28:	afa80010 */ 	sw	$t0,0x10($sp)
/*  f11bb2c:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f11bb30:	0005c603 */ 	sra	$t8,$a1,0x18
/*  f11bb34:	00409825 */ 	or	$s3,$v0,$zero
/*  f11bb38:	03002825 */ 	or	$a1,$t8,$zero
/*  f11bb3c:	00402025 */ 	or	$a0,$v0,$zero
/*  f11bb40:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11bb44:	0fc470e7 */ 	jal	pakHandleResult
/*  f11bb48:	2407112a */ 	addiu	$a3,$zero,_val7f11bb48
/*  f11bb4c:	5440000b */ 	bnezl	$v0,.L0f11bb7c
/*  f11bb50:	2e010010 */ 	sltiu	$at,$s0,0x10
/*  f11bb54:	0c005451 */ 	jal	joyReleaseLock
/*  f11bb58:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11bb5c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11bb60:	16610003 */ 	bne	$s3,$at,.L0f11bb70
/*  f11bb64:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11bb68:	10000012 */ 	beqz	$zero,.L0f11bbb4
/*  f11bb6c:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11bb70:
/*  f11bb70:	10000010 */ 	beqz	$zero,.L0f11bbb4
/*  f11bb74:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f11bb78:
/*  f11bb78:	2e010010 */ 	sltiu	$at,$s0,0x10
.L0f11bb7c:
/*  f11bb7c:	54200007 */ 	bnezl	$at,.L0f11bb9c
/*  f11bb80:	8faa0104 */ 	lw	$t2,0x104($sp)
/*  f11bb84:	12a00004 */ 	beqz	$s5,.L0f11bb98
/*  f11bb88:	0292c821 */ 	addu	$t9,$s4,$s2
/*  f11bb8c:	93290000 */ 	lbu	$t1,0x0($t9)
/*  f11bb90:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f11bb94:	a2a9ffff */ 	sb	$t1,-0x1($s5)
.L0f11bb98:
/*  f11bb98:	8faa0104 */ 	lw	$t2,0x104($sp)
.L0f11bb9c:
/*  f11bb9c:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f11bba0:	560aff9c */ 	bnel	$s0,$t2,.L0f11ba14
/*  f11bba4:	00112600 */ 	sll	$a0,$s1,0x18
.L0f11bba8:
/*  f11bba8:	0c005451 */ 	jal	joyReleaseLock
/*  f11bbac:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11bbb0:	00001025 */ 	or	$v0,$zero,$zero
.L0f11bbb4:
/*  f11bbb4:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11bbb8:
/*  f11bbb8:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f11bbbc:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f11bbc0:	8fb2002c */ 	lw	$s2,0x2c($sp)
/*  f11bbc4:	8fb30030 */ 	lw	$s3,0x30($sp)
/*  f11bbc8:	8fb40034 */ 	lw	$s4,0x34($sp)
/*  f11bbcc:	8fb50038 */ 	lw	$s5,0x38($sp)
/*  f11bbd0:	03e00008 */ 	jr	$ra
/*  f11bbd4:	27bd0130 */ 	addiu	$sp,$sp,0x130
);
#else
GLOBAL_ASM(
glabel pak0f11b86c
/*  f1157f8:	27bdfed0 */ 	addiu	$sp,$sp,-304
/*  f1157fc:	afb2002c */ 	sw	$s2,0x2c($sp)
/*  f115800:	8fb20140 */ 	lw	$s2,0x140($sp)
/*  f115804:	afb10028 */ 	sw	$s1,0x28($sp)
/*  f115808:	00048e00 */ 	sll	$s1,$a0,0x18
/*  f11580c:	00117603 */ 	sra	$t6,$s1,0x18
/*  f115810:	afb50038 */ 	sw	$s5,0x38($sp)
/*  f115814:	afb30030 */ 	sw	$s3,0x30($sp)
/*  f115818:	2401ffff */ 	addiu	$at,$zero,-1
/*  f11581c:	00e09825 */ 	or	$s3,$a3,$zero
/*  f115820:	00c0a825 */ 	or	$s5,$a2,$zero
/*  f115824:	01c08825 */ 	or	$s1,$t6,$zero
/*  f115828:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f11582c:	afb40034 */ 	sw	$s4,0x34($sp)
/*  f115830:	afb00024 */ 	sw	$s0,0x24($sp)
/*  f115834:	afa40130 */ 	sw	$a0,0x130($sp)
/*  f115838:	16410004 */ 	bne	$s2,$at,.NB0f11584c
/*  f11583c:	afa50134 */ 	sw	$a1,0x134($sp)
/*  f115840:	24140001 */ 	addiu	$s4,$zero,0x1
/*  f115844:	10000002 */ 	beqz	$zero,.NB0f115850
/*  f115848:	00009025 */ 	or	$s2,$zero,$zero
.NB0f11584c:
/*  f11584c:	0000a025 */ 	or	$s4,$zero,$zero
.NB0f115850:
/*  f115850:	00112600 */ 	sll	$a0,$s1,0x18
/*  f115854:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f115858:	0fc4428c */ 	jal	pakGetBlockSize
/*  f11585c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f115860:	0012802b */ 	sltu	$s0,$zero,$s2
/*  f115864:	12000008 */ 	beqz	$s0,.NB0f115888
/*  f115868:	00112600 */ 	sll	$a0,$s1,0x18
/*  f11586c:	0015802b */ 	sltu	$s0,$zero,$s5
/*  f115870:	12000005 */ 	beqz	$s0,.NB0f115888
/*  f115874:	26580010 */ 	addiu	$t8,$s2,0x10
/*  f115878:	0058802b */ 	sltu	$s0,$v0,$t8
/*  f11587c:	3a100001 */ 	xori	$s0,$s0,0x1
/*  f115880:	0010c82b */ 	sltu	$t9,$zero,$s0
/*  f115884:	03208025 */ 	or	$s0,$t9,$zero
.NB0f115888:
/*  f115888:	16600002 */ 	bnez	$s3,.NB0f115894
/*  f11588c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f115890:	27b30120 */ 	addiu	$s3,$sp,0x120
.NB0f115894:
/*  f115894:	01202025 */ 	or	$a0,$t1,$zero
/*  f115898:	8fa50134 */ 	lw	$a1,0x134($sp)
/*  f11589c:	0fc4461f */ 	jal	pakReadHeaderAtOffset
/*  f1158a0:	02603025 */ 	or	$a2,$s3,$zero
/*  f1158a4:	50400004 */ 	beqzl	$v0,.NB0f1158b8
/*  f1158a8:	8e6a000c */ 	lw	$t2,0xc($s3)
/*  f1158ac:	100000a3 */ 	beqz	$zero,.NB0f115b3c
/*  f1158b0:	8fbf003c */ 	lw	$ra,0x3c($sp)
/*  f1158b4:	8e6a000c */ 	lw	$t2,0xc($s3)
.NB0f1158b8:
/*  f1158b8:	000a6740 */ 	sll	$t4,$t2,0x1d
/*  f1158bc:	05800003 */ 	bltz	$t4,.NB0f1158cc
/*  f1158c0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1158c4:	1000009c */ 	beqz	$zero,.NB0f115b38
/*  f1158c8:	2402000a */ 	addiu	$v0,$zero,0xa
.NB0f1158cc:
/*  f1158cc:	12000014 */ 	beqz	$s0,.NB0f115920
/*  f1158d0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1158d4:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f1158d8:	01a02025 */ 	or	$a0,$t5,$zero
/*  f1158dc:	8fa50134 */ 	lw	$a1,0x134($sp)
/*  f1158e0:	0fc445e5 */ 	jal	pak0f117430
/*  f1158e4:	27a600e4 */ 	addiu	$a2,$sp,0xe4
/*  f1158e8:	5040000e */ 	beqzl	$v0,.NB0f115924
/*  f1158ec:	8e650008 */ 	lw	$a1,0x8($s3)
/*  f1158f0:	12400009 */ 	beqz	$s2,.NB0f115918
/*  f1158f4:	00008025 */ 	or	$s0,$zero,$zero
/*  f1158f8:	02a01025 */ 	or	$v0,$s5,$zero
/*  f1158fc:	27a300e4 */ 	addiu	$v1,$sp,0xe4
.NB0f115900:
/*  f115900:	906e0010 */ 	lbu	$t6,0x10($v1)
/*  f115904:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f115908:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11590c:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f115910:	1612fffb */ 	bne	$s0,$s2,.NB0f115900
/*  f115914:	a04effff */ 	sb	$t6,-0x1($v0)
.NB0f115918:
/*  f115918:	10000087 */ 	beqz	$zero,.NB0f115b38
/*  f11591c:	00001025 */ 	or	$v0,$zero,$zero
.NB0f115920:
/*  f115920:	8e650008 */ 	lw	$a1,0x8($s3)
.NB0f115924:
/*  f115924:	00112600 */ 	sll	$a0,$s1,0x18
/*  f115928:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11592c:	00057a40 */ 	sll	$t7,$a1,0x9
/*  f115930:	000f2d42 */ 	srl	$a1,$t7,0x15
/*  f115934:	16450002 */ 	bne	$s2,$a1,.NB0f115940
/*  f115938:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11593c:	00009025 */ 	or	$s2,$zero,$zero
.NB0f115940:
/*  f115940:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f115944:	03202025 */ 	or	$a0,$t9,$zero
/*  f115948:	16400005 */ 	bnez	$s2,.NB0f115960
/*  f11594c:	24040fa8 */ 	addiu	$a0,$zero,0xfa8
/*  f115950:	8e700008 */ 	lw	$s0,0x8($s3)
/*  f115954:	00104a40 */ 	sll	$t1,$s0,0x9
/*  f115958:	10000002 */ 	beqz	$zero,.NB0f115964
/*  f11595c:	00098542 */ 	srl	$s0,$t1,0x15
.NB0f115960:
/*  f115960:	02408025 */ 	or	$s0,$s2,$zero
.NB0f115964:
/*  f115964:	12800002 */ 	beqz	$s4,.NB0f115970
/*  f115968:	26030010 */ 	addiu	$v1,$s0,0x10
/*  f11596c:	00401825 */ 	or	$v1,$v0,$zero
.NB0f115970:
/*  f115970:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f115974:	24a5e544 */ 	addiu	$a1,$a1,-6844
/*  f115978:	0c00581b */ 	jal	joyGetLock
/*  f11597c:	afa30108 */ 	sw	$v1,0x108($sp)
/*  f115980:	8fab0108 */ 	lw	$t3,0x108($sp)
/*  f115984:	00008025 */ 	or	$s0,$zero,$zero
/*  f115988:	27b4005c */ 	addiu	$s4,$sp,0x5c
/*  f11598c:	11600065 */ 	beqz	$t3,.NB0f115b24
/*  f115990:	00112600 */ 	sll	$a0,$s1,0x18
.NB0f115994:
/*  f115994:	00046603 */ 	sra	$t4,$a0,0x18
/*  f115998:	0fc4428c */ 	jal	pakGetBlockSize
/*  f11599c:	01802025 */ 	or	$a0,$t4,$zero
/*  f1159a0:	0202001b */ 	divu	$zero,$s0,$v0
/*  f1159a4:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1159a8:	00009010 */ 	mfhi	$s2
/*  f1159ac:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f1159b0:	14400002 */ 	bnez	$v0,.NB0f1159bc
/*  f1159b4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1159b8:	0007000d */ 	break	0x7
.NB0f1159bc:
/*  f1159bc:	01a02025 */ 	or	$a0,$t5,$zero
/*  f1159c0:	0fc4428c */ 	jal	pakGetBlockSize
/*  f1159c4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1159c8:	0202001b */ 	divu	$zero,$s0,$v0
/*  f1159cc:	00001812 */ 	mflo	$v1
/*  f1159d0:	00112600 */ 	sll	$a0,$s1,0x18
/*  f1159d4:	14400002 */ 	bnez	$v0,.NB0f1159e0
/*  f1159d8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1159dc:	0007000d */ 	break	0x7
.NB0f1159e0:
/*  f1159e0:	00047603 */ 	sra	$t6,$a0,0x18
/*  f1159e4:	16400043 */ 	bnez	$s2,.NB0f115af4
/*  f1159e8:	00117880 */ 	sll	$t7,$s1,0x2
/*  f1159ec:	01f17823 */ 	subu	$t7,$t7,$s1
/*  f1159f0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f1159f4:	01f17823 */ 	subu	$t7,$t7,$s1
/*  f1159f8:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1159fc:	01f17821 */ 	addu	$t7,$t7,$s1
/*  f115a00:	3c18800a */ 	lui	$t8,0x800a
/*  f115a04:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f115a08:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f115a0c:	01f89821 */ 	addu	$s3,$t7,$t8
/*  f115a10:	01c02025 */ 	or	$a0,$t6,$zero
/*  f115a14:	0fc4428c */ 	jal	pakGetBlockSize
/*  f115a18:	afa300dc */ 	sw	$v1,0xdc($sp)
/*  f115a1c:	8fa300dc */ 	lw	$v1,0xdc($sp)
/*  f115a20:	00115080 */ 	sll	$t2,$s1,0x2
/*  f115a24:	01515023 */ 	subu	$t2,$t2,$s1
/*  f115a28:	00430019 */ 	multu	$v0,$v1
/*  f115a2c:	8fa90134 */ 	lw	$t1,0x134($sp)
/*  f115a30:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f115a34:	00112600 */ 	sll	$a0,$s1,0x18
/*  f115a38:	00046603 */ 	sra	$t4,$a0,0x18
/*  f115a3c:	01515021 */ 	addu	$t2,$t2,$s1
/*  f115a40:	24010004 */ 	addiu	$at,$zero,0x4
/*  f115a44:	3c0b800a */ 	lui	$t3,0x800a
/*  f115a48:	256b7658 */ 	addiu	$t3,$t3,0x7658
/*  f115a4c:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f115a50:	0000c812 */ 	mflo	$t9
/*  f115a54:	01802025 */ 	or	$a0,$t4,$zero
/*  f115a58:	16210003 */ 	bne	$s1,$at,.NB0f115a68
/*  f115a5c:	03294021 */ 	addu	$t0,$t9,$t1
/*  f115a60:	10000002 */ 	beqz	$zero,.NB0f115a6c
/*  f115a64:	00002825 */ 	or	$a1,$zero,$zero
.NB0f115a68:
/*  f115a68:	014b2821 */ 	addu	$a1,$t2,$t3
.NB0f115a6c:
/*  f115a6c:	afa5004c */ 	sw	$a1,0x4c($sp)
/*  f115a70:	0fc4428c */ 	jal	pakGetBlockSize
/*  f115a74:	afa80058 */ 	sw	$t0,0x58($sp)
/*  f115a78:	8fa80058 */ 	lw	$t0,0x58($sp)
/*  f115a7c:	00112600 */ 	sll	$a0,$s1,0x18
/*  f115a80:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f115a84:	01a02025 */ 	or	$a0,$t5,$zero
/*  f115a88:	8fa5004c */ 	lw	$a1,0x4c($sp)
/*  f115a8c:	8e66029c */ 	lw	$a2,0x29c($s3)
/*  f115a90:	00003825 */ 	or	$a3,$zero,$zero
/*  f115a94:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f115a98:	afb40018 */ 	sw	$s4,0x18($sp)
/*  f115a9c:	0fc450ea */ 	jal	pakReadWriteBlock
/*  f115aa0:	afa80010 */ 	sw	$t0,0x10($sp)
/*  f115aa4:	00112e00 */ 	sll	$a1,$s1,0x18
/*  f115aa8:	00057603 */ 	sra	$t6,$a1,0x18
/*  f115aac:	00409825 */ 	or	$s3,$v0,$zero
/*  f115ab0:	01c02825 */ 	or	$a1,$t6,$zero
/*  f115ab4:	00402025 */ 	or	$a0,$v0,$zero
/*  f115ab8:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f115abc:	0fc458cb */ 	jal	pakHandleResult
/*  f115ac0:	24070fbd */ 	addiu	$a3,$zero,0xfbd
/*  f115ac4:	1440000b */ 	bnez	$v0,.NB0f115af4
/*  f115ac8:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f115acc:	24a5e54c */ 	addiu	$a1,$a1,-6836
/*  f115ad0:	0c005834 */ 	jal	joyReleaseLock
/*  f115ad4:	24040fc0 */ 	addiu	$a0,$zero,0xfc0
/*  f115ad8:	24010001 */ 	addiu	$at,$zero,0x1
/*  f115adc:	16610003 */ 	bne	$s3,$at,.NB0f115aec
/*  f115ae0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f115ae4:	10000014 */ 	beqz	$zero,.NB0f115b38
/*  f115ae8:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f115aec:
/*  f115aec:	10000012 */ 	beqz	$zero,.NB0f115b38
/*  f115af0:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f115af4:
/*  f115af4:	2e010010 */ 	sltiu	$at,$s0,0x10
/*  f115af8:	54200007 */ 	bnezl	$at,.NB0f115b18
/*  f115afc:	8fb90108 */ 	lw	$t9,0x108($sp)
/*  f115b00:	12a00004 */ 	beqz	$s5,.NB0f115b14
/*  f115b04:	02927821 */ 	addu	$t7,$s4,$s2
/*  f115b08:	91f80000 */ 	lbu	$t8,0x0($t7)
/*  f115b0c:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f115b10:	a2b8ffff */ 	sb	$t8,-0x1($s5)
.NB0f115b14:
/*  f115b14:	8fb90108 */ 	lw	$t9,0x108($sp)
.NB0f115b18:
/*  f115b18:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f115b1c:	5619ff9d */ 	bnel	$s0,$t9,.NB0f115994
/*  f115b20:	00112600 */ 	sll	$a0,$s1,0x18
.NB0f115b24:
/*  f115b24:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f115b28:	24a5e554 */ 	addiu	$a1,$a1,-6828
/*  f115b2c:	0c005834 */ 	jal	joyReleaseLock
/*  f115b30:	24040fd6 */ 	addiu	$a0,$zero,0xfd6
/*  f115b34:	00001025 */ 	or	$v0,$zero,$zero
.NB0f115b38:
/*  f115b38:	8fbf003c */ 	lw	$ra,0x3c($sp)
.NB0f115b3c:
/*  f115b3c:	8fb00024 */ 	lw	$s0,0x24($sp)
/*  f115b40:	8fb10028 */ 	lw	$s1,0x28($sp)
/*  f115b44:	8fb2002c */ 	lw	$s2,0x2c($sp)
/*  f115b48:	8fb30030 */ 	lw	$s3,0x30($sp)
/*  f115b4c:	8fb40034 */ 	lw	$s4,0x34($sp)
/*  f115b50:	8fb50038 */ 	lw	$s5,0x38($sp)
/*  f115b54:	03e00008 */ 	jr	$ra
/*  f115b58:	27bd0130 */ 	addiu	$sp,$sp,0x130
);
#endif

GLOBAL_ASM(
glabel pak0f11bbd8
/*  f11bbd8:	27bdffb8 */ 	addiu	$sp,$sp,-72
/*  f11bbdc:	afbf002c */ 	sw	$ra,0x2c($sp)
/*  f11bbe0:	afa40048 */ 	sw	$a0,0x48($sp)
/*  f11bbe4:	afa5004c */ 	sw	$a1,0x4c($sp)
/*  f11bbe8:	83a4004b */ 	lb	$a0,0x4b($sp)
/*  f11bbec:	0fc45d48 */ 	jal	pakReadHeaderAtOffset
/*  f11bbf0:	27a60038 */ 	addiu	$a2,$sp,0x38
/*  f11bbf4:	14400012 */ 	bnez	$v0,.L0f11bc40
/*  f11bbf8:	83a4004b */ 	lb	$a0,0x4b($sp)
/*  f11bbfc:	8fae0040 */ 	lw	$t6,0x40($sp)
/*  f11bc00:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f11bc04:	afb90020 */ 	sw	$t9,0x20($sp)
/*  f11bc08:	31cf0fff */ 	andi	$t7,$t6,0xfff
/*  f11bc0c:	25f8fff0 */ 	addiu	$t8,$t7,-16
/*  f11bc10:	afb80010 */ 	sw	$t8,0x10($sp)
/*  f11bc14:	8fa5004c */ 	lw	$a1,0x4c($sp)
/*  f11bc18:	24060002 */ 	addiu	$a2,$zero,0x2
/*  f11bc1c:	00003825 */ 	or	$a3,$zero,$zero
/*  f11bc20:	afa00014 */ 	sw	$zero,0x14($sp)
/*  f11bc24:	afa00018 */ 	sw	$zero,0x18($sp)
/*  f11bc28:	0fc46f15 */ 	jal	pakWriteFile
/*  f11bc2c:	afa0001c */ 	sw	$zero,0x1c($sp)
/*  f11bc30:	54400004 */ 	bnezl	$v0,.L0f11bc44
/*  f11bc34:	00001025 */ 	or	$v0,$zero,$zero
/*  f11bc38:	10000002 */ 	beqz	$zero,.L0f11bc44
/*  f11bc3c:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11bc40:
/*  f11bc40:	00001025 */ 	or	$v0,$zero,$zero
.L0f11bc44:
/*  f11bc44:	8fbf002c */ 	lw	$ra,0x2c($sp)
/*  f11bc48:	27bd0048 */ 	addiu	$sp,$sp,0x48
/*  f11bc4c:	03e00008 */ 	jr	$ra
/*  f11bc50:	00000000 */ 	sll	$zero,$zero,0x0
);

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pakWriteFile
/*  f11bc54:	27bddf40 */ 	addiu	$sp,$sp,-8384
/*  f11bc58:	afb70044 */ 	sw	$s7,0x44($sp)
/*  f11bc5c:	0004be00 */ 	sll	$s7,$a0,0x18
/*  f11bc60:	00177603 */ 	sra	$t6,$s7,0x18
/*  f11bc64:	afa420c0 */ 	sw	$a0,0x20c0($sp)
/*  f11bc68:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f11bc6c:	afbf004c */ 	sw	$ra,0x4c($sp)
/*  f11bc70:	afb40038 */ 	sw	$s4,0x38($sp)
/*  f11bc74:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f11bc78:	00c0a025 */ 	or	$s4,$a2,$zero
/*  f11bc7c:	01c0b825 */ 	or	$s7,$t6,$zero
/*  f11bc80:	afbe0048 */ 	sw	$s8,0x48($sp)
/*  f11bc84:	afb60040 */ 	sw	$s6,0x40($sp)
/*  f11bc88:	afb5003c */ 	sw	$s5,0x3c($sp)
/*  f11bc8c:	afb30034 */ 	sw	$s3,0x34($sp)
/*  f11bc90:	afb20030 */ 	sw	$s2,0x30($sp)
/*  f11bc94:	afb1002c */ 	sw	$s1,0x2c($sp)
/*  f11bc98:	afb00028 */ 	sw	$s0,0x28($sp)
/*  f11bc9c:	afa520c4 */ 	sw	$a1,0x20c4($sp)
/*  f11bca0:	afa720cc */ 	sw	$a3,0x20cc($sp)
/*  f11bca4:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11bca8:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11bcac:	8fb220e0 */ 	lw	$s2,0x20e0($sp)
/*  f11bcb0:	8fa320d0 */ 	lw	$v1,0x20d0($sp)
/*  f11bcb4:	00409825 */ 	or	$s3,$v0,$zero
/*  f11bcb8:	325801ff */ 	andi	$t8,$s2,0x1ff
/*  f11bcbc:	10600003 */ 	beqz	$v1,.L0f11bccc
/*  f11bcc0:	03009025 */ 	or	$s2,$t8,$zero
/*  f11bcc4:	10000007 */ 	beqz	$zero,.L0f11bce4
/*  f11bcc8:	00608825 */ 	or	$s1,$v1,$zero
.L0f11bccc:
/*  f11bccc:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11bcd0:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11bcd4:	03202025 */ 	or	$a0,$t9,$zero
/*  f11bcd8:	0fc45c25 */ 	jal	pakGetBodyLenByType
/*  f11bcdc:	02802825 */ 	or	$a1,$s4,$zero
/*  f11bce0:	00408825 */ 	or	$s1,$v0,$zero
.L0f11bce4:
/*  f11bce4:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11bce8:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f11bcec:	01202025 */ 	or	$a0,$t1,$zero
/*  f11bcf0:	0fc45996 */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f11bcf4:	02202825 */ 	or	$a1,$s1,$zero
/*  f11bcf8:	8fa320dc */ 	lw	$v1,0x20dc($sp)
/*  f11bcfc:	0040b025 */ 	or	$s6,$v0,$zero
/*  f11bd00:	27b020b0 */ 	addiu	$s0,$sp,0x20b0
/*  f11bd04:	10600015 */ 	beqz	$v1,.L0f11bd5c
/*  f11bd08:	00177880 */ 	sll	$t7,$s7,0x2
/*  f11bd0c:	8e02000c */ 	lw	$v0,0xc($s0)
/*  f11bd10:	01f77823 */ 	subu	$t7,$t7,$s7
/*  f11bd14:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11bd18:	01f77823 */ 	subu	$t7,$t7,$s7
/*  f11bd1c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11bd20:	00025302 */ 	srl	$t2,$v0,0xc
/*  f11bd24:	006a5826 */ 	xor	$t3,$v1,$t2
/*  f11bd28:	01f77821 */ 	addu	$t7,$t7,$s7
/*  f11bd2c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11bd30:	000b6640 */ 	sll	$t4,$t3,0x19
/*  f11bd34:	000c6b42 */ 	srl	$t5,$t4,0xd
/*  f11bd38:	01f77823 */ 	subu	$t7,$t7,$s7
/*  f11bd3c:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11bd40:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11bd44:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11bd48:	01a27026 */ 	xor	$t6,$t5,$v0
/*  f11bd4c:	ae0e000c */ 	sw	$t6,0xc($s0)
/*  f11bd50:	01f8c821 */ 	addu	$t9,$t7,$t8
/*  f11bd54:	10000019 */ 	beqz	$zero,.L0f11bdbc
/*  f11bd58:	afb90058 */ 	sw	$t9,0x58($sp)
.L0f11bd5c:
/*  f11bd5c:	00174880 */ 	sll	$t1,$s7,0x2
/*  f11bd60:	01374823 */ 	subu	$t1,$t1,$s7
/*  f11bd64:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11bd68:	01374823 */ 	subu	$t1,$t1,$s7
/*  f11bd6c:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11bd70:	01374821 */ 	addu	$t1,$t1,$s7
/*  f11bd74:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11bd78:	01374823 */ 	subu	$t1,$t1,$s7
/*  f11bd7c:	3c0a800a */ 	lui	$t2,%hi(g_Paks)
/*  f11bd80:	254a2380 */ 	addiu	$t2,$t2,%lo(g_Paks)
/*  f11bd84:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11bd88:	012a1021 */ 	addu	$v0,$t1,$t2
/*  f11bd8c:	8c4b025c */ 	lw	$t3,0x25c($v0)
/*  f11bd90:	8fa320bc */ 	lw	$v1,0x20bc($sp)
/*  f11bd94:	afa20058 */ 	sw	$v0,0x58($sp)
/*  f11bd98:	256c0001 */ 	addiu	$t4,$t3,0x1
/*  f11bd9c:	00037302 */ 	srl	$t6,$v1,0xc
/*  f11bda0:	018e7826 */ 	xor	$t7,$t4,$t6
/*  f11bda4:	000fc640 */ 	sll	$t8,$t7,0x19
/*  f11bda8:	0018cb42 */ 	srl	$t9,$t8,0xd
/*  f11bdac:	03231826 */ 	xor	$v1,$t9,$v1
/*  f11bdb0:	afa320bc */ 	sw	$v1,0x20bc($sp)
/*  f11bdb4:	ac4c025c */ 	sw	$t4,0x25c($v0)
/*  f11bdb8:	27b020b0 */ 	addiu	$s0,$sp,0x20b0
.L0f11bdbc:
/*  f11bdbc:	8fa90058 */ 	lw	$t1,0x58($sp)
/*  f11bdc0:	960e000c */ 	lhu	$t6,0xc($s0)
/*  f11bdc4:	960a000a */ 	lhu	$t2,0xa($s0)
/*  f11bdc8:	8d2b0260 */ 	lw	$t3,0x260($t1)
/*  f11bdcc:	32c90fff */ 	andi	$t1,$s6,0xfff
/*  f11bdd0:	31cf0007 */ 	andi	$t7,$t6,0x7
/*  f11bdd4:	000b68c0 */ 	sll	$t5,$t3,0x3
/*  f11bdd8:	314bf000 */ 	andi	$t3,$t2,0xf000
/*  f11bddc:	01afc025 */ 	or	$t8,$t5,$t7
/*  f11bde0:	012b6025 */ 	or	$t4,$t1,$t3
/*  f11bde4:	3c057f1b */ 	lui	$a1,%hi(var7f1b45e4)
/*  f11bde8:	a618000c */ 	sh	$t8,0xc($s0)
/*  f11bdec:	a60c000a */ 	sh	$t4,0xa($s0)
/*  f11bdf0:	24a545e4 */ 	addiu	$a1,$a1,%lo(var7f1b45e4)
/*  f11bdf4:	0c004c04 */ 	jal	argFindByPrefix
/*  f11bdf8:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f11bdfc:	10400003 */ 	beqz	$v0,.L0f11be0c
/*  f11be00:	8fa420d4 */ 	lw	$a0,0x20d4($sp)
/*  f11be04:	10000002 */ 	beqz	$zero,.L0f11be10
/*  f11be08:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f11be0c:
/*  f11be0c:	00001825 */ 	or	$v1,$zero,$zero
.L0f11be10:
/*  f11be10:	920f000f */ 	lbu	$t7,0xf($s0)
/*  f11be14:	00607025 */ 	or	$t6,$v1,$zero
/*  f11be18:	31cd0001 */ 	andi	$t5,$t6,0x1
/*  f11be1c:	31f8fffe */ 	andi	$t8,$t7,0xfffe
/*  f11be20:	01b8c825 */ 	or	$t9,$t5,$t8
/*  f11be24:	a219000f */ 	sb	$t9,0xf($s0)
/*  f11be28:	8fa920b8 */ 	lw	$t1,0x20b8($sp)
/*  f11be2c:	0012c8c0 */ 	sll	$t9,$s2,0x3
/*  f11be30:	332a0ff8 */ 	andi	$t2,$t9,0xff8
/*  f11be34:	00095b02 */ 	srl	$t3,$t1,0xc
/*  f11be38:	022b6026 */ 	xor	$t4,$s1,$t3
/*  f11be3c:	000c7540 */ 	sll	$t6,$t4,0x15
/*  f11be40:	000e7a42 */ 	srl	$t7,$t6,0x9
/*  f11be44:	01e96826 */ 	xor	$t5,$t7,$t1
/*  f11be48:	afad20b8 */ 	sw	$t5,0x20b8($sp)
/*  f11be4c:	960b000e */ 	lhu	$t3,0xe($s0)
/*  f11be50:	96180008 */ 	lhu	$t8,0x8($s0)
/*  f11be54:	02807825 */ 	or	$t7,$s4,$zero
/*  f11be58:	316cf007 */ 	andi	$t4,$t3,0xf007
/*  f11be5c:	014c7025 */ 	or	$t6,$t2,$t4
/*  f11be60:	a60e000e */ 	sh	$t6,0xe($s0)
/*  f11be64:	8e02000c */ 	lw	$v0,0xc($s0)
/*  f11be68:	000f69c0 */ 	sll	$t5,$t7,0x7
/*  f11be6c:	3319007f */ 	andi	$t9,$t8,0x7f
/*  f11be70:	00025340 */ 	sll	$t2,$v0,0xd
/*  f11be74:	000a6642 */ 	srl	$t4,$t2,0x19
/*  f11be78:	318e007f */ 	andi	$t6,$t4,0x7f
/*  f11be7c:	00027b02 */ 	srl	$t7,$v0,0xc
/*  f11be80:	01cf4826 */ 	xor	$t1,$t6,$t7
/*  f11be84:	01b95825 */ 	or	$t3,$t5,$t9
/*  f11be88:	0009c640 */ 	sll	$t8,$t1,0x19
/*  f11be8c:	00186b42 */ 	srl	$t5,$t8,0xd
/*  f11be90:	01a2c826 */ 	xor	$t9,$t5,$v0
/*  f11be94:	a60b0008 */ 	sh	$t3,0x8($s0)
/*  f11be98:	10800004 */ 	beqz	$a0,.L0f11beac
/*  f11be9c:	ae19000c */ 	sw	$t9,0xc($s0)
/*  f11bea0:	00195340 */ 	sll	$t2,$t9,0xd
/*  f11bea4:	000a6642 */ 	srl	$t4,$t2,0x19
/*  f11bea8:	ac8c0000 */ 	sw	$t4,0x0($a0)
.L0f11beac:
/*  f11beac:	8fa720cc */ 	lw	$a3,0x20cc($sp)
/*  f11beb0:	50e00007 */ 	beqzl	$a3,.L0f11bed0
/*  f11beb4:	9218000f */ 	lbu	$t8,0xf($s0)
/*  f11beb8:	920e000f */ 	lbu	$t6,0xf($s0)
/*  f11bebc:	31cffffb */ 	andi	$t7,$t6,0xfffb
/*  f11bec0:	35e90004 */ 	ori	$t1,$t7,0x4
/*  f11bec4:	10000004 */ 	beqz	$zero,.L0f11bed8
/*  f11bec8:	a209000f */ 	sb	$t1,0xf($s0)
/*  f11becc:	9218000f */ 	lbu	$t8,0xf($s0)
.L0f11bed0:
/*  f11bed0:	330dfffb */ 	andi	$t5,$t8,0xfffb
/*  f11bed4:	a20d000f */ 	sb	$t5,0xf($s0)
.L0f11bed8:
/*  f11bed8:	8e19000c */ 	lw	$t9,0xc($s0)
/*  f11bedc:	3409ffff */ 	dli	$t1,0xffff
/*  f11bee0:	3418ffff */ 	dli	$t8,0xffff
/*  f11bee4:	00195f40 */ 	sll	$t3,$t9,0x1d
/*  f11bee8:	000b57c2 */ 	srl	$t2,$t3,0x1f
/*  f11beec:	5140000b */ 	beqzl	$t2,.L0f11bf1c
/*  f11bef0:	a7a920b4 */ 	sh	$t1,0x20b4($sp)
/*  f11bef4:	8fac20b8 */ 	lw	$t4,0x20b8($sp)
/*  f11bef8:	00e02025 */ 	or	$a0,$a3,$zero
/*  f11befc:	27a620b4 */ 	addiu	$a2,$sp,0x20b4
/*  f11bf00:	000c7240 */ 	sll	$t6,$t4,0x9
/*  f11bf04:	000e7d42 */ 	srl	$t7,$t6,0x15
/*  f11bf08:	0fc462b9 */ 	jal	pakCalculateChecksum
/*  f11bf0c:	01e72821 */ 	addu	$a1,$t7,$a3
/*  f11bf10:	10000003 */ 	beqz	$zero,.L0f11bf20
/*  f11bf14:	8fa720cc */ 	lw	$a3,0x20cc($sp)
/*  f11bf18:	a7a920b4 */ 	sh	$t1,0x20b4($sp)
.L0f11bf1c:
/*  f11bf1c:	a7b820b6 */ 	sh	$t8,0x20b6($sp)
.L0f11bf20:
/*  f11bf20:	02d14023 */ 	subu	$t0,$s6,$s1
/*  f11bf24:	27b2109c */ 	addiu	$s2,$sp,0x109c
/*  f11bf28:	27b4009c */ 	addiu	$s4,$sp,0x9c
/*  f11bf2c:	2508fff0 */ 	addiu	$t0,$t0,-16
/*  f11bf30:	01003025 */ 	or	$a2,$t0,$zero
/*  f11bf34:	02401025 */ 	or	$v0,$s2,$zero
/*  f11bf38:	02801825 */ 	or	$v1,$s4,$zero
/*  f11bf3c:	0000a825 */ 	or	$s5,$zero,$zero
/*  f11bf40:	2404002b */ 	addiu	$a0,$zero,0x2b
.L0f11bf44:
/*  f11bf44:	02156821 */ 	addu	$t5,$s0,$s5
/*  f11bf48:	91b90000 */ 	lbu	$t9,0x0($t5)
/*  f11bf4c:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f11bf50:	2ea10010 */ 	sltiu	$at,$s5,0x10
/*  f11bf54:	a0590000 */ 	sb	$t9,0x0($v0)
/*  f11bf58:	a0640000 */ 	sb	$a0,0x0($v1)
/*  f11bf5c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11bf60:	1420fff8 */ 	bnez	$at,.L0f11bf44
/*  f11bf64:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11bf68:	12200014 */ 	beqz	$s1,.L0f11bfbc
/*  f11bf6c:	0000a825 */ 	or	$s5,$zero,$zero
/*  f11bf70:	8fa520d8 */ 	lw	$a1,0x20d8($sp)
.L0f11bf74:
/*  f11bf74:	50e00006 */ 	beqzl	$a3,.L0f11bf90
/*  f11bf78:	a0440000 */ 	sb	$a0,0x0($v0)
/*  f11bf7c:	00f55821 */ 	addu	$t3,$a3,$s5
/*  f11bf80:	916a0000 */ 	lbu	$t2,0x0($t3)
/*  f11bf84:	10000002 */ 	beqz	$zero,.L0f11bf90
/*  f11bf88:	a04a0000 */ 	sb	$t2,0x0($v0)
/*  f11bf8c:	a0440000 */ 	sb	$a0,0x0($v0)
.L0f11bf90:
/*  f11bf90:	10a00005 */ 	beqz	$a1,.L0f11bfa8
/*  f11bf94:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11bf98:	00b56021 */ 	addu	$t4,$a1,$s5
/*  f11bf9c:	918e0000 */ 	lbu	$t6,0x0($t4)
/*  f11bfa0:	10000002 */ 	beqz	$zero,.L0f11bfac
/*  f11bfa4:	a06e0000 */ 	sb	$t6,0x0($v1)
.L0f11bfa8:
/*  f11bfa8:	a0640000 */ 	sb	$a0,0x0($v1)
.L0f11bfac:
/*  f11bfac:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f11bfb0:	16b1fff0 */ 	bne	$s5,$s1,.L0f11bf74
/*  f11bfb4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11bfb8:	0000a825 */ 	or	$s5,$zero,$zero
.L0f11bfbc:
/*  f11bfbc:	11000012 */ 	beqz	$t0,.L0f11c008
/*  f11bfc0:	8fa520d8 */ 	lw	$a1,0x20d8($sp)
.L0f11bfc4:
/*  f11bfc4:	50e00006 */ 	beqzl	$a3,.L0f11bfe0
/*  f11bfc8:	a0440000 */ 	sb	$a0,0x0($v0)
/*  f11bfcc:	00f57821 */ 	addu	$t7,$a3,$s5
/*  f11bfd0:	91e90000 */ 	lbu	$t1,0x0($t7)
/*  f11bfd4:	10000002 */ 	beqz	$zero,.L0f11bfe0
/*  f11bfd8:	a0490000 */ 	sb	$t1,0x0($v0)
/*  f11bfdc:	a0440000 */ 	sb	$a0,0x0($v0)
.L0f11bfe0:
/*  f11bfe0:	10a00005 */ 	beqz	$a1,.L0f11bff8
/*  f11bfe4:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11bfe8:	00b5c021 */ 	addu	$t8,$a1,$s5
/*  f11bfec:	930d0000 */ 	lbu	$t5,0x0($t8)
/*  f11bff0:	10000002 */ 	beqz	$zero,.L0f11bffc
/*  f11bff4:	a06d0000 */ 	sb	$t5,0x0($v1)
.L0f11bff8:
/*  f11bff8:	a0640000 */ 	sb	$a0,0x0($v1)
.L0f11bffc:
/*  f11bffc:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f11c000:	16a6fff0 */ 	bne	$s5,$a2,.L0f11bfc4
/*  f11c004:	24630001 */ 	addiu	$v1,$v1,0x1
.L0f11c008:
/*  f11c008:	02d3001b */ 	divu	$zero,$s6,$s3
/*  f11c00c:	00001012 */ 	mflo	$v0
/*  f11c010:	0000c810 */ 	mfhi	$t9
/*  f11c014:	00401825 */ 	or	$v1,$v0,$zero
/*  f11c018:	16600002 */ 	bnez	$s3,.L0f11c024
/*  f11c01c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c020:	0007000d */ 	break	0x7
.L0f11c024:
/*  f11c024:	13200002 */ 	beqz	$t9,.L0f11c030
/*  f11c028:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c02c:	24430001 */ 	addiu	$v1,$v0,0x1
.L0f11c030:
/*  f11c030:	0c00543a */ 	jal	joyGetLock
/*  f11c034:	afa30098 */ 	sw	$v1,0x98($sp)
/*  f11c038:	0000f025 */ 	or	$s8,$zero,$zero
.L0f11c03c:
/*  f11c03c:	13c00005 */ 	beqz	$s8,.L0f11c054
/*  f11c040:	0000a825 */ 	or	$s5,$zero,$zero
/*  f11c044:	93aa10ab */ 	lbu	$t2,0x10ab($sp)
/*  f11c048:	354c0002 */ 	ori	$t4,$t2,0x2
/*  f11c04c:	10000004 */ 	beqz	$zero,.L0f11c060
/*  f11c050:	a3ac10ab */ 	sb	$t4,0x10ab($sp)
.L0f11c054:
/*  f11c054:	93ae10ab */ 	lbu	$t6,0x10ab($sp)
/*  f11c058:	31cffffd */ 	andi	$t7,$t6,0xfffd
/*  f11c05c:	a3af10ab */ 	sb	$t7,0x10ab($sp)
.L0f11c060:
/*  f11c060:	27a410a4 */ 	addiu	$a0,$sp,0x10a4
/*  f11c064:	27a510ac */ 	addiu	$a1,$sp,0x10ac
/*  f11c068:	0fc462b9 */ 	jal	pakCalculateChecksum
/*  f11c06c:	02403025 */ 	or	$a2,$s2,$zero
/*  f11c070:	8fa90098 */ 	lw	$t1,0x98($sp)
/*  f11c074:	11200066 */ 	beqz	$t1,.L0f11c210
/*  f11c078:	00172600 */ 	sll	$a0,$s7,0x18
.L0f11c07c:
/*  f11c07c:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f11c080:	03002025 */ 	or	$a0,$t8,$zero
/*  f11c084:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11c088:	00008025 */ 	or	$s0,$zero,$zero
/*  f11c08c:	00550019 */ 	multu	$v0,$s5
/*  f11c090:	8fad10a4 */ 	lw	$t5,0x10a4($sp)
/*  f11c094:	8fab20cc */ 	lw	$t3,0x20cc($sp)
/*  f11c098:	000dcdc2 */ 	srl	$t9,$t5,0x17
/*  f11c09c:	0000b012 */ 	mflo	$s6
/*  f11c0a0:	2ec10010 */ 	sltiu	$at,$s6,0x10
/*  f11c0a4:	50200004 */ 	beqzl	$at,.L0f11c0b8
/*  f11c0a8:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11c0ac:	10000020 */ 	beqz	$zero,.L0f11c130
/*  f11c0b0:	24100001 */ 	addiu	$s0,$zero,0x1
/*  f11c0b4:	24010001 */ 	addiu	$at,$zero,0x1
.L0f11c0b8:
/*  f11c0b8:	13c10055 */ 	beq	$s8,$at,.L0f11c210
/*  f11c0bc:	8faa20d8 */ 	lw	$t2,0x20d8($sp)
/*  f11c0c0:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11c0c4:	53210053 */ 	beql	$t9,$at,.L0f11c214
/*  f11c0c8:	27de0001 */ 	addiu	$s8,$s8,0x1
/*  f11c0cc:	51600051 */ 	beqzl	$t3,.L0f11c214
/*  f11c0d0:	27de0001 */ 	addiu	$s8,$s8,0x1
/*  f11c0d4:	51400016 */ 	beqzl	$t2,.L0f11c130
/*  f11c0d8:	24100001 */ 	addiu	$s0,$zero,0x1
/*  f11c0dc:	12600014 */ 	beqz	$s3,.L0f11c130
/*  f11c0e0:	00001825 */ 	or	$v1,$zero,$zero
/*  f11c0e4:	02b30019 */ 	multu	$s5,$s3
/*  f11c0e8:	00002012 */ 	mflo	$a0
/*  f11c0ec:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c0f0:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11c0f4:
/*  f11c0f4:	02446021 */ 	addu	$t4,$s2,$a0
/*  f11c0f8:	02847821 */ 	addu	$t7,$s4,$a0
/*  f11c0fc:	91e90000 */ 	lbu	$t1,0x0($t7)
/*  f11c100:	918e0000 */ 	lbu	$t6,0x0($t4)
/*  f11c104:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11c108:	0073082b */ 	sltu	$at,$v1,$s3
/*  f11c10c:	11c90003 */ 	beq	$t6,$t1,.L0f11c11c
/*  f11c110:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c114:	10000006 */ 	beqz	$zero,.L0f11c130
/*  f11c118:	24100001 */ 	addiu	$s0,$zero,0x1
.L0f11c11c:
/*  f11c11c:	1420fff5 */ 	bnez	$at,.L0f11c0f4
/*  f11c120:	24840001 */ 	addiu	$a0,$a0,0x1
/*  f11c124:	10000002 */ 	beqz	$zero,.L0f11c130
/*  f11c128:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c12c:	24100001 */ 	addiu	$s0,$zero,0x1
.L0f11c130:
/*  f11c130:	12000033 */ 	beqz	$s0,.L0f11c200
/*  f11c134:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11c138:	02b30019 */ 	multu	$s5,$s3
/*  f11c13c:	0017c880 */ 	sll	$t9,$s7,0x2
/*  f11c140:	0337c823 */ 	subu	$t9,$t9,$s7
/*  f11c144:	8fad20c4 */ 	lw	$t5,0x20c4($sp)
/*  f11c148:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11c14c:	0337c821 */ 	addu	$t9,$t9,$s7
/*  f11c150:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11c154:	3c0b800a */ 	lui	$t3,%hi(g_Pfses)
/*  f11c158:	256b3180 */ 	addiu	$t3,$t3,%lo(g_Pfses)
/*  f11c15c:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f11c160:	0000c012 */ 	mflo	$t8
/*  f11c164:	00045603 */ 	sra	$t2,$a0,0x18
/*  f11c168:	16e10003 */ 	bne	$s7,$at,.L0f11c178
/*  f11c16c:	030d8821 */ 	addu	$s1,$t8,$t5
/*  f11c170:	10000002 */ 	beqz	$zero,.L0f11c17c
/*  f11c174:	00008025 */ 	or	$s0,$zero,$zero
.L0f11c178:
/*  f11c178:	032b8021 */ 	addu	$s0,$t9,$t3
.L0f11c17c:
/*  f11c17c:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11c180:	01402025 */ 	or	$a0,$t2,$zero
/*  f11c184:	8faf0058 */ 	lw	$t7,0x58($sp)
/*  f11c188:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11c18c:	00046603 */ 	sra	$t4,$a0,0x18
/*  f11c190:	8de6029c */ 	lw	$a2,0x29c($t7)
/*  f11c194:	02567021 */ 	addu	$t6,$s2,$s6
/*  f11c198:	afae0018 */ 	sw	$t6,0x18($sp)
/*  f11c19c:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f11c1a0:	afb10010 */ 	sw	$s1,0x10($sp)
/*  f11c1a4:	01802025 */ 	or	$a0,$t4,$zero
/*  f11c1a8:	02002825 */ 	or	$a1,$s0,$zero
/*  f11c1ac:	0fc46941 */ 	jal	pakReadWriteBlock
/*  f11c1b0:	24070001 */ 	addiu	$a3,$zero,0x1
/*  f11c1b4:	00172e00 */ 	sll	$a1,$s7,0x18
/*  f11c1b8:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f11c1bc:	00408025 */ 	or	$s0,$v0,$zero
/*  f11c1c0:	01202825 */ 	or	$a1,$t1,$zero
/*  f11c1c4:	00402025 */ 	or	$a0,$v0,$zero
/*  f11c1c8:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f11c1cc:	0fc470e7 */ 	jal	pakHandleResult
/*  f11c1d0:	24071286 */ 	addiu	$a3,$zero,_val7f11c1d0
/*  f11c1d4:	5440000b */ 	bnezl	$v0,.L0f11c204
/*  f11c1d8:	8fb80098 */ 	lw	$t8,0x98($sp)
/*  f11c1dc:	0c005451 */ 	jal	joyReleaseLock
/*  f11c1e0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c1e4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11c1e8:	16010003 */ 	bne	$s0,$at,.L0f11c1f8
/*  f11c1ec:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c1f0:	10000022 */ 	beqz	$zero,.L0f11c27c
/*  f11c1f4:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11c1f8:
/*  f11c1f8:	10000020 */ 	beqz	$zero,.L0f11c27c
/*  f11c1fc:	24020004 */ 	addiu	$v0,$zero,0x4
.L0f11c200:
/*  f11c200:	8fb80098 */ 	lw	$t8,0x98($sp)
.L0f11c204:
/*  f11c204:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f11c208:	56b8ff9c */ 	bnel	$s5,$t8,.L0f11c07c
/*  f11c20c:	00172600 */ 	sll	$a0,$s7,0x18
.L0f11c210:
/*  f11c210:	27de0001 */ 	addiu	$s8,$s8,0x1
.L0f11c214:
/*  f11c214:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11c218:	17c1ff88 */ 	bne	$s8,$at,.L0f11c03c
/*  f11c21c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c220:	0c005451 */ 	jal	joyReleaseLock
/*  f11c224:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c228:	3c0d8007 */ 	lui	$t5,%hi(g_PakDebugPakCache)
/*  f11c22c:	8dad5ce8 */ 	lw	$t5,%lo(g_PakDebugPakCache)($t5)
/*  f11c230:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11c234:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f11c238:	51a00010 */ 	beqzl	$t5,.L0f11c27c
/*  f11c23c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11c240:	0fc45974 */ 	jal	pakGetBlockSize
/*  f11c244:	03202025 */ 	or	$a0,$t9,$zero
/*  f11c248:	8faa20c4 */ 	lw	$t2,0x20c4($sp)
/*  f11c24c:	00172600 */ 	sll	$a0,$s7,0x18
/*  f11c250:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f11c254:	0142001b */ 	divu	$zero,$t2,$v0
/*  f11c258:	00002812 */ 	mflo	$a1
/*  f11c25c:	01602025 */ 	or	$a0,$t3,$zero
/*  f11c260:	14400002 */ 	bnez	$v0,.L0f11c26c
/*  f11c264:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c268:	0007000d */ 	break	0x7
.L0f11c26c:
/*  f11c26c:	02403025 */ 	or	$a2,$s2,$zero
/*  f11c270:	0fc46c5e */ 	jal	pakSaveHeaderToCache
/*  f11c274:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11c278:	00001025 */ 	or	$v0,$zero,$zero
.L0f11c27c:
/*  f11c27c:	8fbf004c */ 	lw	$ra,0x4c($sp)
/*  f11c280:	8fb00028 */ 	lw	$s0,0x28($sp)
/*  f11c284:	8fb1002c */ 	lw	$s1,0x2c($sp)
/*  f11c288:	8fb20030 */ 	lw	$s2,0x30($sp)
/*  f11c28c:	8fb30034 */ 	lw	$s3,0x34($sp)
/*  f11c290:	8fb40038 */ 	lw	$s4,0x38($sp)
/*  f11c294:	8fb5003c */ 	lw	$s5,0x3c($sp)
/*  f11c298:	8fb60040 */ 	lw	$s6,0x40($sp)
/*  f11c29c:	8fb70044 */ 	lw	$s7,0x44($sp)
/*  f11c2a0:	8fbe0048 */ 	lw	$s8,0x48($sp)
/*  f11c2a4:	03e00008 */ 	jr	$ra
/*  f11c2a8:	27bd20c0 */ 	addiu	$sp,$sp,0x20c0
);
#else
GLOBAL_ASM(
glabel pakWriteFile
/*  f115bd8:	27bddf40 */ 	addiu	$sp,$sp,-8384
/*  f115bdc:	afb70044 */ 	sw	$s7,0x44($sp)
/*  f115be0:	0004be00 */ 	sll	$s7,$a0,0x18
/*  f115be4:	00177603 */ 	sra	$t6,$s7,0x18
/*  f115be8:	afa420c0 */ 	sw	$a0,0x20c0($sp)
/*  f115bec:	000e2600 */ 	sll	$a0,$t6,0x18
/*  f115bf0:	afbf004c */ 	sw	$ra,0x4c($sp)
/*  f115bf4:	afb40038 */ 	sw	$s4,0x38($sp)
/*  f115bf8:	00047e03 */ 	sra	$t7,$a0,0x18
/*  f115bfc:	00c0a025 */ 	or	$s4,$a2,$zero
/*  f115c00:	01c0b825 */ 	or	$s7,$t6,$zero
/*  f115c04:	afbe0048 */ 	sw	$s8,0x48($sp)
/*  f115c08:	afb60040 */ 	sw	$s6,0x40($sp)
/*  f115c0c:	afb5003c */ 	sw	$s5,0x3c($sp)
/*  f115c10:	afb30034 */ 	sw	$s3,0x34($sp)
/*  f115c14:	afb20030 */ 	sw	$s2,0x30($sp)
/*  f115c18:	afb1002c */ 	sw	$s1,0x2c($sp)
/*  f115c1c:	afb00028 */ 	sw	$s0,0x28($sp)
/*  f115c20:	afa520c4 */ 	sw	$a1,0x20c4($sp)
/*  f115c24:	afa720cc */ 	sw	$a3,0x20cc($sp)
/*  f115c28:	0fc4428c */ 	jal	pakGetBlockSize
/*  f115c2c:	01e02025 */ 	or	$a0,$t7,$zero
/*  f115c30:	8fb220e0 */ 	lw	$s2,0x20e0($sp)
/*  f115c34:	8fa320d0 */ 	lw	$v1,0x20d0($sp)
/*  f115c38:	00409825 */ 	or	$s3,$v0,$zero
/*  f115c3c:	325801ff */ 	andi	$t8,$s2,0x1ff
/*  f115c40:	10600003 */ 	beqz	$v1,.NB0f115c50
/*  f115c44:	03009025 */ 	or	$s2,$t8,$zero
/*  f115c48:	10000007 */ 	beqz	$zero,.NB0f115c68
/*  f115c4c:	00608825 */ 	or	$s1,$v1,$zero
.NB0f115c50:
/*  f115c50:	00172600 */ 	sll	$a0,$s7,0x18
/*  f115c54:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f115c58:	03202025 */ 	or	$a0,$t9,$zero
/*  f115c5c:	0fc444f9 */ 	jal	pakGetBodyLenByType
/*  f115c60:	02802825 */ 	or	$a1,$s4,$zero
/*  f115c64:	00408825 */ 	or	$s1,$v0,$zero
.NB0f115c68:
/*  f115c68:	00172600 */ 	sll	$a0,$s7,0x18
/*  f115c6c:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f115c70:	01202025 */ 	or	$a0,$t1,$zero
/*  f115c74:	0fc442ae */ 	jal	pakGetAlignedFileLenByBodyLen
/*  f115c78:	02202825 */ 	or	$a1,$s1,$zero
/*  f115c7c:	0040b025 */ 	or	$s6,$v0,$zero
/*  f115c80:	8fa220dc */ 	lw	$v0,0x20dc($sp)
/*  f115c84:	27b020b0 */ 	addiu	$s0,$sp,0x20b0
/*  f115c88:	8e03000c */ 	lw	$v1,0xc($s0)
/*  f115c8c:	10400013 */ 	beqz	$v0,.NB0f115cdc
/*  f115c90:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f115c94:	00177880 */ 	sll	$t7,$s7,0x2
/*  f115c98:	01f77823 */ 	subu	$t7,$t7,$s7
/*  f115c9c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f115ca0:	00035302 */ 	srl	$t2,$v1,0xc
/*  f115ca4:	004a5826 */ 	xor	$t3,$v0,$t2
/*  f115ca8:	01f77823 */ 	subu	$t7,$t7,$s7
/*  f115cac:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f115cb0:	000b6640 */ 	sll	$t4,$t3,0x19
/*  f115cb4:	000c6b42 */ 	srl	$t5,$t4,0xd
/*  f115cb8:	01f77821 */ 	addu	$t7,$t7,$s7
/*  f115cbc:	3c18800a */ 	lui	$t8,0x800a
/*  f115cc0:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f115cc4:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f115cc8:	01a37026 */ 	xor	$t6,$t5,$v1
/*  f115ccc:	ae0e000c */ 	sw	$t6,0xc($s0)
/*  f115cd0:	01f8c821 */ 	addu	$t9,$t7,$t8
/*  f115cd4:	10000016 */ 	beqz	$zero,.NB0f115d30
/*  f115cd8:	afb90058 */ 	sw	$t9,0x58($sp)
.NB0f115cdc:
/*  f115cdc:	00174880 */ 	sll	$t1,$s7,0x2
/*  f115ce0:	01374823 */ 	subu	$t1,$t1,$s7
/*  f115ce4:	00094880 */ 	sll	$t1,$t1,0x2
/*  f115ce8:	01374823 */ 	subu	$t1,$t1,$s7
/*  f115cec:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f115cf0:	01374821 */ 	addu	$t1,$t1,$s7
/*  f115cf4:	3c0a800a */ 	lui	$t2,0x800a
/*  f115cf8:	254a6870 */ 	addiu	$t2,$t2,0x6870
/*  f115cfc:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f115d00:	012a1021 */ 	addu	$v0,$t1,$t2
/*  f115d04:	8c4b025c */ 	lw	$t3,0x25c($v0)
/*  f115d08:	8fa320bc */ 	lw	$v1,0x20bc($sp)
/*  f115d0c:	afa20058 */ 	sw	$v0,0x58($sp)
/*  f115d10:	256c0001 */ 	addiu	$t4,$t3,0x1
/*  f115d14:	00037302 */ 	srl	$t6,$v1,0xc
/*  f115d18:	018e7826 */ 	xor	$t7,$t4,$t6
/*  f115d1c:	000fc640 */ 	sll	$t8,$t7,0x19
/*  f115d20:	0018cb42 */ 	srl	$t9,$t8,0xd
/*  f115d24:	03231826 */ 	xor	$v1,$t9,$v1
/*  f115d28:	afa320bc */ 	sw	$v1,0x20bc($sp)
/*  f115d2c:	ac4c025c */ 	sw	$t4,0x25c($v0)
.NB0f115d30:
/*  f115d30:	8fa90058 */ 	lw	$t1,0x58($sp)
/*  f115d34:	960e000c */ 	lhu	$t6,0xc($s0)
/*  f115d38:	960a000a */ 	lhu	$t2,0xa($s0)
/*  f115d3c:	8d2b0260 */ 	lw	$t3,0x260($t1)
/*  f115d40:	32c90fff */ 	andi	$t1,$s6,0xfff
/*  f115d44:	31cf0007 */ 	andi	$t7,$t6,0x7
/*  f115d48:	000b68c0 */ 	sll	$t5,$t3,0x3
/*  f115d4c:	314bf000 */ 	andi	$t3,$t2,0xf000
/*  f115d50:	01afc025 */ 	or	$t8,$t5,$t7
/*  f115d54:	012b6025 */ 	or	$t4,$t1,$t3
/*  f115d58:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f115d5c:	a618000c */ 	sh	$t8,0xc($s0)
/*  f115d60:	a60c000a */ 	sh	$t4,0xa($s0)
/*  f115d64:	0c004e18 */ 	jal	argFindByPrefix
/*  f115d68:	24a5e5fc */ 	addiu	$a1,$a1,-6660
/*  f115d6c:	10400003 */ 	beqz	$v0,.NB0f115d7c
/*  f115d70:	8fa420d4 */ 	lw	$a0,0x20d4($sp)
/*  f115d74:	10000002 */ 	beqz	$zero,.NB0f115d80
/*  f115d78:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f115d7c:
/*  f115d7c:	00001025 */ 	or	$v0,$zero,$zero
.NB0f115d80:
/*  f115d80:	920f000f */ 	lbu	$t7,0xf($s0)
/*  f115d84:	00407025 */ 	or	$t6,$v0,$zero
/*  f115d88:	31cd0001 */ 	andi	$t5,$t6,0x1
/*  f115d8c:	31f8fffe */ 	andi	$t8,$t7,0xfffe
/*  f115d90:	01b8c825 */ 	or	$t9,$t5,$t8
/*  f115d94:	a219000f */ 	sb	$t9,0xf($s0)
/*  f115d98:	8fa920b8 */ 	lw	$t1,0x20b8($sp)
/*  f115d9c:	0012c8c0 */ 	sll	$t9,$s2,0x3
/*  f115da0:	332a0ff8 */ 	andi	$t2,$t9,0xff8
/*  f115da4:	00095b02 */ 	srl	$t3,$t1,0xc
/*  f115da8:	022b6026 */ 	xor	$t4,$s1,$t3
/*  f115dac:	000c7540 */ 	sll	$t6,$t4,0x15
/*  f115db0:	000e7a42 */ 	srl	$t7,$t6,0x9
/*  f115db4:	01e96826 */ 	xor	$t5,$t7,$t1
/*  f115db8:	afad20b8 */ 	sw	$t5,0x20b8($sp)
/*  f115dbc:	960b000e */ 	lhu	$t3,0xe($s0)
/*  f115dc0:	96180008 */ 	lhu	$t8,0x8($s0)
/*  f115dc4:	02807825 */ 	or	$t7,$s4,$zero
/*  f115dc8:	316cf007 */ 	andi	$t4,$t3,0xf007
/*  f115dcc:	014c7025 */ 	or	$t6,$t2,$t4
/*  f115dd0:	a60e000e */ 	sh	$t6,0xe($s0)
/*  f115dd4:	8e03000c */ 	lw	$v1,0xc($s0)
/*  f115dd8:	000f69c0 */ 	sll	$t5,$t7,0x7
/*  f115ddc:	3319007f */ 	andi	$t9,$t8,0x7f
/*  f115de0:	00035340 */ 	sll	$t2,$v1,0xd
/*  f115de4:	000a6642 */ 	srl	$t4,$t2,0x19
/*  f115de8:	318e007f */ 	andi	$t6,$t4,0x7f
/*  f115dec:	00037b02 */ 	srl	$t7,$v1,0xc
/*  f115df0:	01cf4826 */ 	xor	$t1,$t6,$t7
/*  f115df4:	01b95825 */ 	or	$t3,$t5,$t9
/*  f115df8:	0009c640 */ 	sll	$t8,$t1,0x19
/*  f115dfc:	00186b42 */ 	srl	$t5,$t8,0xd
/*  f115e00:	01a3c826 */ 	xor	$t9,$t5,$v1
/*  f115e04:	a60b0008 */ 	sh	$t3,0x8($s0)
/*  f115e08:	10800004 */ 	beqz	$a0,.NB0f115e1c
/*  f115e0c:	ae19000c */ 	sw	$t9,0xc($s0)
/*  f115e10:	00195340 */ 	sll	$t2,$t9,0xd
/*  f115e14:	000a6642 */ 	srl	$t4,$t2,0x19
/*  f115e18:	ac8c0000 */ 	sw	$t4,0x0($a0)
.NB0f115e1c:
/*  f115e1c:	8fa720cc */ 	lw	$a3,0x20cc($sp)
/*  f115e20:	50e00007 */ 	beqzl	$a3,.NB0f115e40
/*  f115e24:	9218000f */ 	lbu	$t8,0xf($s0)
/*  f115e28:	920e000f */ 	lbu	$t6,0xf($s0)
/*  f115e2c:	31cffffb */ 	andi	$t7,$t6,0xfffb
/*  f115e30:	35e90004 */ 	ori	$t1,$t7,0x4
/*  f115e34:	10000004 */ 	beqz	$zero,.NB0f115e48
/*  f115e38:	a209000f */ 	sb	$t1,0xf($s0)
/*  f115e3c:	9218000f */ 	lbu	$t8,0xf($s0)
.NB0f115e40:
/*  f115e40:	330dfffb */ 	andi	$t5,$t8,0xfffb
/*  f115e44:	a20d000f */ 	sb	$t5,0xf($s0)
.NB0f115e48:
/*  f115e48:	8e19000c */ 	lw	$t9,0xc($s0)
/*  f115e4c:	3409ffff */ 	dli	$t1,0xffff
/*  f115e50:	3418ffff */ 	dli	$t8,0xffff
/*  f115e54:	00195f40 */ 	sll	$t3,$t9,0x1d
/*  f115e58:	000b57c2 */ 	srl	$t2,$t3,0x1f
/*  f115e5c:	5140000b */ 	beqzl	$t2,.NB0f115e8c
/*  f115e60:	a7a920b4 */ 	sh	$t1,0x20b4($sp)
/*  f115e64:	8fac20b8 */ 	lw	$t4,0x20b8($sp)
/*  f115e68:	00e02025 */ 	or	$a0,$a3,$zero
/*  f115e6c:	27a620b4 */ 	addiu	$a2,$sp,0x20b4
/*  f115e70:	000c7240 */ 	sll	$t6,$t4,0x9
/*  f115e74:	000e7d42 */ 	srl	$t7,$t6,0x15
/*  f115e78:	0fc44b9b */ 	jal	pakCalculateChecksum
/*  f115e7c:	01e72821 */ 	addu	$a1,$t7,$a3
/*  f115e80:	10000003 */ 	beqz	$zero,.NB0f115e90
/*  f115e84:	8fa720cc */ 	lw	$a3,0x20cc($sp)
/*  f115e88:	a7a920b4 */ 	sh	$t1,0x20b4($sp)
.NB0f115e8c:
/*  f115e8c:	a7b820b6 */ 	sh	$t8,0x20b6($sp)
.NB0f115e90:
/*  f115e90:	02d14023 */ 	subu	$t0,$s6,$s1
/*  f115e94:	27b2109c */ 	addiu	$s2,$sp,0x109c
/*  f115e98:	27b4009c */ 	addiu	$s4,$sp,0x9c
/*  f115e9c:	2508fff0 */ 	addiu	$t0,$t0,-16
/*  f115ea0:	01002825 */ 	or	$a1,$t0,$zero
/*  f115ea4:	02401025 */ 	or	$v0,$s2,$zero
/*  f115ea8:	02801825 */ 	or	$v1,$s4,$zero
/*  f115eac:	0000a825 */ 	or	$s5,$zero,$zero
/*  f115eb0:	2404002b */ 	addiu	$a0,$zero,0x2b
.NB0f115eb4:
/*  f115eb4:	02156821 */ 	addu	$t5,$s0,$s5
/*  f115eb8:	91b90000 */ 	lbu	$t9,0x0($t5)
/*  f115ebc:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f115ec0:	2ea10010 */ 	sltiu	$at,$s5,0x10
/*  f115ec4:	a0590000 */ 	sb	$t9,0x0($v0)
/*  f115ec8:	a0640000 */ 	sb	$a0,0x0($v1)
/*  f115ecc:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f115ed0:	1420fff8 */ 	bnez	$at,.NB0f115eb4
/*  f115ed4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f115ed8:	12200014 */ 	beqz	$s1,.NB0f115f2c
/*  f115edc:	0000a825 */ 	or	$s5,$zero,$zero
/*  f115ee0:	8fa620d8 */ 	lw	$a2,0x20d8($sp)
.NB0f115ee4:
/*  f115ee4:	50e00006 */ 	beqzl	$a3,.NB0f115f00
/*  f115ee8:	a0440000 */ 	sb	$a0,0x0($v0)
/*  f115eec:	00f55821 */ 	addu	$t3,$a3,$s5
/*  f115ef0:	916a0000 */ 	lbu	$t2,0x0($t3)
/*  f115ef4:	10000002 */ 	beqz	$zero,.NB0f115f00
/*  f115ef8:	a04a0000 */ 	sb	$t2,0x0($v0)
/*  f115efc:	a0440000 */ 	sb	$a0,0x0($v0)
.NB0f115f00:
/*  f115f00:	10c00005 */ 	beqz	$a2,.NB0f115f18
/*  f115f04:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f115f08:	00d56021 */ 	addu	$t4,$a2,$s5
/*  f115f0c:	918e0000 */ 	lbu	$t6,0x0($t4)
/*  f115f10:	10000002 */ 	beqz	$zero,.NB0f115f1c
/*  f115f14:	a06e0000 */ 	sb	$t6,0x0($v1)
.NB0f115f18:
/*  f115f18:	a0640000 */ 	sb	$a0,0x0($v1)
.NB0f115f1c:
/*  f115f1c:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f115f20:	16b1fff0 */ 	bne	$s5,$s1,.NB0f115ee4
/*  f115f24:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f115f28:	0000a825 */ 	or	$s5,$zero,$zero
.NB0f115f2c:
/*  f115f2c:	11000012 */ 	beqz	$t0,.NB0f115f78
/*  f115f30:	8fa620d8 */ 	lw	$a2,0x20d8($sp)
.NB0f115f34:
/*  f115f34:	50e00006 */ 	beqzl	$a3,.NB0f115f50
/*  f115f38:	a0440000 */ 	sb	$a0,0x0($v0)
/*  f115f3c:	00f57821 */ 	addu	$t7,$a3,$s5
/*  f115f40:	91e90000 */ 	lbu	$t1,0x0($t7)
/*  f115f44:	10000002 */ 	beqz	$zero,.NB0f115f50
/*  f115f48:	a0490000 */ 	sb	$t1,0x0($v0)
/*  f115f4c:	a0440000 */ 	sb	$a0,0x0($v0)
.NB0f115f50:
/*  f115f50:	10c00005 */ 	beqz	$a2,.NB0f115f68
/*  f115f54:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f115f58:	00d5c021 */ 	addu	$t8,$a2,$s5
/*  f115f5c:	930d0000 */ 	lbu	$t5,0x0($t8)
/*  f115f60:	10000002 */ 	beqz	$zero,.NB0f115f6c
/*  f115f64:	a06d0000 */ 	sb	$t5,0x0($v1)
.NB0f115f68:
/*  f115f68:	a0640000 */ 	sb	$a0,0x0($v1)
.NB0f115f6c:
/*  f115f6c:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f115f70:	16a5fff0 */ 	bne	$s5,$a1,.NB0f115f34
/*  f115f74:	24630001 */ 	addiu	$v1,$v1,0x1
.NB0f115f78:
/*  f115f78:	02d3001b */ 	divu	$zero,$s6,$s3
/*  f115f7c:	00001012 */ 	mflo	$v0
/*  f115f80:	0000c810 */ 	mfhi	$t9
/*  f115f84:	00401825 */ 	or	$v1,$v0,$zero
/*  f115f88:	16600002 */ 	bnez	$s3,.NB0f115f94
/*  f115f8c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f115f90:	0007000d */ 	break	0x7
.NB0f115f94:
/*  f115f94:	240410c4 */ 	addiu	$a0,$zero,0x10c4
/*  f115f98:	13200002 */ 	beqz	$t9,.NB0f115fa4
/*  f115f9c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f115fa0:	24430001 */ 	addiu	$v1,$v0,0x1
.NB0f115fa4:
/*  f115fa4:	24a5e60c */ 	addiu	$a1,$a1,-6644
/*  f115fa8:	0c00581b */ 	jal	joyGetLock
/*  f115fac:	afa30098 */ 	sw	$v1,0x98($sp)
/*  f115fb0:	0000f025 */ 	or	$s8,$zero,$zero
.NB0f115fb4:
/*  f115fb4:	13c00005 */ 	beqz	$s8,.NB0f115fcc
/*  f115fb8:	0000a825 */ 	or	$s5,$zero,$zero
/*  f115fbc:	93aa10ab */ 	lbu	$t2,0x10ab($sp)
/*  f115fc0:	354c0002 */ 	ori	$t4,$t2,0x2
/*  f115fc4:	10000004 */ 	beqz	$zero,.NB0f115fd8
/*  f115fc8:	a3ac10ab */ 	sb	$t4,0x10ab($sp)
.NB0f115fcc:
/*  f115fcc:	93ae10ab */ 	lbu	$t6,0x10ab($sp)
/*  f115fd0:	31cffffd */ 	andi	$t7,$t6,0xfffd
/*  f115fd4:	a3af10ab */ 	sb	$t7,0x10ab($sp)
.NB0f115fd8:
/*  f115fd8:	27a410a4 */ 	addiu	$a0,$sp,0x10a4
/*  f115fdc:	27a510ac */ 	addiu	$a1,$sp,0x10ac
/*  f115fe0:	0fc44b9b */ 	jal	pakCalculateChecksum
/*  f115fe4:	02403025 */ 	or	$a2,$s2,$zero
/*  f115fe8:	8fa90098 */ 	lw	$t1,0x98($sp)
/*  f115fec:	11200067 */ 	beqz	$t1,.NB0f11618c
/*  f115ff0:	00172600 */ 	sll	$a0,$s7,0x18
.NB0f115ff4:
/*  f115ff4:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f115ff8:	03002025 */ 	or	$a0,$t8,$zero
/*  f115ffc:	0fc4428c */ 	jal	pakGetBlockSize
/*  f116000:	00008025 */ 	or	$s0,$zero,$zero
/*  f116004:	00550019 */ 	multu	$v0,$s5
/*  f116008:	8fad10a4 */ 	lw	$t5,0x10a4($sp)
/*  f11600c:	8fab20cc */ 	lw	$t3,0x20cc($sp)
/*  f116010:	000dcdc2 */ 	srl	$t9,$t5,0x17
/*  f116014:	0000b012 */ 	mflo	$s6
/*  f116018:	2ec10010 */ 	sltiu	$at,$s6,0x10
/*  f11601c:	50200004 */ 	beqzl	$at,.NB0f116030
/*  f116020:	24010001 */ 	addiu	$at,$zero,0x1
/*  f116024:	10000020 */ 	beqz	$zero,.NB0f1160a8
/*  f116028:	24100001 */ 	addiu	$s0,$zero,0x1
/*  f11602c:	24010001 */ 	addiu	$at,$zero,0x1
.NB0f116030:
/*  f116030:	13c10056 */ 	beq	$s8,$at,.NB0f11618c
/*  f116034:	8faa20d8 */ 	lw	$t2,0x20d8($sp)
/*  f116038:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11603c:	53210054 */ 	beql	$t9,$at,.NB0f116190
/*  f116040:	27de0001 */ 	addiu	$s8,$s8,0x1
/*  f116044:	51600052 */ 	beqzl	$t3,.NB0f116190
/*  f116048:	27de0001 */ 	addiu	$s8,$s8,0x1
/*  f11604c:	51400016 */ 	beqzl	$t2,.NB0f1160a8
/*  f116050:	24100001 */ 	addiu	$s0,$zero,0x1
/*  f116054:	12600014 */ 	beqz	$s3,.NB0f1160a8
/*  f116058:	00001825 */ 	or	$v1,$zero,$zero
/*  f11605c:	02b30019 */ 	multu	$s5,$s3
/*  f116060:	00002012 */ 	mflo	$a0
/*  f116064:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116068:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f11606c:
/*  f11606c:	02446021 */ 	addu	$t4,$s2,$a0
/*  f116070:	02847821 */ 	addu	$t7,$s4,$a0
/*  f116074:	91e90000 */ 	lbu	$t1,0x0($t7)
/*  f116078:	918e0000 */ 	lbu	$t6,0x0($t4)
/*  f11607c:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f116080:	0073082b */ 	sltu	$at,$v1,$s3
/*  f116084:	11c90003 */ 	beq	$t6,$t1,.NB0f116094
/*  f116088:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11608c:	10000006 */ 	beqz	$zero,.NB0f1160a8
/*  f116090:	24100001 */ 	addiu	$s0,$zero,0x1
.NB0f116094:
/*  f116094:	1420fff5 */ 	bnez	$at,.NB0f11606c
/*  f116098:	24840001 */ 	addiu	$a0,$a0,0x1
/*  f11609c:	10000002 */ 	beqz	$zero,.NB0f1160a8
/*  f1160a0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1160a4:	24100001 */ 	addiu	$s0,$zero,0x1
.NB0f1160a8:
/*  f1160a8:	12000034 */ 	beqz	$s0,.NB0f11617c
/*  f1160ac:	00172600 */ 	sll	$a0,$s7,0x18
/*  f1160b0:	02b30019 */ 	multu	$s5,$s3
/*  f1160b4:	0017c880 */ 	sll	$t9,$s7,0x2
/*  f1160b8:	0337c823 */ 	subu	$t9,$t9,$s7
/*  f1160bc:	8fad20c4 */ 	lw	$t5,0x20c4($sp)
/*  f1160c0:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f1160c4:	0337c821 */ 	addu	$t9,$t9,$s7
/*  f1160c8:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1160cc:	3c0b800a */ 	lui	$t3,0x800a
/*  f1160d0:	256b7658 */ 	addiu	$t3,$t3,0x7658
/*  f1160d4:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f1160d8:	0000c012 */ 	mflo	$t8
/*  f1160dc:	00045603 */ 	sra	$t2,$a0,0x18
/*  f1160e0:	16e10003 */ 	bne	$s7,$at,.NB0f1160f0
/*  f1160e4:	030d8821 */ 	addu	$s1,$t8,$t5
/*  f1160e8:	10000002 */ 	beqz	$zero,.NB0f1160f4
/*  f1160ec:	00008025 */ 	or	$s0,$zero,$zero
.NB0f1160f0:
/*  f1160f0:	032b8021 */ 	addu	$s0,$t9,$t3
.NB0f1160f4:
/*  f1160f4:	0fc4428c */ 	jal	pakGetBlockSize
/*  f1160f8:	01402025 */ 	or	$a0,$t2,$zero
/*  f1160fc:	8faf0058 */ 	lw	$t7,0x58($sp)
/*  f116100:	00172600 */ 	sll	$a0,$s7,0x18
/*  f116104:	00046603 */ 	sra	$t4,$a0,0x18
/*  f116108:	8de6029c */ 	lw	$a2,0x29c($t7)
/*  f11610c:	02567021 */ 	addu	$t6,$s2,$s6
/*  f116110:	afae0018 */ 	sw	$t6,0x18($sp)
/*  f116114:	afa20014 */ 	sw	$v0,0x14($sp)
/*  f116118:	afb10010 */ 	sw	$s1,0x10($sp)
/*  f11611c:	01802025 */ 	or	$a0,$t4,$zero
/*  f116120:	02002825 */ 	or	$a1,$s0,$zero
/*  f116124:	0fc450ea */ 	jal	pakReadWriteBlock
/*  f116128:	24070001 */ 	addiu	$a3,$zero,0x1
/*  f11612c:	00172e00 */ 	sll	$a1,$s7,0x18
/*  f116130:	00054e03 */ 	sra	$t1,$a1,0x18
/*  f116134:	00408025 */ 	or	$s0,$v0,$zero
/*  f116138:	01202825 */ 	or	$a1,$t1,$zero
/*  f11613c:	00402025 */ 	or	$a0,$v0,$zero
/*  f116140:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f116144:	0fc458cb */ 	jal	pakHandleResult
/*  f116148:	24071119 */ 	addiu	$a3,$zero,0x1119
/*  f11614c:	1440000b */ 	bnez	$v0,.NB0f11617c
/*  f116150:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f116154:	24a5e614 */ 	addiu	$a1,$a1,-6636
/*  f116158:	0c005834 */ 	jal	joyReleaseLock
/*  f11615c:	2404111c */ 	addiu	$a0,$zero,0x111c
/*  f116160:	24010001 */ 	addiu	$at,$zero,0x1
/*  f116164:	16010003 */ 	bne	$s0,$at,.NB0f116174
/*  f116168:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11616c:	10000024 */ 	beqz	$zero,.NB0f116200
/*  f116170:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f116174:
/*  f116174:	10000022 */ 	beqz	$zero,.NB0f116200
/*  f116178:	24020004 */ 	addiu	$v0,$zero,0x4
.NB0f11617c:
/*  f11617c:	8fb80098 */ 	lw	$t8,0x98($sp)
/*  f116180:	26b50001 */ 	addiu	$s5,$s5,0x1
/*  f116184:	56b8ff9b */ 	bnel	$s5,$t8,.NB0f115ff4
/*  f116188:	00172600 */ 	sll	$a0,$s7,0x18
.NB0f11618c:
/*  f11618c:	27de0001 */ 	addiu	$s8,$s8,0x1
.NB0f116190:
/*  f116190:	24010002 */ 	addiu	$at,$zero,0x2
/*  f116194:	17c1ff87 */ 	bne	$s8,$at,.NB0f115fb4
/*  f116198:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11619c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1161a0:	24a5e61c */ 	addiu	$a1,$a1,-6628
/*  f1161a4:	0c005834 */ 	jal	joyReleaseLock
/*  f1161a8:	24041129 */ 	addiu	$a0,$zero,0x1129
/*  f1161ac:	3c0d8008 */ 	lui	$t5,0x8008
/*  f1161b0:	8dad80b0 */ 	lw	$t5,-0x7f50($t5)
/*  f1161b4:	00172600 */ 	sll	$a0,$s7,0x18
/*  f1161b8:	0004ce03 */ 	sra	$t9,$a0,0x18
/*  f1161bc:	51a00010 */ 	beqzl	$t5,.NB0f116200
/*  f1161c0:	00001025 */ 	or	$v0,$zero,$zero
/*  f1161c4:	0fc4428c */ 	jal	pakGetBlockSize
/*  f1161c8:	03202025 */ 	or	$a0,$t9,$zero
/*  f1161cc:	8faa20c4 */ 	lw	$t2,0x20c4($sp)
/*  f1161d0:	00172600 */ 	sll	$a0,$s7,0x18
/*  f1161d4:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f1161d8:	0142001b */ 	divu	$zero,$t2,$v0
/*  f1161dc:	00002812 */ 	mflo	$a1
/*  f1161e0:	01602025 */ 	or	$a0,$t3,$zero
/*  f1161e4:	14400002 */ 	bnez	$v0,.NB0f1161f0
/*  f1161e8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1161ec:	0007000d */ 	break	0x7
.NB0f1161f0:
/*  f1161f0:	02403025 */ 	or	$a2,$s2,$zero
/*  f1161f4:	0fc45454 */ 	jal	pakSaveHeaderToCache
/*  f1161f8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1161fc:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116200:
/*  f116200:	8fbf004c */ 	lw	$ra,0x4c($sp)
/*  f116204:	8fb00028 */ 	lw	$s0,0x28($sp)
/*  f116208:	8fb1002c */ 	lw	$s1,0x2c($sp)
/*  f11620c:	8fb20030 */ 	lw	$s2,0x30($sp)
/*  f116210:	8fb30034 */ 	lw	$s3,0x34($sp)
/*  f116214:	8fb40038 */ 	lw	$s4,0x38($sp)
/*  f116218:	8fb5003c */ 	lw	$s5,0x3c($sp)
/*  f11621c:	8fb60040 */ 	lw	$s6,0x40($sp)
/*  f116220:	8fb70044 */ 	lw	$s7,0x44($sp)
/*  f116224:	8fbe0048 */ 	lw	$s8,0x48($sp)
/*  f116228:	03e00008 */ 	jr	$ra
/*  f11622c:	27bd20c0 */ 	addiu	$sp,$sp,0x20c0
);
#endif

const char var7f1b45e4[] = "-forceversion";

// Mismatch: callee-save registers are handled differently
//s32 pakWriteFile(s8 device, u32 offset, s32 filetype, u8 *newdata, s32 bodylenarg, s32 *outfileid, u8 *olddata, u32 fileid, u32 arg8)
//{
//	u8 header[sizeof(struct pakfileheader)]; // 20b0
//	struct pakfileheader *headerptr;
//	u32 blocksize;
//	u32 filelen;
//	u32 bodylen;
//	u8 newfile[0x1000]; // 109c
//	u8 oldfile[0x1000]; // 9c
//	u32 numblocks; // 98
//	s32 i = 0;
//	s32 j;
//	s32 k;
//	s32 index;
//	u32 result;
//	u8 version;
//	u32 paddinglen;
//	struct pakfileheader *s2;
//	struct pakfileheader *s4;
//
//	blocksize = pakGetBlockSize(device);
//
//	arg8 &= 0x1ff;
//	bodylen = bodylenarg ? bodylenarg : pakGetBodyLenByType(device, filetype);
//	filelen = pakGetAlignedFileLenByBodyLen(device, bodylen);
//
//	// Build the header bytes on the stack
//	headerptr = (struct pakfileheader *) &header;
//
//	headerptr->fileid = fileid ? fileid : ++g_Paks[device].nextfileid;
//	headerptr->deviceserial = g_Paks[device].serial;
//	headerptr->filelen = filelen;
//
//	version = argFindByPrefix(1, "-forceversion") ? 1 : 0;
//
//	headerptr->version = version;
//	headerptr->bodylen = bodylen;
//	headerptr->unk0c_21 = arg8;
//	headerptr->filetype = filetype;
//	headerptr->fileid &= 0x7f;
//
//	if (outfileid != NULL) {
//		*outfileid = headerptr->fileid;
//	}
//
//	headerptr->occupied = newdata ? 1 : 0;
//
//	if (headerptr->occupied) {
//		pakCalculateChecksum(newdata, newdata + headerptr->bodylen, headerptr->bodysum);
//	} else {
//		headerptr->bodysum[0] = 0xffff;
//		headerptr->bodysum[1] = 0xffff;
//	}
//
//	// Build "old" and "new" versions of the complete file on the stack.
//	// "old" is what is believed to be on the pak already, and "new" is what is
//	// needed to be written if any. Either the olddata or newdata pointers can
//	// be null when creating or deleting files, so substitute their bytes with
//	// a plus sign if so.
//	// These will then be compared block by block to decide which blocks need
//	// to be written to the pak.
//	s2 = (struct pakfileheader *) newfile;
//	s4 = (struct pakfileheader *) oldfile;
//	paddinglen = filelen - bodylen - sizeof(struct pakfileheader);
//	index = 0;
//
//	// Header
//	// f44
//	for (i = 0; i < sizeof(struct pakfileheader); i++) {
//		s2->bytes[index] = header[i];
//		s4->bytes[index] = '+';
//		index++;
//	}
//
//	// Data
//	// f74
//	for (i = 0; i != bodylen; i++) {
//		s2->bytes[index] = newdata ? newdata[i] : '+';
//		s4->bytes[index] = olddata ? olddata[i] : '+';
//		index++;
//	}
//
//	// Data padding to reach alignment
//	// fc4
//	for (i = 0; i != paddinglen; i++) {
//		s2->bytes[index] = newdata ? newdata[i] : '+';
//		s4->bytes[index] = olddata ? olddata[i] : '+';
//		index++;
//	}
//
//	// 008
//	numblocks = filelen / blocksize;
//
//	if (filelen % blocksize != 0) {
//		numblocks++;
//	}
//
//	joyGetLock();
//
//	// Write the header with writecompleted = 0, followed by the data, then
//	// rewrite the header with writecompleted = 1. This allows the game to
//	// detect if data on the pak was only partially written.
//	// 03c
//	for (j = 0; j < 2; j++) {
//		//struct pakfileheader *headerptr = (struct pakfileheader *) &newfile;
//		s2->writecompleted = j ? 1 : 0;
//
//		pakCalculateChecksum(&s2->bytes[0x08], &s2->bytes[0x10], s2->headersum); // sp10a4, sp10ac, s2
//
//		// 07c
//		for (i = 0; i != numblocks; i++) {
//			u32 offsetinfile = pakGetBlockSize(device) * i;
//			u32 writethisblock = false;
//
//			// 0a4
//			if (offsetinfile < sizeof(struct pakfileheader)) {
//				// Header - always write it
//				writethisblock = true;
//			} else {
//				// 0b8
//				// Don't write data on the second iteration
//				if (j == 1) {
//					break;
//				}
//
//				// 0c4, 0cc
//				// Don't write data for blank files or if the file is being deleted
//				if (s2->filetype == PAKFILETYPE_BLANK || newdata == NULL) {
//					break;
//				}
//
//				// 0d4
//				if (olddata) {
//					// Check if any bytes in the old and new blocks are different
//					for (k = 0; k < blocksize; k++) {
//						if (s2->bytes[i * blocksize + k] != s4->bytes[i * blocksize + k]) { // s2 and s4
//							writethisblock = true;
//							break;
//						}
//					}
//				} else {
//					writethisblock = true;
//				}
//			}
//
//			// 130
//			if (writethisblock) {
//				result = pakReadWriteBlock(device, PFS(device), g_Paks[device].noteindex, OS_WRITE, offset + i * blocksize, pakGetBlockSize(device), &s2->bytes[offsetinfile]);
//
//				if (!pakHandleResult(result, device, 1, 0x1286)) {
//					joyReleaseLock();
//
//					if (result == 1) {
//						return 1;
//					}
//
//					return 4;
//				}
//			}
//		}
//	}
//
//	joyReleaseLock();
//
//	if (g_PakDebugPakCache) {
//		pakSaveHeaderToCache(device, offset / pakGetBlockSize(device), s2);
//	}
//
//	return 0;
//}

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae60cnb[] = "pak.c";
const char var7f1ae614nb[] = "pak.c";
const char var7f1ae61cnb[] = "pak.c";
const char var7f1ae624nb[] = "pak.c";
const char var7f1ae62cnb[] = "pak.c";
#endif

const char var7f1b45f4[] = "PakMac_PaksLive()=%x\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b460c[] = "paksNeedToBeLive4Game=%x\n";
const char var7f1b4628[] = "paksNeedToBeLive4Menu=%x\n";
#endif

const char var7f1b4644[] = "g_LastPackPattern=%x\n";

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae664nb[] = "lvGetPause    = %s";
const char var7f1ae678nb[] = "TRUE";
const char var7f1ae680nb[] = "FALSE";
const char var7f1ae688nb[] = "MP_GetPause   = %s";
const char var7f1ae69cnb[] = "TRUE";
const char var7f1ae6a4nb[] = "FALSE";
const char var7f1ae6acnb[] = "getnumplayers = %d";
const char var7f1ae6c0nb[] = "forcecrc";
const char var7f1ae6ccnb[] = "forcescrub";
const char var7f1ae6d8nb[] = "dumph";
const char var7f1ae6e0nb[] = "pakcache";
const char var7f1ae6ecnb[] = "pakinit";
const char var7f1ae6f4nb[] = "dumpeeprom";
#endif

bool pakRepair(s8 device)
{
	s32 result;

	switch (g_Paks[device].unk010) {
	case 14:
	case 19:
		break;
	default:
#if VERSION >= VERSION_NTSC_1_0
		joyGetLock();
		result = osPfsChecker(PFS(device));
		joyReleaseLock();
#else
		joyGetLock(4425, "pak.c");
		result = osPfsChecker(PFS(device));
		joyReleaseLock(4427, "pak.c");
#endif

		if (result == PAKERROR_OK) {
			g_Paks[device].unk010 = 2;
			return true;
		}

#if VERSION >= VERSION_PAL_FINAL
		pakHandleResult(result, device, 0, 4808);
#elif VERSION >= VERSION_NTSC_FINAL
		pakHandleResult(result, device, 0, 4801);
#elif VERSION >= VERSION_NTSC_1_0
		pakHandleResult(result, device, 0, 4606);
#else
		pakHandleResult(result, device, 0, 4436);
#endif

#if VERSION >= VERSION_NTSC_1_0
		g_Paks[device].unk010 = 22;
#endif
		break;
	}

#if VERSION < VERSION_NTSC_1_0
	g_Paks[device].unk010 = 22;
#endif

	return false;
}

#if VERSION >= VERSION_NTSC_1_0
bool pakHandleResult(s32 result, s8 device, u32 arg2, u32 line)
{
	if (result == 0) {
		return true;
	}

	if (arg2) {
		switch (result) {
		case 1:
			g_Paks[device].unk000 = 2;
			g_Paks[device].unk010 = 1;
			break;
		case 11:
			g_Paks[device].unk000 = 2;
			g_Paks[device].unk010 = 14;
			break;
		case 3:
		case 10:
			g_Paks[device].unk000 = 2;
			g_Paks[device].unk010 = 15;
			break;
		case 7:
		case 8:
			g_Paks[device].unk000 = 2;
			g_Paks[device].unk010 = 16;
			break;
		}
	}

	switch (result) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
	case 0x84:
		break;
	}

	return false;
}
#else
GLOBAL_ASM(
glabel pakHandleResult
.late_rodata
glabel var7f1af094nb
.word pakHandleResult+0x080
glabel var7f1af098nb
.word pakHandleResult+0x0f4
glabel var7f1af09cnb
.word pakHandleResult+0x0f4
glabel var7f1af0a0nb
.word pakHandleResult+0x0f4
glabel var7f1af0a4nb
.word pakHandleResult+0x0bc
glabel var7f1af0a8nb
.word pakHandleResult+0x0bc
glabel var7f1af0acnb
.word pakHandleResult+0x0f4
glabel var7f1af0b0nb
.word pakHandleResult+0x080
glabel var7f1af0b4nb
.word pakHandleResult+0x044
glabel var7f1af0b8nb
.word pakHandleResult+0x148
glabel var7f1af0bcnb
.word pakHandleResult+0x148
glabel var7f1af0c0nb
.word pakHandleResult+0x148
glabel var7f1af0c4nb
.word pakHandleResult+0x148
glabel var7f1af0c8nb
.word pakHandleResult+0x148
glabel var7f1af0ccnb
.word pakHandleResult+0x148
glabel var7f1af0d0nb
.word pakHandleResult+0x148
glabel var7f1af0d4nb
.word pakHandleResult+0x148
glabel var7f1af0d8nb
.word pakHandleResult+0x148
glabel var7f1af0dcnb
.word pakHandleResult+0x148
glabel var7f1af0e0nb
.word pakHandleResult+0x148
glabel var7f1af0e4nb
.word pakHandleResult+0x148
glabel var7f1af0e8nb
.word pakHandleResult+0x148
glabel var7f1af0ecnb
.word pakHandleResult+0x148
glabel var7f1af0f0nb
.word pakHandleResult+0x148
glabel var7f1af0f4nb
.word pakHandleResult+0x148
glabel var7f1af0f8nb
.word pakHandleResult+0x148
glabel var7f1af0fcnb
.word pakHandleResult+0x148
.text
/*  f11632c:	afa50004 */ 	sw	$a1,0x4($sp)
/*  f116330:	00057600 */ 	sll	$t6,$a1,0x18
/*  f116334:	000e2e03 */ 	sra	$a1,$t6,0x18
/*  f116338:	14800003 */ 	bnez	$a0,.NB0f116348
/*  f11633c:	afa7000c */ 	sw	$a3,0xc($sp)
/*  f116340:	03e00008 */ 	jr	$ra
/*  f116344:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f116348:
/*  f116348:	10c00035 */ 	beqz	$a2,.NB0f116420
/*  f11634c:	2498fffd */ 	addiu	$t8,$a0,-3
/*  f116350:	2f010009 */ 	sltiu	$at,$t8,0x9
/*  f116354:	10200032 */ 	beqz	$at,.NB0f116420
/*  f116358:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11635c:	3c017f1b */ 	lui	$at,0x7f1b
/*  f116360:	00380821 */ 	addu	$at,$at,$t8
/*  f116364:	8c38f094 */ 	lw	$t8,-0xf6c($at)
/*  f116368:	03000008 */ 	jr	$t8
/*  f11636c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116370:	0005c880 */ 	sll	$t9,$a1,0x2
/*  f116374:	0325c823 */ 	subu	$t9,$t9,$a1
/*  f116378:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11637c:	0325c823 */ 	subu	$t9,$t9,$a1
/*  f116380:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f116384:	0325c821 */ 	addu	$t9,$t9,$a1
/*  f116388:	3c08800a */ 	lui	$t0,0x800a
/*  f11638c:	25086870 */ 	addiu	$t0,$t0,0x6870
/*  f116390:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f116394:	03281021 */ 	addu	$v0,$t9,$t0
/*  f116398:	24090002 */ 	addiu	$t1,$zero,0x2
/*  f11639c:	240a000e */ 	addiu	$t2,$zero,0xe
/*  f1163a0:	ac490000 */ 	sw	$t1,0x0($v0)
/*  f1163a4:	1000001e */ 	beqz	$zero,.NB0f116420
/*  f1163a8:	ac4a0010 */ 	sw	$t2,0x10($v0)
/*  f1163ac:	00055880 */ 	sll	$t3,$a1,0x2
/*  f1163b0:	01655823 */ 	subu	$t3,$t3,$a1
/*  f1163b4:	000b5880 */ 	sll	$t3,$t3,0x2
/*  f1163b8:	01655823 */ 	subu	$t3,$t3,$a1
/*  f1163bc:	000b58c0 */ 	sll	$t3,$t3,0x3
/*  f1163c0:	01655821 */ 	addu	$t3,$t3,$a1
/*  f1163c4:	3c0c800a */ 	lui	$t4,0x800a
/*  f1163c8:	258c6870 */ 	addiu	$t4,$t4,0x6870
/*  f1163cc:	000b58c0 */ 	sll	$t3,$t3,0x3
/*  f1163d0:	016c1021 */ 	addu	$v0,$t3,$t4
/*  f1163d4:	240d0002 */ 	addiu	$t5,$zero,0x2
/*  f1163d8:	240e000f */ 	addiu	$t6,$zero,0xf
/*  f1163dc:	ac4d0000 */ 	sw	$t5,0x0($v0)
/*  f1163e0:	1000000f */ 	beqz	$zero,.NB0f116420
/*  f1163e4:	ac4e0010 */ 	sw	$t6,0x10($v0)
/*  f1163e8:	00057880 */ 	sll	$t7,$a1,0x2
/*  f1163ec:	01e57823 */ 	subu	$t7,$t7,$a1
/*  f1163f0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f1163f4:	01e57823 */ 	subu	$t7,$t7,$a1
/*  f1163f8:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1163fc:	01e57821 */ 	addu	$t7,$t7,$a1
/*  f116400:	3c18800a */ 	lui	$t8,0x800a
/*  f116404:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f116408:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f11640c:	01f81021 */ 	addu	$v0,$t7,$t8
/*  f116410:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f116414:	24080010 */ 	addiu	$t0,$zero,0x10
/*  f116418:	ac590000 */ 	sw	$t9,0x0($v0)
/*  f11641c:	ac480010 */ 	sw	$t0,0x10($v0)
.NB0f116420:
/*  f116420:	2881000e */ 	slti	$at,$a0,0xe
/*  f116424:	1420000a */ 	bnez	$at,.NB0f116450
/*  f116428:	00801025 */ 	or	$v0,$a0,$zero
/*  f11642c:	2449ff80 */ 	addiu	$t1,$v0,-128
/*  f116430:	2d210005 */ 	sltiu	$at,$t1,0x5
/*  f116434:	1020000f */ 	beqz	$at,.NB0f116474
/*  f116438:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11643c:	3c017f1b */ 	lui	$at,0x7f1b
/*  f116440:	00290821 */ 	addu	$at,$at,$t1
/*  f116444:	8c29f0b8 */ 	lw	$t1,-0xf48($at)
/*  f116448:	01200008 */ 	jr	$t1
/*  f11644c:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f116450:
/*  f116450:	244affff */ 	addiu	$t2,$v0,-1
/*  f116454:	2d41000d */ 	sltiu	$at,$t2,0xd
/*  f116458:	10200006 */ 	beqz	$at,.NB0f116474
/*  f11645c:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f116460:	3c017f1b */ 	lui	$at,0x7f1b
/*  f116464:	002a0821 */ 	addu	$at,$at,$t2
/*  f116468:	8c2af0cc */ 	lw	$t2,-0xf34($at)
/*  f11646c:	01400008 */ 	jr	$t2
/*  f116470:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f116474:
/*  f116474:	00001025 */ 	or	$v0,$zero,$zero
/*  f116478:	03e00008 */ 	jr	$ra
/*  f11647c:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f11c54c(void)
{
	s32 i;

	if (g_Vars.unk0004e4) {
		g_MpPlayerNum = 0;

		func0f0f0ca0(7, true);

		var80075d14 = 0;

		if (g_Vars.unk0004e4 & 0x0f) {
			joy00013974(0);

			// Waiting for some timer
			if ((g_Vars.unk0004e4 & 0x0f) > 9) {
				if ((g_Vars.lvframenum % 7) == 0) {
					g_Vars.unk0004e4--;
				}
			} else {
				g_Vars.unk0004e4--;
			}
		} else {
			joyCheckPfs(2);

			for (i = 0; i < 4; i++) {
				if (g_Vars.unk0004e4 & (1 << (i + 4))) {
					pak0f1169c8(i, true);
					g_Vars.unk0004e4 &= ~(1 << (i + 4));
				} else if (g_Vars.unk0004e4 & (1 << (i + 8))) {
					pak0f1169c8(i, false);
					g_Vars.unk0004e4 &= ~(1 << (i + 8));
				}
			}

			if (!joy00013980()) {
				joy00013974(1);
				joy000139c8();
			}

			func0f0f0ca0(-1, true);

			var80075d14 = 1;
		}
	}
}
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f11c6d0(void)
{
	s32 i;

	for (i = 0; i < 4; i++) {
		switch (g_Paks[i].unk010) {
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			g_Paks[i].unk010 = 1;
			var80075d10 &= ~(1 << i);
			g_MpPlayerNum = i;
			func0f0f0ca0(-1, true);
			break;
		}
	}

	var8005eedc = 1;
}
#endif

#if VERSION >= VERSION_NTSC_1_0
void pakExecuteDebugOperations(void)
{
	static u32 g_PakDebugDumpEeprom = 0;
	s32 pass = false;
	s8 i;

	func0000db30("forcescrub", &g_PakDebugForceScrub);
	func0000db30("pakdump", &g_PakDebugPakDump);
	func0000db30("pakcache", &g_PakDebugPakCache);
	func0000db30("pakinit", &g_PakDebugPakInit);
	func0000db30("corruptme", &g_PakDebugCorruptMe);
	func0000db30("wipeeeprom", &g_PakDebugWipeEeprom);
	func0000db30("dumpeeprom", &g_PakDebugDumpEeprom);

	if (g_PakDebugCorruptMe) {
		g_PakDebugCorruptMe = false;
		pakCorrupt();
	}

	if (g_PakDebugPakDump) {
		pakDumpPak();
		g_PakDebugPakDump = false;
	}

	if (g_PakDebugDumpEeprom) {
		g_PakDebugDumpEeprom = false;
		pakDumpEeprom();
	}

	if (g_PakDebugWipeEeprom) {
		pakWipe(SAVEDEVICE_GAMEPAK, 0, 0x80);
		g_PakDebugWipeEeprom = false;
	}

	if (g_PakDebugPakInit) {
		s32 device = g_PakDebugPakInit - 1;
		joyGetLock();

		pakInitPak(&var80099e78, PFS(device), device, 0);
		joyReleaseLock();
		g_PakDebugPakInit = false;
	}

	if (g_PakDebugForceScrub) {
		pakScrub(SAVEDEVICE_GAMEPAK);
		g_PakDebugForceScrub = false;
	}

	pak0f11ca30();

	for (i = 0; i < 5; i++) {
		if (g_Paks[i].unk014) {
			pak0f11df94(i);
		}
	}

	for (i = 0; i < 5; i++) {
		switch (g_Paks[i].unk010) {
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			pass = true;
			break;
		}
	}

	if (pass) {
		var8005eedc = false;
	} else {
		var8005eedc = true;
	}
}
#else
GLOBAL_ASM(
glabel pakExecuteDebugOperations
/*  f116480:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f116484:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f116488:	0fc59ed0 */ 	jal	lvIsPaused
/*  f11648c:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f116490:	0fc60e8c */ 	jal	mpIsPaused
/*  f116494:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116498:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f11649c:	3c058008 */ 	lui	$a1,0x8008
/*  f1164a0:	24a580a4 */ 	addiu	$a1,$a1,-32604
/*  f1164a4:	0c00381c */ 	jal	func0000db30
/*  f1164a8:	2484e6c0 */ 	addiu	$a0,$a0,-6464
/*  f1164ac:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f1164b0:	3c058008 */ 	lui	$a1,0x8008
/*  f1164b4:	24a580a8 */ 	addiu	$a1,$a1,-32600
/*  f1164b8:	0c00381c */ 	jal	func0000db30
/*  f1164bc:	2484e6cc */ 	addiu	$a0,$a0,-6452
/*  f1164c0:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f1164c4:	3c058008 */ 	lui	$a1,0x8008
/*  f1164c8:	24a580ac */ 	addiu	$a1,$a1,-32596
/*  f1164cc:	0c00381c */ 	jal	func0000db30
/*  f1164d0:	2484e6d8 */ 	addiu	$a0,$a0,-6440
/*  f1164d4:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f1164d8:	3c058008 */ 	lui	$a1,0x8008
/*  f1164dc:	24a580b0 */ 	addiu	$a1,$a1,-32592
/*  f1164e0:	0c00381c */ 	jal	func0000db30
/*  f1164e4:	2484e6e0 */ 	addiu	$a0,$a0,-6432
/*  f1164e8:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f1164ec:	3c058008 */ 	lui	$a1,0x8008
/*  f1164f0:	24a580b4 */ 	addiu	$a1,$a1,-32588
/*  f1164f4:	0c00381c */ 	jal	func0000db30
/*  f1164f8:	2484e6ec */ 	addiu	$a0,$a0,-6420
/*  f1164fc:	3c047f1b */ 	lui	$a0,0x7f1b
/*  f116500:	3c058008 */ 	lui	$a1,0x8008
/*  f116504:	24a58110 */ 	addiu	$a1,$a1,-32496
/*  f116508:	0c00381c */ 	jal	func0000db30
/*  f11650c:	2484e6f4 */ 	addiu	$a0,$a0,-6412
/*  f116510:	3c0e8008 */ 	lui	$t6,0x8008
/*  f116514:	8dce8110 */ 	lw	$t6,-0x7ef0($t6)
/*  f116518:	3c018008 */ 	lui	$at,0x8008
/*  f11651c:	11c00003 */ 	beqz	$t6,.NB0f11652c
/*  f116520:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116524:	0fc446e7 */ 	jal	pakDumpEeprom
/*  f116528:	ac208110 */ 	sw	$zero,-0x7ef0($at)
.NB0f11652c:
/*  f11652c:	3c028008 */ 	lui	$v0,0x8008
/*  f116530:	8c4280b4 */ 	lw	$v0,-0x7f4c($v0)
/*  f116534:	240411ce */ 	addiu	$a0,$zero,0x11ce
/*  f116538:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f11653c:	10400019 */ 	beqz	$v0,.NB0f1165a4
/*  f116540:	2450ffff */ 	addiu	$s0,$v0,-1
/*  f116544:	0c00581b */ 	jal	joyGetLock
/*  f116548:	24a5e700 */ 	addiu	$a1,$a1,-6400
/*  f11654c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f116550:	16010003 */ 	bne	$s0,$at,.NB0f116560
/*  f116554:	3c04800a */ 	lui	$a0,0x800a
/*  f116558:	10000009 */ 	beqz	$zero,.NB0f116580
/*  f11655c:	00002825 */ 	or	$a1,$zero,$zero
.NB0f116560:
/*  f116560:	00107880 */ 	sll	$t7,$s0,0x2
/*  f116564:	01f07823 */ 	subu	$t7,$t7,$s0
/*  f116568:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11656c:	01f07821 */ 	addu	$t7,$t7,$s0
/*  f116570:	3c18800a */ 	lui	$t8,0x800a
/*  f116574:	27187658 */ 	addiu	$t8,$t8,0x7658
/*  f116578:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f11657c:	01f82821 */ 	addu	$a1,$t7,$t8
.NB0f116580:
/*  f116580:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f116584:	0fc4478e */ 	jal	pakInitPak
/*  f116588:	02003025 */ 	or	$a2,$s0,$zero
/*  f11658c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f116590:	24a5e708 */ 	addiu	$a1,$a1,-6392
/*  f116594:	0c005834 */ 	jal	joyReleaseLock
/*  f116598:	240411d0 */ 	addiu	$a0,$zero,0x11d0
/*  f11659c:	3c018008 */ 	lui	$at,0x8008
/*  f1165a0:	ac2080b4 */ 	sw	$zero,-0x7f4c($at)
.NB0f1165a4:
/*  f1165a4:	3c198008 */ 	lui	$t9,0x8008
/*  f1165a8:	8f3980a4 */ 	lw	$t9,-0x7f5c($t9)
/*  f1165ac:	24040004 */ 	addiu	$a0,$zero,0x4
/*  f1165b0:	2405004d */ 	addiu	$a1,$zero,0x4d
/*  f1165b4:	13200005 */ 	beqz	$t9,.NB0f1165cc
/*  f1165b8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1165bc:	0fc453e4 */ 	jal	pakWipe
/*  f1165c0:	2406004e */ 	addiu	$a2,$zero,0x4e
/*  f1165c4:	3c018008 */ 	lui	$at,0x8008
/*  f1165c8:	ac2080a4 */ 	sw	$zero,-0x7f5c($at)
.NB0f1165cc:
/*  f1165cc:	3c088008 */ 	lui	$t0,0x8008
/*  f1165d0:	8d0880a8 */ 	lw	$t0,-0x7f58($t0)
/*  f1165d4:	11000005 */ 	beqz	$t0,.NB0f1165ec
/*  f1165d8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1165dc:	0fc45544 */ 	jal	pakScrub
/*  f1165e0:	24040004 */ 	addiu	$a0,$zero,0x4
/*  f1165e4:	3c018008 */ 	lui	$at,0x8008
/*  f1165e8:	ac2080a8 */ 	sw	$zero,-0x7f58($at)
.NB0f1165ec:
/*  f1165ec:	0fc4599b */ 	jal	pak0f11ca30
/*  f1165f0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1165f4:	0fc45ede */ 	jal	pakDumpPak
/*  f1165f8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1165fc:	00008025 */ 	or	$s0,$zero,$zero
/*  f116600:	00104880 */ 	sll	$t1,$s0,0x2
.NB0f116604:
/*  f116604:	01304823 */ 	subu	$t1,$t1,$s0
/*  f116608:	00094880 */ 	sll	$t1,$t1,0x2
/*  f11660c:	01304823 */ 	subu	$t1,$t1,$s0
/*  f116610:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f116614:	01304821 */ 	addu	$t1,$t1,$s0
/*  f116618:	000948c0 */ 	sll	$t1,$t1,0x3
/*  f11661c:	3c0a800a */ 	lui	$t2,0x800a
/*  f116620:	01495021 */ 	addu	$t2,$t2,$t1
/*  f116624:	914a6884 */ 	lbu	$t2,0x6884($t2)
/*  f116628:	00102600 */ 	sll	$a0,$s0,0x18
/*  f11662c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f116630:	51400004 */ 	beqzl	$t2,.NB0f116644
/*  f116634:	26100001 */ 	addiu	$s0,$s0,0x1
/*  f116638:	0fc45ee0 */ 	jal	pak0f11df94
/*  f11663c:	01602025 */ 	or	$a0,$t3,$zero
/*  f116640:	26100001 */ 	addiu	$s0,$s0,0x1
.NB0f116644:
/*  f116644:	00106600 */ 	sll	$t4,$s0,0x18
/*  f116648:	000c8603 */ 	sra	$s0,$t4,0x18
/*  f11664c:	2a010005 */ 	slti	$at,$s0,0x5
/*  f116650:	5420ffec */ 	bnezl	$at,.NB0f116604
/*  f116654:	00104880 */ 	sll	$t1,$s0,0x2
/*  f116658:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11665c:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f116660:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f116664:	03e00008 */ 	jr	$ra
/*  f116668:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

const char var7f1b46a8[] = "\nOS_GBPAK_GBCART_ON       - ";
const char var7f1b46c8[] = "\nOS_GBPAK_GBCART_PULL     - ";
const char var7f1b46e8[] = "\nOS_GBPAK_POWER           - ";
const char var7f1b4708[] = "\nOS_GBPAK_RSTB_DETECTION  - ";
const char var7f1b4728[] = "Pak -> Dumping contents of Game Boy Pack(TM) Id file";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4760[] = "(u16) - Fixed1             - %d\n";
const char var7f1b4784[] = "(u16) - StartAddress       - %d\n";
const char var7f1b47a8[] = "(u8*) - Nintendo chr data  - %s\n";
const char var7f1b47cc[] = "(u8*) - Game Title         - %s\n";
const char var7f1b47f0[] = "(u16) - Company Code       - %d\n";
const char var7f1b4814[] = "(u8 ) - Body Code          - %d\n";
const char var7f1b4838[] = "(u8 ) - Rom Size           - %d\n";
const char var7f1b485c[] = "(u8 ) - Ram Size           - %d\n";
const char var7f1b4880[] = "(u8 ) - country_code       - %d\n";
const char var7f1b48a4[] = "(u8 ) - Fixed 2 (0x33)     - %d\n";
const char var7f1b48c8[] = "(u8 ) - Version Number     - %d\n";
const char var7f1b48ec[] = "(u8 ) - isum               - %d\n";
const char var7f1b4910[] = "(u16) - sum                - %d\n";
#else
const char var7f1b4760[] = "(u16) - Fixed1             - %d";
const char var7f1b4784[] = "(u16) - StartAddress       - %d";
const char var7f1b47a8[] = "(u8*) - Nintendo chr data  - %s";
const char var7f1b47cc[] = "(u8*) - Game Title         - %s";
const char var7f1b47f0[] = "(u16) - Company Code       - %d";
const char var7f1b4814[] = "(u8 ) - Body Code          - %d";
const char var7f1b4838[] = "(u8 ) - Rom Size           - %d";
const char var7f1b485c[] = "(u8 ) - Ram Size           - %d";
const char var7f1b4880[] = "(u8 ) - country_code       - %d";
const char var7f1b48a4[] = "(u8 ) - Fixed 2 (0x33)     - %d";
const char var7f1b48c8[] = "(u8 ) - Version Number     - %d";
const char var7f1b48ec[] = "(u8 ) - isum               - %d";
const char var7f1b4910[] = "(u16) - sum                - %d";
#endif

const char var7f1b4934[] = "Pak -> Finished Dump";

#if VERSION < VERSION_NTSC_1_0
const char var7f1ae980nb[] = "Pak -> Game Boy Pack reset was sucessful\n";
const char var7f1ae9acnb[] = "Pak -> Connector Check Failed";
const char var7f1ae9ccnb[] = "Pak -> osGbpakReadId - Failed";
#endif

const u32 var7f1b494c[] = {0x43000000};
const u32 var7f1b4950[] = {0x43140000};
const u32 var7f1b4954[] = {0x435c0000};
const u32 var7f1b4958[] = {0x437f0000};
const u32 var7f1b495c[] = {0x43020000};
const u32 var7f1b4960[] = {0x43150000};
const u32 var7f1b4964[] = {0x43520000};
const u32 var7f1b4968[] = {0x437f0000};
const u32 var7f1b496c[] = {0x43040000};
const u32 var7f1b4970[] = {0x43160000};
const u32 var7f1b4974[] = {0x434a0000};
const u32 var7f1b4978[] = {0x437f0000};
const u32 var7f1b497c[] = {0x43060000};
const u32 var7f1b4980[] = {0x43160000};
const u32 var7f1b4984[] = {0x43440000};
const u32 var7f1b4988[] = {0x437f0000};
const u32 var7f1b498c[] = {0x43080000};
const u32 var7f1b4990[] = {0x43170000};
const u32 var7f1b4994[] = {0x433e0000};
const u32 var7f1b4998[] = {0x437f0000};
const u32 var7f1b499c[] = {0x430a0000};
const u32 var7f1b49a0[] = {0x43170000};
const u32 var7f1b49a4[] = {0x43380000};
const u32 var7f1b49a8[] = {0x437f0000};
const u32 var7f1b49ac[] = {0x430b0000};
const u32 var7f1b49b0[] = {0x43180000};
const u32 var7f1b49b4[] = {0x43320000};
const u32 var7f1b49b8[] = {0x43750000};
const u32 var7f1b49bc[] = {0x430c0000};
const u32 var7f1b49c0[] = {0x43180000};
const u32 var7f1b49c4[] = {0x432c0000};
const u32 var7f1b49c8[] = {0x436b0000};
const u32 var7f1b49cc[] = {0x430d0000};
const u32 var7f1b49d0[] = {0x43180000};
const u32 var7f1b49d4[] = {0x432a0000};
const u32 var7f1b49d8[] = {0x435d0000};
const u32 var7f1b49dc[] = {0x430e0000};
const u32 var7f1b49e0[] = {0x43180000};
const u32 var7f1b49e4[] = {0x43280000};
const u32 var7f1b49e8[] = {0x43500000};
const u32 var7f1b49ec[] = {0x430f0000};
const u32 var7f1b49f0[] = {0x43180000};
const u32 var7f1b49f4[] = {0x43260000};
const u32 var7f1b49f8[] = {0x43440000};
const u32 var7f1b49fc[] = {0x43100000};
const u32 var7f1b4a00[] = {0x43180000};
const u32 var7f1b4a04[] = {0x43240000};
const u32 var7f1b4a08[] = {0x433a0000};
const u32 var7f1b4a0c[] = {0x43120000};
const u32 var7f1b4a10[] = {0x43180000};
const u32 var7f1b4a14[] = {0x43210000};
const u32 var7f1b4a18[] = {0x43310000};
const u32 var7f1b4a1c[] = {0x43140000};
const u32 var7f1b4a20[] = {0x43180000};
const u32 var7f1b4a24[] = {0x431d0000};
const u32 var7f1b4a28[] = {0x43280000};
const u32 var7f1b4a2c[] = {0x43160000};
const u32 var7f1b4a30[] = {0x43180000};
const u32 var7f1b4a34[] = {0x43190000};
const u32 var7f1b4a38[] = {0x43200000};
const u32 var7f1b4a3c[] = {0x43180000};
const u32 var7f1b4a40[] = {0x43180000};
const u32 var7f1b4a44[] = {0x43180000};
const u32 var7f1b4a48[] = {0x43180000};
const u32 var7f1b4a4c[] = {0x00000000};
const u32 var7f1b4a50[] = {0x00000000};
const u32 var7f1b4a54[] = {0x00000000};
const u32 var7f1b4a58[] = {0x00000000};
const u32 var7f1b4a5c[] = {0x00000000};
const u32 var7f1b4a60[] = {0x41000000};
const u32 var7f1b4a64[] = {0x40000000};
const u32 var7f1b4a68[] = {0x41200000};
const u32 var7f1b4a6c[] = {0x41400000};
const u32 var7f1b4a70[] = {0x40800000};
const u32 var7f1b4a74[] = {0x41600000};
const u32 var7f1b4a78[] = {0x40c00000};
const u32 var7f1b4a7c[] = {0x40400000};
const u32 var7f1b4a80[] = {0x41300000};
const u32 var7f1b4a84[] = {0x3f800000};
const u32 var7f1b4a88[] = {0x41100000};
const u32 var7f1b4a8c[] = {0x41700000};
const u32 var7f1b4a90[] = {0x40e00000};
const u32 var7f1b4a94[] = {0x41500000};
const u32 var7f1b4a98[] = {0x40a00000};
const u32 var7f1b4a9c[] = {0x00000000};
const u32 var7f1b4aa0[] = {0x41600000};
const u32 var7f1b4aa4[] = {0x40400000};
const u32 var7f1b4aa8[] = {0x41500000};
const u32 var7f1b4aac[] = {0x41300000};
const u32 var7f1b4ab0[] = {0x40a00000};
const u32 var7f1b4ab4[] = {0x41000000};
const u32 var7f1b4ab8[] = {0x40c00000};
const u32 var7f1b4abc[] = {0x41400000};
const u32 var7f1b4ac0[] = {0x40000000};
const u32 var7f1b4ac4[] = {0x41700000};
const u32 var7f1b4ac8[] = {0x3f800000};
const u32 var7f1b4acc[] = {0x40e00000};
const u32 var7f1b4ad0[] = {0x41100000};
const u32 var7f1b4ad4[] = {0x40800000};
const u32 var7f1b4ad8[] = {0x41200000};

const char var7f1b4adc[] = "Pak_StartCapture -> Failed - Code = %d\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4b04[] = "Pak_DownloadNextBlockToPackBuffer : eQuality=ekCapQualityHeader, BufferNum=%d\n";
#else
const char var7f1b4b04[] = "Pak_DownloadNextBlockToPackBuffer : eQuality=ekCapQualityHeader, BufferNum=%d";
#endif

const char var7f1b4b54[] = "Pak : Doing Frame - Top    = %d\n";
const char var7f1b4b78[] = "Pak : Doing Frame - Height = %d\n";
const char var7f1b4b9c[] = "Pak : Doing Frame - Bottom = %d\n";

#if VERSION == VERSION_NTSC_1_0
const char var7f1b4b9c_2[] = "Pak %d - PakDamage_UjiWipedMyAss\n";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1aec60nb[] = "Pak %d -> ekPakInitStatusInitMemoryPak1\n";
const char var7f1aec8cnb[] = "Pak %d -> ekPakInitStatusInitMemoryPak2\n";
const char var7f1aecb8nb[] = "Pak %d -> ekPakInitStatusInitMemoryPak3\n";
const char var7f1aece4nb[] = "pak.c";
const char var7f1aececnb[] = "pak.c";
const char var7f1aecf4nb[] = "pak.c";
const char var7f1aecfcnb[] = "pak.c";
#endif

const char var7f1b4bc0[] = "Pak %d - ekPakInitStatusError_CorruptedPak\n";

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4bec[] = "Pak %d - ekPakInitStatusInitGameBoy_PDGB_Check_Error\n";
#endif

const char var7f1b4c24[] = "Pak %d - ekPakInitStatusError_DamagedPak\n";
const char var7f1b4c50[] = "Pak %d - ekPakInitStatusError_StuffedPak\n";
const char var7f1b4c7c[] = "Pak %d - ekPakInitStatusError_StuffedAndCheckedPak\n";
const char var7f1b4cb0[] = "Pak %d - ekPakInitStatusVoid\n";
const char var7f1b4cd0[] = "Pak %d -> Unhandled Init Status - %d\n";

#if VERSION >= VERSION_NTSC_FINAL
const char var7f1b4cf8[] = "Pak %d - PakDamage_UjiWipedMyAss\n";
#endif

#if VERSION < VERSION_NTSC_1_0
const char var7f1aee04nb[] = "Pak_EEPROM_Init";
#endif

u32 var80075d58 = 0x00000000;
u32 var80075d5c = 0x00000000;

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11ca30
/*  f11ca30:	27bdffc0 */ 	addiu	$sp,$sp,-64
/*  f11ca34:	afb5002c */ 	sw	$s5,0x2c($sp)
/*  f11ca38:	3c15800a */ 	lui	$s5,%hi(g_Vars)
/*  f11ca3c:	26b59fc0 */ 	addiu	$s5,$s5,%lo(g_Vars)
/*  f11ca40:	8eae02ac */ 	lw	$t6,0x2ac($s5)
/*  f11ca44:	24010006 */ 	addiu	$at,$zero,0x6
/*  f11ca48:	afbf003c */ 	sw	$ra,0x3c($sp)
/*  f11ca4c:	afbe0038 */ 	sw	$s8,0x38($sp)
/*  f11ca50:	afb70034 */ 	sw	$s7,0x34($sp)
/*  f11ca54:	afb60030 */ 	sw	$s6,0x30($sp)
/*  f11ca58:	afb40028 */ 	sw	$s4,0x28($sp)
/*  f11ca5c:	afb30024 */ 	sw	$s3,0x24($sp)
/*  f11ca60:	afb20020 */ 	sw	$s2,0x20($sp)
/*  f11ca64:	afb1001c */ 	sw	$s1,0x1c($sp)
/*  f11ca68:	15c10005 */ 	bne	$t6,$at,.L0f11ca80
/*  f11ca6c:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11ca70:	3c0f800a */ 	lui	$t7,%hi(g_MenuData)
/*  f11ca74:	8def19c0 */ 	lw	$t7,%lo(g_MenuData)($t7)
/*  f11ca78:	59e0003d */ 	blezl	$t7,.L0f11cb70
/*  f11ca7c:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11ca80:
/*  f11ca80:	96b804e4 */ 	lhu	$t8,0x4e4($s5)
/*  f11ca84:	3c028007 */ 	lui	$v0,%hi(var80075d10)
/*  f11ca88:	90565d10 */ 	lbu	$s6,%lo(var80075d10)($v0)
/*  f11ca8c:	3319000f */ 	andi	$t9,$t8,0xf
/*  f11ca90:	241400ff */ 	addiu	$s4,$zero,0xff
/*  f11ca94:	17200035 */ 	bnez	$t9,.L0f11cb6c
/*  f11ca98:	02c09825 */ 	or	$s3,$s6,$zero
/*  f11ca9c:	00008825 */ 	or	$s1,$zero,$zero
/*  f11caa0:	241e0002 */ 	addiu	$s8,$zero,0x2
/*  f11caa4:	241700ff */ 	addiu	$s7,$zero,0xff
/*  f11caa8:	92a904d1 */ 	lbu	$t1,0x4d1($s5)
.L0f11caac:
/*  f11caac:	92aa04d0 */ 	lbu	$t2,0x4d0($s5)
/*  f11cab0:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f11cab4:	02288004 */ 	sllv	$s0,$t0,$s1
/*  f11cab8:	012a5825 */ 	or	$t3,$t1,$t2
/*  f11cabc:	01706024 */ 	and	$t4,$t3,$s0
/*  f11cac0:	11800024 */ 	beqz	$t4,.L0f11cb54
/*  f11cac4:	02801825 */ 	or	$v1,$s4,$zero
/*  f11cac8:	16f40005 */ 	bne	$s7,$s4,.L0f11cae0
/*  f11cacc:	02c09025 */ 	or	$s2,$s6,$zero
/*  f11cad0:	0c004e7a */ 	jal	joyShiftPfsStates
/*  f11cad4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11cad8:	305400ff */ 	andi	$s4,$v0,0xff
/*  f11cadc:	02801825 */ 	or	$v1,$s4,$zero
.L0f11cae0:
/*  f11cae0:	00701024 */ 	and	$v0,$v1,$s0
/*  f11cae4:	02506824 */ 	and	$t5,$s2,$s0
/*  f11cae8:	11a2001a */ 	beq	$t5,$v0,.L0f11cb54
/*  f11caec:	00117080 */ 	sll	$t6,$s1,0x2
/*  f11caf0:	01d17023 */ 	subu	$t6,$t6,$s1
/*  f11caf4:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11caf8:	01d17023 */ 	subu	$t6,$t6,$s1
/*  f11cafc:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11cb00:	01d17021 */ 	addu	$t6,$t6,$s1
/*  f11cb04:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11cb08:	01d17023 */ 	subu	$t6,$t6,$s1
/*  f11cb0c:	3c0f800a */ 	lui	$t7,%hi(g_Paks)
/*  f11cb10:	25ef2380 */ 	addiu	$t7,$t7,%lo(g_Paks)
/*  f11cb14:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f11cb18:	10400006 */ 	beqz	$v0,.L0f11cb34
/*  f11cb1c:	01cf1821 */ 	addu	$v1,$t6,$t7
/*  f11cb20:	02709825 */ 	or	$s3,$s3,$s0
/*  f11cb24:	327800ff */ 	andi	$t8,$s3,0xff
/*  f11cb28:	ac7e0010 */ 	sw	$s8,0x10($v1)
/*  f11cb2c:	10000009 */ 	beqz	$zero,.L0f11cb54
/*  f11cb30:	03009825 */ 	or	$s3,$t8,$zero
.L0f11cb34:
/*  f11cb34:	02004027 */ 	nor	$t0,$s0,$zero
/*  f11cb38:	02689824 */ 	and	$s3,$s3,$t0
/*  f11cb3c:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f11cb40:	326900ff */ 	andi	$t1,$s3,0xff
/*  f11cb44:	ac790010 */ 	sw	$t9,0x10($v1)
/*  f11cb48:	01209825 */ 	or	$s3,$t1,$zero
/*  f11cb4c:	0fc44364 */ 	jal	func0f110d90
/*  f11cb50:	02202025 */ 	or	$a0,$s1,$zero
.L0f11cb54:
/*  f11cb54:	26310001 */ 	addiu	$s1,$s1,0x1
/*  f11cb58:	24010005 */ 	addiu	$at,$zero,0x5
/*  f11cb5c:	5621ffd3 */ 	bnel	$s1,$at,.L0f11caac
/*  f11cb60:	92a904d1 */ 	lbu	$t1,0x4d1($s5)
/*  f11cb64:	3c018007 */ 	lui	$at,%hi(var80075d10)
/*  f11cb68:	a0335d10 */ 	sb	$s3,%lo(var80075d10)($at)
.L0f11cb6c:
/*  f11cb6c:	8fbf003c */ 	lw	$ra,0x3c($sp)
.L0f11cb70:
/*  f11cb70:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11cb74:	8fb1001c */ 	lw	$s1,0x1c($sp)
/*  f11cb78:	8fb20020 */ 	lw	$s2,0x20($sp)
/*  f11cb7c:	8fb30024 */ 	lw	$s3,0x24($sp)
/*  f11cb80:	8fb40028 */ 	lw	$s4,0x28($sp)
/*  f11cb84:	8fb5002c */ 	lw	$s5,0x2c($sp)
/*  f11cb88:	8fb60030 */ 	lw	$s6,0x30($sp)
/*  f11cb8c:	8fb70034 */ 	lw	$s7,0x34($sp)
/*  f11cb90:	8fbe0038 */ 	lw	$s8,0x38($sp)
/*  f11cb94:	03e00008 */ 	jr	$ra
/*  f11cb98:	27bd0040 */ 	addiu	$sp,$sp,0x40
);

// Mismatch: regalloc
//void pak0f11ca30(void)
//{
//	if (g_Vars.tickmode != TICKMODE_CUTSCENE || g_MenuData.count > 0) {
//		u8 oldvalue = var80075d10;
//		u8 newvalue = var80075d10;
//		u8 thing = 0xff;
//		s32 i;
//
//		if ((g_Vars.unk0004e4 & 0xf) == 0) {
//			for (i = 0; i < 5; i++) {
//				u32 thisbit = 1 << i;
//
//				if ((g_Vars.paksconnected2 | g_Vars.paksconnected) & thisbit) {
//					if (thing == 0xff) {
//						thing = joyShiftPfsStates();
//					}
//
//					if ((thing & thisbit) != (oldvalue & thisbit)) {
//						if (thing & thisbit) {
//							g_Paks[i].unk010 = 2;
//							newvalue |= thisbit;
//						} else {
//							g_Paks[i].unk010 = 1;
//							newvalue &= ~thisbit;
//							func0f110d90(i);
//						}
//					}
//				}
//			}
//
//			var80075d10 = newvalue;
//		}
//	}
//}
#else
void pak0f11ca30(void)
{
	if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
		u32 thing = joy00013980();
		u8 oldvalue = var80075d10;
		u8 newvalue = var80075d10;
		s32 i;

		for (i = 0; i < 5; i++) {
			u32 thisbit = 1 << i;

			if ((g_Vars.paksconnected2 | g_Vars.paksconnected) & thisbit) {
				if ((thing & thisbit) != (oldvalue & thisbit)) {
					if (thing & thisbit) {
						g_Paks[i].unk010 = 2;
						newvalue |= thisbit;
					} else {
						g_Paks[i].unk010 = 1;
						newvalue &= ~thisbit;
					}
				}
			}
		}

		var80075d10 = newvalue;
	}
}
#endif

void pak0f11cb9c(u32 arg0)
{
#if VERSION >= VERSION_NTSC_1_0
	switch (arg0) {
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
			break;
	}
#endif
}

#if VERSION >= VERSION_NTSC_1_0
void pak0f11cbc8(void)
{
	// empty
}

void pak0f11cbd0(void)
{
	// empty
}
#endif

#if VERSION < VERSION_NTSC_1_0
void pak0f116758nb(u32 arg0)
{
	// empty
}

void pak0f116760nb(u32 arg0)
{
	// empty
}
#endif

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f116768nb
/*  f116768:	00043e00 */ 	sll	$a3,$a0,0x18
/*  f11676c:	00077603 */ 	sra	$t6,$a3,0x18
/*  f116770:	27bdff88 */ 	addiu	$sp,$sp,-120
/*  f116774:	24010004 */ 	addiu	$at,$zero,0x4
/*  f116778:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11677c:	afa40078 */ 	sw	$a0,0x78($sp)
/*  f116780:	15c10003 */ 	bne	$t6,$at,.NB0f116790
/*  f116784:	01c03825 */ 	or	$a3,$t6,$zero
/*  f116788:	10000009 */ 	beqz	$zero,.NB0f1167b0
/*  f11678c:	00002025 */ 	or	$a0,$zero,$zero
.NB0f116790:
/*  f116790:	00077880 */ 	sll	$t7,$a3,0x2
/*  f116794:	01e77823 */ 	subu	$t7,$t7,$a3
/*  f116798:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11679c:	01e77821 */ 	addu	$t7,$t7,$a3
/*  f1167a0:	3c18800a */ 	lui	$t8,0x800a
/*  f1167a4:	27187658 */ 	addiu	$t8,$t8,0x7658
/*  f1167a8:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f1167ac:	01f82021 */ 	addu	$a0,$t7,$t8
.NB0f1167b0:
/*  f1167b0:	27a50028 */ 	addiu	$a1,$sp,0x28
/*  f1167b4:	27a60023 */ 	addiu	$a2,$sp,0x23
/*  f1167b8:	0c014950 */ 	jal	func00050d60
/*  f1167bc:	a3a7007b */ 	sb	$a3,0x7b($sp)
/*  f1167c0:	83a7007b */ 	lb	$a3,0x7b($sp)
/*  f1167c4:	1440001d */ 	bnez	$v0,.NB0f11683c
/*  f1167c8:	00402025 */ 	or	$a0,$v0,$zero
/*  f1167cc:	93a40023 */ 	lbu	$a0,0x23($sp)
/*  f1167d0:	0fc459d6 */ 	jal	pak0f116758nb
/*  f1167d4:	a3a7007b */ 	sb	$a3,0x7b($sp)
/*  f1167d8:	0fc459d8 */ 	jal	pak0f116760nb
/*  f1167dc:	27a40028 */ 	addiu	$a0,$sp,0x28
/*  f1167e0:	83a7007b */ 	lb	$a3,0x7b($sp)
/*  f1167e4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1167e8:	3c08800a */ 	lui	$t0,0x800a
/*  f1167ec:	14e10003 */ 	bne	$a3,$at,.NB0f1167fc
/*  f1167f0:	0007c880 */ 	sll	$t9,$a3,0x2
/*  f1167f4:	10000007 */ 	beqz	$zero,.NB0f116814
/*  f1167f8:	00002025 */ 	or	$a0,$zero,$zero
.NB0f1167fc:
/*  f1167fc:	0327c823 */ 	subu	$t9,$t9,$a3
/*  f116800:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f116804:	0327c821 */ 	addu	$t9,$t9,$a3
/*  f116808:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f11680c:	25087658 */ 	addiu	$t0,$t0,0x7658
/*  f116810:	03282021 */ 	addu	$a0,$t9,$t0
.NB0f116814:
/*  f116814:	0c0149c0 */ 	jal	func50f20
/*  f116818:	27a50023 */ 	addiu	$a1,$sp,0x23
/*  f11681c:	14400003 */ 	bnez	$v0,.NB0f11682c
/*  f116820:	00402025 */ 	or	$a0,$v0,$zero
/*  f116824:	10000008 */ 	beqz	$zero,.NB0f116848
/*  f116828:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f11682c:
/*  f11682c:	0fc459d4 */ 	jal	pak0f11cb9c
/*  f116830:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116834:	10000004 */ 	beqz	$zero,.NB0f116848
/*  f116838:	00001025 */ 	or	$v0,$zero,$zero
.NB0f11683c:
/*  f11683c:	0fc459d4 */ 	jal	pak0f11cb9c
/*  f116840:	00000000 */ 	sll	$zero,$zero,0x0
/*  f116844:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116848:
/*  f116848:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11684c:	27bd0078 */ 	addiu	$sp,$sp,0x78
/*  f116850:	03e00008 */ 	jr	$ra
/*  f116854:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

s32 pak0f11cbd8(s8 device, s32 arg1, char *arg2, u16 arg3)
{
	s32 result = func000513b0(PFS(device), false, arg1, arg2, arg3);

	if (result) {
		pak0f11cb9c(result);
		return false;
	}

	return true;
}

s32 pak0f11cc6c(s8 device, u16 arg1, char *arg2, u16 arg3)
{
	s32 result = func000513b0(PFS(device), true, arg1, arg2, arg3);

	if (result) {
		pak0f11cb9c(result);
		return false;
	}

	return true;
}

/**
 * Note: ntsc-beta needs the tmp variable to get the instruction ordering
 * correct, but there is not enough room in the stack to do this. So for
 * matching purposes, the buffer is being reduced to make room for tmp.
 * buffer is actually 32 though.
 */
bool pak0f11cd00(s8 device, u16 arg1, char *arg2, s32 arg3, s32 arg4)
{
#if VERSION >= VERSION_NTSC_1_0
	u8 buffer[32];
	bool result = false;
	s32 i;

	for (i = 0; i < 32; i++) {
		buffer[i] = '\n';
	}

	pak0f11cc6c(device, 0, buffer, sizeof(buffer));
#else
	bool result = false;
	s32 i;
	s32 tmp;
	u8 buffer[28];
#endif

	if (arg4 >= 0) {
#if VERSION >= VERSION_NTSC_1_0
		for (i = 0; i < 32; i++) {
			buffer[i] = (s32)(arg1 + 0xffff6000) / 0x2000;
		}
#else
		tmp = (s32)(arg1 + 0xffff6000) / 0x2000;

		for (i = 0; i < 32; i++) {
			buffer[i] = tmp;
		}
#endif

		if (pak0f11cc6c(device, 0x4000, buffer, 32)) {
			result = true;
		}
	} else {
		result = true;
	}

	if (result && pak0f11cc6c(device, arg1, arg2, arg3)) {
		result = true;
	}

	return result;
}

bool pak0f11ce00(s8 device, u16 arg1, char *arg2, s32 arg3, bool arg4)
{
#if VERSION >= VERSION_NTSC_1_0
	u8 buffer[32];
	bool result = false;
	s32 i;

	for (i = 0; i < 32; i++) {
		buffer[i] = 0;
	}

	pak0f11cc6c(device, 0, buffer, sizeof(buffer));
#else
	bool result = false;
	s32 i;
	u8 buffer[32];
#endif

	if (arg4) {
		for (i = 0; i < 32; i++) {
			buffer[i] = 0;
		}

		buffer[31] = (s32)(arg1 + 0xffff6000) / 0x2000;

		if (pak0f11cc6c(device, 0x4000, buffer, sizeof(buffer))) {
			result = true;
		}
	} else {
		result = true;
	}

	if (result && pak0f11cbd8(device, arg1, arg2, arg3)) {
		result = true;
	}

	return result;
}

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11cef8
/*  f11cef8:	27bdff30 */ 	addiu	$sp,$sp,-208
/*  f11cefc:	afa400d0 */ 	sw	$a0,0xd0($sp)
/*  f11cf00:	83ae00d3 */ 	lb	$t6,0xd3($sp)
/*  f11cf04:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11cf08:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11cf0c:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11cf10:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11cf14:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11cf18:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11cf1c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11cf20:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11cf24:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11cf28:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11cf2c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11cf30:	01f84021 */ 	addu	$t0,$t7,$t8
/*  f11cf34:	8d190000 */ 	lw	$t9,0x0($t0)
/*  f11cf38:	24010003 */ 	addiu	$at,$zero,0x3
/*  f11cf3c:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11cf40:	1721006c */ 	bne	$t9,$at,.L0f11d0f4
/*  f11cf44:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f11cf48:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f11cf4c:	24030002 */ 	addiu	$v1,$zero,0x2
.L0f11cf50:
/*  f11cf50:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11cf54:	0044082b */ 	sltu	$at,$v0,$a0
/*  f11cf58:	1420fffd */ 	bnez	$at,.L0f11cf50
/*  f11cf5c:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f11cf60:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11cf64:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f11cf68:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f11cf6c:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11cf70:	0fc4731b */ 	jal	pak0f11cc6c
/*  f11cf74:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11cf78:	14400003 */ 	bnez	$v0,.L0f11cf88
/*  f11cf7c:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11cf80:	1000005f */ 	beqz	$zero,.L0f11d100
/*  f11cf84:	00001025 */ 	or	$v0,$zero,$zero
.L0f11cf88:
/*  f11cf88:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11cf8c:	3405afe0 */ 	dli	$a1,0xafe0
/*  f11cf90:	27a6008c */ 	addiu	$a2,$sp,0x8c
/*  f11cf94:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11cf98:	0fc472f6 */ 	jal	pak0f11cbd8
/*  f11cf9c:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11cfa0:	14400003 */ 	bnez	$v0,.L0f11cfb0
/*  f11cfa4:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11cfa8:	10000055 */ 	beqz	$zero,.L0f11d100
/*  f11cfac:	00001025 */ 	or	$v0,$zero,$zero
.L0f11cfb0:
/*  f11cfb0:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f11cfb4:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f11cfb8:	24030008 */ 	addiu	$v1,$zero,0x8
.L0f11cfbc:
/*  f11cfbc:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11cfc0:	0044082b */ 	sltu	$at,$v0,$a0
/*  f11cfc4:	1420fffd */ 	bnez	$at,.L0f11cfbc
/*  f11cfc8:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f11cfcc:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11cfd0:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f11cfd4:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f11cfd8:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11cfdc:	0fc4731b */ 	jal	pak0f11cc6c
/*  f11cfe0:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11cfe4:	14400003 */ 	bnez	$v0,.L0f11cff4
/*  f11cfe8:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11cfec:	10000044 */ 	beqz	$zero,.L0f11d100
/*  f11cff0:	00001025 */ 	or	$v0,$zero,$zero
.L0f11cff4:
/*  f11cff4:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11cff8:	3405bfe0 */ 	dli	$a1,0xbfe0
/*  f11cffc:	27a6006c */ 	addiu	$a2,$sp,0x6c
/*  f11d000:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11d004:	0fc472f6 */ 	jal	pak0f11cbd8
/*  f11d008:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11d00c:	14400003 */ 	bnez	$v0,.L0f11d01c
/*  f11d010:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11d014:	1000003a */ 	beqz	$zero,.L0f11d100
/*  f11d018:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d01c:
/*  f11d01c:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f11d020:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f11d024:	24030010 */ 	addiu	$v1,$zero,0x10
.L0f11d028:
/*  f11d028:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11d02c:	0044082b */ 	sltu	$at,$v0,$a0
/*  f11d030:	1420fffd */ 	bnez	$at,.L0f11d028
/*  f11d034:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f11d038:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11d03c:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f11d040:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f11d044:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11d048:	0fc4731b */ 	jal	pak0f11cc6c
/*  f11d04c:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11d050:	14400003 */ 	bnez	$v0,.L0f11d060
/*  f11d054:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11d058:	10000029 */ 	beqz	$zero,.L0f11d100
/*  f11d05c:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d060:
/*  f11d060:	27a2002c */ 	addiu	$v0,$sp,0x2c
/*  f11d064:	27a3006c */ 	addiu	$v1,$sp,0x6c
.L0f11d068:
/*  f11d068:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11d06c:	1443fffe */ 	bne	$v0,$v1,.L0f11d068
/*  f11d070:	a040ffff */ 	sb	$zero,-0x1($v0)
/*  f11d074:	93a900a6 */ 	lbu	$t1,0xa6($sp)
/*  f11d078:	a10902ba */ 	sb	$t1,0x2ba($t0)
/*  f11d07c:	93aa0086 */ 	lbu	$t2,0x86($sp)
/*  f11d080:	312300ff */ 	andi	$v1,$t1,0xff
/*  f11d084:	314200ff */ 	andi	$v0,$t2,0xff
/*  f11d088:	14430003 */ 	bne	$v0,$v1,.L0f11d098
/*  f11d08c:	a10a02bb */ 	sb	$t2,0x2bb($t0)
/*  f11d090:	10000002 */ 	beqz	$zero,.L0f11d09c
/*  f11d094:	a10302b9 */ 	sb	$v1,0x2b9($t0)
.L0f11d098:
/*  f11d098:	a10202b9 */ 	sb	$v0,0x2b9($t0)
.L0f11d09c:
/*  f11d09c:	0fc52a9c */ 	jal	func0f14aa70
/*  f11d0a0:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f11d0a4:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11d0a8:	27a4002c */ 	addiu	$a0,$sp,0x2c
/*  f11d0ac:	00403025 */ 	or	$a2,$v0,$zero
/*  f11d0b0:	0fc47446 */ 	jal	pak0f11d118
/*  f11d0b4:	910502b9 */ 	lbu	$a1,0x2b9($t0)
/*  f11d0b8:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11d0bc:	0fc4745d */ 	jal	pak0f11d174
/*  f11d0c0:	27a5002c */ 	addiu	$a1,$sp,0x2c
/*  f11d0c4:	93ab002c */ 	lbu	$t3,0x2c($sp)
/*  f11d0c8:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f11d0cc:	3405a000 */ 	dli	$a1,0xa000
/*  f11d0d0:	356c0001 */ 	ori	$t4,$t3,0x1
/*  f11d0d4:	a3ac002c */ 	sb	$t4,0x2c($sp)
/*  f11d0d8:	27a6002c */ 	addiu	$a2,$sp,0x2c
/*  f11d0dc:	0fc4731b */ 	jal	pak0f11cc6c
/*  f11d0e0:	24070040 */ 	addiu	$a3,$zero,0x40
/*  f11d0e4:	14400003 */ 	bnez	$v0,.L0f11d0f4
/*  f11d0e8:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f11d0ec:	10000004 */ 	beqz	$zero,.L0f11d100
/*  f11d0f0:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d0f4:
/*  f11d0f4:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11d0f8:	ad0d0008 */ 	sw	$t5,0x8($t0)
/*  f11d0fc:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11d100:
/*  f11d100:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11d104:	27bd00d0 */ 	addiu	$sp,$sp,0xd0
/*  f11d108:	03e00008 */ 	jr	$ra
/*  f11d10c:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak7f116b0cnb
/*  f116b0c:	27bdff30 */ 	addiu	$sp,$sp,-208
/*  f116b10:	afa400d0 */ 	sw	$a0,0xd0($sp)
/*  f116b14:	83ae00d3 */ 	lb	$t6,0xd3($sp)
/*  f116b18:	3c18800a */ 	lui	$t8,0x800a
/*  f116b1c:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f116b20:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f116b24:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f116b28:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f116b2c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f116b30:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f116b34:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f116b38:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f116b3c:	01f84021 */ 	addu	$t0,$t7,$t8
/*  f116b40:	8d190000 */ 	lw	$t9,0x0($t0)
/*  f116b44:	24010003 */ 	addiu	$at,$zero,0x3
/*  f116b48:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f116b4c:	1721006c */ 	bne	$t9,$at,.NB0f116d00
/*  f116b50:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f116b54:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f116b58:	24030002 */ 	addiu	$v1,$zero,0x2
.NB0f116b5c:
/*  f116b5c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f116b60:	0044082b */ 	sltu	$at,$v0,$a0
/*  f116b64:	1420fffd */ 	bnez	$at,.NB0f116b5c
/*  f116b68:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f116b6c:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116b70:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f116b74:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f116b78:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116b7c:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f116b80:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116b84:	14400003 */ 	bnez	$v0,.NB0f116b94
/*  f116b88:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116b8c:	1000005f */ 	beqz	$zero,.NB0f116d0c
/*  f116b90:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116b94:
/*  f116b94:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116b98:	3405afe0 */ 	dli	$a1,0xafe0
/*  f116b9c:	27a6008c */ 	addiu	$a2,$sp,0x8c
/*  f116ba0:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116ba4:	0fc45a16 */ 	jal	pak0f11cbd8
/*  f116ba8:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116bac:	14400003 */ 	bnez	$v0,.NB0f116bbc
/*  f116bb0:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116bb4:	10000055 */ 	beqz	$zero,.NB0f116d0c
/*  f116bb8:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116bbc:
/*  f116bbc:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f116bc0:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f116bc4:	24030008 */ 	addiu	$v1,$zero,0x8
.NB0f116bc8:
/*  f116bc8:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f116bcc:	0044082b */ 	sltu	$at,$v0,$a0
/*  f116bd0:	1420fffd */ 	bnez	$at,.NB0f116bc8
/*  f116bd4:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f116bd8:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116bdc:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f116be0:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f116be4:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116be8:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f116bec:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116bf0:	14400003 */ 	bnez	$v0,.NB0f116c00
/*  f116bf4:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116bf8:	10000044 */ 	beqz	$zero,.NB0f116d0c
/*  f116bfc:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116c00:
/*  f116c00:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116c04:	3405bfe0 */ 	dli	$a1,0xbfe0
/*  f116c08:	27a6006c */ 	addiu	$a2,$sp,0x6c
/*  f116c0c:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116c10:	0fc45a16 */ 	jal	pak0f11cbd8
/*  f116c14:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116c18:	14400003 */ 	bnez	$v0,.NB0f116c28
/*  f116c1c:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116c20:	1000003a */ 	beqz	$zero,.NB0f116d0c
/*  f116c24:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116c28:
/*  f116c28:	27a200ac */ 	addiu	$v0,$sp,0xac
/*  f116c2c:	27a400cc */ 	addiu	$a0,$sp,0xcc
/*  f116c30:	24030010 */ 	addiu	$v1,$zero,0x10
.NB0f116c34:
/*  f116c34:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f116c38:	0044082b */ 	sltu	$at,$v0,$a0
/*  f116c3c:	1420fffd */ 	bnez	$at,.NB0f116c34
/*  f116c40:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f116c44:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116c48:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f116c4c:	27a600ac */ 	addiu	$a2,$sp,0xac
/*  f116c50:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116c54:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f116c58:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116c5c:	14400003 */ 	bnez	$v0,.NB0f116c6c
/*  f116c60:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116c64:	10000029 */ 	beqz	$zero,.NB0f116d0c
/*  f116c68:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116c6c:
/*  f116c6c:	27a2002c */ 	addiu	$v0,$sp,0x2c
/*  f116c70:	27a3006c */ 	addiu	$v1,$sp,0x6c
.NB0f116c74:
/*  f116c74:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f116c78:	1443fffe */ 	bne	$v0,$v1,.NB0f116c74
/*  f116c7c:	a040ffff */ 	sb	$zero,-0x1($v0)
/*  f116c80:	93a900a6 */ 	lbu	$t1,0xa6($sp)
/*  f116c84:	a10902ba */ 	sb	$t1,0x2ba($t0)
/*  f116c88:	93aa0086 */ 	lbu	$t2,0x86($sp)
/*  f116c8c:	312300ff */ 	andi	$v1,$t1,0xff
/*  f116c90:	314200ff */ 	andi	$v0,$t2,0xff
/*  f116c94:	14430003 */ 	bne	$v0,$v1,.NB0f116ca4
/*  f116c98:	a10a02bb */ 	sb	$t2,0x2bb($t0)
/*  f116c9c:	10000002 */ 	beqz	$zero,.NB0f116ca8
/*  f116ca0:	a10302b9 */ 	sb	$v1,0x2b9($t0)
.NB0f116ca4:
/*  f116ca4:	a10202b9 */ 	sb	$v0,0x2b9($t0)
.NB0f116ca8:
/*  f116ca8:	0fc51490 */ 	jal	func0f14aa70
/*  f116cac:	afa80020 */ 	sw	$t0,0x20($sp)
/*  f116cb0:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116cb4:	27a4002c */ 	addiu	$a0,$sp,0x2c
/*  f116cb8:	00403025 */ 	or	$a2,$v0,$zero
/*  f116cbc:	0fc45b68 */ 	jal	pak0f11d118
/*  f116cc0:	910502b9 */ 	lbu	$a1,0x2b9($t0)
/*  f116cc4:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116cc8:	0fc45b7f */ 	jal	pak0f11d174
/*  f116ccc:	27a5002c */ 	addiu	$a1,$sp,0x2c
/*  f116cd0:	93ab002c */ 	lbu	$t3,0x2c($sp)
/*  f116cd4:	83a400d3 */ 	lb	$a0,0xd3($sp)
/*  f116cd8:	3405a000 */ 	dli	$a1,0xa000
/*  f116cdc:	356c0001 */ 	ori	$t4,$t3,0x1
/*  f116ce0:	a3ac002c */ 	sb	$t4,0x2c($sp)
/*  f116ce4:	27a6002c */ 	addiu	$a2,$sp,0x2c
/*  f116ce8:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f116cec:	24070040 */ 	addiu	$a3,$zero,0x40
/*  f116cf0:	14400003 */ 	bnez	$v0,.NB0f116d00
/*  f116cf4:	8fa80020 */ 	lw	$t0,0x20($sp)
/*  f116cf8:	10000004 */ 	beqz	$zero,.NB0f116d0c
/*  f116cfc:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116d00:
/*  f116d00:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f116d04:	ad0d0008 */ 	sw	$t5,0x8($t0)
/*  f116d08:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f116d0c:
/*  f116d0c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f116d10:	27bd00d0 */ 	addiu	$sp,$sp,0xd0
/*  f116d14:	03e00008 */ 	jr	$ra
/*  f116d18:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f11d110(void)
{
	// empty
}
#else
GLOBAL_ASM(
glabel pak0f11d110
/*  f116d1c:	27bdff80 */ 	addiu	$sp,$sp,-128
/*  f116d20:	afa40080 */ 	sw	$a0,0x80($sp)
/*  f116d24:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f116d28:	27a40040 */ 	addiu	$a0,$sp,0x40
/*  f116d2c:	27a20020 */ 	addiu	$v0,$sp,0x20
/*  f116d30:	24030010 */ 	addiu	$v1,$zero,0x10
.NB0f116d34:
/*  f116d34:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f116d38:	1444fffe */ 	bne	$v0,$a0,.NB0f116d34
/*  f116d3c:	a043ffff */ 	sb	$v1,-0x1($v0)
/*  f116d40:	83a40083 */ 	lb	$a0,0x83($sp)
/*  f116d44:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f116d48:	27a60020 */ 	addiu	$a2,$sp,0x20
/*  f116d4c:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f116d50:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f116d54:	14400003 */ 	bnez	$v0,.NB0f116d64
/*  f116d58:	83a40083 */ 	lb	$a0,0x83($sp)
/*  f116d5c:	1000000c */ 	beqz	$zero,.NB0f116d90
/*  f116d60:	00001025 */ 	or	$v0,$zero,$zero
.NB0f116d64:
/*  f116d64:	3405a000 */ 	dli	$a1,0xa000
/*  f116d68:	27a60040 */ 	addiu	$a2,$sp,0x40
/*  f116d6c:	0fc45a16 */ 	jal	pak0f11cbd8
/*  f116d70:	24070040 */ 	addiu	$a3,$zero,0x40
/*  f116d74:	54400004 */ 	bnezl	$v0,.NB0f116d88
/*  f116d78:	93a20040 */ 	lbu	$v0,0x40($sp)
/*  f116d7c:	10000004 */ 	beqz	$zero,.NB0f116d90
/*  f116d80:	00001025 */ 	or	$v0,$zero,$zero
/*  f116d84:	93a20040 */ 	lbu	$v0,0x40($sp)
.NB0f116d88:
/*  f116d88:	304e0001 */ 	andi	$t6,$v0,0x1
/*  f116d8c:	01c01025 */ 	or	$v0,$t6,$zero
.NB0f116d90:
/*  f116d90:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f116d94:	27bd0080 */ 	addiu	$sp,$sp,0x80
/*  f116d98:	03e00008 */ 	jr	$ra
/*  f116d9c:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

GLOBAL_ASM(
glabel pak0f11d118
/*  f11d118:	27bdffe8 */ 	addiu	$sp,$sp,-24
/*  f11d11c:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11d120:	afa5001c */ 	sw	$a1,0x1c($sp)
/*  f11d124:	240e0002 */ 	addiu	$t6,$zero,0x2
/*  f11d128:	240f00e4 */ 	addiu	$t7,$zero,0xe4
/*  f11d12c:	24180010 */ 	addiu	$t8,$zero,0x10
/*  f11d130:	a08e0000 */ 	sb	$t6,0x0($a0)
/*  f11d134:	a08f0001 */ 	sb	$t7,0x1($a0)
/*  f11d138:	a0980002 */ 	sb	$t8,0x2($a0)
/*  f11d13c:	a0800003 */ 	sb	$zero,0x3($a0)
/*  f11d140:	93a2001f */ 	lbu	$v0,0x1f($sp)
/*  f11d144:	00c02825 */ 	or	$a1,$a2,$zero
/*  f11d148:	24840006 */ 	addiu	$a0,$a0,0x6
/*  f11d14c:	3048003f */ 	andi	$t0,$v0,0x3f
/*  f11d150:	25090080 */ 	addiu	$t1,$t0,0x80
/*  f11d154:	30590007 */ 	andi	$t9,$v0,0x7
/*  f11d158:	a099fffe */ 	sb	$t9,-0x2($a0)
/*  f11d15c:	0fc47485 */ 	jal	pak0f11d214
/*  f11d160:	a089ffff */ 	sb	$t1,-0x1($a0)
/*  f11d164:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11d168:	27bd0018 */ 	addiu	$sp,$sp,0x18
/*  f11d16c:	03e00008 */ 	jr	$ra
/*  f11d170:	00000000 */ 	sll	$zero,$zero,0x0
);

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d174
/*  f11d174:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f11d178:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11d17c:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11d180:	00a08025 */ 	or	$s0,$a1,$zero
/*  f11d184:	0fc52a9c */ 	jal	func0f14aa70
/*  f11d188:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f11d18c:	83ae002b */ 	lb	$t6,0x2b($sp)
/*  f11d190:	3c05800a */ 	lui	$a1,%hi(g_Paks+0x2b9)
/*  f11d194:	02002025 */ 	or	$a0,$s0,$zero
/*  f11d198:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11d19c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11d1a0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11d1a4:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11d1a8:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11d1ac:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11d1b0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11d1b4:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11d1b8:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11d1bc:	00af2821 */ 	addu	$a1,$a1,$t7
/*  f11d1c0:	90a52639 */ 	lbu	$a1,%lo(g_Paks+0x2b9)($a1)
/*  f11d1c4:	0fc47446 */ 	jal	pak0f11d118
/*  f11d1c8:	00403025 */ 	or	$a2,$v0,$zero
/*  f11d1cc:	0fc52b4e */ 	jal	func0f14ad38
/*  f11d1d0:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f11d1d4:	a3a20027 */ 	sb	$v0,0x27($sp)
/*  f11d1d8:	0fc52b45 */ 	jal	func0f14ad14
/*  f11d1dc:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f11d1e0:	92180001 */ 	lbu	$t8,0x1($s0)
/*  f11d1e4:	93a80027 */ 	lbu	$t0,0x27($sp)
/*  f11d1e8:	00025a03 */ 	sra	$t3,$v0,0x8
/*  f11d1ec:	331900e0 */ 	andi	$t9,$t8,0xe0
/*  f11d1f0:	03284821 */ 	addu	$t1,$t9,$t0
/*  f11d1f4:	a2090001 */ 	sb	$t1,0x1($s0)
/*  f11d1f8:	a20b0002 */ 	sb	$t3,0x2($s0)
/*  f11d1fc:	a2020003 */ 	sb	$v0,0x3($s0)
/*  f11d200:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11d204:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11d208:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f11d20c:	03e00008 */ 	jr	$ra
/*  f11d210:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d174
/*  f116dfc:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f116e00:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f116e04:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f116e08:	00a08025 */ 	or	$s0,$a1,$zero
/*  f116e0c:	0fc51490 */ 	jal	func0f14aa70
/*  f116e10:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f116e14:	83ae002b */ 	lb	$t6,0x2b($sp)
/*  f116e18:	3c05800a */ 	lui	$a1,0x800a
/*  f116e1c:	02002025 */ 	or	$a0,$s0,$zero
/*  f116e20:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f116e24:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f116e28:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f116e2c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f116e30:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f116e34:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f116e38:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f116e3c:	00af2821 */ 	addu	$a1,$a1,$t7
/*  f116e40:	90a56b29 */ 	lbu	$a1,0x6b29($a1)
/*  f116e44:	0fc45b68 */ 	jal	pak0f11d118
/*  f116e48:	00403025 */ 	or	$a2,$v0,$zero
/*  f116e4c:	0fc51542 */ 	jal	func0f14ad38
/*  f116e50:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f116e54:	a3a20027 */ 	sb	$v0,0x27($sp)
/*  f116e58:	0fc51539 */ 	jal	func0f14ad14
/*  f116e5c:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f116e60:	92180001 */ 	lbu	$t8,0x1($s0)
/*  f116e64:	93a80027 */ 	lbu	$t0,0x27($sp)
/*  f116e68:	00025a03 */ 	sra	$t3,$v0,0x8
/*  f116e6c:	331900e0 */ 	andi	$t9,$t8,0xe0
/*  f116e70:	03284821 */ 	addu	$t1,$t9,$t0
/*  f116e74:	a2090001 */ 	sb	$t1,0x1($s0)
/*  f116e78:	a20b0002 */ 	sb	$t3,0x2($s0)
/*  f116e7c:	a2020003 */ 	sb	$v0,0x3($s0)
/*  f116e80:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f116e84:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f116e88:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f116e8c:	03e00008 */ 	jr	$ra
/*  f116e90:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

GLOBAL_ASM(
glabel pak0f11d214
/*  f11d214:	3c0f7f1b */ 	lui	$t7,%hi(var7f1b494c)
/*  f11d218:	27bdfe48 */ 	addiu	$sp,$sp,-440
/*  f11d21c:	25ef494c */ 	addiu	$t7,$t7,%lo(var7f1b494c)
/*  f11d220:	00803025 */ 	or	$a2,$a0,$zero
/*  f11d224:	25ed0108 */ 	addiu	$t5,$t7,0x108
/*  f11d228:	27ae00a8 */ 	addiu	$t6,$sp,0xa8
.L0f11d22c:
/*  f11d22c:	8de10000 */ 	lw	$at,0x0($t7)
/*  f11d230:	25ef000c */ 	addiu	$t7,$t7,0xc
/*  f11d234:	25ce000c */ 	addiu	$t6,$t6,0xc
/*  f11d238:	adc1fff4 */ 	sw	$at,-0xc($t6)
/*  f11d23c:	8de1fff8 */ 	lw	$at,-0x8($t7)
/*  f11d240:	adc1fff8 */ 	sw	$at,-0x8($t6)
/*  f11d244:	8de1fffc */ 	lw	$at,-0x4($t7)
/*  f11d248:	15edfff8 */ 	bne	$t7,$t5,.L0f11d22c
/*  f11d24c:	adc1fffc */ 	sw	$at,-0x4($t6)
/*  f11d250:	8de10000 */ 	lw	$at,0x0($t7)
/*  f11d254:	8ded0004 */ 	lw	$t5,0x4($t7)
/*  f11d258:	3c187f1b */ 	lui	$t8,%hi(var7f1b4a5c)
/*  f11d25c:	27184a5c */ 	addiu	$t8,$t8,%lo(var7f1b4a5c)
/*  f11d260:	adc10000 */ 	sw	$at,0x0($t6)
/*  f11d264:	adcd0004 */ 	sw	$t5,0x4($t6)
/*  f11d268:	270e003c */ 	addiu	$t6,$t8,0x3c
/*  f11d26c:	27b90068 */ 	addiu	$t9,$sp,0x68
.L0f11d270:
/*  f11d270:	8f010000 */ 	lw	$at,0x0($t8)
/*  f11d274:	2718000c */ 	addiu	$t8,$t8,0xc
/*  f11d278:	2739000c */ 	addiu	$t9,$t9,0xc
/*  f11d27c:	af21fff4 */ 	sw	$at,-0xc($t9)
/*  f11d280:	8f01fff8 */ 	lw	$at,-0x8($t8)
/*  f11d284:	af21fff8 */ 	sw	$at,-0x8($t9)
/*  f11d288:	8f01fffc */ 	lw	$at,-0x4($t8)
/*  f11d28c:	170efff8 */ 	bne	$t8,$t6,.L0f11d270
/*  f11d290:	af21fffc */ 	sw	$at,-0x4($t9)
/*  f11d294:	8f010000 */ 	lw	$at,0x0($t8)
/*  f11d298:	3c0d7f1b */ 	lui	$t5,%hi(var7f1b4a9c)
/*  f11d29c:	25ad4a9c */ 	addiu	$t5,$t5,%lo(var7f1b4a9c)
/*  f11d2a0:	af210000 */ 	sw	$at,0x0($t9)
/*  f11d2a4:	25b9003c */ 	addiu	$t9,$t5,0x3c
/*  f11d2a8:	27af0028 */ 	addiu	$t7,$sp,0x28
.L0f11d2ac:
/*  f11d2ac:	8da10000 */ 	lw	$at,0x0($t5)
/*  f11d2b0:	25ad000c */ 	addiu	$t5,$t5,0xc
/*  f11d2b4:	25ef000c */ 	addiu	$t7,$t7,0xc
/*  f11d2b8:	ade1fff4 */ 	sw	$at,-0xc($t7)
/*  f11d2bc:	8da1fff8 */ 	lw	$at,-0x8($t5)
/*  f11d2c0:	ade1fff8 */ 	sw	$at,-0x8($t7)
/*  f11d2c4:	8da1fffc */ 	lw	$at,-0x4($t5)
/*  f11d2c8:	15b9fff8 */ 	bne	$t5,$t9,.L0f11d2ac
/*  f11d2cc:	ade1fffc */ 	sw	$at,-0x4($t7)
/*  f11d2d0:	8da10000 */ 	lw	$at,0x0($t5)
/*  f11d2d4:	0005c100 */ 	sll	$t8,$a1,0x4
/*  f11d2d8:	27ae00a8 */ 	addiu	$t6,$sp,0xa8
/*  f11d2dc:	ade10000 */ 	sw	$at,0x0($t7)
/*  f11d2e0:	3c013f00 */ 	lui	$at,0x3f00
/*  f11d2e4:	44816000 */ 	mtc1	$at,$f12
/*  f11d2e8:	3c013d80 */ 	lui	$at,0x3d80
/*  f11d2ec:	44811000 */ 	mtc1	$at,$f2
/*  f11d2f0:	030e1821 */ 	addu	$v1,$t8,$t6
/*  f11d2f4:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d2f8:	240c0003 */ 	addiu	$t4,$zero,0x3
/*  f11d2fc:	27ab0068 */ 	addiu	$t3,$sp,0x68
/*  f11d300:	240a0004 */ 	addiu	$t2,$zero,0x4
.L0f11d304:
/*  f11d304:	c4640004 */ 	lwc1	$f4,0x4($v1)
/*  f11d308:	c4660000 */ 	lwc1	$f6,0x0($v1)
/*  f11d30c:	00c02825 */ 	or	$a1,$a2,$zero
/*  f11d310:	27a80028 */ 	addiu	$t0,$sp,0x28
/*  f11d314:	46062201 */ 	sub.s	$f8,$f4,$f6
/*  f11d318:	46024002 */ 	mul.s	$f0,$f8,$f2
/*  f11d31c:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11d320:
/*  f11d320:	00002025 */ 	or	$a0,$zero,$zero
/*  f11d324:	00a23821 */ 	addu	$a3,$a1,$v0
/*  f11d328:	01004825 */ 	or	$t1,$t0,$zero
.L0f11d32c:
/*  f11d32c:	c52a0000 */ 	lwc1	$f10,0x0($t1)
/*  f11d330:	c4720000 */ 	lwc1	$f18,0x0($v1)
/*  f11d334:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11d338:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f11d33c:	46126100 */ 	add.s	$f4,$f12,$f18
/*  f11d340:	24840001 */ 	addiu	$a0,$a0,0x1
/*  f11d344:	25290004 */ 	addiu	$t1,$t1,0x4
/*  f11d348:	3c014f00 */ 	lui	$at,0x4f00
/*  f11d34c:	46048180 */ 	add.s	$f6,$f16,$f4
/*  f11d350:	4459f800 */ 	cfc1	$t9,$31
/*  f11d354:	44cdf800 */ 	ctc1	$t5,$31
/*  f11d358:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d35c:	46003224 */ 	cvt.w.s	$f8,$f6
/*  f11d360:	444df800 */ 	cfc1	$t5,$31
/*  f11d364:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d368:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f11d36c:	51a00013 */ 	beqzl	$t5,.L0f11d3bc
/*  f11d370:	440d4000 */ 	mfc1	$t5,$f8
/*  f11d374:	44814000 */ 	mtc1	$at,$f8
/*  f11d378:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11d37c:	46083201 */ 	sub.s	$f8,$f6,$f8
/*  f11d380:	44cdf800 */ 	ctc1	$t5,$31
/*  f11d384:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d388:	46004224 */ 	cvt.w.s	$f8,$f8
/*  f11d38c:	444df800 */ 	cfc1	$t5,$31
/*  f11d390:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d394:	31ad0078 */ 	andi	$t5,$t5,0x78
/*  f11d398:	15a00005 */ 	bnez	$t5,.L0f11d3b0
/*  f11d39c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d3a0:	440d4000 */ 	mfc1	$t5,$f8
/*  f11d3a4:	3c018000 */ 	lui	$at,0x8000
/*  f11d3a8:	10000007 */ 	beqz	$zero,.L0f11d3c8
/*  f11d3ac:	01a16825 */ 	or	$t5,$t5,$at
.L0f11d3b0:
/*  f11d3b0:	10000005 */ 	beqz	$zero,.L0f11d3c8
/*  f11d3b4:	240dffff */ 	addiu	$t5,$zero,-1
/*  f11d3b8:	440d4000 */ 	mfc1	$t5,$f8
.L0f11d3bc:
/*  f11d3bc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d3c0:	05a0fffb */ 	bltz	$t5,.L0f11d3b0
/*  f11d3c4:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11d3c8:
/*  f11d3c8:	44d9f800 */ 	ctc1	$t9,$31
/*  f11d3cc:	24e70003 */ 	addiu	$a3,$a3,0x3
/*  f11d3d0:	148affd6 */ 	bne	$a0,$t2,.L0f11d32c
/*  f11d3d4:	a0edfffd */ 	sb	$t5,-0x3($a3)
/*  f11d3d8:	25080010 */ 	addiu	$t0,$t0,0x10
/*  f11d3dc:	150bffd0 */ 	bne	$t0,$t3,.L0f11d320
/*  f11d3e0:	24a5000c */ 	addiu	$a1,$a1,0xc
/*  f11d3e4:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11d3e8:	144cffc6 */ 	bne	$v0,$t4,.L0f11d304
/*  f11d3ec:	24630004 */ 	addiu	$v1,$v1,0x4
/*  f11d3f0:	03e00008 */ 	jr	$ra
/*  f11d3f4:	27bd01b8 */ 	addiu	$sp,$sp,0x1b8
);

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d3f8
/*  f11d3f8:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d3fc:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11d400:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11d404:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d408:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d40c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d410:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d414:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11d418:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d41c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d420:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d424:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d428:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d42c:	03191021 */ 	addu	$v0,$t8,$t9
/*  f11d430:	8c480000 */ 	lw	$t0,0x0($v0)
/*  f11d434:	24010003 */ 	addiu	$at,$zero,0x3
/*  f11d438:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11d43c:	5501000c */ 	bnel	$t0,$at,.L0f11d470
/*  f11d440:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d444:	8c430010 */ 	lw	$v1,0x10($v0)
/*  f11d448:	2401000b */ 	addiu	$at,$zero,0xb
/*  f11d44c:	10610005 */ 	beq	$v1,$at,.L0f11d464
/*  f11d450:	2401000c */ 	addiu	$at,$zero,0xc
/*  f11d454:	10610003 */ 	beq	$v1,$at,.L0f11d464
/*  f11d458:	2401000d */ 	addiu	$at,$zero,0xd
/*  f11d45c:	54610004 */ 	bnel	$v1,$at,.L0f11d470
/*  f11d460:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d464:
/*  f11d464:	03e00008 */ 	jr	$ra
/*  f11d468:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f11d46c:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d470:
/*  f11d470:	03e00008 */ 	jr	$ra
/*  f11d474:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d3f8
/*  f117078:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11707c:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f117080:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f117084:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117088:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11708c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117090:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f117094:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f117098:	3c19800a */ 	lui	$t9,0x800a
/*  f11709c:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f1170a0:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1170a4:	03191021 */ 	addu	$v0,$t8,$t9
/*  f1170a8:	8c480000 */ 	lw	$t0,0x0($v0)
/*  f1170ac:	24010003 */ 	addiu	$at,$zero,0x3
/*  f1170b0:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f1170b4:	5501000c */ 	bnel	$t0,$at,.NB0f1170e8
/*  f1170b8:	00001025 */ 	or	$v0,$zero,$zero
/*  f1170bc:	8c430010 */ 	lw	$v1,0x10($v0)
/*  f1170c0:	2401000b */ 	addiu	$at,$zero,0xb
/*  f1170c4:	10610005 */ 	beq	$v1,$at,.NB0f1170dc
/*  f1170c8:	2401000c */ 	addiu	$at,$zero,0xc
/*  f1170cc:	10610003 */ 	beq	$v1,$at,.NB0f1170dc
/*  f1170d0:	2401000d */ 	addiu	$at,$zero,0xd
/*  f1170d4:	54610004 */ 	bnel	$v1,$at,.NB0f1170e8
/*  f1170d8:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1170dc:
/*  f1170dc:	03e00008 */ 	jr	$ra
/*  f1170e0:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f1170e4:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1170e8:
/*  f1170e8:	03e00008 */ 	jr	$ra
/*  f1170ec:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d478
/*  f11d478:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d47c:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11d480:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11d484:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d488:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d48c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d490:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d494:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11d498:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d49c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d4a0:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d4a4:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d4a8:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d4ac:	03191821 */ 	addu	$v1,$t8,$t9
/*  f11d4b0:	8c680008 */ 	lw	$t0,0x8($v1)
/*  f11d4b4:	2401000b */ 	addiu	$at,$zero,0xb
/*  f11d4b8:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11d4bc:	15010004 */ 	bne	$t0,$at,.L0f11d4d0
/*  f11d4c0:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11d4c4:	ac690008 */ 	sw	$t1,0x8($v1)
/*  f11d4c8:	03e00008 */ 	jr	$ra
/*  f11d4cc:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11d4d0:
/*  f11d4d0:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d4d4:	03e00008 */ 	jr	$ra
/*  f11d4d8:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d478
/*  f1170f0:	00047600 */ 	sll	$t6,$a0,0x18
/*  f1170f4:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f1170f8:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f1170fc:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117100:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f117104:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117108:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f11710c:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f117110:	3c19800a */ 	lui	$t9,0x800a
/*  f117114:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f117118:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f11711c:	03191821 */ 	addu	$v1,$t8,$t9
/*  f117120:	8c680008 */ 	lw	$t0,0x8($v1)
/*  f117124:	2401000b */ 	addiu	$at,$zero,0xb
/*  f117128:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11712c:	15010004 */ 	bne	$t0,$at,.NB0f117140
/*  f117130:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f117134:	ac690008 */ 	sw	$t1,0x8($v1)
/*  f117138:	03e00008 */ 	jr	$ra
/*  f11713c:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f117140:
/*  f117140:	00001025 */ 	or	$v0,$zero,$zero
/*  f117144:	03e00008 */ 	jr	$ra
/*  f117148:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d4dc
/*  f11d4dc:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d4e0:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11d4e4:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11d4e8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d4ec:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d4f0:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d4f4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d4f8:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11d4fc:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d500:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d504:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d508:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d50c:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d510:	03191021 */ 	addu	$v0,$t8,$t9
/*  f11d514:	8c430008 */ 	lw	$v1,0x8($v0)
/*  f11d518:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11d51c:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11d520:	10610003 */ 	beq	$v1,$at,.L0f11d530
/*  f11d524:	24080002 */ 	addiu	$t0,$zero,0x2
/*  f11d528:	14600003 */ 	bnez	$v1,.L0f11d538
/*  f11d52c:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11d530:
/*  f11d530:	ac480008 */ 	sw	$t0,0x8($v0)
/*  f11d534:	ac400270 */ 	sw	$zero,0x270($v0)
.L0f11d538:
/*  f11d538:	03e00008 */ 	jr	$ra
/*  f11d53c:	00001025 */ 	or	$v0,$zero,$zero
);
#else
GLOBAL_ASM(
glabel pak0f11d4dc
/*  f11714c:	00047600 */ 	sll	$t6,$a0,0x18
/*  f117150:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f117154:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f117158:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11715c:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f117160:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117164:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f117168:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11716c:	3c19800a */ 	lui	$t9,0x800a
/*  f117170:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f117174:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f117178:	03191021 */ 	addu	$v0,$t8,$t9
/*  f11717c:	8c430008 */ 	lw	$v1,0x8($v0)
/*  f117180:	24010001 */ 	addiu	$at,$zero,0x1
/*  f117184:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f117188:	10610003 */ 	beq	$v1,$at,.NB0f117198
/*  f11718c:	24080002 */ 	addiu	$t0,$zero,0x2
/*  f117190:	14600003 */ 	bnez	$v1,.NB0f1171a0
/*  f117194:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f117198:
/*  f117198:	ac480008 */ 	sw	$t0,0x8($v0)
/*  f11719c:	ac400270 */ 	sw	$zero,0x270($v0)
.NB0f1171a0:
/*  f1171a0:	03e00008 */ 	jr	$ra
/*  f1171a4:	00001025 */ 	or	$v0,$zero,$zero
);
#endif

s32 pak0f11d540(s8 device, s32 arg1)
{
	if (g_Paks[device].unk008 == 1 || g_Paks[device].unk008 == 0) {
		g_Paks[device].unk008 = 4;
		g_Paks[device].unk270 = arg1;
		return true;
	}

	return false;
}

s32 pak0f11d5b0(s8 device)
{
	if (g_Paks[device].unk008 == 1 || g_Paks[device].unk008 == 0) {
		g_Paks[device].unk008 = 4;
		g_Paks[device].unk270 = 1;
		return true;
	}

	return false;
}

void pak0f11d620(s8 device)
{
	if (g_Paks[device].unk010 == 11) {
		g_Paks[device].unk010 = 12;
	}
}

void pak0f11d678(void)
{
	// empty
}

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak7f1172d0nb
/*  f1172d0:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f1172d4:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f1172d8:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f1172dc:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f1172e0:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1172e4:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f1172e8:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1172ec:	3c01800a */ 	lui	$at,0x800a
/*  f1172f0:	00380821 */ 	addu	$at,$at,$t8
/*  f1172f4:	27bdffb8 */ 	addiu	$sp,$sp,-72
/*  f1172f8:	ac206ae8 */ 	sw	$zero,0x6ae8($at)
/*  f1172fc:	afa40048 */ 	sw	$a0,0x48($sp)
/*  f117300:	00001825 */ 	or	$v1,$zero,$zero
/*  f117304:	01e02025 */ 	or	$a0,$t7,$zero
/*  f117308:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11730c:	27a30048 */ 	addiu	$v1,$sp,0x48
/*  f117310:	27a20028 */ 	addiu	$v0,$sp,0x28
/*  f117314:	04a10003 */ 	bgez	$a1,.NB0f117324
/*  f117318:	00053043 */ 	sra	$a2,$a1,0x1
/*  f11731c:	24a10001 */ 	addiu	$at,$a1,0x1
/*  f117320:	00013043 */ 	sra	$a2,$at,0x1
.NB0f117324:
/*  f117324:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f117328:	1443fffe */ 	bne	$v0,$v1,.NB0f117324
/*  f11732c:	a046ffff */ 	sb	$a2,-0x1($v0)
/*  f117330:	24054000 */ 	addiu	$a1,$zero,0x4000
/*  f117334:	27a60028 */ 	addiu	$a2,$sp,0x28
/*  f117338:	0fc45a3b */ 	jal	pak0f11cc6c
/*  f11733c:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f117340:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117344:	27bd0048 */ 	addiu	$sp,$sp,0x48
/*  f117348:	03e00008 */ 	jr	$ra
/*  f11734c:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d680
/*  f11d680:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f11d684:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f11d688:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d68c:	000e2603 */ 	sra	$a0,$t6,0x18
/*  f11d690:	0004c080 */ 	sll	$t8,$a0,0x2
/*  f11d694:	0304c023 */ 	subu	$t8,$t8,$a0
/*  f11d698:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d69c:	0304c023 */ 	subu	$t8,$t8,$a0
/*  f11d6a0:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d6a4:	0304c021 */ 	addu	$t8,$t8,$a0
/*  f11d6a8:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d6ac:	0304c023 */ 	subu	$t8,$t8,$a0
/*  f11d6b0:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d6b4:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d6b8:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d6bc:	03194021 */ 	addu	$t0,$t8,$t9
/*  f11d6c0:	8d02000c */ 	lw	$v0,0xc($t0)
/*  f11d6c4:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11d6c8:	00001825 */ 	or	$v1,$zero,$zero
/*  f11d6cc:	1040000a */ 	beqz	$v0,.L0f11d6f8
/*  f11d6d0:	24090040 */ 	addiu	$t1,$zero,0x40
/*  f11d6d4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11d6d8:	1041000b */ 	beq	$v0,$at,.L0f11d708
/*  f11d6dc:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11d6e0:	1041000f */ 	beq	$v0,$at,.L0f11d720
/*  f11d6e4:	24010003 */ 	addiu	$at,$zero,0x3
/*  f11d6e8:	10410013 */ 	beq	$v0,$at,.L0f11d738
/*  f11d6ec:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d6f0:	1000001a */ 	beqz	$zero,.L0f11d75c
/*  f11d6f4:	30af0001 */ 	andi	$t7,$a1,0x1
.L0f11d6f8:
/*  f11d6f8:	240301a0 */ 	addiu	$v1,$zero,0x1a0
/*  f11d6fc:	a7a90020 */ 	sh	$t1,0x20($sp)
/*  f11d700:	10000015 */ 	beqz	$zero,.L0f11d758
/*  f11d704:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11d708:
/*  f11d708:	8d030278 */ 	lw	$v1,0x278($t0)
/*  f11d70c:	240b0040 */ 	addiu	$t3,$zero,0x40
/*  f11d710:	a7ab0020 */ 	sh	$t3,0x20($sp)
/*  f11d714:	00035200 */ 	sll	$t2,$v1,0x8
/*  f11d718:	1000000f */ 	beqz	$zero,.L0f11d758
/*  f11d71c:	25430660 */ 	addiu	$v1,$t2,0x660
.L0f11d720:
/*  f11d720:	8d030278 */ 	lw	$v1,0x278($t0)
/*  f11d724:	240d0080 */ 	addiu	$t5,$zero,0x80
/*  f11d728:	a7ad0020 */ 	sh	$t5,0x20($sp)
/*  f11d72c:	00036200 */ 	sll	$t4,$v1,0x8
/*  f11d730:	10000009 */ 	beqz	$zero,.L0f11d758
/*  f11d734:	25830440 */ 	addiu	$v1,$t4,0x440
.L0f11d738:
/*  f11d738:	3c028007 */ 	lui	$v0,%hi(var80075ccc)
/*  f11d73c:	8c425ccc */ 	lw	$v0,%lo(var80075ccc)($v0)
/*  f11d740:	8d0e0278 */ 	lw	$t6,0x278($t0)
/*  f11d744:	a7a20020 */ 	sh	$v0,0x20($sp)
/*  f11d748:	01c20019 */ 	multu	$t6,$v0
/*  f11d74c:	00001812 */ 	mflo	$v1
/*  f11d750:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d754:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11d758:
/*  f11d758:	30af0001 */ 	andi	$t7,$a1,0x1
.L0f11d75c:
/*  f11d75c:	11e00003 */ 	beqz	$t7,.L0f11d76c
/*  f11d760:	3401a000 */ 	dli	$at,0xa000
/*  f11d764:	10000002 */ 	beqz	$zero,.L0f11d770
/*  f11d768:	24021000 */ 	addiu	$v0,$zero,0x1000
.L0f11d76c:
/*  f11d76c:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d770:
/*  f11d770:	00432821 */ 	addu	$a1,$v0,$v1
/*  f11d774:	8d1902c4 */ 	lw	$t9,0x2c4($t0)
/*  f11d778:	00a12821 */ 	addu	$a1,$a1,$at
/*  f11d77c:	30b8ffff */ 	andi	$t8,$a1,0xffff
/*  f11d780:	03002825 */ 	or	$a1,$t8,$zero
/*  f11d784:	97a70020 */ 	lhu	$a3,0x20($sp)
/*  f11d788:	afa80018 */ 	sw	$t0,0x18($sp)
/*  f11d78c:	0fc472f6 */ 	jal	pak0f11cbd8
/*  f11d790:	03233021 */ 	addu	$a2,$t9,$v1
/*  f11d794:	14400003 */ 	bnez	$v0,.L0f11d7a4
/*  f11d798:	8fa80018 */ 	lw	$t0,0x18($sp)
/*  f11d79c:	10000005 */ 	beqz	$zero,.L0f11d7b4
/*  f11d7a0:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d7a4:
/*  f11d7a4:	8d090278 */ 	lw	$t1,0x278($t0)
/*  f11d7a8:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f11d7ac:	252a0001 */ 	addiu	$t2,$t1,0x1
/*  f11d7b0:	ad0a0278 */ 	sw	$t2,0x278($t0)
.L0f11d7b4:
/*  f11d7b4:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11d7b8:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f11d7bc:	03e00008 */ 	jr	$ra
/*  f11d7c0:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d680
/*  f117350:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f117354:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f117358:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11735c:	000e2603 */ 	sra	$a0,$t6,0x18
/*  f117360:	0004c080 */ 	sll	$t8,$a0,0x2
/*  f117364:	0304c023 */ 	subu	$t8,$t8,$a0
/*  f117368:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11736c:	0304c023 */ 	subu	$t8,$t8,$a0
/*  f117370:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f117374:	0304c021 */ 	addu	$t8,$t8,$a0
/*  f117378:	3c19800a */ 	lui	$t9,0x800a
/*  f11737c:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f117380:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f117384:	03194021 */ 	addu	$t0,$t8,$t9
/*  f117388:	8d02000c */ 	lw	$v0,0xc($t0)
/*  f11738c:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f117390:	00001825 */ 	or	$v1,$zero,$zero
/*  f117394:	1040000a */ 	beqz	$v0,.NB0f1173c0
/*  f117398:	24090040 */ 	addiu	$t1,$zero,0x40
/*  f11739c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1173a0:	1041000b */ 	beq	$v0,$at,.NB0f1173d0
/*  f1173a4:	24010002 */ 	addiu	$at,$zero,0x2
/*  f1173a8:	1041000f */ 	beq	$v0,$at,.NB0f1173e8
/*  f1173ac:	24010003 */ 	addiu	$at,$zero,0x3
/*  f1173b0:	10410013 */ 	beq	$v0,$at,.NB0f117400
/*  f1173b4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1173b8:	1000001a */ 	beqz	$zero,.NB0f117424
/*  f1173bc:	30af0001 */ 	andi	$t7,$a1,0x1
.NB0f1173c0:
/*  f1173c0:	240301a0 */ 	addiu	$v1,$zero,0x1a0
/*  f1173c4:	a7a90020 */ 	sh	$t1,0x20($sp)
/*  f1173c8:	10000015 */ 	beqz	$zero,.NB0f117420
/*  f1173cc:	24050001 */ 	addiu	$a1,$zero,0x1
.NB0f1173d0:
/*  f1173d0:	8d030278 */ 	lw	$v1,0x278($t0)
/*  f1173d4:	240b0040 */ 	addiu	$t3,$zero,0x40
/*  f1173d8:	a7ab0020 */ 	sh	$t3,0x20($sp)
/*  f1173dc:	00035200 */ 	sll	$t2,$v1,0x8
/*  f1173e0:	1000000f */ 	beqz	$zero,.NB0f117420
/*  f1173e4:	25430660 */ 	addiu	$v1,$t2,0x660
.NB0f1173e8:
/*  f1173e8:	8d030278 */ 	lw	$v1,0x278($t0)
/*  f1173ec:	240d0080 */ 	addiu	$t5,$zero,0x80
/*  f1173f0:	a7ad0020 */ 	sh	$t5,0x20($sp)
/*  f1173f4:	00036200 */ 	sll	$t4,$v1,0x8
/*  f1173f8:	10000009 */ 	beqz	$zero,.NB0f117420
/*  f1173fc:	25830440 */ 	addiu	$v1,$t4,0x440
.NB0f117400:
/*  f117400:	3c028008 */ 	lui	$v0,0x8008
/*  f117404:	8c428094 */ 	lw	$v0,-0x7f6c($v0)
/*  f117408:	8d0e0278 */ 	lw	$t6,0x278($t0)
/*  f11740c:	a7a20020 */ 	sh	$v0,0x20($sp)
/*  f117410:	01c20019 */ 	multu	$t6,$v0
/*  f117414:	00001812 */ 	mflo	$v1
/*  f117418:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11741c:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f117420:
/*  f117420:	30af0001 */ 	andi	$t7,$a1,0x1
.NB0f117424:
/*  f117424:	11e00003 */ 	beqz	$t7,.NB0f117434
/*  f117428:	3401a000 */ 	dli	$at,0xa000
/*  f11742c:	10000002 */ 	beqz	$zero,.NB0f117438
/*  f117430:	24021000 */ 	addiu	$v0,$zero,0x1000
.NB0f117434:
/*  f117434:	00001025 */ 	or	$v0,$zero,$zero
.NB0f117438:
/*  f117438:	00432821 */ 	addu	$a1,$v0,$v1
/*  f11743c:	8d1902c4 */ 	lw	$t9,0x2c4($t0)
/*  f117440:	00a12821 */ 	addu	$a1,$a1,$at
/*  f117444:	30b8ffff */ 	andi	$t8,$a1,0xffff
/*  f117448:	03002825 */ 	or	$a1,$t8,$zero
/*  f11744c:	97a70020 */ 	lhu	$a3,0x20($sp)
/*  f117450:	afa80018 */ 	sw	$t0,0x18($sp)
/*  f117454:	0fc45a16 */ 	jal	pak0f11cbd8
/*  f117458:	03233021 */ 	addu	$a2,$t9,$v1
/*  f11745c:	14400003 */ 	bnez	$v0,.NB0f11746c
/*  f117460:	8fa80018 */ 	lw	$t0,0x18($sp)
/*  f117464:	10000005 */ 	beqz	$zero,.NB0f11747c
/*  f117468:	00001025 */ 	or	$v0,$zero,$zero
.NB0f11746c:
/*  f11746c:	8d090278 */ 	lw	$t1,0x278($t0)
/*  f117470:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f117474:	252a0001 */ 	addiu	$t2,$t1,0x1
/*  f117478:	ad0a0278 */ 	sw	$t2,0x278($t0)
.NB0f11747c:
/*  f11747c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117480:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f117484:	03e00008 */ 	jr	$ra
/*  f117488:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d7c4
/*  f11d7c4:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d7c8:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11d7cc:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11d7d0:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d7d4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d7d8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d7dc:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d7e0:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11d7e4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d7e8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d7ec:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d7f0:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d7f4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d7f8:	03191821 */ 	addu	$v1,$t8,$t9
/*  f11d7fc:	8c62000c */ 	lw	$v0,0xc($v1)
/*  f11d800:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f11d804:	01e02025 */ 	or	$a0,$t7,$zero
/*  f11d808:	10400009 */ 	beqz	$v0,.L0f11d830
/*  f11d80c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11d810:	1041000d */ 	beq	$v0,$at,.L0f11d848
/*  f11d814:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11d818:	10410011 */ 	beq	$v0,$at,.L0f11d860
/*  f11d81c:	24010003 */ 	addiu	$at,$zero,0x3
/*  f11d820:	10410015 */ 	beq	$v0,$at,.L0f11d878
/*  f11d824:	3c0b8007 */ 	lui	$t3,%hi(var80075ccc)
/*  f11d828:	10000020 */ 	beqz	$zero,.L0f11d8ac
/*  f11d82c:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d830:
/*  f11d830:	8c680278 */ 	lw	$t0,0x278($v1)
/*  f11d834:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11d838:	5501001c */ 	bnel	$t0,$at,.L0f11d8ac
/*  f11d83c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d840:	03e00008 */ 	jr	$ra
/*  f11d844:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11d848:
/*  f11d848:	8c690278 */ 	lw	$t1,0x278($v1)
/*  f11d84c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11d850:	55210016 */ 	bnel	$t1,$at,.L0f11d8ac
/*  f11d854:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d858:	03e00008 */ 	jr	$ra
/*  f11d85c:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11d860:
/*  f11d860:	8c6a0278 */ 	lw	$t2,0x278($v1)
/*  f11d864:	24010008 */ 	addiu	$at,$zero,0x8
/*  f11d868:	55410010 */ 	bnel	$t2,$at,.L0f11d8ac
/*  f11d86c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d870:	03e00008 */ 	jr	$ra
/*  f11d874:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f11d878:
/*  f11d878:	8d6b5ccc */ 	lw	$t3,%lo(var80075ccc)($t3)
/*  f11d87c:	240c1000 */ 	addiu	$t4,$zero,0x1000
/*  f11d880:	8c6e0278 */ 	lw	$t6,0x278($v1)
/*  f11d884:	018b001b */ 	divu	$zero,$t4,$t3
/*  f11d888:	00006812 */ 	mflo	$t5
/*  f11d88c:	15600002 */ 	bnez	$t3,.L0f11d898
/*  f11d890:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d894:	0007000d */ 	break	0x7
.L0f11d898:
/*  f11d898:	55ae0004 */ 	bnel	$t5,$t6,.L0f11d8ac
/*  f11d89c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d8a0:	03e00008 */ 	jr	$ra
/*  f11d8a4:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f11d8a8:	00001025 */ 	or	$v0,$zero,$zero
.L0f11d8ac:
/*  f11d8ac:	03e00008 */ 	jr	$ra
/*  f11d8b0:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d7c4
/*  f11748c:	00047600 */ 	sll	$t6,$a0,0x18
/*  f117490:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f117494:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f117498:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11749c:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f1174a0:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f1174a4:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1174a8:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f1174ac:	3c19800a */ 	lui	$t9,0x800a
/*  f1174b0:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f1174b4:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1174b8:	03191821 */ 	addu	$v1,$t8,$t9
/*  f1174bc:	8c62000c */ 	lw	$v0,0xc($v1)
/*  f1174c0:	afa40000 */ 	sw	$a0,0x0($sp)
/*  f1174c4:	01e02025 */ 	or	$a0,$t7,$zero
/*  f1174c8:	10400009 */ 	beqz	$v0,.NB0f1174f0
/*  f1174cc:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1174d0:	1041000d */ 	beq	$v0,$at,.NB0f117508
/*  f1174d4:	24010002 */ 	addiu	$at,$zero,0x2
/*  f1174d8:	10410011 */ 	beq	$v0,$at,.NB0f117520
/*  f1174dc:	24010003 */ 	addiu	$at,$zero,0x3
/*  f1174e0:	10410015 */ 	beq	$v0,$at,.NB0f117538
/*  f1174e4:	3c0b8008 */ 	lui	$t3,0x8008
/*  f1174e8:	10000020 */ 	beqz	$zero,.NB0f11756c
/*  f1174ec:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1174f0:
/*  f1174f0:	8c680278 */ 	lw	$t0,0x278($v1)
/*  f1174f4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1174f8:	5501001c */ 	bnel	$t0,$at,.NB0f11756c
/*  f1174fc:	00001025 */ 	or	$v0,$zero,$zero
/*  f117500:	03e00008 */ 	jr	$ra
/*  f117504:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f117508:
/*  f117508:	8c690278 */ 	lw	$t1,0x278($v1)
/*  f11750c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f117510:	55210016 */ 	bnel	$t1,$at,.NB0f11756c
/*  f117514:	00001025 */ 	or	$v0,$zero,$zero
/*  f117518:	03e00008 */ 	jr	$ra
/*  f11751c:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f117520:
/*  f117520:	8c6a0278 */ 	lw	$t2,0x278($v1)
/*  f117524:	24010008 */ 	addiu	$at,$zero,0x8
/*  f117528:	55410010 */ 	bnel	$t2,$at,.NB0f11756c
/*  f11752c:	00001025 */ 	or	$v0,$zero,$zero
/*  f117530:	03e00008 */ 	jr	$ra
/*  f117534:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f117538:
/*  f117538:	8d6b8094 */ 	lw	$t3,-0x7f6c($t3)
/*  f11753c:	240c1000 */ 	addiu	$t4,$zero,0x1000
/*  f117540:	8c6e0278 */ 	lw	$t6,0x278($v1)
/*  f117544:	018b001b */ 	divu	$zero,$t4,$t3
/*  f117548:	00006812 */ 	mflo	$t5
/*  f11754c:	15600002 */ 	bnez	$t3,.NB0f117558
/*  f117550:	00000000 */ 	sll	$zero,$zero,0x0
/*  f117554:	0007000d */ 	break	0x7
.NB0f117558:
/*  f117558:	55ae0004 */ 	bnel	$t5,$t6,.NB0f11756c
/*  f11755c:	00001025 */ 	or	$v0,$zero,$zero
/*  f117560:	03e00008 */ 	jr	$ra
/*  f117564:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f117568:	00001025 */ 	or	$v0,$zero,$zero
.NB0f11756c:
/*  f11756c:	03e00008 */ 	jr	$ra
/*  f117570:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

GLOBAL_ASM(
glabel pak0f11d8b4
/*  f11d8b4:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f11d8b8:	afb6001c */ 	sw	$s6,0x1c($sp)
/*  f11d8bc:	afb50018 */ 	sw	$s5,0x18($sp)
/*  f11d8c0:	afb40014 */ 	sw	$s4,0x14($sp)
/*  f11d8c4:	afb30010 */ 	sw	$s3,0x10($sp)
/*  f11d8c8:	afb10008 */ 	sw	$s1,0x8($sp)
/*  f11d8cc:	afb00004 */ 	sw	$s0,0x4($sp)
/*  f11d8d0:	00a08025 */ 	or	$s0,$a1,$zero
/*  f11d8d4:	00808825 */ 	or	$s1,$a0,$zero
/*  f11d8d8:	afb2000c */ 	sw	$s2,0xc($sp)
/*  f11d8dc:	24130008 */ 	addiu	$s3,$zero,0x8
/*  f11d8e0:	24140010 */ 	addiu	$s4,$zero,0x10
/*  f11d8e4:	24150080 */ 	addiu	$s5,$zero,0x80
/*  f11d8e8:	24160080 */ 	addiu	$s6,$zero,0x80
/*  f11d8ec:	00001825 */ 	or	$v1,$zero,$zero
/*  f11d8f0:	00004025 */ 	or	$t0,$zero,$zero
.L0f11d8f4:
/*  f11d8f4:	00032100 */ 	sll	$a0,$v1,0x4
/*  f11d8f8:	00003025 */ 	or	$a2,$zero,$zero
.L0f11d8fc:
/*  f11d8fc:	00002825 */ 	or	$a1,$zero,$zero
/*  f11d900:	01004825 */ 	or	$t1,$t0,$zero
/*  f11d904:	00919021 */ 	addu	$s2,$a0,$s1
.L0f11d908:
/*  f11d908:	000951c0 */ 	sll	$t2,$t1,0x7
/*  f11d90c:	020a5821 */ 	addu	$t3,$s0,$t2
/*  f11d910:	00001025 */ 	or	$v0,$zero,$zero
/*  f11d914:	00c03825 */ 	or	$a3,$a2,$zero
.L0f11d918:
/*  f11d918:	01677021 */ 	addu	$t6,$t3,$a3
/*  f11d91c:	a1c00000 */ 	sb	$zero,0x0($t6)
/*  f11d920:	92580000 */ 	lbu	$t8,0x0($s2)
/*  f11d924:	240f0080 */ 	addiu	$t7,$zero,0x80
/*  f11d928:	004f6007 */ 	srav	$t4,$t7,$v0
/*  f11d92c:	030cc824 */ 	and	$t9,$t8,$t4
/*  f11d930:	17200006 */ 	bnez	$t9,.L0f11d94c
/*  f11d934:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11d938:	01477021 */ 	addu	$t6,$t2,$a3
/*  f11d93c:	01d06821 */ 	addu	$t5,$t6,$s0
/*  f11d940:	91af0000 */ 	lbu	$t7,0x0($t5)
/*  f11d944:	25f80040 */ 	addiu	$t8,$t7,0x40
/*  f11d948:	a1b80000 */ 	sb	$t8,0x0($t5)
.L0f11d94c:
/*  f11d94c:	92590001 */ 	lbu	$t9,0x1($s2)
/*  f11d950:	01477821 */ 	addu	$t7,$t2,$a3
/*  f11d954:	01f06821 */ 	addu	$t5,$t7,$s0
/*  f11d958:	032c7024 */ 	and	$t6,$t9,$t4
/*  f11d95c:	15c00004 */ 	bnez	$t6,.L0f11d970
/*  f11d960:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11d964:	91b80000 */ 	lbu	$t8,0x0($t5)
/*  f11d968:	27190080 */ 	addiu	$t9,$t8,0x80
/*  f11d96c:	a1b90000 */ 	sb	$t9,0x0($t5)
.L0f11d970:
/*  f11d970:	1453ffe9 */ 	bne	$v0,$s3,.L0f11d918
/*  f11d974:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f11d978:	24a50002 */ 	addiu	$a1,$a1,0x2
/*  f11d97c:	25290001 */ 	addiu	$t1,$t1,0x1
/*  f11d980:	14b4ffe1 */ 	bne	$a1,$s4,.L0f11d908
/*  f11d984:	26520002 */ 	addiu	$s2,$s2,0x2
/*  f11d988:	24c60008 */ 	addiu	$a2,$a2,0x8
/*  f11d98c:	14d5ffdb */ 	bne	$a2,$s5,.L0f11d8fc
/*  f11d990:	24840010 */ 	addiu	$a0,$a0,0x10
/*  f11d994:	25080008 */ 	addiu	$t0,$t0,0x8
/*  f11d998:	1516ffd6 */ 	bne	$t0,$s6,.L0f11d8f4
/*  f11d99c:	24630010 */ 	addiu	$v1,$v1,0x10
/*  f11d9a0:	8fb00004 */ 	lw	$s0,0x4($sp)
/*  f11d9a4:	8fb10008 */ 	lw	$s1,0x8($sp)
/*  f11d9a8:	8fb2000c */ 	lw	$s2,0xc($sp)
/*  f11d9ac:	8fb30010 */ 	lw	$s3,0x10($sp)
/*  f11d9b0:	8fb40014 */ 	lw	$s4,0x14($sp)
/*  f11d9b4:	8fb50018 */ 	lw	$s5,0x18($sp)
/*  f11d9b8:	8fb6001c */ 	lw	$s6,0x1c($sp)
/*  f11d9bc:	03e00008 */ 	jr	$ra
/*  f11d9c0:	27bd0020 */ 	addiu	$sp,$sp,0x20
);

#if VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11d9c4
/*  f11d9c4:	00047600 */ 	sll	$t6,$a0,0x18
/*  f11d9c8:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11d9cc:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11d9d0:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d9d4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d9d8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d9dc:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d9e0:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f11d9e4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d9e8:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11d9ec:	3c19800a */ 	lui	$t9,%hi(g_Paks)
/*  f11d9f0:	27392380 */ 	addiu	$t9,$t9,%lo(g_Paks)
/*  f11d9f4:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f11d9f8:	03194021 */ 	addu	$t0,$t8,$t9
/*  f11d9fc:	8d02000c */ 	lw	$v0,0xc($t0)
/*  f11da00:	27bdbfa0 */ 	addiu	$sp,$sp,-16480
/*  f11da04:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11da08:	00a08025 */ 	or	$s0,$a1,$zero
/*  f11da0c:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11da10:	afa44060 */ 	sw	$a0,0x4060($sp)
/*  f11da14:	1040000a */ 	beqz	$v0,.L0f11da40
/*  f11da18:	afa7406c */ 	sw	$a3,0x406c($sp)
/*  f11da1c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11da20:	10410016 */ 	beq	$v0,$at,.L0f11da7c
/*  f11da24:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11da28:	1041002c */ 	beq	$v0,$at,.L0f11dadc
/*  f11da2c:	24010003 */ 	addiu	$at,$zero,0x3
/*  f11da30:	10410042 */ 	beq	$v0,$at,.L0f11db3c
/*  f11da34:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11da38:	10000044 */ 	beqz	$zero,.L0f11db4c
/*  f11da3c:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11da40:
/*  f11da40:	10c00042 */ 	beqz	$a2,.L0f11db4c
/*  f11da44:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11da48:	8d0502c4 */ 	lw	$a1,0x2c4($t0)
/*  f11da4c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11da50:	00c01825 */ 	or	$v1,$a2,$zero
/*  f11da54:	24a401b2 */ 	addiu	$a0,$a1,0x1b2
/*  f11da58:	2405001e */ 	addiu	$a1,$zero,0x1e
.L0f11da5c:
/*  f11da5c:	90890000 */ 	lbu	$t1,0x0($a0)
/*  f11da60:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11da64:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11da68:	24840001 */ 	addiu	$a0,$a0,0x1
/*  f11da6c:	1445fffb */ 	bne	$v0,$a1,.L0f11da5c
/*  f11da70:	a069ffff */ 	sb	$t1,-0x1($v1)
/*  f11da74:	10000035 */ 	beqz	$zero,.L0f11db4c
/*  f11da78:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11da7c:
/*  f11da7c:	12000033 */ 	beqz	$s0,.L0f11db4c
/*  f11da80:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f11da84:	0fc4762d */ 	jal	pak0f11d8b4
/*  f11da88:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
/*  f11da8c:	27a60060 */ 	addiu	$a2,$sp,0x60
/*  f11da90:	00003825 */ 	or	$a3,$zero,$zero
/*  f11da94:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f11da98:	00001025 */ 	or	$v0,$zero,$zero
.L0f11da9c:
/*  f11da9c:	02071821 */ 	addu	$v1,$s0,$a3
/*  f11daa0:	00072882 */ 	srl	$a1,$a3,0x2
.L0f11daa4:
/*  f11daa4:	00025082 */ 	srl	$t2,$v0,0x2
/*  f11daa8:	000a59c0 */ 	sll	$t3,$t2,0x7
/*  f11daac:	00cb6021 */ 	addu	$t4,$a2,$t3
/*  f11dab0:	01856821 */ 	addu	$t5,$t4,$a1
/*  f11dab4:	91ae1830 */ 	lbu	$t6,0x1830($t5)
/*  f11dab8:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11dabc:	24630080 */ 	addiu	$v1,$v1,0x80
/*  f11dac0:	1444fff8 */ 	bne	$v0,$a0,.L0f11daa4
/*  f11dac4:	a06eff80 */ 	sb	$t6,-0x80($v1)
/*  f11dac8:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f11dacc:	54e4fff3 */ 	bnel	$a3,$a0,.L0f11da9c
/*  f11dad0:	00001025 */ 	or	$v0,$zero,$zero
/*  f11dad4:	1000001d */ 	beqz	$zero,.L0f11db4c
/*  f11dad8:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11dadc:
/*  f11dadc:	1200001b */ 	beqz	$s0,.L0f11db4c
/*  f11dae0:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f11dae4:	0fc4762d */ 	jal	pak0f11d8b4
/*  f11dae8:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
/*  f11daec:	27a60060 */ 	addiu	$a2,$sp,0x60
/*  f11daf0:	00003825 */ 	or	$a3,$zero,$zero
/*  f11daf4:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f11daf8:	00001025 */ 	or	$v0,$zero,$zero
.L0f11dafc:
/*  f11dafc:	02071821 */ 	addu	$v1,$s0,$a3
/*  f11db00:	00072842 */ 	srl	$a1,$a3,0x1
.L0f11db04:
/*  f11db04:	00027842 */ 	srl	$t7,$v0,0x1
/*  f11db08:	000fc1c0 */ 	sll	$t8,$t7,0x7
/*  f11db0c:	00d8c821 */ 	addu	$t9,$a2,$t8
/*  f11db10:	03254821 */ 	addu	$t1,$t9,$a1
/*  f11db14:	912a1020 */ 	lbu	$t2,0x1020($t1)
/*  f11db18:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11db1c:	24630080 */ 	addiu	$v1,$v1,0x80
/*  f11db20:	1444fff8 */ 	bne	$v0,$a0,.L0f11db04
/*  f11db24:	a06aff80 */ 	sb	$t2,-0x80($v1)
/*  f11db28:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f11db2c:	54e4fff3 */ 	bnel	$a3,$a0,.L0f11dafc
/*  f11db30:	00001025 */ 	or	$v0,$zero,$zero
/*  f11db34:	10000005 */ 	beqz	$zero,.L0f11db4c
/*  f11db38:	00000000 */ 	sll	$zero,$zero,0x0
.L0f11db3c:
/*  f11db3c:	12000003 */ 	beqz	$s0,.L0f11db4c
/*  f11db40:	02002825 */ 	or	$a1,$s0,$zero
/*  f11db44:	0fc4762d */ 	jal	pak0f11d8b4
/*  f11db48:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
.L0f11db4c:
/*  f11db4c:	12000028 */ 	beqz	$s0,.L0f11dbf0
/*  f11db50:	8fab406c */ 	lw	$t3,0x406c($sp)
/*  f11db54:	11600026 */ 	beqz	$t3,.L0f11dbf0
/*  f11db58:	24080008 */ 	addiu	$t0,$zero,0x8
/*  f11db5c:	24053c00 */ 	addiu	$a1,$zero,0x3c00
/*  f11db60:	24060400 */ 	addiu	$a2,$zero,0x400
/*  f11db64:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f11db68:	00001025 */ 	or	$v0,$zero,$zero
.L0f11db6c:
/*  f11db6c:	00b01821 */ 	addu	$v1,$a1,$s0
.L0f11db70:
/*  f11db70:	906cfc00 */ 	lbu	$t4,-0x400($v1)
/*  f11db74:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11db78:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11db7c:	1444fffc */ 	bne	$v0,$a0,.L0f11db70
/*  f11db80:	a06cffff */ 	sb	$t4,-0x1($v1)
/*  f11db84:	24a5ff80 */ 	addiu	$a1,$a1,-128
/*  f11db88:	00c5082a */ 	slt	$at,$a2,$a1
/*  f11db8c:	5420fff7 */ 	bnezl	$at,.L0f11db6c
/*  f11db90:	00001025 */ 	or	$v0,$zero,$zero
/*  f11db94:	00003825 */ 	or	$a3,$zero,$zero
/*  f11db98:	02003025 */ 	or	$a2,$s0,$zero
.L0f11db9c:
/*  f11db9c:	00001025 */ 	or	$v0,$zero,$zero
/*  f11dba0:	00c01825 */ 	or	$v1,$a2,$zero
.L0f11dba4:
/*  f11dba4:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11dba8:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11dbac:	1444fffd */ 	bne	$v0,$a0,.L0f11dba4
/*  f11dbb0:	a060ffff */ 	sb	$zero,-0x1($v1)
/*  f11dbb4:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f11dbb8:	14e8fff8 */ 	bne	$a3,$t0,.L0f11db9c
/*  f11dbbc:	24c60080 */ 	addiu	$a2,$a2,0x80
/*  f11dbc0:	24053c00 */ 	addiu	$a1,$zero,0x3c00
/*  f11dbc4:	26063c00 */ 	addiu	$a2,$s0,0x3c00
/*  f11dbc8:	24074000 */ 	addiu	$a3,$zero,0x4000
.L0f11dbcc:
/*  f11dbcc:	00001025 */ 	or	$v0,$zero,$zero
/*  f11dbd0:	00c01825 */ 	or	$v1,$a2,$zero
.L0f11dbd4:
/*  f11dbd4:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11dbd8:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11dbdc:	1444fffd */ 	bne	$v0,$a0,.L0f11dbd4
/*  f11dbe0:	a060ffff */ 	sb	$zero,-0x1($v1)
/*  f11dbe4:	24a50080 */ 	addiu	$a1,$a1,0x80
/*  f11dbe8:	14a7fff8 */ 	bne	$a1,$a3,.L0f11dbcc
/*  f11dbec:	24c60080 */ 	addiu	$a2,$a2,0x80
.L0f11dbf0:
/*  f11dbf0:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11dbf4:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11dbf8:	27bd4060 */ 	addiu	$sp,$sp,0x4060
/*  f11dbfc:	03e00008 */ 	jr	$ra
/*  f11dc00:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11d9c4
/*  f117684:	00047600 */ 	sll	$t6,$a0,0x18
/*  f117688:	000e7e03 */ 	sra	$t7,$t6,0x18
/*  f11768c:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f117690:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f117694:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f117698:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f11769c:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1176a0:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f1176a4:	3c19800a */ 	lui	$t9,0x800a
/*  f1176a8:	27396870 */ 	addiu	$t9,$t9,0x6870
/*  f1176ac:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f1176b0:	03194021 */ 	addu	$t0,$t8,$t9
/*  f1176b4:	8d02000c */ 	lw	$v0,0xc($t0)
/*  f1176b8:	27bdbfa0 */ 	addiu	$sp,$sp,-16480
/*  f1176bc:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f1176c0:	00a08025 */ 	or	$s0,$a1,$zero
/*  f1176c4:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f1176c8:	afa44060 */ 	sw	$a0,0x4060($sp)
/*  f1176cc:	1040000a */ 	beqz	$v0,.NB0f1176f8
/*  f1176d0:	afa7406c */ 	sw	$a3,0x406c($sp)
/*  f1176d4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f1176d8:	10410016 */ 	beq	$v0,$at,.NB0f117734
/*  f1176dc:	24010002 */ 	addiu	$at,$zero,0x2
/*  f1176e0:	1041002c */ 	beq	$v0,$at,.NB0f117794
/*  f1176e4:	24010003 */ 	addiu	$at,$zero,0x3
/*  f1176e8:	10410042 */ 	beq	$v0,$at,.NB0f1177f4
/*  f1176ec:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1176f0:	10000044 */ 	beqz	$zero,.NB0f117804
/*  f1176f4:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f1176f8:
/*  f1176f8:	10c00042 */ 	beqz	$a2,.NB0f117804
/*  f1176fc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f117700:	8d0502c4 */ 	lw	$a1,0x2c4($t0)
/*  f117704:	00001025 */ 	or	$v0,$zero,$zero
/*  f117708:	00c01825 */ 	or	$v1,$a2,$zero
/*  f11770c:	24a401b2 */ 	addiu	$a0,$a1,0x1b2
/*  f117710:	2405001e */ 	addiu	$a1,$zero,0x1e
.NB0f117714:
/*  f117714:	90890000 */ 	lbu	$t1,0x0($a0)
/*  f117718:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11771c:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f117720:	24840001 */ 	addiu	$a0,$a0,0x1
/*  f117724:	1445fffb */ 	bne	$v0,$a1,.NB0f117714
/*  f117728:	a069ffff */ 	sb	$t1,-0x1($v1)
/*  f11772c:	10000035 */ 	beqz	$zero,.NB0f117804
/*  f117730:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f117734:
/*  f117734:	12000033 */ 	beqz	$s0,.NB0f117804
/*  f117738:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f11773c:	0fc45d5d */ 	jal	pak0f11d8b4
/*  f117740:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
/*  f117744:	27a60060 */ 	addiu	$a2,$sp,0x60
/*  f117748:	00003825 */ 	or	$a3,$zero,$zero
/*  f11774c:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f117750:	00001025 */ 	or	$v0,$zero,$zero
.NB0f117754:
/*  f117754:	02071821 */ 	addu	$v1,$s0,$a3
/*  f117758:	00072882 */ 	srl	$a1,$a3,0x2
.NB0f11775c:
/*  f11775c:	00025082 */ 	srl	$t2,$v0,0x2
/*  f117760:	000a59c0 */ 	sll	$t3,$t2,0x7
/*  f117764:	00cb6021 */ 	addu	$t4,$a2,$t3
/*  f117768:	01856821 */ 	addu	$t5,$t4,$a1
/*  f11776c:	91ae1830 */ 	lbu	$t6,0x1830($t5)
/*  f117770:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f117774:	24630080 */ 	addiu	$v1,$v1,0x80
/*  f117778:	1444fff8 */ 	bne	$v0,$a0,.NB0f11775c
/*  f11777c:	a06eff80 */ 	sb	$t6,-0x80($v1)
/*  f117780:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f117784:	54e4fff3 */ 	bnel	$a3,$a0,.NB0f117754
/*  f117788:	00001025 */ 	or	$v0,$zero,$zero
/*  f11778c:	1000001d */ 	beqz	$zero,.NB0f117804
/*  f117790:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f117794:
/*  f117794:	1200001b */ 	beqz	$s0,.NB0f117804
/*  f117798:	27a50060 */ 	addiu	$a1,$sp,0x60
/*  f11779c:	0fc45d5d */ 	jal	pak0f11d8b4
/*  f1177a0:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
/*  f1177a4:	27a60060 */ 	addiu	$a2,$sp,0x60
/*  f1177a8:	00003825 */ 	or	$a3,$zero,$zero
/*  f1177ac:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f1177b0:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1177b4:
/*  f1177b4:	02071821 */ 	addu	$v1,$s0,$a3
/*  f1177b8:	00072842 */ 	srl	$a1,$a3,0x1
.NB0f1177bc:
/*  f1177bc:	00027842 */ 	srl	$t7,$v0,0x1
/*  f1177c0:	000fc1c0 */ 	sll	$t8,$t7,0x7
/*  f1177c4:	00d8c821 */ 	addu	$t9,$a2,$t8
/*  f1177c8:	03254821 */ 	addu	$t1,$t9,$a1
/*  f1177cc:	912a1020 */ 	lbu	$t2,0x1020($t1)
/*  f1177d0:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f1177d4:	24630080 */ 	addiu	$v1,$v1,0x80
/*  f1177d8:	1444fff8 */ 	bne	$v0,$a0,.NB0f1177bc
/*  f1177dc:	a06aff80 */ 	sb	$t2,-0x80($v1)
/*  f1177e0:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f1177e4:	54e4fff3 */ 	bnel	$a3,$a0,.NB0f1177b4
/*  f1177e8:	00001025 */ 	or	$v0,$zero,$zero
/*  f1177ec:	10000005 */ 	beqz	$zero,.NB0f117804
/*  f1177f0:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f1177f4:
/*  f1177f4:	12000003 */ 	beqz	$s0,.NB0f117804
/*  f1177f8:	02002825 */ 	or	$a1,$s0,$zero
/*  f1177fc:	0fc45d5d */ 	jal	pak0f11d8b4
/*  f117800:	8d0402c4 */ 	lw	$a0,0x2c4($t0)
.NB0f117804:
/*  f117804:	12000028 */ 	beqz	$s0,.NB0f1178a8
/*  f117808:	8fab406c */ 	lw	$t3,0x406c($sp)
/*  f11780c:	11600026 */ 	beqz	$t3,.NB0f1178a8
/*  f117810:	24080008 */ 	addiu	$t0,$zero,0x8
/*  f117814:	24053c00 */ 	addiu	$a1,$zero,0x3c00
/*  f117818:	24060400 */ 	addiu	$a2,$zero,0x400
/*  f11781c:	24040080 */ 	addiu	$a0,$zero,0x80
/*  f117820:	00001025 */ 	or	$v0,$zero,$zero
.NB0f117824:
/*  f117824:	00b01821 */ 	addu	$v1,$a1,$s0
.NB0f117828:
/*  f117828:	906cfc00 */ 	lbu	$t4,-0x400($v1)
/*  f11782c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f117830:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f117834:	1444fffc */ 	bne	$v0,$a0,.NB0f117828
/*  f117838:	a06cffff */ 	sb	$t4,-0x1($v1)
/*  f11783c:	24a5ff80 */ 	addiu	$a1,$a1,-128
/*  f117840:	00c5082a */ 	slt	$at,$a2,$a1
/*  f117844:	5420fff7 */ 	bnezl	$at,.NB0f117824
/*  f117848:	00001025 */ 	or	$v0,$zero,$zero
/*  f11784c:	00003825 */ 	or	$a3,$zero,$zero
/*  f117850:	02003025 */ 	or	$a2,$s0,$zero
.NB0f117854:
/*  f117854:	00001025 */ 	or	$v0,$zero,$zero
/*  f117858:	00c01825 */ 	or	$v1,$a2,$zero
.NB0f11785c:
/*  f11785c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f117860:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f117864:	1444fffd */ 	bne	$v0,$a0,.NB0f11785c
/*  f117868:	a060ffff */ 	sb	$zero,-0x1($v1)
/*  f11786c:	24e70001 */ 	addiu	$a3,$a3,0x1
/*  f117870:	14e8fff8 */ 	bne	$a3,$t0,.NB0f117854
/*  f117874:	24c60080 */ 	addiu	$a2,$a2,0x80
/*  f117878:	24053c00 */ 	addiu	$a1,$zero,0x3c00
/*  f11787c:	26063c00 */ 	addiu	$a2,$s0,0x3c00
/*  f117880:	24074000 */ 	addiu	$a3,$zero,0x4000
.NB0f117884:
/*  f117884:	00001025 */ 	or	$v0,$zero,$zero
/*  f117888:	00c01825 */ 	or	$v1,$a2,$zero
.NB0f11788c:
/*  f11788c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f117890:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f117894:	1444fffd */ 	bne	$v0,$a0,.NB0f11788c
/*  f117898:	a060ffff */ 	sb	$zero,-0x1($v1)
/*  f11789c:	24a50080 */ 	addiu	$a1,$a1,0x80
/*  f1178a0:	14a7fff8 */ 	bne	$a1,$a3,.NB0f117884
/*  f1178a4:	24c60080 */ 	addiu	$a2,$a2,0x80
.NB0f1178a8:
/*  f1178a8:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f1178ac:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f1178b0:	27bd4060 */ 	addiu	$sp,$sp,0x4060
/*  f1178b4:	03e00008 */ 	jr	$ra
/*  f1178b8:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f11dc04(s32 device, f32 arg1, s32 arg2, s32 arg3)
{
	if (g_Paks[device].unk010 == 11
			&& g_Paks[device].unk000 == 1
			&& g_Paks[device].unk004 != 6
			&& g_Paks[device].unk004 != 7
			&& g_Paks[device].unk2b4 < 60 * arg1) {
		g_Paks[device].unk004 = 3;
		g_Paks[device].unk2b4 = 60 * arg1;
		g_Paks[device].unk284 = arg2;
		g_Paks[device].unk288 = arg2 + arg3;
		g_Paks[device].unk28c = 0;
	}
}
#else
void pak0f11dc04(s8 device, f32 arg1, s32 arg2, s32 arg3)
{
	// This might be a global variable rather than static
	static u8 map[] = {0, 1, 0, 0, 0}; // 0x8009eb9c (ntsc-beta)
	u8 index = map[device];

	if (g_Paks[index].unk010 == 11
			&& g_Paks[index].unk000 == 1
			&& g_Paks[index].unk004 != 6
			&& g_Paks[index].unk004 != 7
			&& g_Paks[index].unk2b4 < 60 * arg1) {
		g_Paks[index].unk004 = 3;
		g_Paks[index].unk2b4 = 60 * arg1;
		g_Paks[index].unk284 = arg2;
		g_Paks[index].unk288 = arg2 + arg3;
		g_Paks[index].unk28c = 0;
	}
}
#endif

void pak0f11dcb0(s32 arg0)
{
	s8 i;

	for (i = 0; i < 5; i++) {
		s32 value = g_Paks[i].unk000;

		if (value);

		if (value != 2 && value != 3) {
			joy000153c4(i, arg0);
		}
	}
}

void pak0f11dd58(s8 playernum)
{
	s32 i;
	s32 tmp = playernum;
	s32 contpads[2];

#if VERSION >= VERSION_NTSC_1_0
	joyGetContpadNumsForPlayer(tmp, &contpads[0], &contpads[1]);

	for (i = 0; i < 2; i++) {
		if (contpads[i] >= 0 && g_Paks[contpads[i]].unk000 == 1) {
			g_Paks[contpads[i]].unk004 = 6;
			joy000153c4(contpads[i], 1);
		}
	}
#else
	if (g_Paks[playernum].unk000 == 1) {
		g_Paks[playernum].unk004 = 6;
		joy000153c4(playernum, 1);
	}
#endif
}

void pak0f11de20(s8 playernum)
{
	s32 i;
	s32 tmp = playernum;
	s32 contpads[2];

#if VERSION >= VERSION_NTSC_1_0
	joyGetContpadNumsForPlayer(tmp, &contpads[0], &contpads[1]);

	for (i = 0; i < 2; i++) {
		if (contpads[i] >= 0 && g_Paks[contpads[i]].unk000 == 1 && g_Paks[contpads[i]].unk004 == 7) {
			g_Paks[contpads[i]].unk004 = 8;
		}
	}
#else
	if (g_Paks[playernum].unk000 == 1 && g_Paks[playernum].unk004 == 7) {
		g_Paks[playernum].unk004 = 8;
	}
#endif
}

void pak0f11deb8(void)
{
	s32 i;

#if VERSION >= VERSION_NTSC_1_0
	for (i = 0; i < 4; i++) {
		if (g_Paks[i].unk000 == 1) {
			g_Paks[i].unk004 = 6;
			joy000153c4(i, 1);
		}
	}
#else
	for (i = 0; i < 4; i++) {
		pak0f11dd58(i);
	}
#endif
}

void pak0f11df38(void)
{
	s32 i;

#if VERSION >= VERSION_NTSC_FINAL
	for (i = 0; i < 4; i++) {
		if (g_Paks[i].unk000 == 1 && g_Paks[i].unk004 == 7) {
			g_Paks[i].unk004 = 8;
		}
	}
#else
	for (i = 0; i < 4; i++) {
		pak0f11de20(i);
	}
#endif
}

#if VERSION >= VERSION_NTSC_1_0
s32 pak0f11df84(s32 arg0)
{
	return arg0;
}
#endif

void pakDumpPak(void)
{
	// empty
}

#if VERSION >= VERSION_NTSC_FINAL
GLOBAL_ASM(
glabel pak0f11df94
.late_rodata
glabel var7f1b4f6c
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4f70
.word pak0f11df94+0x68 # f11dffc
glabel var7f1b4f74
.word pak0f11df94+0xb4 # f11e048
glabel var7f1b4f78
.word pak0f11df94+0xe8 # f11e07c
glabel var7f1b4f7c
.word pak0f11df94+0x154 # f11e0e8
glabel var7f1b4f80
.word pak0f11df94+0x184 # f11e118
glabel var7f1b4f84
.word pak0f11df94+0x1a4 # f11e138
glabel var7f1b4f88
.word pak0f11df94+0x12c # f11e0c0
glabel var7f1b4f8c
.word pak0f11df94+0x1cc # f11e160
glabel var7f1b4f90
.word pak0f11df94+0x1e4 # f11e178
glabel var7f1b4f94
.word pak0f11df94+0x1f4 # f11e188
glabel var7f1b4f98
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4f9c
.word pak0f11df94+0x218 # f11e1ac
glabel var7f1b4fa0
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fa4
.word pak0f11df94+0x2b8 # f11e24c
glabel var7f1b4fa8
.word pak0f11df94+0x224 # f11e1b8
glabel var7f1b4fac
.word pak0f11df94+0x318 # f11e2ac
glabel var7f1b4fb0
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fb4
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fb8
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fbc
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fc0
.word pak0f11df94+0x378 # f11e30c
glabel var7f1b4fc4
.word pak0f11df94+0x39c # f11e330
glabel var7f1b4fc8
.word pak0f11df94+0x3b8 # f11e34c
glabel var7f1b4fcc
.word pak0f11df94+0x200 # f11e194
glabel var7f1b4fd0
.word pak0f11df94+0x20c # f11e1a0
glabel var7f1b4fd4
.word pak0f11df94+0x284 # f11e218
glabel var7f1b4fd8
.word pak0f11df94+0x2ac # f11e240
.text
/*  f11df94:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f11df98:	afa40020 */ 	sw	$a0,0x20($sp)
/*  f11df9c:	83ae0023 */ 	lb	$t6,0x23($sp)
/*  f11dfa0:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11dfa4:	afb00018 */ 	sw	$s0,0x18($sp)
/*  f11dfa8:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11dfac:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dfb0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dfb4:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dfb8:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dfbc:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11dfc0:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dfc4:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dfc8:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dfcc:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11dfd0:	01f88021 */ 	addu	$s0,$t7,$t8
/*  f11dfd4:	8e190010 */ 	lw	$t9,0x10($s0)
/*  f11dfd8:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11dfdc:	2f21001c */ 	sltiu	$at,$t9,0x1c
/*  f11dfe0:	102000da */ 	beqz	$at,.L0f11e34c
/*  f11dfe4:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11dfe8:	3c017f1b */ 	lui	$at,%hi(var7f1b4f6c)
/*  f11dfec:	00390821 */ 	addu	$at,$at,$t9
/*  f11dff0:	8c394f6c */ 	lw	$t9,%lo(var7f1b4f6c)($at)
/*  f11dff4:	03200008 */ 	jr	$t9
/*  f11dff8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11dffc:	8e080264 */ 	lw	$t0,0x264($s0)
/*  f11e000:	920a02b8 */ 	lbu	$t2,0x2b8($s0)
/*  f11e004:	ae000010 */ 	sw	$zero,0x10($s0)
/*  f11e008:	25090001 */ 	addiu	$t1,$t0,0x1
/*  f11e00c:	314bfffd */ 	andi	$t3,$t2,0xfffd
/*  f11e010:	ae090264 */ 	sw	$t1,0x264($s0)
/*  f11e014:	a20b02b8 */ 	sb	$t3,0x2b8($s0)
/*  f11e018:	ae000000 */ 	sw	$zero,0x0($s0)
/*  f11e01c:	3c0c8007 */ 	lui	$t4,%hi(var80075d14)
/*  f11e020:	8d8c5d14 */ 	lw	$t4,%lo(var80075d14)($t4)
/*  f11e024:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e028:	11800003 */ 	beqz	$t4,.L0f11e038
/*  f11e02c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11e030:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e034:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11e038:
/*  f11e038:	0fc52bb4 */ 	jal	func0f14aed0
/*  f11e03c:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e040:	100000c3 */ 	beqz	$zero,.L0f11e350
/*  f11e044:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e048:	3c0d8007 */ 	lui	$t5,%hi(var80075d14)
/*  f11e04c:	8dad5d14 */ 	lw	$t5,%lo(var80075d14)($t5)
/*  f11e050:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e054:	11a00003 */ 	beqz	$t5,.L0f11e064
/*  f11e058:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11e05c:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e060:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11e064:
/*  f11e064:	0fc46b1f */ 	jal	pak0f11ac7c
/*  f11e068:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e06c:	0fc52bb4 */ 	jal	func0f14aed0
/*  f11e070:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e074:	100000b6 */ 	beqz	$zero,.L0f11e350
/*  f11e078:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e07c:	83af0023 */ 	lb	$t7,0x23($sp)
/*  f11e080:	3c0e800a */ 	lui	$t6,%hi(g_Vars+0x4d0)
/*  f11e084:	91cea490 */ 	lbu	$t6,%lo(g_Vars+0x4d0)($t6)
/*  f11e088:	24180001 */ 	addiu	$t8,$zero,0x1
/*  f11e08c:	01f8c804 */ 	sllv	$t9,$t8,$t7
/*  f11e090:	01d94024 */ 	and	$t0,$t6,$t9
/*  f11e094:	11000008 */ 	beqz	$t0,.L0f11e0b8
/*  f11e098:	240a0007 */ 	addiu	$t2,$zero,0x7
/*  f11e09c:	24020004 */ 	addiu	$v0,$zero,0x4
/*  f11e0a0:	15e20003 */ 	bne	$t7,$v0,.L0f11e0b0
/*  f11e0a4:	24090005 */ 	addiu	$t1,$zero,0x5
/*  f11e0a8:	100000a8 */ 	beqz	$zero,.L0f11e34c
/*  f11e0ac:	ae090010 */ 	sw	$t1,0x10($s0)
.L0f11e0b0:
/*  f11e0b0:	100000a6 */ 	beqz	$zero,.L0f11e34c
/*  f11e0b4:	ae020010 */ 	sw	$v0,0x10($s0)
.L0f11e0b8:
/*  f11e0b8:	100000a4 */ 	beqz	$zero,.L0f11e34c
/*  f11e0bc:	ae0a0010 */ 	sw	$t2,0x10($s0)
/*  f11e0c0:	83ac0023 */ 	lb	$t4,0x23($sp)
/*  f11e0c4:	3c0b800a */ 	lui	$t3,%hi(g_Vars+0x4d0)
/*  f11e0c8:	916ba490 */ 	lbu	$t3,%lo(g_Vars+0x4d0)($t3)
/*  f11e0cc:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11e0d0:	018dc004 */ 	sllv	$t8,$t5,$t4
/*  f11e0d4:	01787024 */ 	and	$t6,$t3,$t8
/*  f11e0d8:	11c0009c */ 	beqz	$t6,.L0f11e34c
/*  f11e0dc:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f11e0e0:	1000009a */ 	beqz	$zero,.L0f11e34c
/*  f11e0e4:	ae190010 */ 	sw	$t9,0x10($s0)
/*  f11e0e8:	3c0f8007 */ 	lui	$t7,%hi(var80075d14)
/*  f11e0ec:	8def5d14 */ 	lw	$t7,%lo(var80075d14)($t7)
/*  f11e0f0:	83a80023 */ 	lb	$t0,0x23($sp)
/*  f11e0f4:	3c018007 */ 	lui	$at,%hi(g_MpPlayerNum)
/*  f11e0f8:	11e00004 */ 	beqz	$t7,.L0f11e10c
/*  f11e0fc:	ac281448 */ 	sw	$t0,%lo(g_MpPlayerNum)($at)
/*  f11e100:	24040007 */ 	addiu	$a0,$zero,0x7
/*  f11e104:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e108:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11e10c:
/*  f11e10c:	24090005 */ 	addiu	$t1,$zero,0x5
/*  f11e110:	1000008e */ 	beqz	$zero,.L0f11e34c
/*  f11e114:	ae090010 */ 	sw	$t1,0x10($s0)
/*  f11e118:	0c00543a */ 	jal	joyGetLock
/*  f11e11c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11e120:	0fc46a3d */ 	jal	pak0f11a8f4
/*  f11e124:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e128:	0c005451 */ 	jal	joyReleaseLock
/*  f11e12c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11e130:	10000087 */ 	beqz	$zero,.L0f11e350
/*  f11e134:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e138:	3c0a8007 */ 	lui	$t2,%hi(var80075d14)
/*  f11e13c:	8d4a5d14 */ 	lw	$t2,%lo(var80075d14)($t2)
/*  f11e140:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e144:	51400004 */ 	beqzl	$t2,.L0f11e158
/*  f11e148:	240d000b */ 	addiu	$t5,$zero,0xb
/*  f11e14c:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e150:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e154:	240d000b */ 	addiu	$t5,$zero,0xb
.L0f11e158:
/*  f11e158:	1000007c */ 	beqz	$zero,.L0f11e34c
/*  f11e15c:	ae0d0010 */ 	sw	$t5,0x10($s0)
/*  f11e160:	0fc52ba8 */ 	jal	func0f14aea0
/*  f11e164:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e168:	10400078 */ 	beqz	$v0,.L0f11e34c
/*  f11e16c:	240c0009 */ 	addiu	$t4,$zero,0x9
/*  f11e170:	10000076 */ 	beqz	$zero,.L0f11e34c
/*  f11e174:	ae0c0010 */ 	sw	$t4,0x10($s0)
/*  f11e178:	240b000a */ 	addiu	$t3,$zero,0xa
/*  f11e17c:	ae000008 */ 	sw	$zero,0x8($s0)
/*  f11e180:	10000072 */ 	beqz	$zero,.L0f11e34c
/*  f11e184:	ae0b0010 */ 	sw	$t3,0x10($s0)
/*  f11e188:	24180018 */ 	addiu	$t8,$zero,0x18
/*  f11e18c:	1000006f */ 	beqz	$zero,.L0f11e34c
/*  f11e190:	ae180010 */ 	sw	$t8,0x10($s0)
/*  f11e194:	240e0019 */ 	addiu	$t6,$zero,0x19
/*  f11e198:	1000006c */ 	beqz	$zero,.L0f11e34c
/*  f11e19c:	ae0e0010 */ 	sw	$t6,0x10($s0)
/*  f11e1a0:	2419000b */ 	addiu	$t9,$zero,0xb
/*  f11e1a4:	10000069 */ 	beqz	$zero,.L0f11e34c
/*  f11e1a8:	ae190010 */ 	sw	$t9,0x10($s0)
/*  f11e1ac:	2408000b */ 	addiu	$t0,$zero,0xb
/*  f11e1b0:	10000066 */ 	beqz	$zero,.L0f11e34c
/*  f11e1b4:	ae080010 */ 	sw	$t0,0x10($s0)
/*  f11e1b8:	3c0f8007 */ 	lui	$t7,%hi(var80075d14)
/*  f11e1bc:	8def5d14 */ 	lw	$t7,%lo(var80075d14)($t7)
/*  f11e1c0:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e1c4:	51e00004 */ 	beqzl	$t7,.L0f11e1d8
/*  f11e1c8:	83aa0023 */ 	lb	$t2,0x23($sp)
/*  f11e1cc:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e1d0:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e1d4:	83aa0023 */ 	lb	$t2,0x23($sp)
.L0f11e1d8:
/*  f11e1d8:	3c09800a */ 	lui	$t1,%hi(g_Vars+0x4d0)
/*  f11e1dc:	9129a490 */ 	lbu	$t1,%lo(g_Vars+0x4d0)($t1)
/*  f11e1e0:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11e1e4:	014d6004 */ 	sllv	$t4,$t5,$t2
/*  f11e1e8:	012c5824 */ 	and	$t3,$t1,$t4
/*  f11e1ec:	11600057 */ 	beqz	$t3,.L0f11e34c
/*  f11e1f0:	01402025 */ 	or	$a0,$t2,$zero
/*  f11e1f4:	0fc3f47d */ 	jal	func0f0fd1f4
/*  f11e1f8:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e1fc:	10400053 */ 	beqz	$v0,.L0f11e34c
/*  f11e200:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e204:	0fc3f4c8 */ 	jal	func0f0fd320
/*  f11e208:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e20c:	24180014 */ 	addiu	$t8,$zero,0x14
/*  f11e210:	1000004e */ 	beqz	$zero,.L0f11e34c
/*  f11e214:	ae180010 */ 	sw	$t8,0x10($s0)
/*  f11e218:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e21c:	0fc3f47d */ 	jal	func0f0fd1f4
/*  f11e220:	24050003 */ 	addiu	$a1,$zero,0x3
/*  f11e224:	10400049 */ 	beqz	$v0,.L0f11e34c
/*  f11e228:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e22c:	0fc3f4c8 */ 	jal	func0f0fd320
/*  f11e230:	24050003 */ 	addiu	$a1,$zero,0x3
/*  f11e234:	240e001b */ 	addiu	$t6,$zero,0x1b
/*  f11e238:	10000044 */ 	beqz	$zero,.L0f11e34c
/*  f11e23c:	ae0e0010 */ 	sw	$t6,0x10($s0)
/*  f11e240:	2419000b */ 	addiu	$t9,$zero,0xb
/*  f11e244:	10000041 */ 	beqz	$zero,.L0f11e34c
/*  f11e248:	ae190010 */ 	sw	$t9,0x10($s0)
/*  f11e24c:	3c088007 */ 	lui	$t0,%hi(var80075d14)
/*  f11e250:	8d085d14 */ 	lw	$t0,%lo(var80075d14)($t0)
/*  f11e254:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e258:	51000004 */ 	beqzl	$t0,.L0f11e26c
/*  f11e25c:	83ad0023 */ 	lb	$t5,0x23($sp)
/*  f11e260:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e264:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e268:	83ad0023 */ 	lb	$t5,0x23($sp)
.L0f11e26c:
/*  f11e26c:	3c0f800a */ 	lui	$t7,%hi(g_Vars+0x4d0)
/*  f11e270:	91efa490 */ 	lbu	$t7,%lo(g_Vars+0x4d0)($t7)
/*  f11e274:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11e278:	01a96004 */ 	sllv	$t4,$t1,$t5
/*  f11e27c:	01ec5824 */ 	and	$t3,$t7,$t4
/*  f11e280:	11600032 */ 	beqz	$t3,.L0f11e34c
/*  f11e284:	01a02025 */ 	or	$a0,$t5,$zero
/*  f11e288:	0fc3f47d */ 	jal	func0f0fd1f4
/*  f11e28c:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f11e290:	1040002e */ 	beqz	$v0,.L0f11e34c
/*  f11e294:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e298:	0fc3f4c8 */ 	jal	func0f0fd320
/*  f11e29c:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f11e2a0:	240a0013 */ 	addiu	$t2,$zero,0x13
/*  f11e2a4:	10000029 */ 	beqz	$zero,.L0f11e34c
/*  f11e2a8:	ae0a0010 */ 	sw	$t2,0x10($s0)
/*  f11e2ac:	3c188007 */ 	lui	$t8,%hi(var80075d14)
/*  f11e2b0:	8f185d14 */ 	lw	$t8,%lo(var80075d14)($t8)
/*  f11e2b4:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e2b8:	53000004 */ 	beqzl	$t8,.L0f11e2cc
/*  f11e2bc:	83b90023 */ 	lb	$t9,0x23($sp)
/*  f11e2c0:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e2c4:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e2c8:	83b90023 */ 	lb	$t9,0x23($sp)
.L0f11e2cc:
/*  f11e2cc:	3c0e800a */ 	lui	$t6,%hi(g_Vars+0x4d0)
/*  f11e2d0:	91cea490 */ 	lbu	$t6,%lo(g_Vars+0x4d0)($t6)
/*  f11e2d4:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f11e2d8:	03284804 */ 	sllv	$t1,$t0,$t9
/*  f11e2dc:	01c97824 */ 	and	$t7,$t6,$t1
/*  f11e2e0:	11e0001a */ 	beqz	$t7,.L0f11e34c
/*  f11e2e4:	03202025 */ 	or	$a0,$t9,$zero
/*  f11e2e8:	0fc3f47d */ 	jal	func0f0fd1f4
/*  f11e2ec:	00002825 */ 	or	$a1,$zero,$zero
/*  f11e2f0:	10400016 */ 	beqz	$v0,.L0f11e34c
/*  f11e2f4:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e2f8:	0fc3f4c8 */ 	jal	func0f0fd320
/*  f11e2fc:	00002825 */ 	or	$a1,$zero,$zero
/*  f11e300:	240c0015 */ 	addiu	$t4,$zero,0x15
/*  f11e304:	10000011 */ 	beqz	$zero,.L0f11e34c
/*  f11e308:	ae0c0010 */ 	sw	$t4,0x10($s0)
/*  f11e30c:	3c0b8007 */ 	lui	$t3,%hi(var80075d14)
/*  f11e310:	8d6b5d14 */ 	lw	$t3,%lo(var80075d14)($t3)
/*  f11e314:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e318:	5160000d */ 	beqzl	$t3,.L0f11e350
/*  f11e31c:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e320:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e324:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e328:	10000009 */ 	beqz	$zero,.L0f11e350
/*  f11e32c:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e330:	3c0d8007 */ 	lui	$t5,%hi(var80075d14)
/*  f11e334:	8dad5d14 */ 	lw	$t5,%lo(var80075d14)($t5)
/*  f11e338:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e33c:	51a00004 */ 	beqzl	$t5,.L0f11e350
/*  f11e340:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
/*  f11e344:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e348:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11e34c:
/*  f11e34c:	8e0a02b8 */ 	lw	$t2,0x2b8($s0)
.L0f11e350:
/*  f11e350:	3c0e8007 */ 	lui	$t6,%hi(var80075d14)
/*  f11e354:	000a4180 */ 	sll	$t0,$t2,0x6
/*  f11e358:	05030012 */ 	bgezl	$t0,.L0f11e3a4
/*  f11e35c:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11e360:	8dce5d14 */ 	lw	$t6,%lo(var80075d14)($t6)
/*  f11e364:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e368:	51c00004 */ 	beqzl	$t6,.L0f11e37c
/*  f11e36c:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e370:	0fc3c328 */ 	jal	func0f0f0ca0
/*  f11e374:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e378:	83a40023 */ 	lb	$a0,0x23($sp)
.L0f11e37c:
/*  f11e37c:	0fc3f47d */ 	jal	func0f0fd1f4
/*  f11e380:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11e384:	10400006 */ 	beqz	$v0,.L0f11e3a0
/*  f11e388:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e38c:	0fc3f4c8 */ 	jal	func0f0fd320
/*  f11e390:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11e394:	920902b8 */ 	lbu	$t1,0x2b8($s0)
/*  f11e398:	312ffffd */ 	andi	$t7,$t1,0xfffd
/*  f11e39c:	a20f02b8 */ 	sb	$t7,0x2b8($s0)
.L0f11e3a0:
/*  f11e3a0:	8fbf001c */ 	lw	$ra,0x1c($sp)
.L0f11e3a4:
/*  f11e3a4:	8fb00018 */ 	lw	$s0,0x18($sp)
/*  f11e3a8:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f11e3ac:	03e00008 */ 	jr	$ra
/*  f11e3b0:	00000000 */ 	sll	$zero,$zero,0x0
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak0f11df94
.late_rodata
glabel var7f1b4f6c
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4f70
.word pak0f11df94+0xd8 # f11dde8
glabel var7f1b4f74
.word pak0f11df94+0x110 # f11de20
glabel var7f1b4f78
.word pak0f11df94+0x140 # f11de50
glabel var7f1b4f7c
.word pak0f11df94+0x1b4 # f11dec4
glabel var7f1b4f80
.word pak0f11df94+0x1ec # f11defc
glabel var7f1b4f84
.word pak0f11df94+0x20c # f11df1c
glabel var7f1b4f88
.word pak0f11df94+0x18c # f11de9c
glabel var7f1b4f8c
.word pak0f11df94+0x238 # f11df48
glabel var7f1b4f90
.word pak0f11df94+0x258 # f11df68
glabel var7f1b4f94
.word pak0f11df94+0x268 # f11df78
glabel var7f1b4f98
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4f9c
.word pak0f11df94+0x28c # f11df9c
glabel var7f1b4fa0
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fa4
.word pak0f11df94+0x34c # f11e05c
glabel var7f1b4fa8
.word pak0f11df94+0x298 # f11dfa8
glabel var7f1b4fac
.word pak0f11df94+0x3c0 # f11e0d0
glabel var7f1b4fb0
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fb4
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fb8
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fbc
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fc0
.word pak0f11df94+0x430 # f11e140
glabel var7f1b4fc4
.word pak0f11df94+0x44c # f11e15c
glabel var7f1b4fc8
.word pak0f11df94+0x464 # f11e174
glabel var7f1b4fcc
.word pak0f11df94+0x274 # f11df84
glabel var7f1b4fd0
.word pak0f11df94+0x280 # f11df90
glabel var7f1b4fd4
.word pak0f11df94+0x308 # f11e018
glabel var7f1b4fd8
.word pak0f11df94+0x340 # f11e050
.text
/*  f11dd10:	27bdffe0 */ 	addiu	$sp,$sp,-32
/*  f11dd14:	afa40020 */ 	sw	$a0,0x20($sp)
/*  f11dd18:	83ae0023 */ 	lb	$t6,0x23($sp)
/*  f11dd1c:	3c18800a */ 	lui	$t8,%hi(g_Paks)
/*  f11dd20:	27182380 */ 	addiu	$t8,$t8,%lo(g_Paks)
/*  f11dd24:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f11dd28:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dd2c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dd30:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dd34:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dd38:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f11dd3c:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dd40:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f11dd44:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f11dd48:	01f81821 */ 	addu	$v1,$t7,$t8
/*  f11dd4c:	8c7902b8 */ 	lw	$t9,0x2b8($v1)
/*  f11dd50:	3c068007 */ 	lui	$a2,%hi(var80075d14)
/*  f11dd54:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f11dd58:	00194980 */ 	sll	$t1,$t9,0x6
/*  f11dd5c:	05210017 */ 	bgez	$t1,.L0f11ddbc
/*  f11dd60:	24c65d14 */ 	addiu	$a2,$a2,%lo(var80075d14)
/*  f11dd64:	8cca0000 */ 	lw	$t2,0x0($a2)
/*  f11dd68:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11dd6c:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11dd70:	51400005 */ 	beqzl	$t2,.L0f11dd88
/*  f11dd74:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11dd78:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11dd7c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11dd80:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11dd84:	83a40023 */ 	lb	$a0,0x23($sp)
.L0f11dd88:
/*  f11dd88:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11dd8c:	0fc3f475 */ 	jal	func0f0fd1f4
/*  f11dd90:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11dd94:	10400009 */ 	beqz	$v0,.L0f11ddbc
/*  f11dd98:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11dd9c:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11dda0:	24050004 */ 	addiu	$a1,$zero,0x4
/*  f11dda4:	0fc3f4c0 */ 	jal	func0f0fd320
/*  f11dda8:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11ddac:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11ddb0:	906b02b8 */ 	lbu	$t3,0x2b8($v1)
/*  f11ddb4:	316cfffd */ 	andi	$t4,$t3,0xfffd
/*  f11ddb8:	a06c02b8 */ 	sb	$t4,0x2b8($v1)
.L0f11ddbc:
/*  f11ddbc:	8c6d0010 */ 	lw	$t5,0x10($v1)
/*  f11ddc0:	3c068007 */ 	lui	$a2,%hi(var80075d14)
/*  f11ddc4:	24c65d14 */ 	addiu	$a2,$a2,%lo(var80075d14)
/*  f11ddc8:	2da1001c */ 	sltiu	$at,$t5,0x1c
/*  f11ddcc:	102000e9 */ 	beqz	$at,.L0f11e174
/*  f11ddd0:	000d6880 */ 	sll	$t5,$t5,0x2
/*  f11ddd4:	3c017f1b */ 	lui	$at,0x7f1b
/*  f11ddd8:	002d0821 */ 	addu	$at,$at,$t5
/*  f11dddc:	8c2d4df8 */ 	lw	$t5,0x4df8($at)
/*  f11dde0:	01a00008 */ 	jr	$t5
/*  f11dde4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11dde8:	8c6e0264 */ 	lw	$t6,0x264($v1)
/*  f11ddec:	8cd80000 */ 	lw	$t8,0x0($a2)
/*  f11ddf0:	ac600010 */ 	sw	$zero,0x10($v1)
/*  f11ddf4:	25cf0001 */ 	addiu	$t7,$t6,0x1
/*  f11ddf8:	ac6f0264 */ 	sw	$t7,0x264($v1)
/*  f11ddfc:	13000004 */ 	beqz	$t8,.L0f11de10
/*  f11de00:	ac600000 */ 	sw	$zero,0x0($v1)
/*  f11de04:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11de08:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11de0c:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11de10:
/*  f11de10:	0fc52b28 */ 	jal	func0f14aed0
/*  f11de14:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11de18:	100000d7 */ 	beqz	$zero,.L0f11e178
/*  f11de1c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11de20:	8cd90000 */ 	lw	$t9,0x0($a2)
/*  f11de24:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11de28:	13200003 */ 	beqz	$t9,.L0f11de38
/*  f11de2c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11de30:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11de34:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11de38:
/*  f11de38:	0fc46a7f */ 	jal	pak0f11ac7c
/*  f11de3c:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11de40:	0fc52b28 */ 	jal	func0f14aed0
/*  f11de44:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11de48:	100000cb */ 	beqz	$zero,.L0f11e178
/*  f11de4c:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11de50:	83a90023 */ 	lb	$t1,0x23($sp)
/*  f11de54:	3c08800a */ 	lui	$t0,%hi(g_Vars+0x4d0)
/*  f11de58:	9108a490 */ 	lbu	$t0,%lo(g_Vars+0x4d0)($t0)
/*  f11de5c:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f11de60:	012a5804 */ 	sllv	$t3,$t2,$t1
/*  f11de64:	010b6024 */ 	and	$t4,$t0,$t3
/*  f11de68:	1180000a */ 	beqz	$t4,.L0f11de94
/*  f11de6c:	24180007 */ 	addiu	$t8,$zero,0x7
/*  f11de70:	3c0d800a */ 	lui	$t5,%hi(g_Paks+0xb30)
/*  f11de74:	25ad2eb0 */ 	addiu	$t5,$t5,%lo(g_Paks+0xb30)
/*  f11de78:	146d0004 */ 	bne	$v1,$t5,.L0f11de8c
/*  f11de7c:	240f0004 */ 	addiu	$t7,$zero,0x4
/*  f11de80:	240e0005 */ 	addiu	$t6,$zero,0x5
/*  f11de84:	100000bb */ 	beqz	$zero,.L0f11e174
/*  f11de88:	ac6e0010 */ 	sw	$t6,0x10($v1)
.L0f11de8c:
/*  f11de8c:	100000b9 */ 	beqz	$zero,.L0f11e174
/*  f11de90:	ac6f0010 */ 	sw	$t7,0x10($v1)
.L0f11de94:
/*  f11de94:	100000b7 */ 	beqz	$zero,.L0f11e174
/*  f11de98:	ac780010 */ 	sw	$t8,0x10($v1)
/*  f11de9c:	83aa0023 */ 	lb	$t2,0x23($sp)
/*  f11dea0:	3c19800a */ 	lui	$t9,%hi(g_Vars+0x4d0)
/*  f11dea4:	9339a490 */ 	lbu	$t9,%lo(g_Vars+0x4d0)($t9)
/*  f11dea8:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11deac:	01494004 */ 	sllv	$t0,$t1,$t2
/*  f11deb0:	03285824 */ 	and	$t3,$t9,$t0
/*  f11deb4:	116000af */ 	beqz	$t3,.L0f11e174
/*  f11deb8:	240c0002 */ 	addiu	$t4,$zero,0x2
/*  f11debc:	100000ad */ 	beqz	$zero,.L0f11e174
/*  f11dec0:	ac6c0010 */ 	sw	$t4,0x10($v1)
/*  f11dec4:	83ad0023 */ 	lb	$t5,0x23($sp)
/*  f11dec8:	3c018007 */ 	lui	$at,%hi(g_MpPlayerNum)
/*  f11decc:	24040007 */ 	addiu	$a0,$zero,0x7
/*  f11ded0:	ac2d1448 */ 	sw	$t5,%lo(g_MpPlayerNum)($at)
/*  f11ded4:	8cce0000 */ 	lw	$t6,0x0($a2)
/*  f11ded8:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11dedc:	51c00005 */ 	beqzl	$t6,.L0f11def4
/*  f11dee0:	240f0005 */ 	addiu	$t7,$zero,0x5
/*  f11dee4:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11dee8:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11deec:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11def0:	240f0005 */ 	addiu	$t7,$zero,0x5
.L0f11def4:
/*  f11def4:	1000009f */ 	beqz	$zero,.L0f11e174
/*  f11def8:	ac6f0010 */ 	sw	$t7,0x10($v1)
/*  f11defc:	0c00543a */ 	jal	joyGetLock
/*  f11df00:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11df04:	0fc4699d */ 	jal	pak0f11a8f4
/*  f11df08:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11df0c:	0c005451 */ 	jal	joyReleaseLock
/*  f11df10:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11df14:	10000098 */ 	beqz	$zero,.L0f11e178
/*  f11df18:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11df1c:	8cd80000 */ 	lw	$t8,0x0($a2)
/*  f11df20:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11df24:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11df28:	53000005 */ 	beqzl	$t8,.L0f11df40
/*  f11df2c:	2409000b */ 	addiu	$t1,$zero,0xb
/*  f11df30:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11df34:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11df38:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11df3c:	2409000b */ 	addiu	$t1,$zero,0xb
.L0f11df40:
/*  f11df40:	1000008c */ 	beqz	$zero,.L0f11e174
/*  f11df44:	ac690010 */ 	sw	$t1,0x10($v1)
/*  f11df48:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11df4c:	0fc52b1c */ 	jal	func0f14aea0
/*  f11df50:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11df54:	10400087 */ 	beqz	$v0,.L0f11e174
/*  f11df58:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11df5c:	240a0009 */ 	addiu	$t2,$zero,0x9
/*  f11df60:	10000084 */ 	beqz	$zero,.L0f11e174
/*  f11df64:	ac6a0010 */ 	sw	$t2,0x10($v1)
/*  f11df68:	2419000a */ 	addiu	$t9,$zero,0xa
/*  f11df6c:	ac600008 */ 	sw	$zero,0x8($v1)
/*  f11df70:	10000080 */ 	beqz	$zero,.L0f11e174
/*  f11df74:	ac790010 */ 	sw	$t9,0x10($v1)
/*  f11df78:	24080018 */ 	addiu	$t0,$zero,0x18
/*  f11df7c:	1000007d */ 	beqz	$zero,.L0f11e174
/*  f11df80:	ac680010 */ 	sw	$t0,0x10($v1)
/*  f11df84:	240b0019 */ 	addiu	$t3,$zero,0x19
/*  f11df88:	1000007a */ 	beqz	$zero,.L0f11e174
/*  f11df8c:	ac6b0010 */ 	sw	$t3,0x10($v1)
/*  f11df90:	240c000b */ 	addiu	$t4,$zero,0xb
/*  f11df94:	10000077 */ 	beqz	$zero,.L0f11e174
/*  f11df98:	ac6c0010 */ 	sw	$t4,0x10($v1)
/*  f11df9c:	240d000b */ 	addiu	$t5,$zero,0xb
/*  f11dfa0:	10000074 */ 	beqz	$zero,.L0f11e174
/*  f11dfa4:	ac6d0010 */ 	sw	$t5,0x10($v1)
/*  f11dfa8:	8cce0000 */ 	lw	$t6,0x0($a2)
/*  f11dfac:	11c00005 */ 	beqz	$t6,.L0f11dfc4
/*  f11dfb0:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11dfb4:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11dfb8:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11dfbc:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11dfc0:	8fa3001c */ 	lw	$v1,0x1c($sp)
.L0f11dfc4:
/*  f11dfc4:	83b80023 */ 	lb	$t8,0x23($sp)
/*  f11dfc8:	3c0f800a */ 	lui	$t7,%hi(g_Vars+0x4d0)
/*  f11dfcc:	91efa490 */ 	lbu	$t7,%lo(g_Vars+0x4d0)($t7)
/*  f11dfd0:	24090001 */ 	addiu	$t1,$zero,0x1
/*  f11dfd4:	03095004 */ 	sllv	$t2,$t1,$t8
/*  f11dfd8:	01eac824 */ 	and	$t9,$t7,$t2
/*  f11dfdc:	13200065 */ 	beqz	$t9,.L0f11e174
/*  f11dfe0:	03002025 */ 	or	$a0,$t8,$zero
/*  f11dfe4:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11dfe8:	0fc3f475 */ 	jal	func0f0fd1f4
/*  f11dfec:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11dff0:	10400060 */ 	beqz	$v0,.L0f11e174
/*  f11dff4:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11dff8:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11dffc:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e000:	0fc3f4c0 */ 	jal	func0f0fd320
/*  f11e004:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e008:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e00c:	24080014 */ 	addiu	$t0,$zero,0x14
/*  f11e010:	10000058 */ 	beqz	$zero,.L0f11e174
/*  f11e014:	ac680010 */ 	sw	$t0,0x10($v1)
/*  f11e018:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e01c:	24050003 */ 	addiu	$a1,$zero,0x3
/*  f11e020:	0fc3f475 */ 	jal	func0f0fd1f4
/*  f11e024:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e028:	10400052 */ 	beqz	$v0,.L0f11e174
/*  f11e02c:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e030:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e034:	24050003 */ 	addiu	$a1,$zero,0x3
/*  f11e038:	0fc3f4c0 */ 	jal	func0f0fd320
/*  f11e03c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e040:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e044:	240b001b */ 	addiu	$t3,$zero,0x1b
/*  f11e048:	1000004a */ 	beqz	$zero,.L0f11e174
/*  f11e04c:	ac6b0010 */ 	sw	$t3,0x10($v1)
/*  f11e050:	240c000b */ 	addiu	$t4,$zero,0xb
/*  f11e054:	10000047 */ 	beqz	$zero,.L0f11e174
/*  f11e058:	ac6c0010 */ 	sw	$t4,0x10($v1)
/*  f11e05c:	8ccd0000 */ 	lw	$t5,0x0($a2)
/*  f11e060:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e064:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e068:	51a00005 */ 	beqzl	$t5,.L0f11e080
/*  f11e06c:	83a90023 */ 	lb	$t1,0x23($sp)
/*  f11e070:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11e074:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e078:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e07c:	83a90023 */ 	lb	$t1,0x23($sp)
.L0f11e080:
/*  f11e080:	3c0e800a */ 	lui	$t6,%hi(g_Vars+0x4d0)
/*  f11e084:	91cea490 */ 	lbu	$t6,%lo(g_Vars+0x4d0)($t6)
/*  f11e088:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f11e08c:	012f5004 */ 	sllv	$t2,$t7,$t1
/*  f11e090:	01cac824 */ 	and	$t9,$t6,$t2
/*  f11e094:	13200037 */ 	beqz	$t9,.L0f11e174
/*  f11e098:	01202025 */ 	or	$a0,$t1,$zero
/*  f11e09c:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f11e0a0:	0fc3f475 */ 	jal	func0f0fd1f4
/*  f11e0a4:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e0a8:	10400032 */ 	beqz	$v0,.L0f11e174
/*  f11e0ac:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e0b0:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e0b4:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f11e0b8:	0fc3f4c0 */ 	jal	func0f0fd320
/*  f11e0bc:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e0c0:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e0c4:	24180013 */ 	addiu	$t8,$zero,0x13
/*  f11e0c8:	1000002a */ 	beqz	$zero,.L0f11e174
/*  f11e0cc:	ac780010 */ 	sw	$t8,0x10($v1)
/*  f11e0d0:	8cc80000 */ 	lw	$t0,0x0($a2)
/*  f11e0d4:	11000005 */ 	beqz	$t0,.L0f11e0ec
/*  f11e0d8:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e0dc:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e0e0:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11e0e4:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e0e8:	8fa3001c */ 	lw	$v1,0x1c($sp)
.L0f11e0ec:
/*  f11e0ec:	83ac0023 */ 	lb	$t4,0x23($sp)
/*  f11e0f0:	3c0b800a */ 	lui	$t3,%hi(g_Vars+0x4d0)
/*  f11e0f4:	916ba490 */ 	lbu	$t3,%lo(g_Vars+0x4d0)($t3)
/*  f11e0f8:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f11e0fc:	018d7804 */ 	sllv	$t7,$t5,$t4
/*  f11e100:	016f7024 */ 	and	$t6,$t3,$t7
/*  f11e104:	11c0001b */ 	beqz	$t6,.L0f11e174
/*  f11e108:	01802025 */ 	or	$a0,$t4,$zero
/*  f11e10c:	00002825 */ 	or	$a1,$zero,$zero
/*  f11e110:	0fc3f475 */ 	jal	func0f0fd1f4
/*  f11e114:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e118:	10400016 */ 	beqz	$v0,.L0f11e174
/*  f11e11c:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e120:	83a40023 */ 	lb	$a0,0x23($sp)
/*  f11e124:	00002825 */ 	or	$a1,$zero,$zero
/*  f11e128:	0fc3f4c0 */ 	jal	func0f0fd320
/*  f11e12c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f11e130:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f11e134:	240a0015 */ 	addiu	$t2,$zero,0x15
/*  f11e138:	1000000e */ 	beqz	$zero,.L0f11e174
/*  f11e13c:	ac6a0010 */ 	sw	$t2,0x10($v1)
/*  f11e140:	8cd90000 */ 	lw	$t9,0x0($a2)
/*  f11e144:	1320000b */ 	beqz	$t9,.L0f11e174
/*  f11e148:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e14c:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11e150:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f11e154:	10000008 */ 	beqz	$zero,.L0f11e178
/*  f11e158:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11e15c:	8cc90000 */ 	lw	$t1,0x0($a2)
/*  f11e160:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f11e164:	51200004 */ 	beqzl	$t1,.L0f11e178
/*  f11e168:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f11e16c:	0fc3c320 */ 	jal	func0f0f0ca0
/*  f11e170:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f11e174:
/*  f11e174:	8fbf0014 */ 	lw	$ra,0x14($sp)
.L0f11e178:
/*  f11e178:	27bd0020 */ 	addiu	$sp,$sp,0x20
/*  f11e17c:	03e00008 */ 	jr	$ra
/*  f11e180:	00000000 */ 	sll	$zero,$zero,0x0
);
#else
GLOBAL_ASM(
glabel pak0f11df94
.late_rodata
glabel var7f1af100nb
.word pak0f11df94+0x404
glabel var7f1af104nb
.word pak0f11df94+0x064
glabel var7f1af108nb
.word pak0f11df94+0x098
glabel var7f1af10cnb
.word pak0f11df94+0x0bc
glabel var7f1af110nb
.word pak0f11df94+0x140
glabel var7f1af114nb
.word pak0f11df94+0x168
glabel var7f1af118nb
.word pak0f11df94+0x180
glabel var7f1af11cnb
.word pak0f11df94+0x110
glabel var7f1af120nb
.word pak0f11df94+0x1a0
glabel var7f1af124nb
.word pak0f11df94+0x1c0
glabel var7f1af128nb
.word pak0f11df94+0x278
glabel var7f1af12cnb
.word pak0f11df94+0x404
glabel var7f1af130nb
.word pak0f11df94+0x29c
glabel var7f1af134nb
.word pak0f11df94+0x404
glabel var7f1af138nb
.word pak0f11df94+0x34c
glabel var7f1af13cnb
.word pak0f11df94+0x300
glabel var7f1af140nb
.word pak0f11df94+0x398
glabel var7f1af144nb
.word pak0f11df94+0x404
glabel var7f1af148nb
.word pak0f11df94+0x404
glabel var7f1af14cnb
.word pak0f11df94+0x404
glabel var7f1af150nb
.word pak0f11df94+0x404
glabel var7f1af154nb
.word pak0f11df94+0x3e4
glabel var7f1af158nb
.word pak0f11df94+0x3f8
glabel var7f1af15cnb
.word pak0f11df94+0x404
glabel var7f1af160nb
.word pak0f11df94+0x284
glabel var7f1af164nb
.word pak0f11df94+0x290
.text
/*  f117b80:	00043600 */ 	sll	$a2,$a0,0x18
/*  f117b84:	00067603 */ 	sra	$t6,$a2,0x18
/*  f117b88:	000e7880 */ 	sll	$t7,$t6,0x2
/*  f117b8c:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f117b90:	000f7880 */ 	sll	$t7,$t7,0x2
/*  f117b94:	01ee7823 */ 	subu	$t7,$t7,$t6
/*  f117b98:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f117b9c:	01ee7821 */ 	addu	$t7,$t7,$t6
/*  f117ba0:	3c18800a */ 	lui	$t8,0x800a
/*  f117ba4:	27186870 */ 	addiu	$t8,$t8,0x6870
/*  f117ba8:	000f78c0 */ 	sll	$t7,$t7,0x3
/*  f117bac:	01f81821 */ 	addu	$v1,$t7,$t8
/*  f117bb0:	8c790010 */ 	lw	$t9,0x10($v1)
/*  f117bb4:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f117bb8:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f117bbc:	2f21001a */ 	sltiu	$at,$t9,0x1a
/*  f117bc0:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f117bc4:	102000ef */ 	beqz	$at,.NB0f117f84
/*  f117bc8:	01c03025 */ 	or	$a2,$t6,$zero
/*  f117bcc:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f117bd0:	3c017f1b */ 	lui	$at,0x7f1b
/*  f117bd4:	00390821 */ 	addu	$at,$at,$t9
/*  f117bd8:	8c39f100 */ 	lw	$t9,-0xf00($at)
/*  f117bdc:	03200008 */ 	jr	$t9
/*  f117be0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f117be4:	8c680264 */ 	lw	$t0,0x264($v1)
/*  f117be8:	ac600010 */ 	sw	$zero,0x10($v1)
/*  f117bec:	ac600000 */ 	sw	$zero,0x0($v1)
/*  f117bf0:	25090001 */ 	addiu	$t1,$t0,0x1
/*  f117bf4:	ac690264 */ 	sw	$t1,0x264($v1)
/*  f117bf8:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117bfc:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117c00:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117c04:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117c08:	0fc515a8 */ 	jal	func0f14aed0
/*  f117c0c:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117c10:	100000dd */ 	beqz	$zero,.NB0f117f88
/*  f117c14:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117c18:	00062600 */ 	sll	$a0,$a2,0x18
/*  f117c1c:	00045603 */ 	sra	$t2,$a0,0x18
/*  f117c20:	01402025 */ 	or	$a0,$t2,$zero
/*  f117c24:	0fc45375 */ 	jal	pak0f114dd4nb
/*  f117c28:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117c2c:	0fc515a8 */ 	jal	func0f14aed0
/*  f117c30:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117c34:	100000d4 */ 	beqz	$zero,.NB0f117f88
/*  f117c38:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117c3c:	3c02800a */ 	lui	$v0,0x800a
/*  f117c40:	2442e6c0 */ 	addiu	$v0,$v0,-6464
/*  f117c44:	904b04d1 */ 	lbu	$t3,0x4d1($v0)
/*  f117c48:	904c04d0 */ 	lbu	$t4,0x4d0($v0)
/*  f117c4c:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f117c50:	00ce7804 */ 	sllv	$t7,$t6,$a2
/*  f117c54:	016c6825 */ 	or	$t5,$t3,$t4
/*  f117c58:	01afc024 */ 	and	$t8,$t5,$t7
/*  f117c5c:	1300000a */ 	beqz	$t8,.NB0f117c88
/*  f117c60:	240a0007 */ 	addiu	$t2,$zero,0x7
/*  f117c64:	3c19800a */ 	lui	$t9,0x800a
/*  f117c68:	27397390 */ 	addiu	$t9,$t9,0x7390
/*  f117c6c:	14790004 */ 	bne	$v1,$t9,.NB0f117c80
/*  f117c70:	24090004 */ 	addiu	$t1,$zero,0x4
/*  f117c74:	24080005 */ 	addiu	$t0,$zero,0x5
/*  f117c78:	100000c2 */ 	beqz	$zero,.NB0f117f84
/*  f117c7c:	ac680010 */ 	sw	$t0,0x10($v1)
.NB0f117c80:
/*  f117c80:	100000c0 */ 	beqz	$zero,.NB0f117f84
/*  f117c84:	ac690010 */ 	sw	$t1,0x10($v1)
.NB0f117c88:
/*  f117c88:	100000be */ 	beqz	$zero,.NB0f117f84
/*  f117c8c:	ac6a0010 */ 	sw	$t2,0x10($v1)
/*  f117c90:	3c02800a */ 	lui	$v0,0x800a
/*  f117c94:	2442e6c0 */ 	addiu	$v0,$v0,-6464
/*  f117c98:	904b04d1 */ 	lbu	$t3,0x4d1($v0)
/*  f117c9c:	904c04d0 */ 	lbu	$t4,0x4d0($v0)
/*  f117ca0:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f117ca4:	00cd7804 */ 	sllv	$t7,$t5,$a2
/*  f117ca8:	016c7025 */ 	or	$t6,$t3,$t4
/*  f117cac:	01cfc024 */ 	and	$t8,$t6,$t7
/*  f117cb0:	130000b4 */ 	beqz	$t8,.NB0f117f84
/*  f117cb4:	24190002 */ 	addiu	$t9,$zero,0x2
/*  f117cb8:	100000b2 */ 	beqz	$zero,.NB0f117f84
/*  f117cbc:	ac790010 */ 	sw	$t9,0x10($v1)
/*  f117cc0:	3c018007 */ 	lui	$at,0x8007
/*  f117cc4:	ac263af0 */ 	sw	$a2,0x3af0($at)
/*  f117cc8:	24040007 */ 	addiu	$a0,$zero,0x7
/*  f117ccc:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117cd0:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117cd4:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117cd8:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117cdc:	24080005 */ 	addiu	$t0,$zero,0x5
/*  f117ce0:	100000a8 */ 	beqz	$zero,.NB0f117f84
/*  f117ce4:	ac680010 */ 	sw	$t0,0x10($v1)
/*  f117ce8:	00062600 */ 	sll	$a0,$a2,0x18
/*  f117cec:	00044e03 */ 	sra	$t1,$a0,0x18
/*  f117cf0:	0fc45232 */ 	jal	pak0f11a8f4
/*  f117cf4:	01202025 */ 	or	$a0,$t1,$zero
/*  f117cf8:	100000a3 */ 	beqz	$zero,.NB0f117f88
/*  f117cfc:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117d00:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117d04:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117d08:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117d0c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117d10:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117d14:	240a000b */ 	addiu	$t2,$zero,0xb
/*  f117d18:	1000009a */ 	beqz	$zero,.NB0f117f84
/*  f117d1c:	ac6a0010 */ 	sw	$t2,0x10($v1)
/*  f117d20:	00c02025 */ 	or	$a0,$a2,$zero
/*  f117d24:	0fc5159c */ 	jal	func0f14aea0
/*  f117d28:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117d2c:	10400095 */ 	beqz	$v0,.NB0f117f84
/*  f117d30:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117d34:	240b0009 */ 	addiu	$t3,$zero,0x9
/*  f117d38:	10000092 */ 	beqz	$zero,.NB0f117f84
/*  f117d3c:	ac6b0010 */ 	sw	$t3,0x10($v1)
/*  f117d40:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f117d44:	24a5ece4 */ 	addiu	$a1,$a1,-4892
/*  f117d48:	2404171a */ 	addiu	$a0,$zero,0x171a
/*  f117d4c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117d50:	0c00581b */ 	jal	joyGetLock
/*  f117d54:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117d58:	83a6002b */ 	lb	$a2,0x2b($sp)
/*  f117d5c:	24010004 */ 	addiu	$at,$zero,0x4
/*  f117d60:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117d64:	14c10003 */ 	bne	$a2,$at,.NB0f117d74
/*  f117d68:	3c04800a */ 	lui	$a0,0x800a
/*  f117d6c:	10000009 */ 	beqz	$zero,.NB0f117d94
/*  f117d70:	00002825 */ 	or	$a1,$zero,$zero
.NB0f117d74:
/*  f117d74:	00066080 */ 	sll	$t4,$a2,0x2
/*  f117d78:	01866023 */ 	subu	$t4,$t4,$a2
/*  f117d7c:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f117d80:	01866021 */ 	addu	$t4,$t4,$a2
/*  f117d84:	3c0d800a */ 	lui	$t5,0x800a
/*  f117d88:	25ad7658 */ 	addiu	$t5,$t5,0x7658
/*  f117d8c:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f117d90:	018d2821 */ 	addu	$a1,$t4,$t5
.NB0f117d94:
/*  f117d94:	2484e5d8 */ 	addiu	$a0,$a0,-6696
/*  f117d98:	0c001890 */ 	jal	func00006100
/*  f117d9c:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117da0:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f117da4:	24a5ecec */ 	addiu	$a1,$a1,-4884
/*  f117da8:	2404171c */ 	addiu	$a0,$zero,0x171c
/*  f117dac:	0c005834 */ 	jal	joyReleaseLock
/*  f117db0:	afa20024 */ 	sw	$v0,0x24($sp)
/*  f117db4:	8fa60024 */ 	lw	$a2,0x24($sp)
/*  f117db8:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117dbc:	240e0003 */ 	addiu	$t6,$zero,0x3
/*  f117dc0:	14c00006 */ 	bnez	$a2,.NB0f117ddc
/*  f117dc4:	00c02025 */ 	or	$a0,$a2,$zero
/*  f117dc8:	240f000a */ 	addiu	$t7,$zero,0xa
/*  f117dcc:	ac6e0000 */ 	sw	$t6,0x0($v1)
/*  f117dd0:	ac600008 */ 	sw	$zero,0x8($v1)
/*  f117dd4:	1000006b */ 	beqz	$zero,.NB0f117f84
/*  f117dd8:	ac6f0010 */ 	sw	$t7,0x10($v1)
.NB0f117ddc:
/*  f117ddc:	0fc459d4 */ 	jal	pak0f11cb9c
/*  f117de0:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117de4:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117de8:	24180004 */ 	addiu	$t8,$zero,0x4
/*  f117dec:	ac780000 */ 	sw	$t8,0x0($v1)
/*  f117df0:	10000064 */ 	beqz	$zero,.NB0f117f84
/*  f117df4:	ac600010 */ 	sw	$zero,0x10($v1)
/*  f117df8:	24190018 */ 	addiu	$t9,$zero,0x18
/*  f117dfc:	10000061 */ 	beqz	$zero,.NB0f117f84
/*  f117e00:	ac790010 */ 	sw	$t9,0x10($v1)
/*  f117e04:	24080019 */ 	addiu	$t0,$zero,0x19
/*  f117e08:	1000005e */ 	beqz	$zero,.NB0f117f84
/*  f117e0c:	ac680010 */ 	sw	$t0,0x10($v1)
/*  f117e10:	2409000b */ 	addiu	$t1,$zero,0xb
/*  f117e14:	1000005b */ 	beqz	$zero,.NB0f117f84
/*  f117e18:	ac690010 */ 	sw	$t1,0x10($v1)
/*  f117e1c:	8c6a0000 */ 	lw	$t2,0x0($v1)
/*  f117e20:	24010003 */ 	addiu	$at,$zero,0x3
/*  f117e24:	24041748 */ 	addiu	$a0,$zero,0x1748
/*  f117e28:	15410013 */ 	bne	$t2,$at,.NB0f117e78
/*  f117e2c:	240d000b */ 	addiu	$t5,$zero,0xb
/*  f117e30:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f117e34:	24a5ecf4 */ 	addiu	$a1,$a1,-4876
/*  f117e38:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117e3c:	0c00581b */ 	jal	joyGetLock
/*  f117e40:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117e44:	83a6002b */ 	lb	$a2,0x2b($sp)
/*  f117e48:	00062600 */ 	sll	$a0,$a2,0x18
/*  f117e4c:	00045e03 */ 	sra	$t3,$a0,0x18
/*  f117e50:	0fc45fe5 */ 	jal	pak7f117f94nb
/*  f117e54:	01602025 */ 	or	$a0,$t3,$zero
/*  f117e58:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f117e5c:	24a5ecfc */ 	addiu	$a1,$a1,-4868
/*  f117e60:	0c005834 */ 	jal	joyReleaseLock
/*  f117e64:	2404174a */ 	addiu	$a0,$zero,0x174a
/*  f117e68:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117e6c:	240c000d */ 	addiu	$t4,$zero,0xd
/*  f117e70:	10000044 */ 	beqz	$zero,.NB0f117f84
/*  f117e74:	ac6c0010 */ 	sw	$t4,0x10($v1)
.NB0f117e78:
/*  f117e78:	10000042 */ 	beqz	$zero,.NB0f117f84
/*  f117e7c:	ac6d0010 */ 	sw	$t5,0x10($v1)
/*  f117e80:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117e84:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117e88:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117e8c:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117e90:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117e94:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117e98:	0fc3e5ed */ 	jal	func0f0fd1f4
/*  f117e9c:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117ea0:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117ea4:	10400037 */ 	beqz	$v0,.NB0f117f84
/*  f117ea8:	83a6002b */ 	lb	$a2,0x2b($sp)
/*  f117eac:	00c02025 */ 	or	$a0,$a2,$zero
/*  f117eb0:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117eb4:	0fc3e632 */ 	jal	func0f0fd320
/*  f117eb8:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117ebc:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117ec0:	240e0014 */ 	addiu	$t6,$zero,0x14
/*  f117ec4:	1000002f */ 	beqz	$zero,.NB0f117f84
/*  f117ec8:	ac6e0010 */ 	sw	$t6,0x10($v1)
/*  f117ecc:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117ed0:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117ed4:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117ed8:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117edc:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117ee0:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117ee4:	0fc3e5ed */ 	jal	func0f0fd1f4
/*  f117ee8:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f117eec:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117ef0:	10400024 */ 	beqz	$v0,.NB0f117f84
/*  f117ef4:	83a6002b */ 	lb	$a2,0x2b($sp)
/*  f117ef8:	00c02025 */ 	or	$a0,$a2,$zero
/*  f117efc:	24050002 */ 	addiu	$a1,$zero,0x2
/*  f117f00:	0fc3e632 */ 	jal	func0f0fd320
/*  f117f04:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117f08:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117f0c:	240f0013 */ 	addiu	$t7,$zero,0x13
/*  f117f10:	1000001c */ 	beqz	$zero,.NB0f117f84
/*  f117f14:	ac6f0010 */ 	sw	$t7,0x10($v1)
/*  f117f18:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117f1c:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117f20:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117f24:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117f28:	a3a6002b */ 	sb	$a2,0x2b($sp)
/*  f117f2c:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117f30:	0fc3e5ed */ 	jal	func0f0fd1f4
/*  f117f34:	00002825 */ 	or	$a1,$zero,$zero
/*  f117f38:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117f3c:	10400011 */ 	beqz	$v0,.NB0f117f84
/*  f117f40:	83a6002b */ 	lb	$a2,0x2b($sp)
/*  f117f44:	00c02025 */ 	or	$a0,$a2,$zero
/*  f117f48:	00002825 */ 	or	$a1,$zero,$zero
/*  f117f4c:	0fc3e632 */ 	jal	func0f0fd320
/*  f117f50:	afa3001c */ 	sw	$v1,0x1c($sp)
/*  f117f54:	8fa3001c */ 	lw	$v1,0x1c($sp)
/*  f117f58:	24180015 */ 	addiu	$t8,$zero,0x15
/*  f117f5c:	10000009 */ 	beqz	$zero,.NB0f117f84
/*  f117f60:	ac780010 */ 	sw	$t8,0x10($v1)
/*  f117f64:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117f68:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117f6c:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f117f70:	10000005 */ 	beqz	$zero,.NB0f117f88
/*  f117f74:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f117f78:	2404ffff */ 	addiu	$a0,$zero,-1
/*  f117f7c:	0fc3b669 */ 	jal	func0f0f0ca0
/*  f117f80:	24050001 */ 	addiu	$a1,$zero,0x1
.NB0f117f84:
/*  f117f84:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f117f88:
/*  f117f88:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f117f8c:	03e00008 */ 	jr	$ra
/*  f117f90:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

#if VERSION >= VERSION_NTSC_1_0
void pak0f11e3b4(void)
{
	// empty
}
#endif

#if VERSION < VERSION_NTSC_1_0
GLOBAL_ASM(
glabel pak7f117f94nb
/*  f117f94:	27bdffd8 */ 	addiu	$sp,$sp,-40
/*  f117f98:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f117f9c:	afa40028 */ 	sw	$a0,0x28($sp)
/*  f117fa0:	0fc461b0 */ 	jal	pak0f11e844
/*  f117fa4:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117fa8:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f117fac:	3c0f800a */ 	lui	$t7,0x800a
/*  f117fb0:	25ef6870 */ 	addiu	$t7,$t7,0x6870
/*  f117fb4:	00047080 */ 	sll	$t6,$a0,0x2
/*  f117fb8:	01c47023 */ 	subu	$t6,$t6,$a0
/*  f117fbc:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f117fc0:	01c47023 */ 	subu	$t6,$t6,$a0
/*  f117fc4:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f117fc8:	01c47021 */ 	addu	$t6,$t6,$a0
/*  f117fcc:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f117fd0:	01cf3021 */ 	addu	$a2,$t6,$t7
/*  f117fd4:	90c902b8 */ 	lbu	$t1,0x2b8($a2)
/*  f117fd8:	8ccc0008 */ 	lw	$t4,0x8($a2)
/*  f117fdc:	0002c8c0 */ 	sll	$t9,$v0,0x3
/*  f117fe0:	33280008 */ 	andi	$t0,$t9,0x8
/*  f117fe4:	312afff7 */ 	andi	$t2,$t1,0xfff7
/*  f117fe8:	010a5825 */ 	or	$t3,$t0,$t2
/*  f117fec:	2401000c */ 	addiu	$at,$zero,0xc
/*  f117ff0:	15810011 */ 	bne	$t4,$at,.NB0f118038
/*  f117ff4:	a0cb02b8 */ 	sb	$t3,0x2b8($a2)
/*  f117ff8:	0fc461b0 */ 	jal	pak0f11e844
/*  f117ffc:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f118000:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f118004:	000270c0 */ 	sll	$t6,$v0,0x3
/*  f118008:	31cf0008 */ 	andi	$t7,$t6,0x8
/*  f11800c:	90d802b8 */ 	lbu	$t8,0x2b8($a2)
/*  f118010:	3319fff7 */ 	andi	$t9,$t8,0xfff7
/*  f118014:	01f94825 */ 	or	$t1,$t7,$t9
/*  f118018:	a0c902b8 */ 	sb	$t1,0x2b8($a2)
/*  f11801c:	8cc302b8 */ 	lw	$v1,0x2b8($a2)
/*  f118020:	00035100 */ 	sll	$t2,$v1,0x4
/*  f118024:	05410004 */ 	bgez	$t2,.NB0f118038
/*  f118028:	00036080 */ 	sll	$t4,$v1,0x2
/*  f11802c:	05830003 */ 	bgezl	$t4,.NB0f11803c
/*  f118030:	8ccd02b8 */ 	lw	$t5,0x2b8($a2)
/*  f118034:	acc00008 */ 	sw	$zero,0x8($a2)
.NB0f118038:
/*  f118038:	8ccd02b8 */ 	lw	$t5,0x2b8($a2)
.NB0f11803c:
/*  f11803c:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f118040:	000dc100 */ 	sll	$t8,$t5,0x4
/*  f118044:	07020008 */ 	bltzl	$t8,.NB0f118068
/*  f118048:	8cc30008 */ 	lw	$v1,0x8($a2)
/*  f11804c:	0fc51dcf */ 	jal	func0f14cf6c
/*  f118050:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f118054:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f118058:	240f000c */ 	addiu	$t7,$zero,0xc
/*  f11805c:	10000063 */ 	beqz	$zero,.NB0f1181ec
/*  f118060:	accf0008 */ 	sw	$t7,0x8($a2)
/*  f118064:	8cc30008 */ 	lw	$v1,0x8($a2)
.NB0f118068:
/*  f118068:	24010001 */ 	addiu	$at,$zero,0x1
/*  f11806c:	50610060 */ 	beql	$v1,$at,.NB0f1181f0
/*  f118070:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f118074:	14600005 */ 	bnez	$v1,.NB0f11808c
/*  f118078:	24010002 */ 	addiu	$at,$zero,0x2
/*  f11807c:	0fc45ac3 */ 	jal	pak7f116b0cnb
/*  f118080:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118084:	1000005a */ 	beqz	$zero,.NB0f1181f0
/*  f118088:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f11808c:
/*  f11808c:	54610008 */ 	bnel	$v1,$at,.NB0f1180b0
/*  f118090:	24010003 */ 	addiu	$at,$zero,0x3
/*  f118094:	0fc45ac3 */ 	jal	pak7f116b0cnb
/*  f118098:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f11809c:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f1180a0:	24030003 */ 	addiu	$v1,$zero,0x3
/*  f1180a4:	acc30008 */ 	sw	$v1,0x8($a2)
/*  f1180a8:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1180ac:	24010003 */ 	addiu	$at,$zero,0x3
.NB0f1180b0:
/*  f1180b0:	54610008 */ 	bnel	$v1,$at,.NB0f1180d4
/*  f1180b4:	8cc30008 */ 	lw	$v1,0x8($a2)
/*  f1180b8:	0fc45b47 */ 	jal	pak0f11d110
/*  f1180bc:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f1180c0:	14400003 */ 	bnez	$v0,.NB0f1180d0
/*  f1180c4:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f1180c8:	24090004 */ 	addiu	$t1,$zero,0x4
/*  f1180cc:	acc90008 */ 	sw	$t1,0x8($a2)
.NB0f1180d0:
/*  f1180d0:	8cc30008 */ 	lw	$v1,0x8($a2)
.NB0f1180d4:
/*  f1180d4:	24010004 */ 	addiu	$at,$zero,0x4
/*  f1180d8:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1180dc:	5461000b */ 	bnel	$v1,$at,.NB0f11810c
/*  f1180e0:	24010005 */ 	addiu	$at,$zero,0x5
/*  f1180e4:	8cc802b8 */ 	lw	$t0,0x2b8($a2)
/*  f1180e8:	24030008 */ 	addiu	$v1,$zero,0x8
/*  f1180ec:	00085940 */ 	sll	$t3,$t0,0x5
/*  f1180f0:	05630005 */ 	bgezl	$t3,.NB0f118108
/*  f1180f4:	acc30008 */ 	sw	$v1,0x8($a2)
/*  f1180f8:	24030005 */ 	addiu	$v1,$zero,0x5
/*  f1180fc:	10000002 */ 	beqz	$zero,.NB0f118108
/*  f118100:	acc30008 */ 	sw	$v1,0x8($a2)
/*  f118104:	acc30008 */ 	sw	$v1,0x8($a2)
.NB0f118108:
/*  f118108:	24010005 */ 	addiu	$at,$zero,0x5
.NB0f11810c:
/*  f11810c:	54610008 */ 	bnel	$v1,$at,.NB0f118130
/*  f118110:	24010006 */ 	addiu	$at,$zero,0x6
/*  f118114:	0fc45ac3 */ 	jal	pak7f116b0cnb
/*  f118118:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f11811c:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f118120:	24030006 */ 	addiu	$v1,$zero,0x6
/*  f118124:	acc30008 */ 	sw	$v1,0x8($a2)
/*  f118128:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f11812c:	24010006 */ 	addiu	$at,$zero,0x6
.NB0f118130:
/*  f118130:	54610008 */ 	bnel	$v1,$at,.NB0f118154
/*  f118134:	8cc30008 */ 	lw	$v1,0x8($a2)
/*  f118138:	0fc45b47 */ 	jal	pak0f11d110
/*  f11813c:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f118140:	14400003 */ 	bnez	$v0,.NB0f118150
/*  f118144:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f118148:	24180007 */ 	addiu	$t8,$zero,0x7
/*  f11814c:	acd80008 */ 	sw	$t8,0x8($a2)
.NB0f118150:
/*  f118150:	8cc30008 */ 	lw	$v1,0x8($a2)
.NB0f118154:
/*  f118154:	24010007 */ 	addiu	$at,$zero,0x7
/*  f118158:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f11815c:	14610003 */ 	bne	$v1,$at,.NB0f11816c
/*  f118160:	00001025 */ 	or	$v0,$zero,$zero
/*  f118164:	24030008 */ 	addiu	$v1,$zero,0x8
/*  f118168:	acc30008 */ 	sw	$v1,0x8($a2)
.NB0f11816c:
/*  f11816c:	24010008 */ 	addiu	$at,$zero,0x8
/*  f118170:	5461000f */ 	bnel	$v1,$at,.NB0f1181b0
/*  f118174:	24010009 */ 	addiu	$at,$zero,0x9
/*  f118178:	24031000 */ 	addiu	$v1,$zero,0x1000
.NB0f11817c:
/*  f11817c:	8cd902c4 */ 	lw	$t9,0x2c4($a2)
/*  f118180:	03224821 */ 	addu	$t1,$t9,$v0
/*  f118184:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f118188:	1443fffc */ 	bne	$v0,$v1,.NB0f11817c
/*  f11818c:	a1200000 */ 	sb	$zero,0x0($t1)
/*  f118190:	8cc50270 */ 	lw	$a1,0x270($a2)
/*  f118194:	0fc45cb2 */ 	jal	pak0f11d678
/*  f118198:	afa60018 */ 	sw	$a2,0x18($sp)
/*  f11819c:	8fa60018 */ 	lw	$a2,0x18($sp)
/*  f1181a0:	24030009 */ 	addiu	$v1,$zero,0x9
/*  f1181a4:	acc30008 */ 	sw	$v1,0x8($a2)
/*  f1181a8:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1181ac:	24010009 */ 	addiu	$at,$zero,0x9
.NB0f1181b0:
/*  f1181b0:	5461000f */ 	bnel	$v1,$at,.NB0f1181f0
/*  f1181b4:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f1181b8:	0fc44589 */ 	jal	pakGetUnk270
/*  f1181bc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1181c0:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1181c4:	0fc45cd4 */ 	jal	pak0f11d680
/*  f1181c8:	00402825 */ 	or	$a1,$v0,$zero
/*  f1181cc:	50400008 */ 	beqzl	$v0,.NB0f1181f0
/*  f1181d0:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f1181d4:	0fc45d23 */ 	jal	pak0f11d7c4
/*  f1181d8:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1181dc:	10400003 */ 	beqz	$v0,.NB0f1181ec
/*  f1181e0:	83a4002b */ 	lb	$a0,0x2b($sp)
/*  f1181e4:	0fc4457b */ 	jal	pakSetUnk008
/*  f1181e8:	2405000b */ 	addiu	$a1,$zero,0xb
.NB0f1181ec:
/*  f1181ec:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f1181f0:
/*  f1181f0:	27bd0028 */ 	addiu	$sp,$sp,0x28
/*  f1181f4:	03e00008 */ 	jr	$ra
/*  f1181f8:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

void pak0f11e3bc(s8 device)
{
	g_Paks[device].unk008 = 0;
}

void pakProbeEeprom(void)
{
	s32 type;

#if VERSION >= VERSION_NTSC_1_0
	joyGetLock();
#else
	joyGetLock(6199, "pak.c");
#endif

	type = osEepromProbe(&var80099e78);

#if VERSION >= VERSION_NTSC_1_0
	joyReleaseLock();
#else
	joyReleaseLock(6201, "pak.c");
#endif

	if (type == EEPROM_TYPE_16K) {
		g_PakHasEeprom = true;

		if (argFindByPrefix(1, "-scrub")) {
			pakScrub(SAVEDEVICE_GAMEPAK);
		}
	} else {
		g_PakHasEeprom = false;
	}
}

s32 pakReadEeprom(u8 address, u8 *buffer, u32 len)
{
	s32 result;

#if VERSION >= VERSION_NTSC_1_0
	joyGetLock();
#else
	joyGetLock(6234, "pak.c");
#endif

	result = osEepromLongRead(&var80099e78, address, buffer, len);

#if VERSION >= VERSION_NTSC_1_0
	joyReleaseLock();
#else
	joyReleaseLock(6236, "pak.c");
#endif

	return result == 0 ? PAKERROR_OK : PAKERROR_EEPROM_READFAILED;
}

s32 pakWriteEeprom(u8 address, u8 *buffer, u32 len)
{
	s32 result;

#if VERSION >= VERSION_NTSC_1_0
	joyGetLock();
#else
	joyGetLock(6269, "pak.c");
#endif

	result = osEepromLongWrite(&var80099e78, address, buffer, len);

#if VERSION >= VERSION_NTSC_1_0
	joyReleaseLock();
#else
	joyReleaseLock(6271, "pak.c");
#endif

	return result == 0 ? PAKERROR_OK : PAKERROR_EEPROM_WRITEFAILED;
}

GLOBAL_ASM(
glabel pakSetBitflag
/*  f11e530:	10c0000a */ 	beqz	$a2,.L0f11e55c
/*  f11e534:	000458c2 */ 	srl	$t3,$a0,0x3
/*  f11e538:	000470c2 */ 	srl	$t6,$a0,0x3
/*  f11e53c:	00ae1021 */ 	addu	$v0,$a1,$t6
/*  f11e540:	904f0000 */ 	lbu	$t7,0x0($v0)
/*  f11e544:	30980007 */ 	andi	$t8,$a0,0x7
/*  f11e548:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f11e54c:	03194804 */ 	sllv	$t1,$t9,$t8
/*  f11e550:	01e95025 */ 	or	$t2,$t7,$t1
/*  f11e554:	03e00008 */ 	jr	$ra
/*  f11e558:	a04a0000 */ 	sb	$t2,0x0($v0)
.L0f11e55c:
/*  f11e55c:	00ab1021 */ 	addu	$v0,$a1,$t3
/*  f11e560:	904c0000 */ 	lbu	$t4,0x0($v0)
/*  f11e564:	308d0007 */ 	andi	$t5,$a0,0x7
/*  f11e568:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f11e56c:	01aec004 */ 	sllv	$t8,$t6,$t5
/*  f11e570:	03007827 */ 	nor	$t7,$t8,$zero
/*  f11e574:	018f4824 */ 	and	$t1,$t4,$t7
/*  f11e578:	a0490000 */ 	sb	$t1,0x0($v0)
/*  f11e57c:	03e00008 */ 	jr	$ra
/*  f11e580:	00000000 */ 	sll	$zero,$zero,0x0
);

// Mismatch: regalloc
//void pakSetBitflag(u32 flagnum, u8 *bitstream, bool set)
//{
//	u32 byteindex = flagnum / 8;
//	u8 mask = 1 << (flagnum % 8);
//
//	if (set) {
//		bitstream[byteindex] |= mask;
//	} else {
//		bitstream[byteindex] &= ~mask;
//	}
//}

bool pakHasBitflag(u32 flagnum, u8 *bitstream)
{
	u32 byteindex = flagnum / 8;
	u8 mask = 1 << (flagnum % 8);

	return bitstream[byteindex] & mask ? 1 : 0;
}

void pakClearAllBitflags(u8 *flags)
{
	s32 i;

	for (i = 0; i <= GAMEFILEFLAG_4E; i++) {
		pakSetBitflag(i, flags, false);
	}
}

u32 pak0f11e610(u32 arg0)
{
	return arg0;
}

/**
 * The note name and note extension are stored on the pak using N64 font code.
 * This is different to ASCII.
 *
 * This function expects src to be a pointer to an N64 font code string.
 * It converts it to ASCII and writes it to dst. Characters are replaced with
 * an asterisk if they are invalid font codes or if the character doesn't exist
 * in PD's font.
 */
void pakN64FontCodeToAscii(char *src, char *dst, s32 len)
{
	char buffer[256];
	s32 i;
	char in;
	char c;
	char *ptr = buffer;

	for (i = 0; i < len;) {
		in = *src;
		src++;
		i++;
		c = '*';

		// @bug: The length check of the map is off by 1. The last char in the
		// list is '@', so if an @ sign appears in a note name then PD will
		// incorrectly replace it with '*' when displaying the name.
		// The original source likely used a literal here instead of sizeof().
		if (in < (s32)(sizeof(g_N64FontCodeMap) - 1)) {
			c = g_N64FontCodeMap[in];
		}

		// PD has a double quote in its fonts, but I guess it doesn't render
		// very well. So it gets replaced with two single quotes.
		if ((u32)c == '"') {
			*ptr = '\'';
			ptr++;
			*ptr = '\'';
		} else {
			*ptr = c;
		}

		ptr++;
	}

	*ptr = '\0';

	strcpy(dst, buffer);
}

s8 pakFindBySerial(s32 findserial)
{
	s8 device = -1;
	s32 i;

	for (i = 0; i < 5; i++) {
		if (pak0f116aec(i)) {
			s32 serial = pakGetDeviceSerial(i);

			if (findserial == serial) {
				device = i;
			}
		}
	}

	return device;
}

s32 pak0f11e750(s8 arg0)
{
	char sp18[32];
	sp18[0] = '\n';

	return pak0f11cc6c(arg0, 0, sp18, 32);
}

bool pak0f11e78c(void)
{
	s8 i;

	for (i = 0; i < 4; i++) {
		if (pak0f11e844(i) == 2) {
			return true;
		}
	}

	return false;
}

/**
 * Probable @bug: This function is probably intended to be a "strings are equal"
 * check, however it's actually checking if either string starts with the other.
 */
bool pak0f11e7f0(char *a, char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return false;
		}

		a++;
		b++;
	}

	return true;
}

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4d24[] = "Pak %d -> Pak_PdGameBoySetRWByte - Fatal Error\n";
#else
const char var7f1b4d24[] = "Pak %d -> Pak_PdGameBoySetRWByte - Fatal Error";
#endif

#if VERSION >= VERSION_NTSC_1_0
s32 pak0f11e844(s8 device)
{
	s32 stack1;
	s32 stack2;
	struct pakthing sp6c;
	char sp38[52];
	u8 sp37;
	s32 value;
	s32 sp2c = 0;
	s32 sp28 = 0;
	s32 sp24 = 1;

	if (g_Paks[device].unk000 != 3) {
		return 0;
	}

	joyGetLock();

	value = func00050d60(PFS(device), sp38, &sp37);

#if VERSION >= VERSION_NTSC_FINAL
	// NTSC Final sets sp28 to 1 unconditionally.
	// If we just set it to 1 without the if-statement then it creates a
	// mismatch because the compiler optimises out the sp28 = 0 line earlier.
	// Using this if-statement with a condition that's always true makes the
	// compiler optimise out the if-statement but leave both assignments to
	// sp28 intact.
	if (sp24) {
		sp28 = 1;
	}

	if (value) {
		sp24 = 0;
	}
#else
	// NTSC 1.0 only sets sp28 to 1 if the call to func00050d60 returned 0.
	// The else here might have been else if (sp24). This optimises itself out,
	// but may explain why the final code appears to use a condition. They could
	// have moved the else-if into its own check (and had to do it prior to the
	// value check for it to work as intended).
	if (value) {
		sp24 = 0;
	} else {
		sp28 = 1;
	}
#endif

	if (var80075cb0 == sp6c.unk10) {
		if (pak0f11e7f0(var80075cb4, sp6c.unk00) || pak0f11e7f0(var80075cc0, sp6c.unk00)) {
			g_Paks[device].unk2b8_03 = 0;
			g_Paks[device].unk2b8_04 = 1;
			sp2c = 2;
		}
	}

	if (sp2c != 2) {
		sp24 = 0;
	}

	if (sp28) {
		if (func00006330(PFS(device), 0)) {
			sp24 = 0;
		}
	}

	joyReleaseLock();

	if (sp24) {
		return sp2c;
	}

	g_Paks[device].unk010 = 26;
	return 0;
}
#else
const char var7f1aee7c[] = "pak.c";
const char var7f1aee84[] = "pak.c";
const char var7f1aee8c[] = "pak.c";
const char var7f1aee94[] = "pak.c";
const char var7f1aee9c[] = "pak.c";
const char var7f1aeea4[] = "pak.c";
const char var7f1aeeac[] = "pak.c";
const char var7f1aeeb4[] = "pak.c";
const char var7f1aeebc[] = "pak.c";
const char var7f1aeec4[] = "pak.c";

GLOBAL_ASM(
glabel pak0f11e844
/*  f1186c0:	27bdff80 */ 	addiu	$sp,$sp,-128
/*  f1186c4:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f1186c8:	afa40080 */ 	sw	$a0,0x80($sp)
/*  f1186cc:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1186d0:	24a5ee7c */ 	addiu	$a1,$a1,-4484
/*  f1186d4:	0c00581b */ 	jal	joyGetLock
/*  f1186d8:	24041b85 */ 	addiu	$a0,$zero,0x1b85
/*  f1186dc:	83a20083 */ 	lb	$v0,0x83($sp)
/*  f1186e0:	3c0f800a */ 	lui	$t7,0x800a
/*  f1186e4:	25ef6870 */ 	addiu	$t7,$t7,0x6870
/*  f1186e8:	00027080 */ 	sll	$t6,$v0,0x2
/*  f1186ec:	01c27023 */ 	subu	$t6,$t6,$v0
/*  f1186f0:	000e7080 */ 	sll	$t6,$t6,0x2
/*  f1186f4:	01c27023 */ 	subu	$t6,$t6,$v0
/*  f1186f8:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f1186fc:	01c27021 */ 	addu	$t6,$t6,$v0
/*  f118700:	000e70c0 */ 	sll	$t6,$t6,0x3
/*  f118704:	01cf1821 */ 	addu	$v1,$t6,$t7
/*  f118708:	8c780000 */ 	lw	$t8,0x0($v1)
/*  f11870c:	24010003 */ 	addiu	$at,$zero,0x3
/*  f118710:	24041b8a */ 	addiu	$a0,$zero,0x1b8a
/*  f118714:	13010006 */ 	beq	$t8,$at,.NB0f118730
/*  f118718:	27a5002f */ 	addiu	$a1,$sp,0x2f
/*  f11871c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f118720:	0c005834 */ 	jal	joyReleaseLock
/*  f118724:	24a5ee84 */ 	addiu	$a1,$a1,-4476
/*  f118728:	100000a3 */ 	beqz	$zero,.NB0f1189b8
/*  f11872c:	00001025 */ 	or	$v0,$zero,$zero
.NB0f118730:
/*  f118730:	24010004 */ 	addiu	$at,$zero,0x4
/*  f118734:	14410003 */ 	bne	$v0,$at,.NB0f118744
/*  f118738:	0002c880 */ 	sll	$t9,$v0,0x2
/*  f11873c:	10000008 */ 	beqz	$zero,.NB0f118760
/*  f118740:	00002025 */ 	or	$a0,$zero,$zero
.NB0f118744:
/*  f118744:	0322c823 */ 	subu	$t9,$t9,$v0
/*  f118748:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f11874c:	0322c821 */ 	addu	$t9,$t9,$v0
/*  f118750:	3c08800a */ 	lui	$t0,0x800a
/*  f118754:	25087658 */ 	addiu	$t0,$t0,0x7658
/*  f118758:	0019c8c0 */ 	sll	$t9,$t9,0x3
/*  f11875c:	03282021 */ 	addu	$a0,$t9,$t0
.NB0f118760:
/*  f118760:	0c0148f0 */ 	jal	func00050be0
/*  f118764:	afa30020 */ 	sw	$v1,0x20($sp)
/*  f118768:	10400007 */ 	beqz	$v0,.NB0f118788
/*  f11876c:	93a3002f */ 	lbu	$v1,0x2f($sp)
/*  f118770:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f118774:	24a5ee8c */ 	addiu	$a1,$a1,-4468
/*  f118778:	0c005834 */ 	jal	joyReleaseLock
/*  f11877c:	24041b91 */ 	addiu	$a0,$zero,0x1b91
/*  f118780:	1000008d */ 	beqz	$zero,.NB0f1189b8
/*  f118784:	00001025 */ 	or	$v0,$zero,$zero
.NB0f118788:
/*  f118788:	30690004 */ 	andi	$t1,$v1,0x4
/*  f11878c:	11200010 */ 	beqz	$t1,.NB0f1187d0
/*  f118790:	83a20083 */ 	lb	$v0,0x83($sp)
/*  f118794:	24010004 */ 	addiu	$at,$zero,0x4
/*  f118798:	14410003 */ 	bne	$v0,$at,.NB0f1187a8
/*  f11879c:	00025080 */ 	sll	$t2,$v0,0x2
/*  f1187a0:	10000008 */ 	beqz	$zero,.NB0f1187c4
/*  f1187a4:	00002025 */ 	or	$a0,$zero,$zero
.NB0f1187a8:
/*  f1187a8:	01425023 */ 	subu	$t2,$t2,$v0
/*  f1187ac:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f1187b0:	01425021 */ 	addu	$t2,$t2,$v0
/*  f1187b4:	3c0b800a */ 	lui	$t3,0x800a
/*  f1187b8:	256b7658 */ 	addiu	$t3,$t3,0x7658
/*  f1187bc:	000a50c0 */ 	sll	$t2,$t2,0x3
/*  f1187c0:	014b2021 */ 	addu	$a0,$t2,$t3
.NB0f1187c4:
/*  f1187c4:	0c0148f0 */ 	jal	func00050be0
/*  f1187c8:	27a5002f */ 	addiu	$a1,$sp,0x2f
/*  f1187cc:	93a3002f */ 	lbu	$v1,0x2f($sp)
.NB0f1187d0:
/*  f1187d0:	30620080 */ 	andi	$v0,$v1,0x80
/*  f1187d4:	14400006 */ 	bnez	$v0,.NB0f1187f0
/*  f1187d8:	24041b9f */ 	addiu	$a0,$zero,0x1b9f
/*  f1187dc:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1187e0:	0c005834 */ 	jal	joyReleaseLock
/*  f1187e4:	24a5ee94 */ 	addiu	$a1,$a1,-4460
/*  f1187e8:	10000073 */ 	beqz	$zero,.NB0f1189b8
/*  f1187ec:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1187f0:
/*  f1187f0:	14400006 */ 	bnez	$v0,.NB0f11880c
/*  f1187f4:	24041ba6 */ 	addiu	$a0,$zero,0x1ba6
/*  f1187f8:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1187fc:	0c005834 */ 	jal	joyReleaseLock
/*  f118800:	24a5ee9c */ 	addiu	$a1,$a1,-4452
/*  f118804:	1000006c */ 	beqz	$zero,.NB0f1189b8
/*  f118808:	00001025 */ 	or	$v0,$zero,$zero
.NB0f11880c:
/*  f11880c:	83a20083 */ 	lb	$v0,0x83($sp)
/*  f118810:	24010004 */ 	addiu	$at,$zero,0x4
/*  f118814:	3c0d800a */ 	lui	$t5,0x800a
/*  f118818:	14410003 */ 	bne	$v0,$at,.NB0f118828
/*  f11881c:	00026080 */ 	sll	$t4,$v0,0x2
/*  f118820:	10000007 */ 	beqz	$zero,.NB0f118840
/*  f118824:	00002025 */ 	or	$a0,$zero,$zero
.NB0f118828:
/*  f118828:	01826023 */ 	subu	$t4,$t4,$v0
/*  f11882c:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f118830:	01826021 */ 	addu	$t4,$t4,$v0
/*  f118834:	000c60c0 */ 	sll	$t4,$t4,0x3
/*  f118838:	25ad7658 */ 	addiu	$t5,$t5,0x7658
/*  f11883c:	018d2021 */ 	addu	$a0,$t4,$t5
.NB0f118840:
/*  f118840:	0c00191c */ 	jal	func00006330
/*  f118844:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f118848:	10400007 */ 	beqz	$v0,.NB0f118868
/*  f11884c:	83ae0083 */ 	lb	$t6,0x83($sp)
/*  f118850:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f118854:	24a5eea4 */ 	addiu	$a1,$a1,-4444
/*  f118858:	0c005834 */ 	jal	joyReleaseLock
/*  f11885c:	24041bad */ 	addiu	$a0,$zero,0x1bad
/*  f118860:	10000055 */ 	beqz	$zero,.NB0f1189b8
/*  f118864:	00001025 */ 	or	$v0,$zero,$zero
.NB0f118868:
/*  f118868:	24010004 */ 	addiu	$at,$zero,0x4
/*  f11886c:	15c10003 */ 	bne	$t6,$at,.NB0f11887c
/*  f118870:	27a50030 */ 	addiu	$a1,$sp,0x30
/*  f118874:	1000000a */ 	beqz	$zero,.NB0f1188a0
/*  f118878:	00002025 */ 	or	$a0,$zero,$zero
.NB0f11887c:
/*  f11887c:	83af0083 */ 	lb	$t7,0x83($sp)
/*  f118880:	3c19800a */ 	lui	$t9,0x800a
/*  f118884:	27397658 */ 	addiu	$t9,$t9,0x7658
/*  f118888:	000fc080 */ 	sll	$t8,$t7,0x2
/*  f11888c:	030fc023 */ 	subu	$t8,$t8,$t7
/*  f118890:	0018c080 */ 	sll	$t8,$t8,0x2
/*  f118894:	030fc021 */ 	addu	$t8,$t8,$t7
/*  f118898:	0018c0c0 */ 	sll	$t8,$t8,0x3
/*  f11889c:	03192021 */ 	addu	$a0,$t8,$t9
.NB0f1188a0:
/*  f1188a0:	0c014950 */ 	jal	func00050d60
/*  f1188a4:	27a6002f */ 	addiu	$a2,$sp,0x2f
/*  f1188a8:	10400007 */ 	beqz	$v0,.NB0f1188c8
/*  f1188ac:	97a80074 */ 	lhu	$t0,0x74($sp)
/*  f1188b0:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1188b4:	24a5eeac */ 	addiu	$a1,$a1,-4436
/*  f1188b8:	0c005834 */ 	jal	joyReleaseLock
/*  f1188bc:	24041bb8 */ 	addiu	$a0,$zero,0x1bb8
/*  f1188c0:	1000003d */ 	beqz	$zero,.NB0f1189b8
/*  f1188c4:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1188c8:
/*  f1188c8:	3c098008 */ 	lui	$t1,0x8008
/*  f1188cc:	95298050 */ 	lhu	$t1,-0x7fb0($t1)
/*  f1188d0:	3c048008 */ 	lui	$a0,0x8008
/*  f1188d4:	24848058 */ 	addiu	$a0,$a0,-32680
/*  f1188d8:	15090014 */ 	bne	$t0,$t1,.NB0f11892c
/*  f1188dc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f1188e0:	0fc4619b */ 	jal	pak0f11e7f0
/*  f1188e4:	27a50064 */ 	addiu	$a1,$sp,0x64
/*  f1188e8:	14400006 */ 	bnez	$v0,.NB0f118904
/*  f1188ec:	3c048008 */ 	lui	$a0,0x8008
/*  f1188f0:	24848068 */ 	addiu	$a0,$a0,-32664
/*  f1188f4:	0fc4619b */ 	jal	pak0f11e7f0
/*  f1188f8:	27a50064 */ 	addiu	$a1,$sp,0x64
/*  f1188fc:	1040000b */ 	beqz	$v0,.NB0f11892c
/*  f118900:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f118904:
/*  f118904:	8fa20020 */ 	lw	$v0,0x20($sp)
/*  f118908:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f11890c:	24a5eeb4 */ 	addiu	$a1,$a1,-4428
/*  f118910:	904b02b8 */ 	lbu	$t3,0x2b8($v0)
/*  f118914:	24041bc7 */ 	addiu	$a0,$zero,0x1bc7
/*  f118918:	356d0020 */ 	ori	$t5,$t3,0x20
/*  f11891c:	31ae00ef */ 	andi	$t6,$t5,0xef
/*  f118920:	a04d02b8 */ 	sb	$t5,0x2b8($v0)
/*  f118924:	0c005834 */ 	jal	joyReleaseLock
/*  f118928:	a04e02b8 */ 	sb	$t6,0x2b8($v0)
.NB0f11892c:
/*  f11892c:	3c188008 */ 	lui	$t8,0x8008
/*  f118930:	97188054 */ 	lhu	$t8,-0x7fac($t8)
/*  f118934:	97af0074 */ 	lhu	$t7,0x74($sp)
/*  f118938:	3c048008 */ 	lui	$a0,0x8008
/*  f11893c:	24848078 */ 	addiu	$a0,$a0,-32648
/*  f118940:	15f80018 */ 	bne	$t7,$t8,.NB0f1189a4
/*  f118944:	00000000 */ 	sll	$zero,$zero,0x0
/*  f118948:	0fc4619b */ 	jal	pak0f11e7f0
/*  f11894c:	27a50064 */ 	addiu	$a1,$sp,0x64
/*  f118950:	14400006 */ 	bnez	$v0,.NB0f11896c
/*  f118954:	3c048008 */ 	lui	$a0,0x8008
/*  f118958:	24848084 */ 	addiu	$a0,$a0,-32636
/*  f11895c:	0fc4619b */ 	jal	pak0f11e7f0
/*  f118960:	27a50064 */ 	addiu	$a1,$sp,0x64
/*  f118964:	1040000f */ 	beqz	$v0,.NB0f1189a4
/*  f118968:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f11896c:
/*  f11896c:	8fa20020 */ 	lw	$v0,0x20($sp)
/*  f118970:	83a40083 */ 	lb	$a0,0x83($sp)
/*  f118974:	905902b8 */ 	lbu	$t9,0x2b8($v0)
/*  f118978:	332affdf */ 	andi	$t2,$t9,0xffdf
/*  f11897c:	354b0010 */ 	ori	$t3,$t2,0x10
/*  f118980:	a04a02b8 */ 	sb	$t2,0x2b8($v0)
/*  f118984:	0fc462a0 */ 	jal	pak0f11eaec
/*  f118988:	a04b02b8 */ 	sb	$t3,0x2b8($v0)
/*  f11898c:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f118990:	24a5eebc */ 	addiu	$a1,$a1,-4420
/*  f118994:	0c005834 */ 	jal	joyReleaseLock
/*  f118998:	24041bdf */ 	addiu	$a0,$zero,0x1bdf
/*  f11899c:	10000006 */ 	beqz	$zero,.NB0f1189b8
/*  f1189a0:	24020002 */ 	addiu	$v0,$zero,0x2
.NB0f1189a4:
/*  f1189a4:	3c057f1b */ 	lui	$a1,0x7f1b
/*  f1189a8:	24a5eec4 */ 	addiu	$a1,$a1,-4412
/*  f1189ac:	0c005834 */ 	jal	joyReleaseLock
/*  f1189b0:	24041be4 */ 	addiu	$a0,$zero,0x1be4
/*  f1189b4:	00001025 */ 	or	$v0,$zero,$zero
.NB0f1189b8:
/*  f1189b8:	8fbf0014 */ 	lw	$ra,0x14($sp)
/*  f1189bc:	27bd0080 */ 	addiu	$sp,$sp,0x80
/*  f1189c0:	03e00008 */ 	jr	$ra
/*  f1189c4:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

bool pak0f11ea34(s8 arg0)
{
	char numbers[] = "0123456789012345678901234567890123456789";
	u8 sp20[36];

	if (!pak0f11cd00(arg0, 0xa000, numbers, 32, true)) {
		return false;
	}

	if (!pak0f11ce00(arg0, 0xa000, sp20, 32, true)) {
		return false;
	}

	return true;
}

#if VERSION >= VERSION_NTSC_1_0
const char var7f1b4d80[] = "PerfDark\n";
#else
const char var7f1b4d80[] = "PerfDark";
#endif

GLOBAL_ASM(
glabel pak0f11eaec
/*  f11eaec:	27bdff68 */ 	addiu	$sp,$sp,-152
/*  f11eaf0:	afa40098 */ 	sw	$a0,0x98($sp)
/*  f11eaf4:	afbf001c */ 	sw	$ra,0x1c($sp)
/*  f11eaf8:	afa00030 */ 	sw	$zero,0x30($sp)
/*  f11eafc:	afa0002c */ 	sw	$zero,0x2c($sp)
/*  f11eb00:	27a40078 */ 	addiu	$a0,$sp,0x78
/*  f11eb04:	27a30078 */ 	addiu	$v1,$sp,0x78
/*  f11eb08:	27a20058 */ 	addiu	$v0,$sp,0x58
.L0f11eb0c:
/*  f11eb0c:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11eb10:	0044082b */ 	sltu	$at,$v0,$a0
/*  f11eb14:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f11eb18:	a060ffff */ 	sb	$zero,-0x1($v1)
/*  f11eb1c:	1420fffb */ 	bnez	$at,.L0f11eb0c
/*  f11eb20:	a040ffff */ 	sb	$zero,-0x1($v0)
/*  f11eb24:	a3a00078 */ 	sb	$zero,0x78($sp)
/*  f11eb28:	0fc47a8d */ 	jal	pak0f11ea34
/*  f11eb2c:	83a4009b */ 	lb	$a0,0x9b($sp)
/*  f11eb30:	0fc47a8d */ 	jal	pak0f11ea34
/*  f11eb34:	83a4009b */ 	lb	$a0,0x9b($sp)
/*  f11eb38:	0fc47a8d */ 	jal	pak0f11ea34
/*  f11eb3c:	83a4009b */ 	lb	$a0,0x9b($sp)
/*  f11eb40:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f11eb44:	afae0010 */ 	sw	$t6,0x10($sp)
/*  f11eb48:	83a4009b */ 	lb	$a0,0x9b($sp)
/*  f11eb4c:	3405bfe0 */ 	dli	$a1,0xbfe0
/*  f11eb50:	27a60038 */ 	addiu	$a2,$sp,0x38
/*  f11eb54:	0fc47380 */ 	jal	pak0f11ce00
/*  f11eb58:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11eb5c:	14400003 */ 	bnez	$v0,.L0f11eb6c
/*  f11eb60:	27a30058 */ 	addiu	$v1,$sp,0x58
/*  f11eb64:	240f0001 */ 	addiu	$t7,$zero,0x1
/*  f11eb68:	afaf0030 */ 	sw	$t7,0x30($sp)
.L0f11eb6c:
/*  f11eb6c:	27a20038 */ 	addiu	$v0,$sp,0x38
/*  f11eb70:	a3a00029 */ 	sb	$zero,0x29($sp)
.L0f11eb74:
/*  f11eb74:	90580000 */ 	lbu	$t8,0x0($v0)
/*  f11eb78:	24420001 */ 	addiu	$v0,$v0,0x1
/*  f11eb7c:	1443fffd */ 	bne	$v0,$v1,.L0f11eb74
/*  f11eb80:	a3b80028 */ 	sb	$t8,0x28($sp)
/*  f11eb84:	3c047f1b */ 	lui	$a0,%hi(var7f1b4d80)
/*  f11eb88:	24844d80 */ 	addiu	$a0,$a0,%lo(var7f1b4d80)
/*  f11eb8c:	0fc479fc */ 	jal	pak0f11e7f0
/*  f11eb90:	27a50038 */ 	addiu	$a1,$sp,0x38
/*  f11eb94:	24190001 */ 	addiu	$t9,$zero,0x1
/*  f11eb98:	afb90010 */ 	sw	$t9,0x10($sp)
/*  f11eb9c:	83a4009b */ 	lb	$a0,0x9b($sp)
/*  f11eba0:	3405a000 */ 	dli	$a1,0xa000
/*  f11eba4:	27a60038 */ 	addiu	$a2,$sp,0x38
/*  f11eba8:	0fc47380 */ 	jal	pak0f11ce00
/*  f11ebac:	24070020 */ 	addiu	$a3,$zero,0x20
/*  f11ebb0:	14400002 */ 	bnez	$v0,.L0f11ebbc
/*  f11ebb4:	24080001 */ 	addiu	$t0,$zero,0x1
/*  f11ebb8:	afa80030 */ 	sw	$t0,0x30($sp)
.L0f11ebbc:
/*  f11ebbc:	93a90038 */ 	lbu	$t1,0x38($sp)
/*  f11ebc0:	240b0001 */ 	addiu	$t3,$zero,0x1
/*  f11ebc4:	312a0001 */ 	andi	$t2,$t1,0x1
/*  f11ebc8:	51400003 */ 	beqzl	$t2,.L0f11ebd8
/*  f11ebcc:	8fac0030 */ 	lw	$t4,0x30($sp)
/*  f11ebd0:	afab002c */ 	sw	$t3,0x2c($sp)
/*  f11ebd4:	8fac0030 */ 	lw	$t4,0x30($sp)
.L0f11ebd8:
/*  f11ebd8:	8fbf001c */ 	lw	$ra,0x1c($sp)
/*  f11ebdc:	8fa2002c */ 	lw	$v0,0x2c($sp)
/*  f11ebe0:	11800003 */ 	beqz	$t4,.L0f11ebf0
/*  f11ebe4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f11ebe8:	10000001 */ 	beqz	$zero,.L0f11ebf0
/*  f11ebec:	00001025 */ 	or	$v0,$zero,$zero
.L0f11ebf0:
/*  f11ebf0:	03e00008 */ 	jr	$ra
/*  f11ebf4:	27bd0098 */ 	addiu	$sp,$sp,0x98
);

void pak0f11ebf8(s8 arg0)
{
	u8 sp20[32];

	if (pak0f11cbd8(arg0, 0xa000, sp20, 32)) {
		pak0f11e750(arg0);
		sp20[0] &= 0x7f;

		if (pak0f11cc6c(arg0, 0xa000, sp20, 32)) {
			if (pak0f11cbd8(arg0, 0xa000, sp20, 32)) {
				sp20[0] |= 0x80;

				if (pak0f11cc6c(arg0, 0xa000, sp20, 32)) {
					pak0f11cbd8(arg0, 0xa000, sp20, 32);
				}
			}
		}
	}
}
