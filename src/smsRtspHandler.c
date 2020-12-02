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

#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "smsRtspSession.h"
#include "smsRtspHandler.h"
#include "sms.h"

//#define DEBUG_SMS_RTSP_HANDLER
#if defined(DEBUG_SMS_RTSP_HANDLER)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-HANDLER] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-HANDLER-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)


unsigned int random32(void) 
{
  /* Return a 32-bit random number.
     Because "our_random()" returns a 31-bit random number, we call it a second
     time, to generate the high bit.
     (Actually, to increase the likelhood of randomness, we take the middle 16 bits of two successive calls to "our_random()")
  */
  long random_1 = random();
  unsigned int random16_1 = (unsigned int)(random_1&0x00FFFF00);

  long random_2 = random();
  unsigned int random16_2 = (unsigned int)(random_2&0x00FFFF00);

  return (random16_1<<8) | (random16_2>>8);
}

unsigned short random16(void) 
{

  long random_1 = random();
  unsigned short random8_1 = (unsigned short)(random_1&0xFF0);

  long random_2 = random();
  unsigned short random8_2 = (unsigned short)(random_2&0xFF0);

  return (random8_1<<4) | (random8_2>>4);
}


char* dateHeader(char* buf) 
{	
	time_t now;

	SK_ASSERT(buf);
	
	now = time(NULL);
	strftime(buf, COMMON_STR_LEN-1, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&now));

	return buf;
}


int setRTSPResponse(struct _ClientConnection* conn, const char* responseStr, const char* contentStr)
{
	int rval = SMS_RET_ERR_INTERNAL;
	char tmpbuf[COMMON_STR_LEN];
	char contentBuf[COMMON_STR_LEN*2];

	memset(contentBuf, 0x0, sizeof(char)*COMMON_STR_LEN*2);
	if(contentStr){
		unsigned int contentLen = strlen(contentStr);
		if( contentLen > 0 ){
			snprintf(contentBuf, COMMON_STR_LEN*2-1, "Content-Length: %d\r\n\r\n%s", contentLen, contentStr);
		}
	}
	conn->sendBufLen = snprintf(conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1, "RTSP/1.0 %s\r\n" "CSeq: %d\r\n" "%s" "%s" "\r\n", responseStr, conn->CSeq, dateHeader(tmpbuf), contentBuf);

	rval = SMS_RET_OK;

	return rval;  
}

void handleCmd_sessionNotFound(struct _ClientConnection* conn)
{
	setRTSPResponse(conn, "454 Session Not Found", NULL);
}

int handleCmd_OPTIONS(struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;
	char datestr[128];
	time_t now;
	
	DBGMSG();
	now = time(NULL);
	memset(datestr, 0x0, sizeof(char)*128);
	strftime(datestr, sizeof(char)*128, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&now));
	snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,	"RTSP/1.0 200 OK\r\nCSeq: %d\r\n%sPublic: %s\r\n\r\n",
		conn->CSeq, datestr, "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");
	
	conn->sendBufLen = strlen(conn->sendBuf);
	DBGMSG("sendBufLen[%u] buf[%s]", conn->sendBufLen, conn->sendBuf);
//    send(conn->clientSocket, conn->sendBuf, strlen(conn->sendBuf), 0);

	return rval;
}

int handleCmd_GET_PARAMETER(RtspServer *server, struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;
	
	setRTSPResponse(conn, "200 OK", "SMS");

	return rval;
}

int authenticationOK(char const* cmdName, char const* urlSuffix, char const* fullRequestStr)
{
	int rval = SMS_RET_ERR_INTERNAL;

	rval = SMS_RET_OK;
	
	return rval;
}

int handleCmd_notFound(struct _ClientConnection* conn) 
{
	int rval = 0;

//	setRTSPResponse(conn, "404 Stream Not Found", NULL);
  
	return rval;
}

