
/*
 * The purpose of this application is to non-interactively provide Level I Market
 * Price and Level 2 Market By Order data to an Advanced Data Hub (ADH). It is a 
 * single-threaded client application. First the application initializes the RSSL transport 
 * and connects the client. After that, it attempts to load the dictionary from a file. 
 * The application sends a login request like a consumer to the ADH.
 * A source directory refresh message is published to the ADH without any request for it.
 * If the dictionary could not be loaded from a file and the login response from ADH 
 * indicates that the ADH does support the Provider Dictionary Download feature
 * the application will send dictionary request like a consumer and proccess the 
 * dictionary refresh message.
 * If the ADH login response indicates no support for the Provider Dictionary Download feature
 * the application will exit.
 * A market price and/or market by order refresh/update messages are published
 * to the ADH without any request for them.
 *
 * If the dictionary is found in the directory of execution, then it is loaded
 * directly from the file. However, the default configuration for this application
 * is to request the dictionary from the ADH. Hence, no link to the dictionary
 * is made in the execution directory by the build script. The user can change this
 * behavior by manually creating a link to the dictionary in the execution directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <unistd.h>
#endif

#include "rsslNIProvider.h"
#include "rsslLoginConsumer.h"
#include "rsslDirectoryHandler.h"
#include "rsslDictionaryProvider.h"
#include "rsslItemHandler.h"
#include "rsslSendMessage.h"


static fd_set	readfds;
static fd_set	exceptfds;
fd_set	wrtfds; /* used by sendMessage() */
RsslError error;
RsslServer *rsslSrvr;
static RsslChannel *rsslNIProviderChannel = 0;
static char infraHostname[128];
static char infraPortNo[128];
static char serviceName[128];
static char interfaceName[128];
static char sendAddr[128];
static char recvAddr[128];
static char sendPort[128];
static char recvPort[128];
static char unicastPort[128];
static RsslBool enableHostStatMessages;
static char hsmAddress[128];
static char hsmPort[128];
static char hsmInterface[128];
static char traceOutputFile[128];
static char hsmInterval = 5;
static RsslConnectionTypes connType = RSSL_CONN_TYPE_SOCKET;
static RsslUInt32 pingTimeout;
static time_t nextReceivePingTime = 0;
static RsslBool receivedInfraMsg = RSSL_FALSE;
static RsslInt32 timeToRun = 1200;
static time_t rsslNIProviderRuntime = 0;
static RsslBool shouldRecoverConnection = RSSL_TRUE;
static RsslBool isInLoginSuspectState = RSSL_TRUE;
static RsslBool sAddr = RSSL_FALSE;
static RsslBool rAddr = RSSL_FALSE;

static RsslBool xmlTrace = RSSL_FALSE;

RsslBool showTransportDetails = RSSL_FALSE;

/* default server host name */
static const char *defaultInfraHostname = "localhost";
/* default server port number */
static const char *defaultInfraPortNo = "14003";
/* default service name */
static const char *defaultServiceName = "DIRECT_FEED";
/* default item name */
static const char *defaultItemName = "TRI";
static const char *defaultInterface = "";

static RsslBool directorySent = RSSL_FALSE;

/* 
 * Prints example usage and exits. 
 */
void printUsageAndExit(char *appName)
{

	/* exit and display usage */
	printf("Usage: %s or\n%s [-uname <LoginUsername>] [-s <ServiceName>] [-id <ServiceId>] [-mp <MarketPrice Item Name>] [-mbo <MarketByOrder Item Name>] [-runtime <seconds>] [-td]\n", appName, appName);
	printf(" -mp For each occurance, provides item using Market Price domain.\n");
	printf(" -mbo For each occurance, provides item using Market By Order domain.\n");
	printf(" -id allows user to specify optional serviceId.\n");
	printf("\n -td prints out additional transport details from rsslReadEx() and rsslWriteEx() function calls \n");
	printf(" -x provides an XML trace of messages.\n");
	printf(" -runtime adjusts the running time of the application.\n");
	printf("\nIf using TCP Socket:\n");
	printf("[-h <InfraHostname>] [-p <InfraPortNo>]\n");
	printf("\nIf using Reliable Multicast:\n");
	printf("[-sa <SendAddress>] [-sp <SendPort>]\n");
	printf("[-ra <RecvAddress>] [-rp <RecvPort>]\n");
	printf("[-u <UnicastPort>] [-i <Interface>]\n");
	printf("\nIf using Host Stat Messages on a Reliable Multicast connection:\n");
	printf("[-hsmAddr <Address>] [-hsmPort <Port>] [-hsmInterface <Interface>] [-hsmInterval <Seconds>] \n");

	/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
	printf("\nPress Enter or Return key to exit application:");
	getchar();
#endif
	exit(RSSL_RET_FAILURE);
}

