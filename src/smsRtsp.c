/*******select.c*********/
/*******Using select() for I / O multiplexing */
#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>   //cpu_set_t , CPU_SET
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <ctype.h> // for "isxdigit()
#include <errno.h>

#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspHandler.h"
#include "smsRtspStream.h"
#include "smsRtspSession.h"
#include "sms.h"

//#define DEBUG_SMS_RTSP
#if defined(DEBUG_SMS_RTSP)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

ClientList gClientList;
RtspServer gRtspServer[MAX_RTSP_SERVER_CNT];
pthread_mutex_t gRtspServerMutex = PTHREAD_MUTEX_INITIALIZER;

int gStreamId = 0;
int gVideoTrackId = 0;
int gAudioTrackId = 0;

static void InitClientList(ClientList *lptr)
{
	memset(lptr, 0x0, sizeof(ClientList));

	lptr->count = 0;
	lptr->head = NULL;
}

static void InsertClient(ClientList *lptr, ClientConnection *conn)
{
	ClientNodePtr new_nptr = NULL;

	new_nptr = (ClientNode*) malloc(sizeof(ClientNode));
	memset(new_nptr, 0x0, sizeof(ClientNode));
	memcpy(&new_nptr->conn, conn, sizeof(ClientConnection));

	if (lptr->count == 0) {
		new_nptr->next = lptr->head;
		lptr->head = new_nptr;
	}
	else {
		ClientNodePtr tmp = lptr->head;
		int i = 0;
		for (i = 1; i < lptr->count; i++) {
			tmp = tmp->next;
		}
		new_nptr->next = tmp->next;
		tmp->next = new_nptr;
	}
	lptr->count++;
}

static int DeleteClient(ClientList *lptr, int socketNum)
{
	int i = 0, rval = SMS_RET_ERR_INTERNAL;

	ClientNodePtr tmp = lptr->head, prev = lptr->head;

	for (i = 0; i < lptr->count; i++) {
		if (tmp->conn.clientSocket == socketNum) {
			if (i == 0) {
				lptr->head = tmp->next;
			}
			else {
				prev->next = tmp->next;
			}

			/* close RTP Sessions that use TCP buffer*/

			/* destory TCP buffer */
			DestoryTCPBuffer(&tmp->conn);

			/* free Connection */
			free(tmp);

			lptr->count--;
			rval = SMS_RET_OK;
			break;
		}
		else {
			prev = tmp;
			tmp = tmp->next;
		}
	}

	if (rval != SMS_RET_OK) {
		ERRMSG("socketNum[%d] not found!", socketNum);
	}

	return rval;
}

static ClientConnection* SearchClient(ClientList *lptr, int socketNum)
{
	ClientNodePtr tmp = lptr->head;
	ClientConnection* rConn = NULL;

	while (tmp != NULL) {
		if (tmp->conn.clientSocket == socketNum) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("socketNum[%d] not found!", socketNum);
	}
	else {
		rConn = &tmp->conn;
	}

	return rConn;
}

static ClientConnection* SearchClientBySessionId(ClientList *lptr, const char * sessionId)
{
	ClientNodePtr tmp = lptr->head;
	ClientConnection* rConn = NULL;

	if (sessionId == NULL) {
		ERRMSG("sessionId == NULL");
	}
	else {
		unsigned int len = 0;

		len = strlen(sessionId);

		while (tmp != NULL) {
			if (strncmp(tmp->conn.sessionIdStr, sessionId, len) == 0) {
				break;
			}
			tmp = tmp->next;
		}

		if (tmp == NULL) {
			ERRMSG("sessionId[%s] not found!", sessionId);
		}
		else {
			rConn = &tmp->conn;
		}
	}

	return rConn;
}

void PrintClientList(ClientList *lptr)
{
	ClientNodePtr tmp = lptr->head;

	DBGMSG("List value: ");
	while (tmp != NULL) {
		DBGMSG("[%d]", tmp->conn.clientSocket);
		tmp = tmp->next;
	}
	DBGMSG("Total: %d nodes", lptr->count);
}

static int parseRTSPReqCmd(struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL, i = 0;
	char resultCmdName[MAX_RTSP_CMD_LEN] = { 0, };
	RtspCmd RtspCmdTable[RTSP_CMD_MAX] = { { "OPTIONS", RTSP_CMD_OPTIONS }, { "DESCRIBE", RTSP_CMD_DESCRIBE }, { "SETUP", RTSP_CMD_SETUP }, { "GET_PARAMETER", RTSP_CMD_GET_PARAMETER }, { "SET_PARAMETER", RTSP_CMD_SET_PARAMETER }, { "TEARDOWN", RTSP_CMD_TEARDOWN }, { "PLAY", RTSP_CMD_PLAY }, { "PAUSE", RTSP_CMD_PAUSE }, };

	memset(resultCmdName, 0x0, sizeof(char) * RTSP_CMD_MAX);
	for (i = 0; i < MAX_RTSP_CMD_LEN - 1 && conn->parseIndex < conn->msgLen; conn->parseIndex++, i++) {
		char c = conn->pMsg[conn->parseIndex];

		if (c == ' ' || c == '\t') {
			break;
		}
		resultCmdName[i] = c;
	}
	resultCmdName[i] = '\0';

	DBGMSG("resultCmdName[%s]", resultCmdName);

	for (i = 0; i < RTSP_CMD_MAX; i++) {
		if (strcmp(RtspCmdTable[i].cmdstr, resultCmdName) == 0) {
			conn->rtspCmd = RtspCmdTable[i].cmdnum;
			rval = SMS_RET_OK;
			break;
		}
	}

	return rval;
}

