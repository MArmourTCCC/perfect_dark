#ifndef IN_GAME_DATA_000000_H
#define IN_GAME_DATA_000000_H
#include <ultra64.h>
#include "types.h"

extern s32 g_StageNum;
extern u64 rand_seed;

extern struct chrdata *g_ChrsA; // pointer to first element of chrs array
extern s32 g_NumChrsA;
extern u32 var8005ce60;
extern u32 var8005ce64;
extern u32 var8005ce68;
extern u32 var8005ce6c;
extern u32 var8005ce70;
extern u32 var80065be0;
extern u32 var80067aa0;
extern u32 var80067ae8;
extern u32 var800656c0;
extern u32 var80067a10;
extern u32 var80067a58;
extern u32 var800663d8;
extern u32 var80067b30;
extern u32 var80067b78;
extern u32 g_CountdownTimerVisible;
extern bool g_CountdownTimerRunning;
extern f32 g_CountdownTimerValue;
extern u32 g_StageFlags;

extern struct audiodefinition audiodefinitions[];
extern struct audioconfig audioconfigs[];

extern bool (*g_CommandPointers[NUM_AICOMMANDS])(void);
extern u16 g_CommandLengths[NUM_AICOMMANDS];

extern struct coord var80068fec;

extern u32 g_TintedGlassEnabled;
extern s32 g_AlarmTimer; // counts upwards
extern u32 var80059fe0;
extern u32 var8005a0b0;
extern u32 var8005b4d0;
extern u32 var8005ce10;
extern u32 var8005ce2c;
extern u32 var8005ce48;
extern u32 var8005ce74;
extern u32 var8005ce8c;
extern u32 var8005ce90;
extern u32 var8005ce94;
extern u32 var8005ce9c;
extern u32 var8005cea8;
extern u32 __osViDevMgr;
extern u32 __osPiDevMgr;
extern u32 var8005cf30;
extern u32 var8005cf60;
extern u32 osViClock;
extern u32 var8005cf6c;
extern u32 __osGlobalIntMask;
extern u32 var8005cf84;
extern u32 var8005cf90;
extern u32 var8005cf94;
extern u32 var8005cf98;
extern u32 var8005cfc0;
extern u32 var8005cfe8;
extern u32 var8005d010;
extern u32 var8005d0d8;
extern u32 var8005d120;
extern u32 var8005d188;
extern u32 var8005d1f0;
extern u32 var8005d258;
extern u32 var8005d2e0;
extern u32 var8005d308;
extern u32 var8005d390;
extern u32 var8005d3b8;
extern u32 var8005d4c0;
extern u32 var8005d4e8;
extern s8 g_AudioIsThreadRunning;
extern u32 var8005d520;
extern struct rend_vidat var8005d530;
extern u32 var8005d588;
extern struct rend_vidat *var8005d590;
extern struct rend_vidat *g_ViData;
extern u32 var8005d59c;
extern u32 var8005d5b4;
extern u32 var8005d5b8;
extern u32 var8005d5bc;
extern u32 var8005d880;
extern u32 var8005d994;
extern u8 var8005d9a0;
extern s32 var8005d9c8;
extern u32 var8005d9cc;
extern s32 var8005d9d0;
extern s32 g_DoBootPakMenu;
extern Gfx var8005dcc8[];
extern Gfx var8005dcf0[];
extern u32 var8005dd1c;
extern s32 g_MainStageNum;
extern u32 var8005dd5c;
extern u32 var8005dd7c;
extern u32 var8005dda0;
extern u32 var8005dda8;
extern u32 var8005ddac;
extern u32 var8005ddb4;
extern u32 var8005ddb8;
extern u32 var8005ddc0;
extern u32 var8005ddc4;
extern u16 g_SfxVolume;
extern u32 g_SoundMode;
extern u32 var8005ddd4;
extern u32 var8005ddd8;
extern u32 var8005dde0;
extern u32 var8005edf0;
extern u32 var8005ee10;
extern u32 var8005ee14;
extern u32 var8005ee18;
extern struct contdata *g_ContDataPtr;
extern bool g_ContBusy;
extern u32 var8005ee68;
extern u32 g_ContBadReadsStickX[4];
extern u32 g_ContBadReadsStickY[4];
extern u32 g_ContBadReadsButtons[4];
extern u32 g_ContBadReadsButtonsPressed[4];
extern u8 g_ConnectedControllers;
extern bool g_ContInitDone;
extern bool g_ContNeedsInit;
extern u32 var8005eebc;
extern s32 g_ContNextPfsStateIndex;
extern u32 var8005eedc;
extern s32 var8005eee0;
extern s32 var8005eee4;
extern u32 var8005eee8;
extern u32 var8005ef08;
extern s32 var8005ef0c;
extern u32 var8005ef10;
extern s32 g_NumGlobalAilists;
extern s32 g_NumLvAilists;
extern u32 var8005ef40;
extern u32 var8005ef5c;
extern u32 var8005ef7c;
extern u32 var8005ef90;
extern u32 var8005efb4;
extern u32 var8005efb8;
extern void *var8005efc8;
extern u32 var8005efcc;
extern u32 var8005efd0;
extern u32 var8005efe0;
extern u32 var8005efec;
extern u32 var8005f008;
extern struct animheader *g_Anims;
extern u32 var8005f010;
extern u32 var8005f014;
extern u32 var8005f018;
extern u32 var8005f01c;
extern u16 *var8005f040;
extern u16 *var8005f044;
extern u32 var8005f048;
extern u32 var8005f0a8;
extern u32 var8005f108;
extern u32 var8005f548;
extern u32 var8005f6f8;
extern u32 var8005f6fc;
extern u32 var8005f710;
extern u32 var8005f7b0;
extern u32 var8005f8f0;
extern u32 var8005fa80;
extern u32 var80060004;
extern u32 var80060008;
extern u32 var8006000c;
extern u32 var80060014;
extern u32 var80060028;
extern u32 var8006005c;
extern u32 var80060070;
extern u32 var80060190;
extern u32 var800601b0;
extern u32 var80060340;
extern u32 var80060368;
extern u32 var800608b0;
extern u32 var80060910;
extern u32 var80060914;
extern u32 __osThreadTail;
extern u32 __osRunQueue;
extern u32 __osActiveQueue;
extern OSThread *__osRunningThread;
extern u32 var80060970;
extern u32 var800609a0;
extern u32 var800609c4;
extern u32 var800611f0;
extern u32 var80061240;
extern u32 var80061290;
extern u32 var800612e0;
extern u32 var80061330;
extern u32 var80061344;
extern u32 var80061360;
extern u32 var80061380;
extern u32 var800613a0;
extern struct var80061420 *var80061420;
extern u32 var80061424;
extern u32 var8006142c;
extern u16 **var80061430;
extern u32 var80061434;
extern u32 var80061438;
extern u32 var80061440;
extern u32 var80061444;
extern s32 var80061458;
extern u32 var80061460;
extern u32 var80061468;
extern s32 g_FootstepSounds[];
extern u16 var800615a0[][2];
extern f32 var80061630;
extern u32 var80061634;
extern u32 var80061640;
extern u32 var80061644;
extern u32 var80061648;
extern u32 var8006164c;
extern u32 var80061694;
extern u32 var800616dc;
extern u32 var800616e4;
extern u32 var800616e8;
extern u32 var80061710;
extern struct monitorscreen var80061a80;
extern struct monitorscreen var80061af4;
extern struct monitorscreen var80061b68;
extern f32 g_DoorScale;
extern u32 var80061bf0;

#endif
