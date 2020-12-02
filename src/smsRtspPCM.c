/*******select.c*********/
/*******Using select() for I / O multiplexing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <time.h>
#include <string.h>
#include <errno.h>


#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "smsRtspSession.h"
#include "smsRtspPCM.h"
#include "sms.h"

#define DEBUG_SMS_RTSP_PCM
#if defined(DEBUG_SMS_RTSP_PCM)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-PCM] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-PCM-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

#define MAX_RTP_PACKET_SIZE	(1400)
#define MAX_FU_PAYLOAD_SIZE	(MAX_RTP_PACKET_SIZE-14)

static	unsigned int _convert_timestamp(Frame *frame, unsigned int resolution)
{
	unsigned int rval = 0;
	double	cal = 0.0;

	cal = 8000.0/(double)(frame->timeResolution);

	//FIXME.
	rval = frame->timestamp*cal;
//	DBGMSG("cal[%lf] frame->timestamp[%u] resol[%u] to [%u] rval[%u]", cal, frame->timestamp, frame->timeResolution, resolution, rval);
	
	return rval;
}

int putPCMFrameFunc(Track *track, Frame *frame, int *tcpSendStatus)
{
	RTPSessionNodePtr	pNode = NULL;
	RTPSession			*pSession = NULL;
	RTPPacketHeader		rtpHeader;
	int byteSent = 0, ACbyteSent = 0;
	char	sendbuf[MAX_RTP_PACKET_SIZE];
	int rval = 0;
	int tmpTcpStatus = 0;

	SK_ASSERT(track);
	SK_ASSERT(frame);

	memset(&rtpHeader, 0x0, sizeof(RTPPacketHeader));

#if 0 //[[ baek_0160303_BEGIN -- test code
	FILE	*fp = NULL;

	fp = fopen("dump.aac", "a+");
	fwrite(frame->pData, 1, frame->size, fp);
	fclose(fp);	
#endif //]] baek_0160303_END -- test code

	/* send frame to all destination */
	LOCK_MTX(&track->rtpSessionList.rtpSessionListMutex);
	do {
		pNode = NextRTPSessionNode(&track->rtpSessionList, pNode);
		if( pNode ){
			pSession = &pNode->session;
			if(pSession->state == RTP_STATE_WAIT_FOR_KEY_FRAME){
				/* If Key frame, set state to PLAY */				
				pSession->state = RTP_STATE_PLAY;
			}
			
			if( pSession->state == RTP_STATE_PLAY ){
				unsigned int timestamp = 0;
				RTPPacketDesc		RtpDesc;

				memset(sendbuf, 0x0, sizeof(char)*MAX_RTP_PACKET_SIZE);
				sendbuf[0] = 0x80; // RTP version, extenstion, cc
				sendbuf[1] = 0x61|0x80; // RTP marker, pt
				sendbuf[2] = (char)(pSession->seqNum>>8);
				sendbuf[3] = (char)(pSession->seqNum&0x00FF);
				timestamp = pSession->timestampBase + _convert_timestamp(frame, track->timeResolution);
				sendbuf[4] = (timestamp>>24)&0xFF;
				sendbuf[5] = (timestamp>>16)&0xFF;
				sendbuf[6] = (timestamp>>8)&0xFF;
				sendbuf[7] = timestamp&0xFF;
				sendbuf[8] = (pSession->ssrc>>24)&0xFF;
				sendbuf[9] = (pSession->ssrc>>16)&0xFF;
				sendbuf[10] = (pSession->ssrc>>8)&0xFF;
				sendbuf[11] = pSession->ssrc&0xFF;

				//DBGMSG("frame->size[%u] pts[%u]", frame->size, timestamp);

				if( frame->size <= MAX_FU_PAYLOAD_SIZE ){
					unsigned char *pStart = NULL;
					unsigned int dataSize = 0;
					
					/* Jump start code */
					pStart = frame->pData;
					dataSize = frame->size;
					memcpy(&sendbuf[12], pStart, dataSize);
					
					switch(pSession->mode){
						case RTP_UDP:
							byteSent = sendto(pSession->rtpSocket, (char*)sendbuf, dataSize+12, 0, (struct sockaddr*)&pSession->rtpAddr, sizeof(struct sockaddr));
							if(byteSent != (int)dataSize+12) {
								ERRMSG("sendto fail ret[%d] err[%d](%s)", byteSent, errno, strerror(errno));
							}
							else{
								ACbyteSent += byteSent;
								//DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d] ts[%u] seq[%u]", pSession->strId, ACbyteSent, byteSent, pSession->timestamp, pSession->seqNum);
							}
							break;
						case RTP_RTSP_TCP:
							memset(&RtpDesc, 0x0, sizeof(RTPPacketDesc));
							RtpDesc.pStartAddr = sendbuf;
							RtpDesc.Len = dataSize+12;
							RtpDesc.Marker = sendbuf[1];
							rval = pushTCPDataToChannel(pSession->rtspSession->ClientConn, &RtpDesc, pSession->rtpChannel, &tmpTcpStatus);
							if( rval == SMS_RET_OK ){
								byteSent = dataSize+12;
								ACbyteSent += byteSent;
								//DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d] ts[%u] seq[%u]", pSession->strId, ACbyteSent, byteSent, pSession->timestamp, pSession->seqNum);
								if( *tcpSendStatus < tmpTcpStatus ){
									*tcpSendStatus = tmpTcpStatus;
								}
							}
							else{
								pSession->state = RTP_STATE_TO_BE_REMOVED;
								ERRMSG("data push fail! rval[%d]", rval);
							}							
							break;
					}
					pSession->seqNum++;
				}
				else{
					ERRMSG(">>> PCM frame size[%u] exeed! MAX_FU_PAYLOAD_SIZE[%u]", frame->size, MAX_FU_PAYLOAD_SIZE);
				}
			}
		}
	} while(pNode);
	UNLOCK_MTX(&track->rtpSessionList.rtpSessionListMutex);

	return ACbyteSent;
}