static void decodeURL(char* url)
{
	// Replace (in place) any %<hex><hex> sequences with the appropriate 8-bit character.
	char* cursor = url;
	while (*cursor) {
		if ((cursor[0] == '%') && cursor[1] && isxdigit(cursor[1]) && cursor[2] && isxdigit(cursor[2])) {
			// We saw a % followed by 2 hex digits, so we copy the literal hex value into the URL, then advance the cursor past it:
			char hex[3];
			hex[0] = cursor[1];
			hex[1] = cursor[2];
			hex[2] = '\0';
			*url++ = (char) strtol(hex, NULL, 16);
			cursor += 3;
		}
		else {
			// Common case: This is a normal character or a bogus % expression, so just copy it
			*url++ = *cursor++;
		}
	}

	*url = '\0';
}

static int chekRTSPReqUrl(struct _ClientConnection* conn)
{
	int k = 0, rval = SMS_RET_ERR_INTERNAL;

	for (k = conn->parseIndex + 1; (int) k < (int) (conn->msgLen - 5); ++k) {
		if (conn->pMsg[k] == 'R' && conn->pMsg[k + 1] == 'T' && conn->pMsg[k + 2] == 'S' && conn->pMsg[k + 3] == 'P' && conn->pMsg[k + 4] == '/') {
			while (--k >= conn->parseIndex && conn->pMsg[k] == ' ') {
				;
			}
			int k1 = k;
			while (k1 > conn->parseIndex && (conn->pMsg[k1] != '/' || (conn->pMsg[k1] == '/' && conn->pMsg[k1 + 1] == ' '))) {
				--k1;
			}

			// ASSERT: At this point
			//   i: first space or slash after "host" or "host:port"
			//   k: last non-space before "RTSP/"
			//   k1: last slash in the range [i,k]

			// The URL suffix comes from [k1+1,k]
			// Copy "resultURLSuffix":
			int n = 0, k2 = 0;

			k2 = k1 + 1;
			if (k2 <= k) {
				if (k - k1 + 1 > MAX_RTSP_URL_SUFFIX_LEN) {
					return SMS_RET_ERR_INTERNAL; // there's no room
				}
				while (k2 <= k) {
					if (conn->pMsg[k2] != '/') {
						conn->urlSuffix[n++] = conn->pMsg[k2++];
					}
					else {
						k2++;
					}
				}
			}
			conn->urlSuffix[n] = '\0';

			// The URL 'pre-suffix' comes from [i+1,k1-1]
			// Copy "resultURLPreSuffix":
			n = 0;
			k2 = conn->parseIndex + 1;
			if (k2 + 1 <= k1) {
				if (k1 - conn->parseIndex > MAX_RTSP_URL_PRE_SUFFIX_LEN) {
					return SMS_RET_ERR_INTERNAL; // there's no room
				}
				while (k2 <= k1 - 1) {
					conn->urlPreSuffix[n++] = conn->pMsg[k2++];
				}
			}
			conn->urlPreSuffix[n] = '\0';
			decodeURL(conn->urlPreSuffix);

			conn->parseIndex = k + 7; // to go past " RTSP/"
			rval = SMS_RET_OK;
			DBGMSG("urlPreSuffix[%s]", conn->urlPreSuffix);
			DBGMSG("urlSuffix[%s]", conn->urlSuffix);
			break;
		}
	}

	return rval;
}

static int parseCSeq(struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL, j = 0;
	char resultCSeq[MAX_RTSP_CSEQ_LEN] = { 0, };

	for (j = conn->parseIndex; j < (conn->msgLen - 5); ++j) {
		if (strncasecmp("CSeq:", &conn->pMsg[j], 5) == 0) {
			j += 5;
			while (j < conn->msgLen && (conn->pMsg[j] == ' ' || conn->pMsg[j] == '\t')) {
				++j;
			}
			int n = 0;
			for (n = 0; n < MAX_RTSP_CSEQ_LEN - 1 && j < conn->msgLen; ++n, ++j) {
				char c = conn->pMsg[j];
				if (c == '\r' || c == '\n') {
					rval = SMS_RET_OK;
					break;
				}
				resultCSeq[n] = c;
			}
			resultCSeq[n] = '\0';
			DBGMSG("resultCSeq[%s]", resultCSeq);
			conn->CSeq = atoi(resultCSeq);
			break;
		}
	}

	return rval;
}

static int parseSessionIdStr(struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL, j = 0;

	conn->sessionIdStr[0] = '\0'; // default value (empty string)
	for (j = conn->parseIndex; j < conn->msgLen - 8; ++j) {
		if (strncasecmp("Session:", &conn->pMsg[j], 8) == 0) {
			j += 8;
			while (j < conn->msgLen && (conn->pMsg[j] == ' ' || conn->pMsg[j] == '\t')) {
				++j;
			}
			int n = 0;
			for (n = 0; n < MAX_RTSP_SESSION_ID_LEN - 1 && j < conn->msgLen; ++n, ++j) {
				char c = conn->pMsg[j];
				if (c == '\r' || c == '\n') {
					break;
				}
				conn->sessionIdStr[n] = c;
			}
			conn->sessionIdStr[n] = '\0';
			DBGMSG("sessionIdStr[%s]", conn->sessionIdStr);
			break;
		}
	}

	return rval;
}

static int parseContentLength(struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL, j = 0, num = 0;
	unsigned int contentLength = 0;

	contentLength = 0; // default value
	for (j = conn->parseIndex; (int) j < (int) (conn->msgLen - 15); ++j) {
		if (strncasecmp("Content-Length:", &(conn->pMsg[j]), 15) == 0) {
			j += 15;
			while (j < conn->msgLen && (conn->pMsg[j] == ' ' || conn->pMsg[j] == '\t')) {
				++j;
			}

			if (sscanf(&conn->pMsg[j], "%u", &num) == 1) {
				contentLength = num;
			}
		}
	}

	return rval;
}