int handleCmd_DESCRIBE(struct _ClientConnection* conn)
{
	char* sdpDescription = NULL;
	char rtspURLBuf[COMMON_STR_LEN];  
	char urlTotalSuffix[COMMON_STR_LEN];
	Stream	*stream = NULL;
	unsigned int sdpDescriptionSize = 0;
	int rval = SMS_RET_ERR_INTERNAL;

	//enough space for urlPreSuffix/urlSuffix'\0'
	urlTotalSuffix[0] = '\0';
    if (conn->urlPreSuffix[0] != '\0') {
      strcat(urlTotalSuffix, conn->urlPreSuffix);
      strcat(urlTotalSuffix, "/");
    }
    strcat(urlTotalSuffix, conn->urlSuffix);

	if (!authenticationOK("DESCRIBE", urlTotalSuffix, conn->pMsg)){
		ERRMSG("Authentication FAIL");
	}
	else{		
		// We should really check that the request contains an "Accept:" #####
		// for "application/sdp", because that's what we're sending back #####
		
		// Begin by looking up the "ServerMediaSession" object for the specified "urlTotalSuffix":
		stream = LookupStreamByName(urlTotalSuffix);
		if (stream == NULL) {
		  handleCmd_notFound(conn);
		}
		else{
			char dateTimeBuf[128];
			
			// Increment the "ServerMediaSession" object's reference count, in case someone removes it
			// while we're using it:
			//session->incrementReferenceCount();
			
			// Then, assemble a SDP description for this session:
			sdpDescription = stream->getSDP(stream);
			if (sdpDescription == NULL) {
				// This usually means that a file name that was specified for a
				// "ServerMediaSubsession" does not exist.
				setRTSPResponse(conn, "404 File Not Found, Or In Incorrect Format", NULL);
			}
			else{
				sdpDescriptionSize = strlen(sdpDescription);
				DBGMSG("size[%d]\r\n%s", sdpDescriptionSize, sdpDescription);

				// Also, generate our RTSP URL, for the "Content-Base:" header
				// (which is necessary to ensure that the correct URL gets used in subsequent "SETUP" requests).
				memset(rtspURLBuf, 0x0, sizeof(char)*COMMON_STR_LEN);
				rtspStreamURL(stream, conn->clientSocket, rtspURLBuf);

				snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,
					"RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
					"%s"
					"Content-Base: %s/\r\n"
					"Content-Type: application/sdp\r\n"
					"Content-Length: %d\r\n\r\n"
					"%s",
					conn->CSeq,
					dateHeader(dateTimeBuf),
					rtspURLBuf,
					sdpDescriptionSize,
					sdpDescription);

				conn->sendBufLen = strlen(conn->sendBuf);
			}
		}
	}

	return rval;
}

static void initRTSPConfig(RTSPConfiguration *config, struct _ClientConnection* conn)
{
	SK_ASSERT(conn);
	SK_ASSERT(config);
	
	memset(config, 0x0, sizeof(RTSPConfiguration));
	config->mode = RTP_UDP;
	snprintf(config->destAddrStr, IPADDR_STR_LEN, "%s", inet_ntoa(conn->clientAddr.sin_addr));
	config->ttl = 255;
	config->rtpClientPort = 0;
	config->rtcpClientPort = 1;
	config->rtpChannel = 0xFF;
	config->rtcpChannel = 0xFF;	
}