int main(int argc, char **argv)
{
	int i;
	struct timeval time_interval;
	RsslError error;
	fd_set useRead;
	fd_set useExcept;
	fd_set useWrt;
	int selRet;
	char errTxt[256];
	RsslBuffer errorText = {255, (char*)errTxt};
	RsslInProgInfo inProg = RSSL_INIT_IN_PROG_INFO;
	RsslRet	retval = 0;
	RsslUInt64 serviceId = 0;
	int itemNameStartArg = 4;
	static RsslBool itemProvided = RSSL_FALSE;
#ifdef _WIN32
	int rcvBfrSize = 65535;
	int sendBfrSize = 65535;
#endif

	/* Initialize item handler */
	initItemHandler();

	snprintf(infraHostname, 128, "%s", defaultInfraHostname);
	snprintf(infraPortNo, 128, "%s",  defaultInfraPortNo);
	snprintf(serviceName, 128, "%s", defaultServiceName);
	snprintf(interfaceName, 128, "%s", defaultInterface);
	snprintf(sendAddr, 128, "%s", "");
	snprintf(recvAddr, 128, "%s", "");
	snprintf(sendPort, 128, "%s", "");
	snprintf(recvPort, 128, "%s",  "");
	snprintf(unicastPort, 128, "%s", "");
	snprintf(hsmAddress, 128, "%s", "");
	snprintf(hsmPort, 128, "%s", "");
	snprintf(hsmInterface, 128, "%s", "");
	

	setUsername((char *)"");

	if (argc == 1) /* use default operating parameters */
	{
	
		/* add item name in default market price handler */
		addItemName(defaultItemName, RSSL_DMT_MARKET_PRICE);
		itemProvided = RSSL_TRUE;

		printf("\nUsing default operating parameters...\n\n");
		printf("infraHostname: %s\n", infraHostname);
		printf("infraPortNo: %s\n", infraPortNo);
		printf("serviceName: %s\n", serviceName);
		printf("itemName: %s\n", defaultItemName);
	}
	else if (argc > 1) /* all operating parameters entered by user */
	{
		i = 1;

		while(i < argc)
		{
			if(strcmp("-uname", argv[i]) == 0)
			{
				i += 2;
				setUsername(argv[i-1]);
			}
			else if(strcmp("-h", argv[i]) == 0)
			{
				i += 2;
				snprintf(infraHostname, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-p", argv[i]) == 0)
			{
				i += 2;
				snprintf(infraPortNo, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-i", argv[i]) == 0)
			{
				i += 2;
				snprintf(interfaceName, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-u", argv[i]) == 0)
			{
				i += 2;
				snprintf(unicastPort, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-s", argv[i]) == 0)
			{
				i += 2;
				snprintf(serviceName, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-id", argv[i]) == 0)
			{
				i += 2;
				serviceId = atol(argv[i-1]);
				setServiceId(serviceId);
			}
			else if(strcmp("-sa", argv[i]) == 0)
			{
				i += 2;
				snprintf(sendAddr, 128, "%s", argv[i-1]);
				sAddr = RSSL_TRUE;
			}
			else if(strcmp("-ra", argv[i]) == 0)
			{
				i += 2;
				snprintf(recvAddr, 128, "%s", argv[i-1]);
				rAddr = RSSL_TRUE;
			}
			else if(strcmp("-sp", argv[i]) == 0)
			{
				i += 2;
				snprintf(sendPort, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-rp", argv[i]) == 0)
			{
				i += 2;
				snprintf(recvPort, 128, "%s", argv[i-1]);
			}
			else if(strcmp("-hsmAddr", argv[i]) == 0)
			{
				i += 2;
				snprintf(hsmAddress, 128, "%s", argv[i-1]);
				enableHostStatMessages = RSSL_TRUE;
			}
			else if(strcmp("-hsmPort", argv[i]) == 0)
			{
				i += 2;
				snprintf(hsmPort, 128, "%s", argv[i-1]);
				enableHostStatMessages = RSSL_TRUE;
			}
			else if(strcmp("-hsmInterface", argv[i]) == 0)
			{
				i += 2;
				snprintf(hsmInterface, 128, "%s", argv[i-1]);
				enableHostStatMessages = RSSL_TRUE;
			}
			else if(strcmp("-hsmInterval", argv[i]) == 0)
			{
				i += 2;
				hsmInterval = atoi(argv[i-1]);
				enableHostStatMessages = RSSL_TRUE;
			}
			else if(strcmp("-mp", argv[i]) == 0)
			{
				i += 2;
				/* add item name in market price handler */
				addItemName(argv[i-1], RSSL_DMT_MARKET_PRICE);	
				itemProvided = RSSL_TRUE;
				
			}
			else if(strcmp("-mbo", argv[i]) == 0)
			{
				i += 2;
				/* add item name in market by order handler */
				addItemName(argv[i-1], RSSL_DMT_MARKET_BY_ORDER);	
				itemProvided = RSSL_TRUE;
				
			}
			else if(strcmp("-x", argv[i]) == 0)
			{
				i += 1;
				xmlTrace = RSSL_TRUE;
				snprintf(traceOutputFile, 128, "RsslNIProvider\0");
			}
			else if(strcmp("-td", argv[i]) == 0)
			{
				i += 1;
				showTransportDetails = RSSL_TRUE;
			}
			else if(strcmp("-runtime", argv[i]) == 0)
			{
				i += 2;
				timeToRun = atoi(argv[i-1]);
			}
			else
			{
				printf("Error: Unrecognized option: %s\n\n", argv[i]);
				printUsageAndExit(argv[0]);
			}

		}

		printf("\nInput arguments...\n\n");

		if (sAddr || rAddr)
		{	
			connType = RSSL_CONN_TYPE_RELIABLE_MCAST;
			printf("Using Reliable Multicast Connection Type\n");
			printf("sendAddress: %s\n", sendAddr);
			printf("sendPort: %s\n", sendPort);
			printf("recvAddress: %s\n", recvAddr);
			printf("recvPort: %s\n", recvPort);
			printf("unicastPort: %s\n", unicastPort);
			printf("interface: %s\n", interfaceName);

			if (enableHostStatMessages)
			{
				printf("hsmAddress: %s\n", hsmAddress);
				printf("hsmPort: %s\n", hsmPort);
				printf("hsmInterface: %s\n", hsmInterface);
				printf("hsmInterval: %u\n", hsmInterval);
			}
		}
		else
		{
			connType = RSSL_CONN_TYPE_SOCKET;
			printf("Using TCP Socket Connection Type\n");
			printf("infraHostname: %s\n", infraHostname);
			printf("infraPortNo: %s\n", infraPortNo);
		}
			

		printf("serviceName: %s\n", serviceName);
		printf("serviceId: %d\n", serviceId);
	}
	
	if (!itemProvided)
	{
		/* no market price or market by order item was specified, but a command line was provided */
		/* add item name in market price handler */
		addItemName(defaultItemName, RSSL_DMT_MARKET_PRICE);
		itemProvided = RSSL_TRUE;
	}

	/* set service name in directory handler */
	setServiceName(serviceName);

	/* Initialize dictionary provider */
	initDictionaryProvider();

	/* try to load local dictionary */
	if (loadDictionary() != RSSL_RET_SUCCESS)
	{
		/* if no local dictionary found maybe we can request it from ADH */
		printf("\nNo local dictionary found, will try to request it from ADH if it supports the Provider Dictionary Download\n");
	}

	/* Initialize RSSL */
	/* RSSL_LOCK_NONE is used since this is a single threaded application. */
	if (rsslInitialize(RSSL_LOCK_NONE, &error) != RSSL_RET_SUCCESS)
	{
		printf("rsslInitialize(): failed <%s>\n",error.text);
		/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
		printf("\nPress Enter or Return key to exit application:");
		getchar();
#endif
		exit(RSSL_RET_FAILURE);
	}

	FD_ZERO(&readfds);
	FD_ZERO(&exceptfds);
	FD_ZERO(&wrtfds);

	/* Initialize run-time */
	initRuntime();

	/* this is the main loop */
	while(1)
	{
		/* this is the connection recovery loop */
		while(shouldRecoverConnection)
		{
			/* Connect to Infrastructure */
			if ((rsslNIProviderChannel = connectToInfrastructure(connType, &error)) == NULL)
			{
				printf("Unable to connect to RSSL server: <%s>\n",error.text);
			}

			if (rsslNIProviderChannel != NULL && rsslNIProviderChannel->state == RSSL_CH_STATE_ACTIVE)
				shouldRecoverConnection = RSSL_FALSE;

			/* Wait for channel to become active.  This finalizes the three-way handshake. */
			while (rsslNIProviderChannel != NULL && rsslNIProviderChannel->state != RSSL_CH_STATE_ACTIVE)
			{
				if (rsslNIProviderChannel->state == RSSL_CH_STATE_INITIALIZING)
				{
					FD_CLR(rsslNIProviderChannel->socketId,&wrtfds);
					if ((retval = rsslInitChannel(rsslNIProviderChannel, &inProg, &error)) < RSSL_RET_SUCCESS)
					{
						printf("\nchannelInactive fd=%d <%s>\n",
							rsslNIProviderChannel->socketId,error.text);
						recoverConnection();
						break;
					}
					else 
					{
			  			switch (retval)
						{
						case RSSL_RET_CHAN_INIT_IN_PROGRESS:
							if (inProg.flags & RSSL_IP_FD_CHANGE)
							{
								printf("\nChannel In Progress - New FD: %d  Old FD: %d\n",rsslNIProviderChannel->socketId, inProg.oldSocket );

								FD_CLR(inProg.oldSocket,&readfds);
								FD_CLR(inProg.oldSocket,&exceptfds);
								FD_SET(rsslNIProviderChannel->socketId,&readfds);
								FD_SET(rsslNIProviderChannel->socketId,&exceptfds);
								FD_SET(rsslNIProviderChannel->socketId,&wrtfds);
							}
							else
							{
								printf("\nChannel %d In Progress...\n", rsslNIProviderChannel->socketId);
							}
							break;
						case RSSL_RET_SUCCESS:
							{
								RsslChannelInfo chanInfo;

								printf("\nChannel %d Is Active\n" ,rsslNIProviderChannel->socketId);
								/* reset should recover connection flag */
								shouldRecoverConnection = RSSL_FALSE;
								/* if device we connect to supports connected component versioning, 
								 * also display the product version of what this connection is to */
								if ((retval = rsslGetChannelInfo(rsslNIProviderChannel, &chanInfo, &error)) >= RSSL_RET_SUCCESS)
								{
									RsslUInt32 i;
									for (i = 0; i < chanInfo.componentInfoCount; i++)
									{
										printf("Connected to %s device.\n", chanInfo.componentInfo[i]->componentVersion.data);
									}
								}
							}
							break;
						default:
							printf("\nBad return value fd=%d <%s>\n",
								   rsslNIProviderChannel->socketId,error.text);
							cleanUpAndExit();
							break;
						}
					}
				}

				/* sleep before trying again */
#ifdef _WIN32
				Sleep(1000);
#else
				sleep(1);
#endif

				handleRuntime();
			}

			/* sleep before trying again */
			if (shouldRecoverConnection)
			{
				for(i = 0; i < NI_PROVIDER_CONNECTION_RETRY_TIME; ++i)
				{
#ifdef _WIN32
					Sleep(1000);
#else
					sleep(1);
#endif
					handleRuntime();

				}

			}

		}

		/* WINDOWS: change size of send/receive buffer since it's small by default */
#ifdef _WIN32
		if (rsslIoctl(rsslNIProviderChannel, RSSL_SYSTEM_WRITE_BUFFERS, &sendBfrSize, &error) != RSSL_RET_SUCCESS)
		{
			printf("rsslIoctl(): failed <%s>\n", error.text);
		}
		if (rsslIoctl(rsslNIProviderChannel, RSSL_SYSTEM_READ_BUFFERS, &rcvBfrSize, &error) != RSSL_RET_SUCCESS)
		{
			printf("rsslIoctl(): failed <%s>\n", error.text);
		}
#endif

		/* Initialize ping handler */
		initPingHandler(rsslNIProviderChannel);

		/* Send login request message */
		if (sendLoginRequest(rsslNIProviderChannel, "rsslNIProvider", RSSL_PROVIDER, &loginSuccessCallBack) != RSSL_RET_SUCCESS)
		{
			cleanUpAndExit();
		}



		/* this is the message processing loop */
		while(1)
		{
			useRead = readfds;
			useExcept = exceptfds;
			useWrt = wrtfds;
			time_interval.tv_sec = UPDATE_INTERVAL;
			time_interval.tv_usec = 0;

			/* Call select() to check for any messages */
			selRet = select(FD_SETSIZE,&useRead,
				&useWrt,&useExcept,&time_interval);

			if (selRet <= 0) /* no messages received, send updates and continue */
			{
				/* Send item updates */
				if (!isInLoginSuspectState && isDictionaryReady())
				{
					updateItemInfo();
					
					if (sendItemUpdates(rsslNIProviderChannel) != RSSL_RET_SUCCESS)
						recoverConnection();
				}

				/* continue */
	#ifdef _WIN32
				if (WSAGetLastError() == WSAEINTR)
					continue;
	#else
				if (errno == EINTR)
				{
					continue;
				}
	#endif
			}
			else if (selRet > 0) /* messages received, read from channel */
			{
				if ((rsslNIProviderChannel != NULL) && (rsslNIProviderChannel->socketId != -1))
				{
					if ((FD_ISSET(rsslNIProviderChannel->socketId, &useRead)) ||
						(FD_ISSET(rsslNIProviderChannel->socketId, &useExcept)))
					{
						if (readFromChannel(rsslNIProviderChannel) != RSSL_RET_SUCCESS)
							recoverConnection();
					}

					/* flush for write file descriptor and active state */
					if (rsslNIProviderChannel != NULL &&
						FD_ISSET(rsslNIProviderChannel->socketId, &useWrt) &&
						rsslNIProviderChannel->state == RSSL_CH_STATE_ACTIVE)
					{
						if ((retval = rsslFlush(rsslNIProviderChannel, &error)) < RSSL_RET_SUCCESS)
						{
							printf("rsslFlush() failed with return code %d - <%s>\n", retval, error.text);
						}
						else if (retval == RSSL_RET_SUCCESS)
						{
							/* clear write fd */
							FD_CLR(rsslNIProviderChannel->socketId, &wrtfds);
						}
					}
				}
			}

			/* break out of message processing loop if should recover connection */
			if (shouldRecoverConnection == RSSL_TRUE)
			{
				break;
			}

			/* Handle pings */
			handlePings();

			/* Handle run-time */
			handleRuntime();
		}
	}
}

/*
 * Reads from a channel.
 * chnl - The channel to be read from
 */
static RsslRet readFromChannel(RsslChannel* chnl)
{
	RsslError		error;
	RsslBuffer *msgBuf=0;
	RsslRet	readret;

	if(chnl->socketId != -1 && chnl->state == RSSL_CH_STATE_ACTIVE)
	{
		readret = 1;
		while (readret > 0) /* read until no more to read */
		{
			if ((msgBuf = rsslRead(chnl,&readret,&error)) != 0)
			{
				if (processResponse(chnl, msgBuf) != RSSL_RET_SUCCESS)
					return RSSL_RET_FAILURE;

				/* set flag for infrastructure message received */
				receivedInfraMsg = RSSL_TRUE;
			}
			else
			{
				switch (readret)
				{
					case RSSL_RET_CONGESTION_DETECTED:
					case RSSL_RET_SLOW_READER:
					case RSSL_RET_PACKET_GAP_DETECTED:
						if (chnl->state != RSSL_CH_STATE_CLOSED)
						{
							/* disconnectOnGaps must be false.  Connection is not closed */
							printf("\nRead Error: %s <%d>\n", error.text, readret);
							/* break out of switch */
							break;
						}
						/* if channel is closed, we want to fall through */
					case RSSL_RET_FAILURE:
					{
						printf("\nchannelInactive fd=%d <%s>\n",
					    chnl->socketId,error.text);
						recoverConnection();
					}
					break;
					case RSSL_RET_READ_FD_CHANGE:
					{
						printf("\nrsslRead() FD Change - Old FD: %d New FD: %d\n", chnl->oldSocketId, chnl->socketId);
						FD_CLR(chnl->oldSocketId, &readfds);
						FD_CLR(chnl->oldSocketId, &exceptfds);
						FD_SET(chnl->socketId, &readfds);
						FD_SET(chnl->socketId, &exceptfds);
					}
					break;
					case RSSL_RET_READ_PING: 
					{
						/* set flag for infrastructure message received */
						receivedInfraMsg = RSSL_TRUE;
					}
					break;
					default:
						if (readret < 0 && readret != RSSL_RET_READ_WOULD_BLOCK)
						{
							printf("\nRead Error: %s <%d>\n", error.text, readret);
						
						}
					break;
				}
			}
		}
	}
	else if (chnl->state == RSSL_CH_STATE_CLOSED)
	{
		printf("Channel fd=%d Closed.\n", chnl->socketId);
		recoverConnection();
	}

	return RSSL_RET_SUCCESS;
}

/*
 * Connects to the infrastructure and returns the channel to the caller.
 * hostname - The hostname of the infrastructure to connect to
 * portno - The port number of the infrastructure to connect to
 * error - The error information in case of failure
 */
static RsslChannel* connectToInfrastructure(RsslConnectionTypes connType,  RsslError* error)
{
	RsslChannel* chnl;
	RsslConnectOptions copts = RSSL_INIT_CONNECT_OPTS;		

	copts.guaranteedOutputBuffers = 500;
	if (sAddr || rAddr)
	{
		printf("\nAttempting segmented connect to server %s:%s  %s:%s unicastPort %s...\n", sendAddr, sendPort, recvAddr, recvPort, unicastPort);
		copts.connectionInfo.segmented.recvAddress = recvAddr;
		copts.connectionInfo.segmented.recvServiceName = recvPort;
		copts.connectionInfo.segmented.sendAddress = sendAddr;
		copts.connectionInfo.segmented.sendServiceName = sendPort;
		copts.connectionInfo.segmented.interfaceName = interfaceName;
		copts.connectionInfo.unified.unicastServiceName = unicastPort;
		
	}
	else
	{
		printf("\nAttempting to connect to server %s:%s...\n", infraHostname, infraPortNo);
		copts.connectionInfo.unified.address = infraHostname;
		copts.connectionInfo.unified.serviceName = infraPortNo;
		copts.connectionInfo.unified.interfaceName = interfaceName;
	}

	copts.majorVersion = RSSL_RWF_MAJOR_VERSION;
	copts.minorVersion = RSSL_RWF_MINOR_VERSION;
	copts.protocolType = RSSL_RWF_PROTOCOL_TYPE;
	copts.connectionType = connType;

	if (connType == RSSL_CONN_TYPE_RELIABLE_MCAST && enableHostStatMessages)
	{
		/* Enable publishing Host Stat Messages. */
		copts.multicastOpts.hsmMultAddress = hsmAddress;
		copts.multicastOpts.hsmPort = hsmPort;
		copts.multicastOpts.hsmInterface = hsmInterface;
		copts.multicastOpts.hsmInterval = hsmInterval;
	}

	if ( (chnl = rsslConnect(&copts,error)) != 0)
	{
		directorySent = RSSL_FALSE;
		FD_SET(chnl->socketId,&readfds);
		FD_SET(chnl->socketId,&exceptfds);
		printf("\nChannel IPC descriptor = %d\n", chnl->socketId);
		if (xmlTrace) 
		{
        	RsslTraceOptions traceOptions;

        	rsslClearTraceOptions(&traceOptions);
        	traceOptions.traceMsgFileName = traceOutputFile;
        	traceOptions.traceMsgMaxFileSize = 10000000;
        	traceOptions.traceFlags |= RSSL_TRACE_TO_FILE_ENABLE | RSSL_TRACE_TO_MULTIPLE_FILES | RSSL_TRACE_TO_STDOUT | RSSL_TRACE_READ | RSSL_TRACE_WRITE;
        	rsslIoctl(chnl, (RsslIoctlCodes)RSSL_TRACE, (void *)&traceOptions, error);
		}	
		if (!copts.blocking)
		{	
			if (!FD_ISSET(chnl->socketId,&wrtfds))
				FD_SET(chnl->socketId,&wrtfds);
		}
	}

	return chnl;
}

/*
 * Initializes the ping time for a channel.
 * chnl - The channel to initialize ping time for
 */
static void initPingHandler(RsslChannel* chnl)
{
	time_t currentTime;

	assert(chnl && chnl->state == RSSL_CH_STATE_ACTIVE);

	/* get current time */
	time(&currentTime);

	pingTimeout = chnl->pingTimeout;

	/* set time rsslNIProvider should receive next message/ping from infrastructure */
	nextReceivePingTime = currentTime + (time_t)pingTimeout;
}

/*
 * Initializes the run-time for the rsslNIProvider.
 */
static void initRuntime()
{
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);
	
	rsslNIProviderRuntime = currentTime + (time_t)timeToRun;
}

/*
 * Handles the ping processing.  Checks if a ping has been received
 * from the infrastructure within the next receive ping time.
 */
static void handlePings()
{
	time_t currentTime = 0;

	/* get current time */
	time(&currentTime);

	if ((rsslNIProviderChannel != NULL) && (rsslNIProviderChannel->socketId != -1))
	{
		/* handle infrastructure pings */
		if (currentTime >= nextReceivePingTime)
		{
			/* check if received message from infrastructure since last time */
			if (receivedInfraMsg)
			{
				/* reset flag for infrastructure message received */
				receivedInfraMsg = RSSL_FALSE;

				/* set time should receive next message/ping from infrastructure */
				nextReceivePingTime = currentTime + (time_t)pingTimeout;
			}
			else /* lost contact with infrastructure */
			{
				printf("\nLost contact with infrastructure fd=%d\n", rsslNIProviderChannel->socketId);
				cleanUpAndExit();
			}
		}
	}
}

/*
 * Handles the run-time for the rsslNIProvider.  Sends close status
 * messages to all streams on all channels after run-time has elapsed.
 */
static void handleRuntime()
{
	time_t currentTime = 0;
	RsslRet	retval = 0;
	RsslError error;

	/* get current time */
	time(&currentTime);

	if (currentTime >= rsslNIProviderRuntime)
	{
		/* send close status messages to all streams */
		if ((rsslNIProviderChannel != NULL) && (rsslNIProviderChannel->socketId != -1))
		{
			/* send close status messages to all item streams */
			sendItemCloseStatusMsgs(rsslNIProviderChannel);

			/* send close status message to dictionary stream */
			sendDictionaryCloseStatusMsgs(rsslNIProviderChannel);

			/* close login stream */
			/* note that closing login stream will automatically
			   close all other streams at the provider */
			if (closeLoginStream(rsslNIProviderChannel) != RSSL_RET_SUCCESS)
			{
				cleanUpAndExit();
			}

			/* flush before exiting */
			if (FD_ISSET(rsslNIProviderChannel->socketId, &wrtfds))
			{
				retval = 1;
				while (retval > RSSL_RET_SUCCESS)
				{
					retval = rsslFlush(rsslNIProviderChannel, &error);
				}
				if (retval < RSSL_RET_SUCCESS)
				{
					printf("rsslFlush() failed with return code %d - <%s>\n", retval, error.text);
				}
			}
		}

		printf("\nrsslNIProvider run-time expired...\n");
		cleanUpAndExit();
	}
}

/*
 * Processes a response from a channel.  This consists of
 * performing a high level decode of the message and then
 * calling the applicable specific function for further
 * processing.
 * chnl - The channel of the response
 * buffer - The message buffer containing the response
 */
static RsslRet processResponse(RsslChannel* chnl, RsslBuffer* buffer)
{
	RsslRet ret = 0;
	RsslMsg msg = RSSL_INIT_MSG;
	RsslDecodeIterator dIter;
	
	/* clear decode iterator */
	rsslClearDecodeIterator(&dIter);
	
	/* set version info */
	rsslSetDecodeIteratorRWFVersion(&dIter, chnl->majorVersion, chnl->minorVersion);

	rsslSetDecodeIteratorBuffer(&dIter, buffer);

	ret = rsslDecodeMsg(&dIter, &msg);				
	if (ret != RSSL_RET_SUCCESS)
	{
		printf("\nrsslDecodeMsg(): Error %d on SessionData fd=%d  Size %d \n", ret, chnl->socketId, buffer->length);
		return RSSL_RET_FAILURE;
	}

	switch ( msg.msgBase.domainType )
	{
		case RSSL_DMT_LOGIN:
			if (processLoginResponse(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
			{
				if (isLoginStreamClosed())
				{
					return RSSL_RET_FAILURE;
				}
				else if (isLoginStreamClosedRecoverable())
				{
					recoverConnection();
				}
				else if (isLoginStreamSuspect())
				{
					resetRefreshComplete();
					isInLoginSuspectState = RSSL_TRUE;
				}
			}
			else
			{
				if (isInLoginSuspectState)
				{
					isInLoginSuspectState = RSSL_FALSE;
				}
			}
			break;
		case RSSL_DMT_DICTIONARY:
			if (msg.msgBase.msgClass == RSSL_MC_REQUEST)
			{
				if (isDictionaryReady())
				{
					if (processDictionaryRequest(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
					{
						return RSSL_RET_FAILURE;
					}
				}
				else
					return RSSL_RET_FAILURE;
			}
			else
			{
				if (processDictionaryResponse(chnl, &msg, &dIter) != RSSL_RET_SUCCESS)
				{
					return RSSL_RET_FAILURE;
				}
			}
			break;
		default:
			if (sendNotSupportedStatus(chnl, &msg) != RSSL_RET_SUCCESS)
			{
				return RSSL_RET_FAILURE;
			}
			break;
	}

	return RSSL_RET_SUCCESS;
}

/*
 * Removes a channel.
 * chnl - The channel to be removed
 */
static void removeChannel(RsslChannel* chnl)
{
	RsslError error;
	RsslRet ret;

	FD_CLR(chnl->socketId, &readfds);
	FD_CLR(chnl->socketId, &exceptfds);
	if (FD_ISSET(chnl->socketId, &wrtfds))
		FD_CLR(chnl->socketId, &wrtfds);

	directorySent = RSSL_FALSE;
	if ((ret = rsslCloseChannel(chnl, &error)) < RSSL_RET_SUCCESS)
	{
		printf("rsslCloseChannel() failed with return code: %d\n", ret);
	}
}

/*
 * Called upon successful login.  Sends source directory response
 * to the channel.
 * chnl - The channel of the successful login
 */
void loginSuccessCallBack(RsslChannel* chnl)
{

	if (!directorySent)
	{
		directorySent = RSSL_TRUE;
		sendSourceDirectoryResponse(chnl);
	}

	if (!isDictionaryReady())
	{
		/* no local dictionary was loaded, we need to check if we can get it from the provider */
		if (isProviderDictionaryDownloadSupported())
		{
			sendDictionaryRequests(chnl);
			printf("Send Dictionary Request\n");
		}
		else
		{
			/* exit if dictionary cannot be loaded or requested */
			printf("\nDictionary could not be downloaded, the connection does not support Provider Dictionary Download\n");
			cleanUpAndExit();
		}
	}
}

/*
 * Cleans up and exits the application.
 */
void cleanUpAndExit()
{
	/* clean up channel */
	if ((rsslNIProviderChannel != NULL) && (rsslNIProviderChannel->socketId != -1))
	{
		removeChannel(rsslNIProviderChannel);
	}

	/* free memory for dictionary */
	freeDictionary();

	rsslUninitialize();

	/* WINDOWS: wait for user to enter something before exiting  */
#ifdef _WIN32
	printf("\nPress Enter or Return key to exit application:");
	getchar();
#endif
	exit(RSSL_RET_FAILURE);
}

/*
 * Recovers connection in case of connection failure.
 */
void recoverConnection()
{
	/* clean up channel */
	if ((rsslNIProviderChannel != NULL) && (rsslNIProviderChannel->socketId != -1))
	{
		removeChannel(rsslNIProviderChannel);
		rsslNIProviderChannel = NULL;
	}

	/* set connection recovery flag */
	shouldRecoverConnection = RSSL_TRUE;
	isInLoginSuspectState = RSSL_TRUE;

	/* reset referesh complete flag for items */
	resetRefreshComplete();
}