int FindCompleteRTSPMessage(ClientConnection* conn)
{
	int rval = 0;
	char* tmpPtr = NULL;
	unsigned int interleavedLen = 0;

	SK_ASSERT(conn);

	// Look for the end of the message: <CR><LF><CR><LF>
	tmpPtr = conn->pRemain;
	conn->pMsg = NULL;
	conn->msgLen = 0;

	while (conn->msgLen < conn->remainLen) {
		if (tmpPtr[conn->msgLen] == '$') {
			if (conn->msgLen + 3 < conn->remainLen) {
				interleavedLen |= tmpPtr[conn->msgLen + 2] << 8;
				interleavedLen |= tmpPtr[conn->msgLen + 3];
				if (conn->msgLen + 3 + interleavedLen <= conn->remainLen) {
					conn->pMsg = &tmpPtr[conn->msgLen];
					conn->pRemain = &tmpPtr[conn->msgLen + 3 + interleavedLen];
					conn->msgLen = 4 + interleavedLen;
					conn->msgType = MSG_TYPE_INTERLEAVED;
				}
			}
			break;
		}
		else {
			if (tmpPtr[conn->msgLen] == '\r' && tmpPtr[conn->msgLen + 1] == '\n' && tmpPtr[conn->msgLen + 2] == '\r' && tmpPtr[conn->msgLen + 3] == '\n') {
				conn->pMsg = conn->pRemain;
				conn->pRemain = &tmpPtr[conn->msgLen];
				conn->msgType = MSG_TYPE_RTSP_REQUEST;
			}
			conn->msgLen++;
		}
	}

	if (conn->pMsg != NULL) {
		conn->remainLen = conn->remainLen - conn->msgLen;
		rval = SMS_RET_OK;
	}
	else {
		/* End of message not found */
		rval = SMS_RET_ERR_INCOMPLIETE_REQUEST;
	}

#if 0 //[[ baek_0160414_BEGIN -- del
	DBGMSG("REMAIN[%d]===================", conn->remainLen );
	DBGMSG("%s", conn->pRemain );
	DBGMSG("=============================");
#endif //]] baek_0160414_END -- del

	return rval;
}

#define INTLVD_MSG_ID_IDX	(0)
#define INTLVD_MSG_CH_IDX	(1)
#define INTLVD_MSG_LEN_IDX	(2)

static rtcpMessageType getRtcpPacketType(const unsigned char TypeNum)
{
	rtcpMessageType Type;

	switch (TypeNum) {
	case 200:
		Type = RTCP_MSG_TYPE_SR;
		break;
	case 201:
		Type = RTCP_MSG_TYPE_RR;
		break;
	case 202:
		Type = RTCP_MSG_TYPE_SDES;
		break;
	case 203:
		Type = RTCP_MSG_TYPE_BYE;
		break;
	case 204:
		Type = RTCP_MSG_TYPE_APP;
		break;
	default:
		Type = RTCP_MSG_TYPE_UNKNOWN;
		break;
	}

	return Type;
}

static int KeepAlive(RtspServer* server, char *pSessionIdStr)
{
	RTSPSession* session = NULL;
	int rval = SMS_RET_ERR_INTERNAL;

	SK_ASSERT(server);
	SK_ASSERT(pSessionIdStr);

	session = LookUpRTSPSession(server, pSessionIdStr);
	if (session) {
		if (session->handleRenewTermTime) {
			session->handleRenewTermTime(session);
		}
		rval = SMS_RET_OK;
	}
	else {
		ERRMSG("LookUpRTSPSession failed. pSessionIdStr(%s)", pSessionIdStr);
		rval = SMS_RET_ERR_INTERNAL;
	}

	return rval;
}

static int KeepAliveBySsrc(RtspServer* server, unsigned int ssrc)
{
	int rval = SMS_RET_ERR_INTERNAL, done = 0;
	StreamNodePtr pStream = NULL;
	TrackNodePtr pTrack = NULL;
	RTPSessionList *pRtpList = NULL;
	RTPSessionNodePtr pRtp = NULL;

	SK_ASSERT(server);

	do {
		pStream = NextStreamNode(&server->streamList, pStream);

		if (pStream) {
			do {
				pTrack = NextTrackNode(&pStream->stream.trackList, pTrack);
				if (pTrack) {
					pRtpList = &pTrack->track.rtpSessionList;
					LOCK_MTX(&pRtpList->rtpSessionListMutex);
					do {
						pRtp = NextRTPSessionNode(&pTrack->track.rtpSessionList, pRtp);
						if (pRtp) {
							if (pRtp->session.ssrc == ssrc) {
								DBGMSG("pRtp->session.state(%d) pRtp->session.rtspSession(%p)", pRtp->session.state, pRtp->session.rtspSession);
								if (pRtp->session.rtspSession != NULL) {
									KeepAlive(server, pRtp->session.rtspSession->strId);
									done = 1;
									rval = SMS_RET_OK;
									break;
								}
								else {
									ERRMSG("pRtp->session.rtspSession == NULL");
								}
							}
						}
					} while (pRtp != NULL);
					UNLOCK_MTX(&pRtpList->rtpSessionListMutex);
					if (done) {
						break;
					}
				}
			} while (pTrack != NULL);

			if (done) {
				break;
			}
		}
	} while (pStream != NULL);

	if (done == 0) {
		ERRMSG("ssrc(%08X) not found", ssrc);
		rval = SMS_RET_ERROR;
	}

	return rval;
}

