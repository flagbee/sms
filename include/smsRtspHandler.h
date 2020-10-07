#ifndef _SMSRTSP_HANDLER_H
#define _SMSRTSP_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned short random16(void);
unsigned int random32(void);

int handleCmd_GET_PARAMETER(RtspServer *server, struct _ClientConnection* conn);
int handleCmd_OPTIONS(struct _ClientConnection* conn);
int handleCmd_DESCRIBE(struct _ClientConnection* conn);
int handleCmd_SETUP(RtspServer *server, struct _ClientConnection* conn);
int handleCmd_TEARDOWN(RtspServer *server, struct _ClientConnection* conn);
int handleCmd_PLAY(RtspServer *server, struct _ClientConnection* conn);

#ifdef __cplusplus
}
#endif

#endif
