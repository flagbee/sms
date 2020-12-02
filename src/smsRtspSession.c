/*******select.c*********/
/*******Using select() for I / O multiplexing */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>   //cpu_set_t , CPU_SET
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <linux/kernel.h>       /* for struct sysinfo */
#include <sys/sysinfo.h>


#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "smsRtspSession.h"
#include "smsRtspHandler.h"
#include "sms.h"


//#define DEBUG_SMS_RTSP_SESSION
#if defined(DEBUG_SMS_RTSP_SESSION)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-SESSION] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-SESSION-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

RTSPSessionList gRtspSessionList;


static void InitRTSPSessionList(RTSPSessionList	*lptr)
{
	SK_ASSERT(lptr);

	memset(lptr, 0x0, sizeof(RTSPSessionList));
	
	lptr->count = 0;
	lptr->head	= NULL;
}

static RTSPSession* InsertRTSPSession(RTSPSessionList *lptr, RTSPSession *rtspSession)
{
	RTSPSessionNodePtr new_nptr = NULL;
	RTSPSession	*rSession = NULL;
		
	SK_ASSERT(rtspSession);
	SK_ASSERT(lptr);

	new_nptr = (RTSPSessionNode*)malloc(sizeof(RTSPSessionNode));	
	memset(new_nptr, 0x0, sizeof(RTSPSessionNode));
	memcpy(&new_nptr->session, rtspSession, sizeof(RTSPSession));

	/* Insert Session */	
	if(lptr->count == 0){
		new_nptr->next = lptr->head;
		lptr->head = new_nptr;
	}
	else{
		RTSPSessionNodePtr tmp = lptr->head;
		int i=0;
		for(i=1; i<lptr->count; i++){
			tmp = tmp->next;
		}
		new_nptr->next = tmp->next;
		tmp->next = new_nptr;
	}
	lptr->count++;

	rSession = &new_nptr->session;

	return rSession;
}

static int DeleteRTSPSession(RTSPSessionList	*lptr, const char *strId)
{
	int i=0, rval=SMS_RET_ERR_INTERNAL;

	RTSPSessionNodePtr tmp=NULL, prev=NULL;

	SK_ASSERT(lptr);

	tmp=lptr->head;
	prev=lptr->head;

	for(i=0; i<lptr->count; i++){
		if(	strcmp(tmp->session.strId,strId) == 0 ){			
			if(i==0){
				lptr->head = tmp->next;
			}
			else{
				prev->next = tmp->next;
			}			
			
			/* free Connection */
			free(tmp);
			
			lptr->count--;
			rval = SMS_RET_OK;
			break;
		}
		else{
			prev = tmp;
			tmp=tmp->next;
		}
	}

	if( rval != SMS_RET_OK ){
		ERRMSG("rtspSession id[%s] not found!", strId);
	}

	return rval;
}

static RTSPSession* SearchRTSPSession(RTSPSessionList *lptr, const char *strId)
{
	RTSPSessionNodePtr tmp=lptr->head;	
	RTSPSession* 	rRTSPSession = NULL;
	
	while( tmp != NULL ){
		if( strcmp(tmp->session.strId, strId) == 0 ){
			break;
		}
		tmp = tmp->next;
	}
	
	if(tmp){
		rRTSPSession = &tmp->session;
		DBGMSG("rRTSPSession(%p)", rRTSPSession);
	}

	return rRTSPSession;
}

static int SetStateRTSPSession(RTSPSessionList *lptr, const char *strId, RTSPSessionState state)
{
	RTSPSession	*pRtspSession=NULL;
	int rval = SMS_RET_ERR_INTERNAL;

	SK_ASSERT(lptr);
	SK_ASSERT(strId);

	/* Find RTSP Session */
	pRtspSession = SearchRTSPSession(lptr, strId);
	if(pRtspSession){
		pRtspSession->state = state;
		rval = SMS_RET_OK;
	}
	else{
		ERRMSG("rtspSession ID[%s] not found!", strId);
		rval = SMS_ERR_SESSION_NOT_FOUND;
	}
	
	return rval;
}

static void PrintRTSPSessionList(RTSPSessionList *lptr)
{
	RTSPSessionNodePtr tmp=lptr->head;

	DBGMSG("List value: ");
	while(tmp!=NULL){
		DBGMSG("[%s]",tmp->session.strId);
		tmp=tmp->next;
	}
	DBGMSG("Total: %d rtspSessions",lptr->count);	
}