static int handleRTCPMessage(RtspServer *server, char *pMsg, unsigned int MsgLen)
{
	int rval = 0;
	unsigned int ssrc = 0;
	const char MinRtcpMsgBodyLen = 8;
	rtcpMessageType	rtcpType;

	if (MinRtcpMsgBodyLen > MsgLen || pMsg == NULL) {
		ERRMSG("MinRtcpMsgBodyLen(%d) > MsgLen(%d) || pConn->pRtcpMsg(%p) == NULL ", MinRtcpMsgBodyLen, MsgLen, pMsg);
		rval = SMS_RET_ERROR;
	}
	else {
		rtcpType = getRtcpPacketType(pMsg[1]); 
		switch (rtcpType) {
		case RTCP_MSG_TYPE_RR:
			ssrc = (pMsg[8] << 24) | (pMsg[9] << 16) | (pMsg[10] << 8) | pMsg[11];
			DBGMSG("RTCP_MSG_TYPE_RR ssrc(%08X)", ssrc);
			rval = KeepAliveBySsrc(server, ssrc);
			if (SMS_RET_OK != rval) {
				ERRMSG("KeepAliveBySsrc failed. rval(%d)", rval);
			}
			break;
		default:
			DBGMSG("UKNOWN RTCP PACKET TYPE(%d)", rtcpType);
			rval = SMS_RET_ERROR;
			break;
		}
	}

	return rval;
}

static int parseInterleavedMessage(ClientConnection* pConn)
{
	int rval = 0;
	char *pRtcpMsgHead = NULL, *pRtcpMsgBody = NULL;
	short RtcpLen = 0, RtcpMsgBodyLen = 0;
	const char InterleavedMsgHeadLen = 4;
	const char RtcpMsgHeadLen = 4;

	SK_ASSERT(pConn);

	/* TCP interleaved
	 0(1) : 0x24
	 1(1) : Channel
	 2(2) : Length
	 4...N(Length) : RTCP Message 
	 */

	/* RTCP
	 0(1) : 0x81
	 1(1) : Packet type
	 2(2) : length
	 4...N(Length * 5 - HeaderLen) : RTCP Message body
	 */

	if (InterleavedMsgHeadLen >= pConn->msgLen || pConn->pMsg == NULL) {
		ERRMSG("InterleavedMsgHeadLen(%d) >= pConn->msgLen(%d) || pConn->pMsg(%p) == NULL", InterleavedMsgHeadLen, pConn->msgLen, pConn->pMsg);
		rval = SMS_RET_ERROR;
	}
	else {
		RtcpLen = pConn->pMsg[2] << 8 | pConn->pMsg[3];
		pRtcpMsgHead = &pConn->pMsg[4];
		if (RtcpMsgHeadLen >= RtcpLen || pRtcpMsgHead == NULL) {
			ERRMSG("RtcpMsgHeadLen(%d) >= RtcpLen(%d) || pRtcpMsgHead(%p) == NULL ", RtcpMsgHeadLen, RtcpLen, pRtcpMsgHead);
			rval = SMS_RET_ERROR;
		}
		else {
			pConn->rtcpType = getRtcpPacketType(pRtcpMsgHead[1]);
			pConn->rtcpMsgLen = ((pRtcpMsgHead[2] << 8) | pRtcpMsgHead[3]) * 5 - RtcpMsgHeadLen;
			pConn->pRtcpMsg = &pRtcpMsgHead[0];
			DBGMSG("RTCP message(%d) len(%d) received", pConn->rtcpType, pConn->rtcpMsgLen);
			rval = SMS_RET_OK;
		}
	}

	return rval;
}

static int parseRTSPRequestMessage(ClientConnection* conn)
{
	// This parser is currently rather dumb; it should be made smarter #####
	// "Be liberal in what you accept": Skip over any whitespace at the start of the request:
	int iBuf = 0;
	int rval = 0;

#if 0 //[[ baek_0160414_BEGIN -- del
	DBGMSG("MSG[%d]===================", conn->msgLen);
	DBGMSG("%s", conn->pMsg );
	DBGMSG("==========================");
#endif //]] baek_0160414_END -- del

	/* Init index */
	conn->parseIndex = 0;

	/* Find start ponint */
	for (iBuf = 0; iBuf < conn->msgLen; iBuf++) {
		char c = conn->pMsg[iBuf];
		if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0')) {
			conn->pMsg = &conn->pMsg[iBuf];
			conn->msgLen = conn->msgLen - iBuf;
			break;
		}
	}

	if (iBuf == conn->msgLen) {
		return SMS_RET_ERR_INTERNAL; // The request consisted of nothing but whitespace!
	}

	// Then read everything up to the next space (or tab) as the command name:
	rval = parseRTSPReqCmd(conn);
	if (rval != SMS_RET_OK) {
		return rval;
	}

	// Skip over the prefix of any "rtsp://" or "rtsp:/" URL that follows:
	int j = conn->parseIndex + 1;
	while (j < conn->msgLen && (conn->pMsg[j] == ' ' || conn->pMsg[j] == '\t')) {
		j++; // skip over any additional white space
	}

	for (; j < (conn->msgLen - 8); ++j) {
		if ((conn->pMsg[j] == 'r' || conn->pMsg[j] == 'R') && (conn->pMsg[j + 1] == 't' || conn->pMsg[j + 1] == 'T') && (conn->pMsg[j + 2] == 's' || conn->pMsg[j + 2] == 'S') && (conn->pMsg[j + 3] == 'p' || conn->pMsg[j + 3] == 'P') && conn->pMsg[j + 4] == ':' && conn->pMsg[j + 5] == '/') {
			j += 6;
			if (conn->pMsg[j] == '/') {
				// This is a "rtsp://" URL; skip over the host:port part that follows:
				++j;
				while (j < conn->msgLen && conn->pMsg[j] != '/' && conn->pMsg[j] != ' ') {
					++j;
				}
			}
			else {
				// This is a "rtsp:/" URL; back up to the "/":
				--j;
			}
			conn->parseIndex = j;
			break;
		}
	}

	// Look for the URL suffix (before the following "RTSP/"):
	rval = chekRTSPReqUrl(conn);
	if (rval != SMS_RET_OK) {
		return rval;
	}

	// Look for "CSeq:" (mandatory, case insensitive), skip whitespace,
	// then read everything up to the next \r or \n as 'CSeq':
	rval = parseCSeq(conn);
	if (rval != SMS_RET_OK) {
		return rval;
	}

	// Look for "Session:" (optional, case insensitive), skip whitespace,
	// then read everything up to the next \r or \n as 'Session':
	parseSessionIdStr(conn);

	parseContentLength(conn);
	// Also: Look for "Content-Length:" (optional, case insensitive)
	return SMS_RET_OK;
}