static void parseTransportHeader(const char* buf, RTSPConfiguration *config)
{
	unsigned int p1=0, p2=0;
	unsigned int ttl=0, rtpCid=0, rtcpCid=0;
	const char *fields = NULL;
	char *field = NULL;

	// First, find "Transport:"
	while (1) {
		if (*buf == '\0'){
			return; // not found
		}
		if (*buf == '\r' && *(buf + 1) == '\n' && *(buf + 2) == '\r'){
			return; // end of the headers => not found
		}
		if (strncasecmp(buf, "Transport:", 10) == 0){
			break;
		}
		++buf;
	}

	// Then, run through each of the fields, looking for ones we handle:
	fields = buf + 10;
	while (*fields == ' '){
		++fields;
	}

	field = strdup(fields);
	while (sscanf(fields, "%[^;\r\n]", field) == 1) {
		if (strcmp(field, "RTP/AVP/TCP") == 0) {
			config->mode = RTP_RTSP_TCP;
			DBGMSG("config->mode = RTP_RTSP_TCP");
		}
		else if (strcmp(field, "RAW/RAW/UDP") == 0 || strcmp(field, "MP2T/H2221/UDP") == 0) {
			config->mode = RAW_UDP;
			DBGMSG("config->mode = RAW_UDP");
		}
		else if (strncasecmp(field, "destination=", 12) == 0) {
			snprintf(config->destAddrStr, IPADDR_STR_LEN-1, "%s", field + 12);
			DBGMSG("config->destAddrStr[%s]", config->destAddrStr);			
		//         config->destAddrStr = strdup(field + 12);
		}
		else if (sscanf(field, "ttl%u", &ttl) == 1) {
			config->ttl = ttl;
			DBGMSG("ttl[%d]", ttl);			
		}
		else if (sscanf(field, "client_port=%u-%u", &p1, &p2) == 2) {
			config->rtpClientPort = p1;
			config->rtcpClientPort = config->mode == RAW_UDP ? 0 : p2; // ignore the second port number if the client asked for raw UDP
			DBGMSG("config->rtpClientPort[%u] config->rtcpClientPort[%u]", config->rtpClientPort, config->rtcpClientPort);
		}
		else if (sscanf(field, "client_port=%u", &p1) == 1) {
			config->rtpClientPort = p1;
			config->rtcpClientPort = config->mode == RAW_UDP ? 0 : p1 + 1;
			DBGMSG("rtpClientPort[%u] rtcpClientPort[%u]", config->rtpClientPort, config->rtcpClientPort);
		}
		else if (sscanf(field, "interleaved=%u-%u", &rtpCid, &rtcpCid) == 2) {
			config->rtpChannel = (unsigned char)rtpCid;
			config->rtcpChannel = (unsigned char)rtcpCid;
			DBGMSG("config->rtpChannel[%u] rtcpChannel[%u]", config->rtpChannel, config->rtcpChannel);
		}

		fields += strlen(field);
		while (*fields == ';' || *fields == ' ' || *fields == '\t'){
			++fields; // skip over separating ';' chars or whitespace
		}
		if (*fields == '\0' || *fields == '\r' || *fields == '\n'){
			break;
		}
	}
	free(field);
}