char* getPCMSDPFunc(Track *track)
{
	char *rSdp = NULL;

	SK_ASSERT(track);

	rSdp = track->sdp;

	return rSdp;
}

const char sdpPrefixFmt[] =
    "m=%s %u RTP/AVP %d\r\n"
    "c=IN IP4 %s\r\n"
    "b=AS:%u\r\n"
    "a=rtpmap:%d %s/%d%s\r\n"
    "%s"
    "a=range:clock=%s-\r\n"
    "%s"
    "a=control:track%d\r\n";
const char sdpMediaType[] = "audio";
const char sdpIpAddress[] = "192.168.10.102";
const char sdpRtpPayloadFormatName[] = "L16";
const char sdpEncodingParamsPart[] = "";
const char sdpRtcpmuxLine[] = "";
const char sdpRangeLine[] = ""; 
const char sdpAuxSDPLine[] = "a=fmtp:96 streamtype=5; profile-level-id=16; mode=PCM-hbr; config=11B0; sizeLength=13; indexLength=3; indexDeltaLength=3; constantDuration=1024\r\n";

int setPCMSDPFunc(Track *track, MediaInfo *pInfo)
{
	unsigned int sdpLength = 0;	
	char sdpRtpPayloadType = 97;
	int	sdpRtpTimestampFrequency = 8000;

	SK_ASSERT(track);

	if( track->sdp ){
		/* If sdp is exist, return it */
	}
	else{		
		sdpLength += strlen(sdpPrefixFmt);
		sdpLength += strlen(sdpMediaType) + 5 /* max short len */ + 3 /* max char len */;
		sdpLength += strlen(sdpIpAddress);
		sdpLength += 20; /* max int len */
		sdpLength += 3 /* max char len */ + strlen(sdpRtpPayloadFormatName) + 20 /* max int len */+ strlen(sdpEncodingParamsPart);
		sdpLength += strlen(sdpRtcpmuxLine);
		sdpLength += strlen(sdpRangeLine);
		sdpLength += strlen(sdpAuxSDPLine);
		sdpLength += strlen("track000");
        sdpLength += 1024;

		track->sdp = (char *)malloc(sdpLength);
		if(track->sdp){
			memset(track->sdp, 0x0, sdpLength);
			snprintf(track->sdp, sdpLength-1, sdpPrefixFmt,
				sdpMediaType, // m= <media>
				0, // m= <port>
				sdpRtpPayloadType, // m= <fmt list>
				sdpIpAddress, // c= address
				0, // b=AS:<bandwidth>
				sdpRtpPayloadType, sdpRtpPayloadFormatName, sdpRtpTimestampFrequency, sdpEncodingParamsPart,// a=rtpmap:... (if present)
				sdpRtcpmuxLine, // a=rtcp-mux:... (if present)
				sdpRangeLine, // a=range:... (if present)
				sdpAuxSDPLine, // optional extra SDP line
				track->Id); // a=control:<track-id>
		}
		else{
			ERRMSG("malloc fai1 len[%d]", sdpLength);
		}
	}

	return 1;
}