int RTSPSessionPLAYFunc(RtspServer *server, ClientConnection *clientConn, const char *strId, const char *strPreUrlSuffix, const char *strUrlSuffix, char *rtpInfo)
{
	RTSPSession	*pRtspSession=NULL;
	Stream	*pStream = NULL;
	Track	*pTrack = NULL;	
	TrackNodePtr pTrackNode = NULL;
	char	rtpInfoBody[COMMON_STR_LEN*4];
	char tmpbuf[COMMON_STR_LEN];	
	int rval=SMS_RET_ERR_INTERNAL;
	unsigned int ssrc=0, timestampBase=0;
	unsigned short seqNum=0;
	SK_ASSERT(server);
	SK_ASSERT(clientConn);
	SK_ASSERT(strId);
	SK_ASSERT(strPreUrlSuffix);
	SK_ASSERT(strUrlSuffix);	
	SK_ASSERT(rtpInfo);

	memset(rtpInfoBody, 0x0, sizeof(char)*COMMON_STR_LEN*4);

	/* Set State of RTSP Session */
	rval = SetStateRTSPSession(&server->rtspSessionList, strId, RTSP_STATE_PLAY);
	if( rval != SMS_RET_OK ){
		ERRMSG("SetStateRTSPSession fail Id[%s]", strId);
		return rval;
	}

	if( strlen(strUrlSuffix) ){
		/* Find Track */
		pTrack = LookupTrackByName(strUrlSuffix);
		if(pTrack){
			/* Set State of RTP Session */
			rval = StartRTPStreaming(&pTrack->rtpSessionList, strId, &ssrc, &timestampBase, &seqNum);
			if( rval != SMS_RET_OK ){
				ERRMSG("StartRTPStreaming fail Id[%s]", strId);
				return rval;
			}
			memset(tmpbuf, 0x0, sizeof(char)*COMMON_STR_LEN);
			rtspTrackURL(pStream, pTrack, clientConn->clientSocket, tmpbuf);			
			snprintf(rtpInfoBody, COMMON_STR_LEN*4-1, "url=%s;seq=%d;rtptime=%u",tmpbuf, seqNum, timestampBase);			
			snprintf(rtpInfo, COMMON_STR_LEN*2-1, "RTP-Info: %s\r\n", rtpInfoBody);
		}
		else{
			char tmpUrl[COMMON_STR_LEN];
			
			/* strPreUrlPrefix is not a track name */
			DBGMSG("Track[%s] not found. It may be a stream name.", strUrlSuffix);

            memset(tmpUrl, 0x0, sizeof(char)*COMMON_STR_LEN);
			if( strlen(strPreUrlSuffix) )
			{
				snprintf(tmpUrl, COMMON_STR_LEN-1, "%s/%s", strPreUrlSuffix, strUrlSuffix);
			}
			else
			{
				snprintf(tmpUrl, COMMON_STR_LEN-1, "%s", strUrlSuffix);
			}
			
			/* Find Stream */
			pStream = LookupStreamByName(tmpUrl);
			if( pStream ){
				for(pTrackNode = pStream->trackList.head; pTrackNode != NULL; pTrackNode = pTrackNode->next){
					pTrack = &pTrackNode->track;
					/* Start RTP Session */
					if( pTrack && 0 < pTrack->rtpSessionList.count ) {
						rval = StartRTPStreaming(&pTrack->rtpSessionList, strId, &ssrc, &timestampBase, &seqNum);
						if( rval == SMS_RET_OK ){
							memset(tmpbuf, 0x0, sizeof(char)*COMMON_STR_LEN);
							rtspTrackURL(pStream, pTrack, clientConn->clientSocket, tmpbuf);
							if( pTrackNode == pStream->trackList.head ){
								snprintf(rtpInfoBody, COMMON_STR_LEN*4-1, "url=%s;seq=%d;rtptime=%u",tmpbuf, seqNum, timestampBase);
							}
							else{
								char tmpInfo[COMMON_STR_LEN];

								memset(tmpInfo, 0x0, sizeof(char)*COMMON_STR_LEN);
								snprintf(tmpInfo, COMMON_STR_LEN-1, ",url=%s;seq=%d;rtptime=%u",tmpbuf, seqNum, timestampBase);
								strcat(rtpInfoBody, tmpInfo);
							}
						}
					}
				}

				if( strlen(rtpInfoBody) != 0 ){
					snprintf(rtpInfo, COMMON_STR_LEN*2-1, "RTP-Info: %s\r\n", rtpInfoBody);
					rval = SMS_RET_OK;
				}
				else{
					rval = SMS_ERR_SESSION_NOT_FOUND;
				}
			}		
			else{
				ERRMSG("Stream[%s] not found", strUrlSuffix);
				rval = SMS_ERR_TRACK_NOT_FOUND;
			}			
		}			
	}
	else{
		/* Find Stream */
		pStream = LookupStreamByName(strPreUrlSuffix);
		if( pStream ){
			for(pTrackNode = pStream->trackList.head; pTrackNode != NULL; pTrackNode = pTrackNode->next){
				pTrack = &pTrackNode->track;
				/* Start RTP Session */
				if( pTrack && 0 < pTrack->rtpSessionList.count ) {
					rval = StartRTPStreaming(&pTrack->rtpSessionList, strId, &ssrc, &timestampBase, &seqNum);
					if( rval == SMS_RET_OK ){
						memset(tmpbuf, 0x0, sizeof(char)*COMMON_STR_LEN);
						rtspTrackURL(pStream, pTrack, clientConn->clientSocket, tmpbuf);
						if( pTrackNode == pStream->trackList.head ){
							snprintf(rtpInfoBody, COMMON_STR_LEN*4-1, "url=%s;seq=%d;rtptime=%u",tmpbuf, seqNum, timestampBase);
						}
						else{
							char tmpInfo[COMMON_STR_LEN];

							memset(tmpInfo, 0x0, sizeof(char)*COMMON_STR_LEN);
							snprintf(tmpInfo, COMMON_STR_LEN-1, ",url=%s;seq=%d;rtptime=%u",tmpbuf, seqNum, timestampBase);
							strcat(rtpInfoBody, tmpInfo);
						}
					}
				}
			}

			if( strlen(rtpInfoBody) != 0 ){
				snprintf(rtpInfo, COMMON_STR_LEN*2-1, "RTP-Info: %s\r\n", rtpInfoBody);
				rval = SMS_RET_OK;
			}
			else{
				rval = SMS_ERR_SESSION_NOT_FOUND;
			}
		}		
		else{
			ERRMSG("Stream[%s] not found", strPreUrlSuffix);
			rval = SMS_ERR_STREAM_NOT_FOUND;
		}	
	}

	return rval;
}