int RtspConnHandlerFunc(RtspServer* server, struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;

	if (server == NULL || conn == NULL) {
		ERRMSG("server == NULL || conn == NULL");
	}
	else {
		char SessionIdIncluded = 0;

		/* Clear send buffer */
		conn->sendBufLen = 0;
		memset(conn->sendBuf, 0x0, sizeof(char) * RTSP_CONNECTION_BUFFER_LEN);

		/* set remaining buffer */
		conn->pRemain = conn->clientBuf;
		conn->remainLen = conn->clientBufLen;

		do {
			/* Find Complete Message */
			rval = FindCompleteRTSPMessage(conn);
			if (rval != SMS_RET_OK) {
				break;
			}

			if (conn->msgType == MSG_TYPE_RTSP_REQUEST) {
				DBGMSG("received MSG_TYPE_RTSP_REQUEST");
				/* Parse */
				rval = parseRTSPRequestMessage(conn);
				if (rval != SMS_RET_OK) {
					ERRMSG("rval[%d] != SMS_RET_OK", rval);
				}
				else {
					/* set flags */
					if (conn->sessionIdStr[0] != 0) {
						SessionIdIncluded = 1;
					}

					if (SessionIdIncluded) {
						KeepAlive(server, conn->sessionIdStr);
					}

					switch (conn->rtspCmd) {
					case RTSP_CMD_OPTIONS:
						rval = handleCmd_OPTIONS(conn);
						break;
					case RTSP_CMD_DESCRIBE:
						rval = handleCmd_DESCRIBE(conn);
						break;
					case RTSP_CMD_GET_PARAMETER:
						rval = handleCmd_GET_PARAMETER(server, conn);
						break;
					case RTSP_CMD_SET_PARAMETER:
						break;
					case RTSP_CMD_TEARDOWN:
						rval = handleCmd_TEARDOWN(server, conn);
						break;
					case RTSP_CMD_PLAY:
						rval = handleCmd_PLAY(server, conn);
						break;
					case RTSP_CMD_PAUSE:
						break;
					case RTSP_CMD_SETUP:
						rval = handleCmd_SETUP(server, conn);
						break;
					default:
						ERRMSG("unknown RTSP request [%d]", conn->rtspCmd)
						;
						break;
					}

					if (conn->sendBufLen > 0) {
						if (conn->sendTCPBuf) {
							pushTCPData(conn, conn->sendBuf, conn->sendBufLen);
						}
						else {
							send(conn->clientSocket, conn->sendBuf, conn->sendBufLen, 0);
						}
						DBGMSG("conn->sendBuf[%s]", conn->sendBuf);
					}
				}
			}
			else if (conn->msgType == MSG_TYPE_INTERLEAVED) {
#if 0 //[[ baek_0160414_BEGIN -- del
				DBGMSG("received MSG_TYPE_INTERLEAVED");
				DBGMSG("MSG[%d]===================", conn->msgLen);
				DBGMSG("%s", conn->pMsg);
				DBGMSG("==========================");
#endif //]] baek_0160414_END -- del
				rval = parseInterleavedMessage(conn);
				if (rval != SMS_RET_OK) {
					ERRMSG("rval(%d) != SMS_RET_OK", rval);
				}

				handleRTCPMessage(server, conn->pRtcpMsg, conn->rtcpMsgLen);
			}
			else {
				ERRMSG("conn->msgType[%d] error", conn->msgType);
			}
		} while (1);

		if (conn->remainLen > 0) {
			/* unused data remain */
			//DBGMSG("unused data remain(%d)[%s]", conn->remainLen, conn->pRemain);
			memcpy(conn->clientBuf, conn->pRemain, conn->remainLen);
			memset(conn->clientBuf + conn->remainLen, 0x0, sizeof(char) * RTSP_CONNECTION_BUFFER_LEN - conn->remainLen);
			conn->clientBufLen = conn->remainLen;
		}
		else {
			conn->clientBufLen = 0;
			memset(conn->clientBuf, 0x0, sizeof(char) * RTSP_CONNECTION_BUFFER_LEN);
		}
	}

	return rval;
}

int CreateClientConnection(ClientList *CList, int fd, struct sockaddr_in *clientaddr)
{
	int rval = 0, optval = 0;
	ClientConnection newConn;
	socklen_t optlen;
	struct timeval timeout;

	/* add */
	memset(&newConn, 0x0, sizeof(ClientConnection));
	memcpy(&newConn.clientAddr, clientaddr, sizeof(struct sockaddr_in));
	newConn.clientSocket = fd;
	newConn.handler = (clientHandlerFunc) RtspConnHandlerFunc;
	pthread_mutex_init(&newConn.tcpSenderMutex, NULL);

	/* set send timeout */
	timeout.tv_sec = 0;
	timeout.tv_usec = 300000;
	rval = setsockopt(newConn.clientSocket, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof(timeout));
	if (rval == -1) {
		DBGMSG("Error setsockopt");
	}

	InsertClient(CList, &newConn);
	PrintClientList(CList);

	/* change socket option */

	return rval;
}

int DestoryClientConnection(ClientList *CList, int fd)
{
	int rval = 0;

	DeleteClient(CList, fd);
	PrintClientList(CList);

	return rval;
}

