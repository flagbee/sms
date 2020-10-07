#ifndef _SMSRTSP_SESSION_H
#define _SMSRTSP_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "smsDefine.h"

struct	_RTSPConfiguration {
	StreamingMode	mode;
	int		rtpClientPort;
	int		rtcpClientPort;
	int		rtpServerPort;	
	int		rtcpServerPort;
	int		rtpChannel;	// tcp
	int		rtcpChannel;	// tcp
	int		trackId;
	int		ttl;
	char	destAddrStr[IPADDR_STR_LEN];
};

void InitRTSPSessions(RtspServer *server);
RTSPSession* CreateRTSPSession(RtspServer *server, ClientConnection *clientConn);
int DestoryRTSPSession(RtspServer *server, const char *strId);
int SetupRTSPSession(RtspServer *server, ClientConnection *clientConn, const char *strId, RTSPConfiguration *config);
RTSPSession* LookUpRTSPSession(RtspServer *server, const char *strId);

int pushTCPDataToChannel(ClientConnection * conn, RTPPacketDesc *pRtpDesc, int channel, int *transState);
int DestoryTCPBuffer(ClientConnection *conn);

int pushTCPData(ClientConnection * conn, char *buf, unsigned int len);
int TerminateExpiredRTSPSessions(RtspServer *server);
int SetAllTerminationTimeForce(RtspServer *server);
int TerminateAllRTSPSessions(RtspServer *server);

#ifdef __cplusplus
}
#endif

#endif