static long GetUptime(void)
{
    struct sysinfo s_info;
    int Rval=0;
    long Rtime=0;

    Rval = sysinfo(&s_info);
    if(Rval != 0)
    {
        ERRMSG("code error = %d", Rval);
        Rtime = 0;
    }
    else
    {
    	Rtime = s_info.uptime;
    }

    return Rtime;
}

int RTSPSessionRenewTerminationTimeFunc(RTSPSession *session)
{
	int rval=0;

	SK_ASSERT(session);

	session->terminationUptime = GetUptime() + RTSP_SESSION_TIMEOUT_SECS;
	DBGMSG("Update Termination Time(%ld). Session(%s)", session->terminationUptime, session->strId);

	return rval;
}

/* 
Function 
	pushTCPData
Description 
	add TCP data to tcp buffer on client connection
Arguments
	- client connection
	- buffer pointer
	- length
Return value
	- OK : OK
	- Fail : NULL
*/

int pushTCPData(ClientConnection * conn, char *buf, unsigned int len)
{
	int rval = SMS_RET_ERR_INTERNAL;
	unsigned int head = 0, tail = 0, remainLen = 0;

	SK_ASSERT(buf);
	SK_ASSERT(conn);
	SK_ASSERT(conn->sendTCPBuf);
	
	LOCK_MTX(&conn->tcpSenderMutex);

	head = conn->head;
	tail = conn->tail;

	if( head <= tail ){
		remainLen = MAX_TCP_SEND_BUF_SIZE - tail;
		if( remainLen > len ){
			memcpy(&conn->sendTCPBuf[tail], buf, len );
			conn->tail += len;
			rval = SMS_RET_OK;
		}
		else{
			remainLen += head;
			if( remainLen <= len ){
				ERRMSG("not enough tcp buffer remain[%u] <= len[%u]", remainLen, len);
				rval = SMS_TCP_SEND_BUFFER_FULL;
			}
			else{
				unsigned int writeLen = 0;
				
				writeLen = MAX_TCP_SEND_BUF_SIZE - tail;
				memcpy(&conn->sendTCPBuf[tail], buf, writeLen);
				memcpy(&conn->sendTCPBuf[0], buf+writeLen, len-writeLen);
				conn->tail = len-writeLen;
				rval = SMS_RET_OK;
			}
		}
	}
	else if( head > tail ){
		remainLen = head - tail - 1;
		if( remainLen > len ){
			memcpy(&conn->sendTCPBuf[tail], buf, len);
			conn->tail += len;
			rval = SMS_RET_OK;
		}
		else{
			ERRMSG("not enough tcp buffer remain[%u] <= len[%u]", remainLen, len);
			rval = SMS_TCP_SEND_BUFFER_FULL;
		}
	}

	//DBGMSG("h[%d] t[%d]", conn->head, conn->tail);
	
	UNLOCK_MTX(&conn->tcpSenderMutex);
	
	return rval;
}


/* 
Function 
	pushTCPDataToChannel
Description 
	add TCP data to tcp exact channel
Arguments
	- client connection
	- buffer pointer
	- length
	- channel
Return value
	- OK : OK
	- Fail : NULL
*/