int handleCmd_SETUP(RtspServer *server, struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;
	char urlTotalSuffix[COMMON_STR_LEN];
	RTSPSession	*session = NULL;
	RTSPConfiguration config;
	StreamingMode streamingMode;
	char *streamingModeString = NULL; // set when RAW_UDP streaming is specified
	char *clientsDestinationAddressStr = NULL;
	char clientsDestinationTTL = 0;
	unsigned char rtpChannelId=0, rtcpChannelId=0;
	char tmpbuf[COMMON_STR_LEN];
	char timeoutParameterString[COMMON_STR_LEN];
	Track* track = NULL;

	if( conn->sessionIdStr[0] == 0 ){
		/* If request does not include session ID, create new RTSP session */
		//enough space for urlPreSuffix/urlSuffix'\0'
		urlTotalSuffix[0] = '\0';
		if (conn->urlPreSuffix[0] != '\0') {
			strcat(urlTotalSuffix, conn->urlPreSuffix);
			strcat(urlTotalSuffix, "/");
		}
		strcat(urlTotalSuffix, conn->urlSuffix);
		DBGMSG("urlTotalSuffix[%s]", urlTotalSuffix);

		if (!authenticationOK("SETUP", urlTotalSuffix, conn->pMsg)){
			ERRMSG("Authentication FAIL");
		}	
		else{
			/* new RTSP session */
			session = CreateRTSPSession(server, conn);
			SK_ASSERT(session);
			snprintf(conn->sessionIdStr, MAX_RTSP_SESSION_ID_LEN-1, "%s", session->strId);
		}
	}
	else{
		session = LookUpRTSPSession(server, conn->sessionIdStr);
		if( session == NULL ){
			ERRMSG("SMS_ERR_SESSION_NOT_FOUND sessionId[%s]", conn->sessionIdStr);
			handleCmd_sessionNotFound(conn);
			return rval;
		}
	}
	
	/* parse request */
	
	// Initialize the result parameters to default values:
	initRTSPConfig(&config, conn);
	parseTransportHeader(conn->pMsg, &config);

	/* make config */	
	track = LookupTrackByName(conn->urlSuffix);
	if( track == NULL ){		
		//baek.debug
		Stream	*pStream = NULL;
		
		pStream = LookupStreamByName(urlTotalSuffix);
		if( NULL == pStream ) {
			ERRMSG("track == NULL. conn->urlSuffix(%s)", conn->urlSuffix);		
			rval = SMS_ERR_TRACK_NOT_FOUND;
			return rval;
		}
		else
		{
			track = GetFirstTrackFromStream(pStream);
			if( NULL == track ){
				ERRMSG("first track == NULL. conn->urlSuffix(%s)", conn->urlSuffix);
				rval = SMS_ERR_TRACK_NOT_FOUND;
				return rval;
			}			
			else {
				config.trackId = track->Id;
				DBGMSG("Found first track. from conn->urlSuffix(%s)", conn->urlSuffix);		
			}
		}
	}
	else{
		config.trackId = track->Id;
	}

	rval = session->handleSETUP(server, conn, session, &config);
	if(rval != SMS_RET_OK	){
		ERRMSG("rval != SMS_RET_OK");		
	}
	else{
		struct sockaddr_in sourceAddr;
		socklen_t namelen = sizeof(struct sockaddr_in);
		getsockname(conn->clientSocket, (struct sockaddr*)&sourceAddr, &namelen);

        memset(timeoutParameterString, 0x0, sizeof(char)*COMMON_STR_LEN);
		snprintf(timeoutParameterString, COMMON_STR_LEN-1, ";timeout=%u", 60);		
		/* OK */
		switch (config.mode) {
			case RTP_UDP:
				snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,
					"RTSP/1.0 200 OK\r\n"
					"CSeq: %d\r\n"
					"%s"
					"Transport: RTP/AVP;unicast;destination=%s;source=%s;client_port=%d-%d;server_port=%d-%d\r\n"
					"Session: %s%s\r\n\r\n",
					conn->CSeq,
					dateHeader(tmpbuf),
					config.destAddrStr, inet_ntoa(sourceAddr.sin_addr), config.rtpClientPort, config.rtcpClientPort, config.rtpServerPort, config.rtcpServerPort, conn->sessionIdStr, timeoutParameterString);
					break;
			case RTP_RTSP_TCP:
				snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,
					"RTSP/1.0 200 OK\r\n"
					"CSeq: %d\r\n"
					"%s"
					"Transport: RTP/AVP/TCP;unicast;destination=%s;source=%s;interleaved=%d-%d\r\n"
					"Session: %s%s\r\n\r\n",
					conn->CSeq,
					dateHeader(tmpbuf),
					config.destAddrStr, inet_ntoa(sourceAddr.sin_addr), config.rtpChannel, config.rtcpChannel,
					conn->sessionIdStr, timeoutParameterString);
				break;
			case RAW_UDP:
				snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,
					"RTSP/1.0 200 OK\r\n"
					"CSeq: %d\r\n"
					"%s"
					"Transport: %s;unicast;destination=%s;source=%s;client_port=%d;server_port=%d\r\n"
					"Session: %s%s\r\n\r\n",
					conn->CSeq,
					dateHeader(tmpbuf),
					"RAW/RAW/UDP", config.destAddrStr, inet_ntoa(sourceAddr.sin_addr), config.rtpClientPort, config.rtpServerPort, conn->sessionIdStr, timeoutParameterString);
				break;
			default:
				break;
		}		
		conn->sendBufLen = strlen(conn->sendBuf);		
	}

	return rval;
}

