/*
 * This source code is provided under the Apache 2.0 license and is provided
 * AS IS with no warranty or guarantee of fit for purpose.  See the project's 
 * LICENSE.md for details. 
 * Copyright Thomson Reuters 2015. All rights reserved.
*/

/* channelHandler.h
 * Performs the associated with setting up and using RsslChannels, such as 
 * initializing channels, reading, flushing, and checking ping timeouts. */

#ifndef _CHANNEL_HANDLER_H
#define _CHANNEL_HANDLER_H

#include "getTime.h"

#include "rtr/rsslQueue.h"

#include "rtr/rsslTransport.h"

#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Used by the ChannelHandler structure.  Maintains information about a channel,
 * such as ping time information and whether flushing is being done. */
typedef struct {
	RsslQueueLink		queueLink;				/* Link for ChannelHandler queue. */
	RsslChannel			*pChannel;				/* The RsslChannel associated with this info. */
	void				*pUserSpec;				/* Pointer to user-specified data associated with this channel. */
	RsslBool			needFlush;				/* Whether this channel needs to have data flushed. */
	RsslBool			receivedMsg;			/* Whether a ping or messages have been received since the last ping check. */
	RsslBool			checkPings;				/* Whether ping handling is done for this channel. */
	TimeValue			nextReceivePingTime;	/* Time before which this channel should receive a ping. */
	TimeValue			nextSendPingTime;		/* Time before which a ping should be sent for this channel. */
	RsslQueue			*parentQueue;			/* Pointer back to the list this channel is an element of. */
} ChannelInfo;

/* Clears a ChannelInfo. */
RTR_C_INLINE void clearChannelInfo(ChannelInfo *pInfo)
{
	memset(pInfo, 0, sizeof(ChannelInfo));
}

typedef struct _ChannelHandler ChannelHandler;

typedef RsslRet ChannelActiveCallback(ChannelHandler*, ChannelInfo*);
typedef RsslRet MsgCallback(ChannelHandler*, ChannelInfo*, RsslBuffer*);
typedef void ChannelInactiveCallback(ChannelHandler*, ChannelInfo*, RsslError*);

/* Maintains a list of open RsslChannels and handles transport-related functionality on them
 * such as reading, initializing, flushing, and pings. */
struct _ChannelHandler {
	RsslQueue				activeChannelList;			/* List of channels that are active. */
	RsslQueue				initializingChannelList;	/* List of channels that are initializing. */
	MsgCallback 			*msgCallback;				/* Function to be called when rsslRead() returns a buffer. */
	void					*pUserSpec;					/* Pointer to application-specified data. */
	ChannelActiveCallback	*channelActiveCallback;		/* Function to be called when a channel finishes initializing and becomes active. */
	ChannelInactiveCallback	*channelInactiveCallback;	/* Function to be called when a channel is closed. */
};

/* Requests that the ChannelHandler begin calling rsslFlush() for a channel.  Used when a call to rsslWrite()
 * indicates there is still data to be written to the network. */
RTR_C_INLINE void channelHandlerRequestFlush(ChannelHandler *pHandler, ChannelInfo *pChannelInfo)
{
	pChannelInfo->needFlush = RSSL_TRUE;
}

/* Initializes a ChannelHandler. */
RTR_C_INLINE void initChannelHandler(ChannelHandler *pHandler, 
		ChannelActiveCallback *channelActiveCallback,
		ChannelInactiveCallback *channelInactiveCallback,
		MsgCallback *msgCallback,
		void *pUserSpec)
{
	rsslInitQueue(&pHandler->activeChannelList);
	rsslInitQueue(&pHandler->initializingChannelList);
	pHandler->channelActiveCallback = channelActiveCallback;
	pHandler->channelInactiveCallback = channelInactiveCallback;
	pHandler->msgCallback = msgCallback;
	pHandler->pUserSpec = pUserSpec;
}

/* Cleans up a ChannelHandler. */
void channelHandlerCleanup(ChannelHandler *pHandler);

/* Adds a connected or accepted channel to the ChannelHandler. */
ChannelInfo *channelHandlerAddChannel(ChannelHandler *pHandler, RsslChannel *pChannel, void *pUserSpec, RsslBool checkPings);

/* Closes and removes a channel from the channelHandler. */
void channelHandlerCloseChannel(ChannelHandler *pHandler, ChannelInfo *pChannelInfo, RsslError *pError);

/* Tries to read data from channels until stopTimeNsec is reached(stopTimeNsec should be based on getTimeNano()). 
 * Also initializes any new channels. */
void channelHandlerReadChannels(ChannelHandler *pHandler, TimeValue stopTimeNsec);

/* Try to initialize a single channel. */
RsslRet channelHandlerInitializeChannel(ChannelHandler *pHandler, ChannelInfo *pChannelInfo);

/* Attempts initializtion on the given channel for the specified period of time. */
RsslRet channelHandlerWaitForChannelInit(ChannelHandler *pHandler, ChannelInfo *pChannelInfo,
		RsslUInt32 waitTimeUsec);

/* Read data from a single channel until no more data is available. */
RsslRet channelHandlerReadChannel(ChannelHandler *pHandler, ChannelInfo *pChannelInfo);

/* Write a buffer to a channel. */
RsslRet channelHandlerWriteChannel(ChannelHandler *pHandler, ChannelInfo *pChannelInfo, RsslBuffer *pBuffer, RsslUInt8 writeFlags);

/* Performs ping timeout checks for channels in the given ChannelHandler. */
void channelHandlerCheckPings(ChannelHandler *pHandler);


#ifdef __cplusplus
}
#endif


#endif