// core_id = 0, 1, ... n-1, where n is the system's number of cores
static int stick_this_thread_to_core_normal(int core_id, const char *pName) 
{
   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   int policy;
   struct sched_param param;

   if (core_id < 0 || core_id >= num_cores)
      return EINVAL;

   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);

   pthread_t current_thread = pthread_self();    
   pthread_setname_np(current_thread, pName);
   
   pthread_getschedparam(pthread_self(), &policy, &param);
   policy = SCHED_OTHER;
   param.sched_priority = sched_get_priority_max(SCHED_OTHER);
   pthread_setschedparam(pthread_self(), policy, &param);
      
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void RtcpMessageHandler(void *data)
{
	RtspServer *server = (RtspServer *) data;
	struct timeval RecvTimeout;
	struct sockaddr_in srv, cli_addr;		// srv used by bind()
	socklen_t clilen = sizeof(cli_addr);
	char buf[2048];
	int RetSelect = 0;
	int recvSocket = 0;
	int fdmax = 0, nbytes=0;
	fd_set	fds;
	
	//baek.debug
//	stick_this_thread_to_core_normal(0, "RtcpHandle");
	
	FD_ZERO(&fds);
	fdmax = 0;	
	
	while (server->rtcpHandlerRunning) {
		
		RecvTimeout.tv_sec = 1;
		RecvTimeout.tv_usec = 0;
		RetSelect = select(fdmax + 1, &fds, NULL, NULL, &RecvTimeout);
		if (RetSelect == -1) {
			ERRMSG("Server-select() error lol!");
			return;
		}
		else{
			if (RetSelect != 0) {
				/*run through the existing connections looking for data to be read*/
				for (recvSocket = 0; recvSocket <= fdmax; recvSocket++) {
					if (FD_ISSET(recvSocket, &fds)) {
						nbytes = recvfrom(recvSocket, buf, 2048, 0, (struct sockaddr*)&cli_addr, &clilen);
						if (nbytes < 0){
							ERRMSG("ERROR in recvfrom()");
							close(recvSocket);
							FD_CLR(recvSocket, &fds);
						}
						
						if(nbytes > 0){
							DBGMSG("RECV RTCP Message Size(%d) recvSocket(%d) from %s(%d)", nbytes, recvSocket, inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
							handleRTCPMessage(server, buf, nbytes);
						}
					}
				}
			}
		}
		
		LOCK_MTX(&server->rtcpHandlerMutex);		
		fds = server->rtcpFds;
		fdmax = server->rtcpFdMax;		
		UNLOCK_MTX(&server->rtcpHandlerMutex);		
	}
}

void RtspFunc(void *data)
{
	RtspServer *server = (RtspServer *) data;
	struct sockaddr_in clientaddr;
	int fdmax;
	int newfd;
	char buf[2048];
	int nbytes;
	int addrlen = 0;
	int recvSocket = 0, j;
	fd_set master;
	fd_set read_fds;
	struct timeval RecvTimeout;
	int RetSelect = 0;

	/* clear the master and temp sets */
	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	/* add the listener to the master set */
	FD_SET(server->ServerSocket, &master);

	/* keep track of the biggest file descriptor */
	fdmax = server->ServerSocket; /* so far, it's this one*/
	
	stick_this_thread_to_core_normal(0, "RtspFunc");

	while (server->rtspRunning) {

		/* copy it */
		read_fds = master;

		RecvTimeout.tv_sec = 1;
		RecvTimeout.tv_usec = 0;
		RetSelect = select(fdmax + 1, &read_fds, NULL, NULL, &RecvTimeout);
		if (RetSelect == -1) {
			ERRMSG("Server-select() error lol!");
			return;
		}
		else {

			/* Check expired rtsp sessions */
			TerminateExpiredRTSPSessions(server);

			if (RetSelect != 0) {
				/*run through the existing connections looking for data to be read*/
				for (recvSocket = 0; recvSocket <= fdmax; recvSocket++) {
					if (FD_ISSET(recvSocket, &read_fds)) {
						if (recvSocket == server->ServerSocket) {
							/* handle new connections */
							addrlen = sizeof(clientaddr);
							if ((newfd = accept(server->ServerSocket, (struct sockaddr *) &clientaddr, &addrlen)) == -1) {
								ERRMSG("Server-accept() error lol!");
							}
							else {
								FD_SET(newfd, &master); /* add to master set */
								if (newfd > fdmax) { /* keep track of the maximum */
									fdmax = newfd;
								}

								//Limit only one client
								//SetAllTerminationTimeForce(server);

								/* Create new Client connection */
								CreateClientConnection(&server->clientList, newfd, &clientaddr);

								DBGMSG("New connection from %s on socket %d", inet_ntoa(clientaddr.sin_addr), newfd);
							}
						}
						else {
							ClientConnection *pConn = NULL;

							/* Find RTSP pConnection */
							pConn = SearchClient(&server->clientList, recvSocket);
							if (pConn) {
								if ((nbytes = recv(recvSocket, pConn->clientBuf + pConn->clientBufLen, RTSP_CONNECTION_BUFFER_LEN - pConn->clientBufLen, 0)) <= 0) {
									if (nbytes == 0) {
										/* connection closed */
										DBGMSG("socket %d hung up", recvSocket);
									}
									else {
										ERRMSG("recv() error lol!");
									}

									DestoryClientConnection(&server->clientList, recvSocket);
									close(recvSocket);
									FD_CLR(recvSocket, &master);
								}
								else {
									/* set total buffer length */
									pConn->clientBufLen += nbytes;

									/* run handler */
									pConn->handler(server, pConn);
								}
							}
							else {
								DestoryClientConnection(&server->clientList, recvSocket);
								close(recvSocket);
								FD_CLR(recvSocket, &master);
								ERRMSG("SearchClient fail recvSocket[%d]", recvSocket);
							}
						}
					}
				}
			}
		}
	}

	TerminateAllRTSPSessions(server);

	return;
}

int smsRTSPServerInit(int port, const char *serverName)
{
	int rval = -1, serverid = 0;
	int yes = 1;
	RtspServer *newServer = NULL;
	struct sockaddr_in serveraddr;

	LOCK_MTX(&gRtspServerMutex);

	serverid = 0;
	newServer = &gRtspServer[serverid];

	SK_ASSERT(serverName);

	if (newServer->initdone == 0) {
		/* initialize RTSP Server */
		memset(newServer, 0x0, sizeof(RtspServer));
		newServer->ServerPort = port;
		snprintf(newServer->name, MAX_RTSP_URL_PRE_SUFFIX_LEN - 1, "/%s", serverName);

		/* initialize RTSP Client List */
		InitClientList(&newServer->clientList);

		/* get the listener */
		if ((newServer->ServerSocket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			ERRMSG("Server-socket() error lol!");
			rval = SMS_RET_ERR_INTERNAL;
		}
		else {
			/*"address already in use" error message */
			if (setsockopt(newServer->ServerSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
				ERRMSG("Server-setsockopt() error lol!");
				rval = SMS_RET_ERR_INTERNAL;
			}
			else {
				/* bind */
				serveraddr.sin_family = AF_INET;
				serveraddr.sin_addr.s_addr = INADDR_ANY;
				serveraddr.sin_port = htons(port);
				memset(&(serveraddr.sin_zero), '\0', 8);

				if (bind(newServer->ServerSocket, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1) {
					ERRMSG("Server-bind() error lol!");
					rval = SMS_RET_ERR_INTERNAL;
				}
				else {
					/* listen */
					if (listen(newServer->ServerSocket, 10) == -1) {
						ERRMSG("Server-listen() error lol!");
						rval = SMS_RET_ERR_INTERNAL;
					}
					else {
						InitRTSPSessions(newServer);

						newServer->initdone = 1;

						rval = serverid;
					}
				}
			}
		}
	}
	else {
		ERRMSG("RTSP Server is already Initialized!");
		rval = SMS_RET_ERR_INTERNAL;
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

int smsAddRTSPStream(int serverid, const char *streamName)
{
	int streamId = 0, rval = -1;
	RtspServer *server = NULL;

	SK_ASSERT(serverid < MAX_RTSP_SERVER_CNT);

	LOCK_MTX(&gRtspServerMutex);

	server = &gRtspServer[serverid];

	if (server->initdone) {
		streamId = CreateStream(server, streamName);
	}
	else {
		ERRMSG("RTSP Server is not initialized!");
	}

	if (streamId > 0) {
		rval = streamId;
	}
	else {
		ERRMSG("CreateStream fail serverid[%d] streamName[%s]", serverid, streamName);
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

int smsAddRTSPTrack(int streamId, const char *trackName, int trackType)
{
	int trackId = 0, i = 0, rval = -1;
	RtspServer *server = NULL;

	LOCK_MTX(&gRtspServerMutex);

	for (i = 0; i < MAX_RTSP_SERVER_CNT; i++) {
		server = &gRtspServer[i];
		if (server->initdone) {
			trackId = AddTrack(server, streamId, trackName, trackType);
		}
	}

	if (trackId > 0) {
		rval = trackId;
	}
	else {
		ERRMSG("AddTrack fail streamid[%d] trackName[%s] type[%d]", streamId, trackName, trackType);
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

int smsRemoveRTSPTrack(int streamId, int trackId)
{
	int i = 0, rval = -1;	
	RtspServer *server = NULL;
	
	LOCK_MTX(&gRtspServerMutex);
	
	for (i = 0; i < MAX_RTSP_SERVER_CNT; i++) {
		server = &gRtspServer[i];
		if (server->initdone) {
			rval = RemoveTrack(server, streamId, trackId);
			if( rval != SMS_RET_OK ){
				
			}
		}
	}
	
	UNLOCK_MTX(&gRtspServerMutex);
	
	return rval;	
}

int smsStartRTSPServer(int serverid)
{
	int rval = SMS_RET_ERR_INTERNAL;
	RtspServer *server;

	SK_ASSERT(serverid < MAX_RTSP_SERVER_CNT);

	LOCK_MTX(&gRtspServerMutex);

	server = &gRtspServer[serverid];

	if (server->initdone && server->rtspRunning == 0) {

		PrintStreams(server);

		/* Start RTSP thread */
		server->rtspRunning = 1;
		if (pthread_create(&server->rtspThreadId, NULL, (void *) RtspFunc, (void*) server) != 0) {
			ERRMSG("%s pthread_create error", __FUNCTION__);
		}
		
		/* Start RTCP thread */
		pthread_mutex_init(&server->rtcpHandlerMutex, NULL);
		server->rtcpHandlerRunning = 1;
		FD_ZERO(&server->rtcpFds);
		server->rtcpFdMax = 0;
		if (pthread_create(&server->rtcpHandlerThread, NULL, (void *) RtcpMessageHandler, (void*) server) != 0) {
			ERRMSG("%s pthread_create error", __FUNCTION__);
		}
		
		rval = SMS_RET_OK;
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

int smsStopRTSPServer(int serverid)
{
	int rval = SMS_RET_ERR_INTERNAL;
	RtspServer *server;

	SK_ASSERT(serverid < MAX_RTSP_SERVER_CNT);

	LOCK_MTX(&gRtspServerMutex);

	server = &gRtspServer[serverid];

	if (server->initdone && server->rtspRunning) {
		server->rtspRunning = 0;
		pthread_join(server->rtspThreadId, NULL);
		server->rtspThreadId = 0;
		
		server->rtcpHandlerRunning = 0;
		pthread_join(server->rtcpHandlerThread, NULL);
		server->rtcpHandlerThread = 0;
		rval = SMS_RET_OK;
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

char* rtspURLPrefix(int clientSocket, char *buf)
{
	struct sockaddr_in ourAddress;
	int portNumHostOrder = 0;

	SK_ASSERT(buf);
	SK_ASSERT(clientSocket >= 0);

	socklen_t namelen = sizeof(struct sockaddr_in);
	getsockname(clientSocket, (struct sockaddr*) &ourAddress, &namelen);

	//portNumHostOrder = ourAddress.sin_port;
	portNumHostOrder = gRtspServer[0].ServerPort;

	if (portNumHostOrder == 554 /* the default port number */) {
		snprintf(buf, COMMON_STR_LEN - 1, "rtsp://%s/", inet_ntoa(ourAddress.sin_addr));
	}
	else {
		snprintf(buf, COMMON_STR_LEN - 1, "rtsp://%s:%hu/", inet_ntoa(ourAddress.sin_addr), portNumHostOrder);
	}

	return buf;
}

char* rtspStreamURL(Stream *stream, int clientSocket, char* buf)
{
	char urlPrefix[COMMON_STR_LEN];

	SK_ASSERT(stream);
	SK_ASSERT(buf);

	memset(urlPrefix, 0x0, sizeof(char) * COMMON_STR_LEN);
	rtspURLPrefix(clientSocket, urlPrefix);
	snprintf(buf, COMMON_STR_LEN - 1, "%s%s", urlPrefix, stream->name);

	return buf;
}

char* rtspTrackURL(Stream *stream, Track *track, int clientSocket, char* buf)
{
	char urlPrefix[COMMON_STR_LEN];

	SK_ASSERT(stream);
	SK_ASSERT(track);
	SK_ASSERT(buf);

	memset(urlPrefix, 0x0, sizeof(char) * COMMON_STR_LEN);
	rtspURLPrefix(clientSocket, urlPrefix);
	snprintf(buf, COMMON_STR_LEN - 1, "%s%s/%s", urlPrefix, stream->name, track->name);

	return buf;
}

int appMutexLock(pthread_mutex_t *mutex)
{
	int rval = 0;

//	do {		
	rval = pthread_mutex_lock(mutex);
	if (rval != 0) {
		ERRMSG("pthread_mutex_lock fail[%d]", rval);
	}
//	} while(rval!=0);

	return rval;
}

int appMutexUnlock(pthread_mutex_t *mutex)
{
	int rval = 0;

//	do {	
	rval = pthread_mutex_unlock(mutex);
	if (rval != 0) {
		ERRMSG("pthread_mutex_unlock fail[%d]", rval);
	}
//	} while(rval!=0);	

	return rval;
}

/*
 Function
 smsPutFrame
 Description
 Put Frame to Track
 Arguments
 - track ID
 - Frame pointer
 Return value
 - success : handled bytes
 - failure : -1
 */

int smsPutFrame(int trackId, Frame *frame)
{
	int rval = 0;

	SK_ASSERT(frame);

	LOCK_MTX(&gRtspServerMutex);

	if (gRtspServer[0].initdone == 0) {
		ERRMSG("gRtspServer.initdone == 0");
	}
	else {
		rval = PutFrame(&gRtspServer[0], trackId, frame, NULL);
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

static Stream* SearchStream(RtspServer *server, const char *name)
{
	StreamList *lptr = NULL;
	StreamNodePtr tmp = NULL;
	Stream* rStream = NULL;

	SK_ASSERT(server);
	SK_ASSERT(name);

	lptr = &server->streamList;
	tmp = lptr->head;

	while (tmp != NULL) {
		if (strcmp(tmp->stream.name, name) == 0) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("stream name[%s] not found!", name);
	}
	else {
		rStream = &tmp->stream;
	}

	return rStream;
}

/*
 Function
 LookupStreamByName
 Description
 Retrieve Stream
 Arguments
 - url
 Return value
 - success : Stream pointer
 - failure : NULL
 */
//FIXME.gRtspServer
Stream* LookupStreamByName(const char *name)
{
	Stream *rStream = NULL;

	LOCK_MTX(&gRtspServerMutex);

	rStream = SearchStream(&gRtspServer[0], name);

	UNLOCK_MTX(&gRtspServerMutex);

	return rStream;
}

int smsSetMediaInfo(int trackId, MediaInfo *pInfo)
{
	int rval = 0;

	SK_ASSERT(pInfo);

	LOCK_MTX(&gRtspServerMutex);

	if (gRtspServer[0].initdone == 0) {
		ERRMSG("gRtspServer.initdone == 0");
	}
	else {
		rval = RenewMediaInfo(&gRtspServer[0], trackId, pInfo);
	}

	UNLOCK_MTX(&gRtspServerMutex);

	return rval;
}

static Track* SearchTrack(TrackList *lptr, const char *name)
{
	TrackNodePtr tmp = lptr->head;
	Track* rTrack = NULL;

	while (tmp != NULL) {
		if (strcmp(tmp->track.name, name) == 0) {
			break;
		}
		tmp = tmp->next;
	}

	if ( NULL != tmp) {
		rTrack = &tmp->track;
	}

	return rTrack;
}

static Track* SearchTrackFromServer(RtspServer *server, const char *name)
{
	StreamList *lptr = NULL;
	StreamNodePtr tmp = NULL;
	Track *rTrack = NULL;

	SK_ASSERT(server);
	SK_ASSERT(name);

	lptr = &server->streamList;
	tmp = lptr->head;

	while (tmp != NULL) {
		rTrack = SearchTrack(&tmp->stream.trackList, name);
		if (rTrack) {
			break;
		}
		tmp = tmp->next;
	}

	return rTrack;
}

/*
 Function
 LookupTrackByName
 Description
 Retrieve Track
 Arguments
 - url
 Return value
 - success : Track pointer
 - failure : NULL
 */
//FIXME.gRtspServer
Track* LookupTrackByName(const char *name)
{
	Track *rTrack = NULL;

	LOCK_MTX(&gRtspServerMutex);

	rTrack = SearchTrackFromServer(&gRtspServer[0], name);

	UNLOCK_MTX(&gRtspServerMutex);

	return rTrack;
}

Track* GetFirstTrackFromStream(Stream *pStream)
{
	Track *rTrack = NULL;
	StreamList *lptr = NULL;
	StreamNodePtr tmp = NULL;

	SK_ASSERT(pStream);

	if (pStream->trackList.head) {
		rTrack = &pStream->trackList.head->track;
	}

	return rTrack;
}