int handleCmd_PLAY(RtspServer *server, struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;
	RTSPSession	*session = NULL;
	char tmpbuf[COMMON_STR_LEN];
	char scaleHeader[COMMON_STR_LEN];
	char rangeHeader[COMMON_STR_LEN];
	char rtpInfo[COMMON_STR_LEN*2];	

	SK_ASSERT(conn);
	SK_ASSERT(server);
	
	if( conn->sessionIdStr[0] == 0 ){
		ERRMSG("session id not found!");
		rval = SMS_RET_ERR_INVALID_REQUEST;
	}
	else{
		memset(scaleHeader, 0x0, sizeof(char)*128);
		memset(rangeHeader, 0x0, sizeof(char)*128);		
		session = LookUpRTSPSession(server, conn->sessionIdStr);
		if( session ){
			rval = session->handlePLAY(server, conn, session->strId, conn->urlPreSuffix, conn->urlSuffix, rtpInfo);
			if( rval == SMS_ERR_SESSION_NOT_FOUND ){		
				ERRMSG("rval == SMS_ERR_SESSION_NOT_FOUND");
				handleCmd_sessionNotFound(conn);
			}
			else if(rval != SMS_RET_OK	){
				ERRMSG("rval != SMS_RET_OK");
				handleCmd_sessionNotFound(conn);				
			}
			else{
				snprintf((char*)conn->sendBuf, RTSP_CONNECTION_BUFFER_LEN-1,
						 "RTSP/1.0 200 OK\r\n"
						 "CSeq: %d\r\n"
						 "%s"
						 "%s"
						 "%s"
						 "Session: %s\r\n"
						 "%s\r\n",
						 conn->CSeq,
						 dateHeader(tmpbuf),
						 scaleHeader,
						 rangeHeader,
						 session->strId,
						 rtpInfo);
				conn->sendBufLen = strlen(conn->sendBuf);
			}
		}
		else{
			ERRMSG("session[%s] not found", conn->sessionIdStr);
			handleCmd_sessionNotFound(conn);			
			rval = SMS_ERR_SESSION_NOT_FOUND;
		}	
	}

	return rval;
}

void flushSendBuffer(struct _ClientConnection* conn)
{
	if( conn->sendBufLen > 0 ){
		if( conn->sendTCPBuf ){
			pushTCPData(conn, conn->sendBuf, conn->sendBufLen);
		}
		else{
			send(conn->clientSocket, conn->sendBuf, conn->sendBufLen, 0);
		}
		DBGMSG("conn->sendBuf[%s]", conn->sendBuf);
	}
}

int handleCmd_TEARDOWN(RtspServer *server, struct _ClientConnection* conn)
{
	int rval = SMS_RET_ERR_INTERNAL;

	SK_ASSERT(conn);
	SK_ASSERT(server);

	if( conn->sessionIdStr[0] == 0 ){
		ERRMSG("session id not found!");
		rval = SMS_RET_ERR_INVALID_REQUEST;		
	}
	else{
		/* Destory RTP Sessions */
		rval = DestoryRTPSession(server, conn->sessionIdStr);
		if( rval == SMS_ERR_SESSION_NOT_FOUND ){
			handleCmd_sessionNotFound(conn);
		}
		else if(rval != SMS_RET_OK){
			ERRMSG("rval != SMS_RET_OK");
		}
		else{
			/* Destory TCP buffer */
			DestoryTCPBuffer(conn);

			/* Destory RTSP Sessions */ 				
			DestoryRTSPSession(server, conn->sessionIdStr);

			/* Destory RTSP Connection */
			//DestoryRTSPConnection(&server->clientList, conn->clientSocket);
			//close(conn->clientSocket);
		}
	}

	return rval;
}