int pushTCPDataToChannel(ClientConnection * conn, RTPPacketDesc *pRtpDesc, int channel, int *transState)
{
	int rval = SMS_RET_ERR_INTERNAL;
	unsigned int head = 0, tail = 0, remainLen = 0, totalLen = 0;
	unsigned int usedBufLen = 0, rTCPBufLen = 0;
	char *buf=NULL;
	unsigned int len=0, is_marker=0;
	
	SK_ASSERT(pRtpDesc);
	SK_ASSERT(pRtpDesc->pStartAddr);
	SK_ASSERT(conn);
	
	buf = pRtpDesc->pStartAddr;
	len = pRtpDesc->Len;
	is_marker = (pRtpDesc->Marker&0x80)?1:0;

	LOCK_MTX(&conn->tcpSenderMutex);

	if( conn->sendTCPBuf == NULL ){
		ERRMSG("conn->sendTCPBuf == NULL");
		UNLOCK_MTX(&conn->tcpSenderMutex);
		return rval;
	}

	head = conn->head;
	tail = conn->tail;
	if( head <= tail ){
		char	tcpMsgHeader[4];

		tcpMsgHeader[0] = 0x24;
		tcpMsgHeader[1] = (char)channel&0xFF;
		tcpMsgHeader[2] = (char)(len>>8)&0xFF;
		tcpMsgHeader[3] = (char)len&0xFF;

		totalLen = len + 4;
		remainLen = MAX_TCP_SEND_BUF_SIZE - tail;
		
		if( remainLen > totalLen ){
			memcpy(&conn->sendTCPBuf[tail], tcpMsgHeader, 4);
			tail += 4;
			memcpy(&conn->sendTCPBuf[tail], buf, len);
			tail += len;
			conn->tail = tail;
			if( is_marker )
			{
				conn->marker = tail;
			}
			rval = SMS_RET_OK;
		}
		else{
			/* recycle */
			remainLen += head;
			if( remainLen <= totalLen ){
				ERRMSG("not enough tcp buffer remain[%u] <= len[%u]", remainLen, totalLen);
				rval = SMS_TCP_SEND_BUFFER_FULL;
			}
			else{
				unsigned int writeLen = 0, start_pos = 0;
				
				writeLen = MAX_TCP_SEND_BUF_SIZE - tail;
				if( writeLen > 4 ){
					/* write header */
					memcpy(&conn->sendTCPBuf[tail], tcpMsgHeader, 4);
					tail += 4;
					writeLen -= 4;				

					/* write frame to remaining buffer */
					memcpy(&conn->sendTCPBuf[tail], buf, writeLen);
					tail += writeLen;
					if( tail != MAX_TCP_SEND_BUF_SIZE ){
						ERRMSG("tail[%d] != MAX_TCP_SEND_BUF_SIZE[%d]", tail, MAX_TCP_SEND_BUF_SIZE);
					}
					tail = 0;

					/* write remaining frame to start point of buffer */
					memcpy(&conn->sendTCPBuf[tail], buf+writeLen, len-writeLen);
					tail += len-writeLen;
				}
				else{
					/* write a portion of header */
					memcpy(&conn->sendTCPBuf[tail], tcpMsgHeader, writeLen);
					tail += writeLen;
					if( tail != MAX_TCP_SEND_BUF_SIZE ){
						ERRMSG("tail[%d] != MAX_TCP_SEND_BUF_SIZE[%d]", tail, MAX_TCP_SEND_BUF_SIZE);
					}
					tail = 0;

					/* write remaing header */
					memcpy(&conn->sendTCPBuf[tail], tcpMsgHeader+writeLen, 4-writeLen);
					tail += 4-writeLen;

					/* write remaing header */
					memcpy(&conn->sendTCPBuf[tail], buf, len);
					tail += len;
				}
				conn->tail = tail;
				if( is_marker )
				{
					conn->marker = tail;
				}
				//DBGMSG("conn->tail[%d] totalLen[%d] writeLen[%d]", conn->tail, totalLen, writeLen);
				rval = SMS_RET_OK;
			}
		}
	}
	else if( head > tail ){
		char	tcpMsgHeader[4];

		tcpMsgHeader[0] = 0x24;
		tcpMsgHeader[1] = (char)channel&0xFF;
		tcpMsgHeader[2] = (char)(len>>8)&0xFF;
		tcpMsgHeader[3] = (char)len&0xFF;
		
		totalLen = len + 4;		
		remainLen = head - tail - 1;
		if( remainLen > totalLen ){
			memcpy(&conn->sendTCPBuf[tail], tcpMsgHeader, 4);
			tail += 4;
			memcpy(&conn->sendTCPBuf[tail], buf, len);
			tail += len;			
			conn->tail = tail;
			if( is_marker )
			{
				conn->marker = tail;
			}
			rval = SMS_RET_OK;
			//DBGMSG("h[%d] t[%d]", conn->head, conn->tail);
		}
		else{
			ERRMSG("not enough tcp buffer remain[%u] <= len[%u]", remainLen, totalLen);
			rval = SMS_TCP_SEND_BUFFER_FULL;
		}
	}
//	DBGMSG("h[%d] t[%d]", conn->head, conn->tail);

#if 1
	/* check buffer */
	if( conn->loopCnt%10000 == 0){
		ioctl(conn->clientSocket, SIOCOUTQ, &rTCPBufLen);
//		
//		/* */
//		if( rTCPBufLen >= 100000 ){
//			*transState = SMS_TCP_STATUS_BUFFER_OK;
//		}
//		else if( rTCPBufLen >= 20000 ){
//			*transState = SMS_TCP_STATUS_BUFFER_WARNING;
//		}
//		else if( rTCPBufLen >= 7000 ){
//			*transState = SMS_TCP_STATUS_BUFFER_UNSTABLE;
//		}
//		else{
//			*transState = SMS_TCP_STATUS_BUFFER_FULL;
//		}
		DBGMSG("usedBufLen[%u] transState[%d] rTCPBufLen[%u]", usedBufLen, *transState, rTCPBufLen);
		conn->tcpSendStatus = *transState;
	}
	else{
		*transState = SMS_TCP_STATUS_BUFFER_UNKNOWN;
	}
	conn->loopCnt++;
#else
	*transState = SMS_TCP_STATUS_BUFFER_OK;
#endif

	UNLOCK_MTX(&conn->tcpSenderMutex);

	return rval;
}

