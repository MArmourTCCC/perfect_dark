#ifndef _IN_NET_H
#define _IN_NET_H

#include "types.h"
#include "constants.h"
#include "net/netbuf.h"

#define NET_PROTOCOL_VER 1

#define NET_MAX_CLIENTS MAX_PLAYERS
#define NET_MAX_NAME MAX_PLAYERNAME

#define NET_BUFSIZE 1440

#define NET_DEFAULT_PORT 27100

#define NETCHAN_DEFAULT 0
#define NETCHAN_COUNT 1

#define DISCONNECT_UNKNOWN  0
#define DISCONNECT_SHUTDOWN 1
#define DISCONNECT_VERSION 2
#define DISCONNECT_KICKED 3
#define DISCONNECT_BANNED 4
#define DISCONNECT_TIMEOUT 5
#define DISCONNECT_FULL 6

#define CLSTATE_DISCONNECTED 0
#define CLSTATE_CONNECTING 1
#define CLSTATE_AUTH 2
#define CLSTATE_LOBBY 3
#define CLSTATE_GAME 4

#define UCMD_FIRE (1 << 0)
#define UCMD_ACTIVATE (1 << 1)
#define UCMD_RELOAD (1 << 2)
#define UCMD_FIREMODE (1 << 3)
#define UCMD_AIMMODE (1 << 4)
#define UCMD_DUCK (1 << 5)
#define UCMD_SQUAT (1 << 6)
#define UCMD_ZOOMIN (1 << 7)
#define UCMD_SELECT (1 << 8)
#define UCMD_CHAT (1 << 19)
#define UCMD_FL_FORCEPOS (1 << 29)
#define UCMD_FL_FORCEANGLE (1 << 30)
#define UCMD_FL_FORCEGROUND (1 << 31)
#define UCMD_FL_FORCEMASK (UCMD_FL_FORCEPOS | UCMD_FL_FORCEANGLE | UCMD_FL_FORCEGROUND)

struct netplayermove {
	u32 tick; // g_NetTIck value when this struct was written; if 0, this struct is invalid
	u32 ucmd; // player commands (UCMD_)
	f32 leanofs; // analog lean value (-1 .. 1; equal to player->swaytarget / 75.f)
	f32 crouchofs; // analog crouch value (-90 for SQUAT, 0 for STAND; player->crouchofs)
	f32 movespeed[2]; // move inputs, [0] is forward, [1] is sideways; used mostly for animation
	f32 lookspeed[2]; // look inputs, [0] is theta, [1] is verta
	f32 angles[2]; // view angles, [0] is theta, [1] is verta
	struct coord pos; // player position at g_NetTick == tick
	s16 rooms[2]; // cam_room, floorroom
};

struct netclient {
	struct _ENetPeer *peer;
	u32 id; // remote client number, server is always 0, even on clients
	u32 state; // CLSTATE_
	u32 flags; // CLFLAG_

	struct {
		char name[NET_MAX_NAME];
		u8 headnum;
		u8 bodynum;
	} settings;

	struct mpplayerconfig *config;
	struct player *player;
	u8 playernum;

	struct netplayermove outmove[2]; // last 2 outgoing player inputs, newest one first
	struct netplayermove inmove[2]; // last 2 incoming player inputs, newest one first
	u32 inmovetick; // last inmove tick which was applied to the player
	u32 outmoveack; // last acked outmove tick
	u32 forcetick; // tick on which the client's position was forced, or 0 if not forcing
	u32 lerpticks; // how many ticks we've been lerping the position

	struct netbuf out; // outbound messages are written here, except broadcasts
	struct netbuf in; // incoming packets are fed here

	u8 out_data[NET_BUFSIZE]; // buffer for out
};

extern s32 g_NetMode;

// net frame, ticks at 60 fps, starts at 0 when the server is started
extern u32 g_NetTick;

extern u32 g_NetInterpTicks;
extern u32 g_NetExterpTicks;

extern s32 g_NetMaxClients;
extern s32 g_NetNumClients;
extern struct netclient g_NetClients[NET_MAX_CLIENTS + 1]; // last is an extra temporary client
extern struct netclient *g_NetLocalClient;

extern struct netbuf g_NetMsg;
extern struct netbuf g_NetMsgRel;

const char *netFormatClientAddr(const struct netclient *cl);

void netInit(void);
s32 netDisconnect(void);
void netStartFrame(void);
void netEndFrame(void);

s32 netStartServer(u16 port, s32 maxclients);
s32 netStartClient(const char *addr);

u32 netSend(struct netclient *dstcl, struct netbuf *buf, const s32 reliable);

void netServerStageStart(void);
void netServerStageEnd(void);

void netPlayersAllocate(void);

#endif // _IN_NET_H