int tcpSend(int socket, char *buf, int len, int option)
{
	int rval = SMS_RET_ERR_INTERNAL;
	int bufidx=0, remains=0, warn_cnt=0;

	remains = len;
	bufidx = 0;
	while( remains != 0 )
	{
		rval = send(socket, buf+bufidx, remains, 0);
		if( rval > 0 )
		{
			if( remains != rval )
			{
				//ERRMSG("rval[%d] != remains[%d], len(%d) warn_cnt(%d)", rval, remains, len, warn_cnt);
				if( warn_cnt > 10 )
				{
					rval = SMS_TCP_SEND_BUFFER_FULL;
					break;
				}
				warn_cnt++;
				usleep(200000);
			}
			else
			{
				warn_cnt--;
				rval = SMS_RET_OK;
				break;
			}
			remains -= rval;
			bufidx += rval;
		}
		else if( rval == 0 )
		{
			ERRMSG("send warn rval[%d] err[%d](%s)", rval, errno, strerror(errno));
			break;
		}
		else
		{
			ERRMSG("send fail rval[%d] err[%d](%s)", rval, errno, strerror(errno));
			if( errno == 11 ) // Resource temporarily unavailable
			{
				if( warn_cnt > 10 )
				{
					rval = SMS_TCP_SEND_BUFFER_FULL;
					break;
				}
				warn_cnt++;
				usleep(200000);
			}
			else
			{
				rval = SMS_RET_ERR_INTERNAL;
				break;
			}
		}
	}

#if 0
	rval = send(socket, buf, len, 0);
	if( rval > 0 ){
		if( rval != len ){
			//FIXME. remains ??
			ERRMSG("rval[%d] != len[%d]", rval, len);
		}
		rval = SMS_RET_OK;
	}
	else if( rval == 0 ){
		ERRMSG("send warn rval[%d] err[%d](%s)", rval, errno, strerror(errno));
	}
	else{
		ERRMSG("send fail rval[%d] err[%d](%s)", rval, errno, strerror(errno));
	}
#endif

	return rval;
}

int DestoryTCPBuffer(ClientConnection *conn)
{
	int rval = SMS_RET_ERROR;
    pthread_t thread_t;
    int status;
	
    conn->tcpSenderTerminate = 1;

	LOCK_MTX(&conn->tcpSenderMutex);

	if(conn->sendTCPBuf){
		free(conn->sendTCPBuf);
		conn->sendTCPBuf = NULL;
		conn->head = 0;
		conn->tail = 0;
		conn->marker = 0;
		rval = SMS_RET_OK;
	}	
	else{
		rval = SMS_RET_ERROR;
	}

	UNLOCK_MTX(&conn->tcpSenderMutex);
	pthread_mutex_destroy(&conn->tcpSenderMutex);

	return rval;
}

// core_id = 0, 1, ... n-1, where n is the system's number of cores
static int stick_this_thread_to_core(int core_id, const char *pName, int realtime_priority) 
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
   policy = SCHED_FIFO;
   param.sched_priority = realtime_priority;
   pthread_setschedparam(pthread_self(), policy, &param);
      
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

void tcpSenderFunc(void *data)
{
	ClientConnection *conn = (ClientConnection *)data;
	int rval = 0;

	SK_ASSERT(conn)
	
	//baek.debug
//	stick_this_thread_to_core(3, "tcpSenderFunc", 2);
	
	while(conn->tcpSenderTerminate == 0){
		int len = 0, rval = 0;
		unsigned int head = 0, tail = 0, tobehead=0;
		
		if( conn->head == conn->tail )
		{
			usleep(100);
		}
		else
		{
			LOCK_MTX(&conn->tcpSenderMutex);
			if( conn->head < conn->tail )
			{
				if( conn->marker > conn->head && conn->marker <= conn->tail )
				{
					/* start---head---marker---tail---end */
					head = conn->head;
					tail = conn->marker;
					len = tail - head;
					tobehead = conn->marker;
				}
				else
				{
					/* start---head-------tail---end */
					head = conn->head;
					tail = conn->tail;
					len = tail - head;
					tobehead = conn->tail;
				}
			}
			else
			{
				if( conn->marker > conn->head )
				{
					/* start---tail---head---marker---end */
					head = conn->head;
					tail = conn->marker;
					len = tail - head;
					tobehead = conn->marker;
				}
				else
				{
					/* start---marker---tail---head---end */
					/* start---tail---marker---head---end */
					head = conn->head;
					tail = MAX_TCP_SEND_BUF_SIZE;
					len = tail - head;
					tobehead = 0;
				}
			}
			UNLOCK_MTX(&conn->tcpSenderMutex);

			rval = tcpSend(conn->clientSocket, &conn->sendTCPBuf[head], len, 0);
			if( rval == SMS_RET_OK ){
				//conn->head = conn->tail;
			}
			else if( rval == SMS_RET_ERR_INTERNAL ){
				ERRMSG("tcpSend fail SMS_RET_ERR_INTERNAL. terminated");
				conn->tcpSenderTerminate = 1;
				break;
			}			
			else{
				ERRMSG("tcpSend fail rval[%d]", rval);
			}
			LOCK_MTX(&conn->tcpSenderMutex);
			conn->head = tobehead;
			UNLOCK_MTX(&conn->tcpSenderMutex);
		}
	}
}

int RTSPSessionSETUPFunc(RtspServer *server, ClientConnection *clientConn, RTSPSession *pRtspSession, RTSPConfiguration *config)
{
	RTPSession	rtpSession;
	int rtpSessionId=0;
	int rval = 0;
	int bindRval = 0;
	int sockopt=0;

	struct sockaddr_in serveraddr;	

	SK_ASSERT(server);
	SK_ASSERT(clientConn);
	SK_ASSERT(config);
	SK_ASSERT(pRtspSession);	
	
	/* Make RTP Session */
	memset(&rtpSession, 0x0, sizeof(RTPSession));
	rtpSession.rtspSession = pRtspSession;
	rtpSession.seqNum = random16();
	rtpSession.state = RTP_STATE_IDLE;
	rtpSession.mode = config->mode;
	//baek.debug
//	rtpSession.ssrc = (unsigned int)strtol(pRtspSession->strId, NULL, 16);
	rtpSession.ssrc = random32();
	rtpSession.timestampBase = random32();
	rtpSession.timestampLast = rtpSession.timestampBase;

	/* create destination */

	switch(config->mode){
		case RTP_UDP:
			/* addr, port, ttl */
			sockopt = 1;
			rtpSession.rtpSocket = socket(AF_INET, SOCK_DGRAM, 0);
			rtpSession.rtcpSocket = socket(AF_INET, SOCK_DGRAM, 0);
			setsockopt(rtpSession.rtpSocket, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
			setsockopt(rtpSession.rtcpSocket, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
			
			serveraddr.sin_family = AF_INET;
			serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
			serveraddr.sin_port = htons(config->rtpServerPort);
			memset(&(serveraddr.sin_zero), '\0', 8);

			/* Set server port */
			if( config->rtpServerPort == 0 || config->rtcpServerPort == 0 ){
				//FIXME.find unused port
				config->rtpServerPort = INIT_RTP_SERVER_PORT;
				config->rtcpServerPort = config->rtpServerPort+1;
			}
			
			do {
				// RTP Port
				serveraddr.sin_port = htons(config->rtpServerPort);
				bindRval = bind(rtpSession.rtpSocket, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
				if ( bindRval == -1) {
					ERRMSG("config->rtpServerPort[%u] bind() error", config->rtpServerPort);
				}
				else{
					DBGMSG("rtp server port bind success[%u]", config->rtpServerPort);
					// RTCP Port
					serveraddr.sin_port = htons(config->rtcpServerPort);
					bindRval = bind(rtpSession.rtcpSocket, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
					if ( bindRval == -1) {
						ERRMSG("config->rtcpServerPort[%u] bind() error", config->rtcpServerPort);
					}
					else{
						DBGMSG("rtcp server port bind success[%u]", config->rtcpServerPort);
						break;
					}						
				}
				
				config->rtpServerPort += 2;
				config->rtcpServerPort = config->rtpServerPort + 1;
				if( config->rtpServerPort >= 65500 ){
					rval = SMS_RET_ERR_INTERNAL;
					return rval;
				}
			} while(bindRval == -1);
			
			rtpSession.rtpAddr.sin_family = AF_INET;
			rtpSession.rtpAddr.sin_addr.s_addr = clientConn->clientAddr.sin_addr.s_addr;
			rtpSession.rtpAddr.sin_port = htons(config->rtpClientPort);
			memset(&(rtpSession.rtpAddr.sin_zero), '\0', 8);
			
			rtpSession.rtcpAddr.sin_family = AF_INET;
			rtpSession.rtcpAddr.sin_addr.s_addr = clientConn->clientAddr.sin_addr.s_addr;
			rtpSession.rtcpAddr.sin_port = htons(config->rtcpClientPort);
			memset(&(rtpSession.rtcpAddr.sin_zero), '\0', 8);
			memcpy(rtpSession.strId, pRtspSession->strId, RTSP_SESSIONID_LEN);
			
			{
				LOCK_MTX(&server->rtcpHandlerMutex);
				
				FD_SET(rtpSession.rtcpSocket, &server->rtcpFds);
				if (rtpSession.rtcpSocket > server->rtcpFdMax) {
					server->rtcpFdMax = rtpSession.rtcpSocket;
				}
				DBGMSG(">> new RTCP channel Socket(%d)", rtpSession.rtcpSocket);
				
				UNLOCK_MTX(&server->rtcpHandlerMutex);
			}
						
			/* Add RTP Session to Track */
			AddRTPSession(server, config->trackId, &rtpSession);	
			rval = SMS_RET_OK;
			break;
		case RTP_RTSP_TCP:
			LOCK_MTX(&clientConn->tcpSenderMutex);
			if( clientConn->sendTCPBuf == NULL ){				
				clientConn->sendTCPBuf = (char *)malloc(sizeof(char)*MAX_TCP_SEND_BUF_SIZE);
				if( clientConn->sendTCPBuf ){
					clientConn->head = 0;
					clientConn->tail = 0;
					clientConn->tcpSenderTerminate = 0;
					if( pthread_create(&clientConn->tcpSenderId, NULL, (void *)tcpSenderFunc, (void*)clientConn) != 0){
						ERRMSG("%s pthread_create error", __FUNCTION__);
					}
					else{
						pthread_detach(clientConn->tcpSenderId);
					}
				}
				else{
					ERRMSG("malloc failed");
					rval = SMS_RET_ERR_INTERNAL;
				}
			}
			else{
				DBGMSG("clientConn->sendTCPBuf[%p] is already exist", clientConn->sendTCPBuf);
			}
			UNLOCK_MTX(&clientConn->tcpSenderMutex);
			rtpSession.rtpChannel = config->rtpChannel;
			rtpSession.rtcpChannel = config->rtcpChannel;
			memcpy(rtpSession.strId, pRtspSession->strId, RTSP_SESSIONID_LEN);
			
			/* Add RTP Session to Track */
			AddRTPSession(server, config->trackId, &rtpSession);
			rval = SMS_RET_OK;
			break;			
		default:
			ERRMSG("config->mode[%d] not defined", config->mode);
			break;
	}

	return rval;
}


/* 
Function 
	InitRTSPSessions
Description 
	Initialize RTSP Session List
Arguments
	- RTSP server pointer
Return value
	- none
*/
void InitRTSPSessions(RtspServer *server)
{
	InitRTSPSessionList(&server->rtspSessionList);
}


/* 
Function 
	CreateRTSPSession
Description 
	Create RTPS Session and add to the list
Arguments
	- RTSP server pointer
	- ClientConnection pointer
Return value
	- RTSP Session pointer
*/
RTSPSession* CreateRTSPSession(RtspServer *server, ClientConnection *clientConn)
{
	RTSPSession	rtspSession, *rSession = NULL;
	unsigned int sessionId = 0;

	SK_ASSERT(server);

	memset(&rtspSession, 0x0, sizeof(RTSPSession));
	
	// Choose a random (unused) 32-bit integer for the session id
	// (it will be encoded as a 8-digit hex number).  (We avoid choosing session id 0,
	// because that has a special use by some servers.)
	do {
		sessionId = (unsigned int)random32();
		snprintf(rtspSession.strId, sizeof(char)*RTSP_SESSIONID_LEN, "%08X", sessionId);
	} while (sessionId == 0 || SearchRTSPSession(&server->rtspSessionList, rtspSession.strId) != NULL);
	
	/* Make RTSP Session */
	rtspSession.state = RTSP_STATE_IDLE;
	rtspSession.ClientConn = clientConn;
	rtspSession.handleSETUP = (handleSETUPFunc)RTSPSessionSETUPFunc;
	rtspSession.handlePLAY = (handlePLAYFunc)RTSPSessionPLAYFunc;
	rtspSession.handleRenewTermTime = (renewTerminationTimeFunc)RTSPSessionRenewTerminationTimeFunc;
	rtspSession.handleRenewTermTime(&rtspSession);

	rSession = InsertRTSPSession(&server->rtspSessionList, &rtspSession);

	return rSession;
}

/* 
Function 
	DestoryRTSP Session
Description 
	Termination RTSP connection and Destory RTSP Session
Arguments
	- RTSP server pointer
	- RTSP Session ID
Return value
	- OK : 1
	- Fail : other values
*/
int DestoryRTSPSession(RtspServer *server, const char *strId)
{
	int rval=SMS_RET_ERR_INTERNAL;
	
	SK_ASSERT(strId);
	
	rval = DeleteRTSPSession(&server->rtspSessionList, strId);	

	return rval;
}

/* 
Function 
	CreateNewDestination
Description 
	Create new destination 
Arguments
	- ClientConnection pointer
	- RTSP session ID
	- RTSP Session Configuration
Return value
	- OK : 1
	- Fail : other values
*/


/* 
Function 
	SetupRTSPSession
Description 
	Delete current RTSP configuration and setup new configuration
	Setup RTP Session on Track.
Arguments
	- ClientConnection pointer
	- RTSP session ID
	- RTSP Session Configuration
Return value
	- OK : 1
	- Fail : other values
*/
int SetupRTSPSession(RtspServer *server, ClientConnection *clientConn, const char *strId, RTSPConfiguration *config)
{
	RTPSession	rtpSession;
	RTSPSession	*pRtspSession=NULL;
	int trackId = 0, rtpSessionId=0;
	int rval = 0;

	//FIXME.
	struct sockaddr_in addr;	

	SK_ASSERT(server);
//	SK_ASSERT(clientConn);
	SK_ASSERT(strId);
//	SK_ASSERT(config);	
	
	/* Find RTSP Session */
	pRtspSession = SearchRTSPSession(&server->rtspSessionList, strId);
	if(pRtspSession){
		/* Make RTP Session */
		memset(&rtpSession, 0x0, sizeof(RTPSession));
		rtpSession.rtspSession = pRtspSession;

#if 1 //[[ baek_0160201_BEGIN -- FIXME
		rtpSession.seqNum = 0;
		rtpSession.timestampBase = 600000;
		
		/* create destination */
		/* addr, port, ttl */
		rtpSession.rtpSocket = socket(AF_INET, SOCK_DGRAM, 0);
		rtpSession.rtpAddr.sin_family = AF_INET;
		rtpSession.rtpAddr.sin_addr.s_addr = inet_addr("192.168.10.46");
//		rtpSession.rtpAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		rtpSession.rtpAddr.sin_port = htons(55555);
		memset(&(rtpSession.rtpAddr.sin_zero), '\0', 8);
		
		rtpSession.rtcpSocket = 0;
		snprintf(rtpSession.strId, RTSP_SESSIONID_LEN-1, "%d", 401);

		trackId = gVideoTrackId;
	
		/* Add RTP Session to Track */
		AddRTPSession(server, trackId, &rtpSession);

		/* create destination */
		/* addr, port, ttl */
		rtpSession.rtpSocket = socket(AF_INET, SOCK_DGRAM, 0);
		rtpSession.rtpAddr.sin_family = AF_INET;
		rtpSession.rtpAddr.sin_addr.s_addr = inet_addr("192.168.10.46");
//		rtpSession.rtpAddr.sin_addr.s_addr = htonl(INADDR_ANY);		
		rtpSession.rtpAddr.sin_port = htons(44444);
		memset(&(rtpSession.rtpAddr.sin_zero), '\0', 8);

		rtpSession.rtcpSocket = 0;
		snprintf(rtpSession.strId, RTSP_SESSIONID_LEN-1, "%d", 402);

		trackId = gVideoTrackId;

		/* Add RTP Session to Track */
		AddRTPSession(server, trackId, &rtpSession);
#endif //]] baek_0160201_END -- FIXME				
	}
	else{
		ERRMSG("RTSP Session [%s] not found", strId);
		rval = -1;
	}
	
	return rval;
}

/* 
Function 
	LookUpRTSPSession
Description 
	Lookup RTSP Session
Arguments
	- RTSP server pointer
	- RTSP session ID
Return value
	- OK : RTSP Session pointer
	- Fail : NULL
*/
RTSPSession* LookUpRTSPSession(RtspServer *server, const char *strId)
{
	RTSPSession* rSession = NULL;

	rSession = SearchRTSPSession(&server->rtspSessionList, strId);
	
	return rSession;
}


/* 
Function 
	DestoryAllRTSPSessions
Description 
	Destory all RTSP sessions
Arguments
	- RTSP server pointer
	- RTSP session ID
Return value
	- OK : RTSP Session pointer
	- Fail : NULL
*/
int DestoryAllRTSPSessions(RtspServer *server)
{
	int i=0, rval=SMS_RET_ERR_INTERNAL;
	RTSPSessionList	*lptr = &server->rtspSessionList;
	RTSPSessionNodePtr tmp=NULL, prev=NULL;

	SK_ASSERT(lptr);

	tmp=lptr->head;
	prev=lptr->head;

	for(i=0; i<lptr->count; i++){
		if(i==0){
			lptr->head = tmp->next;
		}
		else{
			prev->next = tmp->next;
		}

		/* free Connection */
		free(tmp);
		
		lptr->count--;
		rval = SMS_RET_OK;

		prev = tmp;
		tmp=tmp->next;
	}

	return rval;
}

static int IsSessionExpired(RTSPSession *pSession, long NowUptime)
{
	int Rval=0;

	SK_ASSERT(pSession);

	if( pSession->terminationUptime < NowUptime )
	{
		Rval = 1;
	}

	return Rval;
}

/*
Function
	TerminateExpiredRTSPSessions
Description
	Terminate Expired RTSP Sessions
Arguments
	- RTSP server pointer
Return value
	- OK : The number of terminated RTSP sessions. 0 is none.
	- Fail : -1
*/
int TerminateExpiredRTSPSessions(RtspServer *server)
{
	RTSPSessionNodePtr Node = server->rtspSessionList.head;
	long UptimeSec=0;
	int CntTerminatedSessions=0;

	UptimeSec = GetUptime();	
	while( Node != NULL ){		
		if( IsSessionExpired(&Node->session, UptimeSec) )
		{
			ERRMSG("Terminating... session(%s)", Node->session.strId);

			DestoryRTPSession(server, Node->session.strId);

			DestoryRTSPSession(server, Node->session.strId);

			CntTerminatedSessions++;
		}
		Node = Node->next;
	}

	return CntTerminatedSessions;
}


/*
Function
	TerminateExpiredRTSPSessions
Description
	Terminate Expired RTSP Sessions
Arguments
	- RTSP server pointer
Return value
	- OK : The number of terminated RTSP sessions. 0 is none.
	- Fail : -1
*/
int TerminateAllRTSPSessions(RtspServer *server)
{
	RTSPSessionNodePtr Node = server->rtspSessionList.head;
	int CntTerminatedSessions=0;

	while( Node != NULL ){
		ERRMSG("Terminating... session(%s)", Node->session.strId);
		DestoryRTPSession(server, Node->session.strId);
		DestoryRTSPSession(server, Node->session.strId);
		DestoryClientConnection(&server->clientList, Node->session.ClientConn->clientSocket);
		close(Node->session.ClientConn->clientSocket);
		CntTerminatedSessions++;
		Node = Node->next;
	}

	return CntTerminatedSessions;
}



int SetAllTerminationTimeForce(RtspServer *server)
{
    int rval=0;
	RTSPSessionNodePtr Node = server->rtspSessionList.head;    

	while( Node != NULL ){
        Node->session.terminationUptime = 0;
		Node = Node->next;
	}

    return rval;
}

