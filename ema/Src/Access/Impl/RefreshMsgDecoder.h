/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#ifndef __thomsonreuters_ema_access_RefreshMsgDecoder_h
#define __thomsonreuters_ema_access_RefreshMsgDecoder_h

#include "EmaPool.h"
#include "MsgDecoder.h"
#include "RefreshMsg.h"
#include "EmaStringInt.h"
#include "EmaBufferInt.h"

namespace thomsonreuters {

namespace ema {

namespace access {

class RefreshMsgDecoder : public MsgDecoder
{
public :

	RefreshMsgDecoder();

	~RefreshMsgDecoder();

	void setRsslData( UInt8 , UInt8 , RsslMsg* , const RsslDataDictionary* );

	void setRsslData( UInt8 , UInt8 , RsslBuffer* , const RsslDataDictionary* , void* );

	void setRsslData( RsslDecodeIterator* , RsslBuffer* );

	bool hasMsgKey() const;

	bool hasName() const;

	bool hasNameType() const;

	bool hasServiceId() const;

	bool hasServiceName() const;

	bool hasId() const;

	bool hasFilter() const;

	bool hasAttrib() const;

	bool hasPayload() const;

	bool hasHeader() const;

	Int32 getStreamId() const;

	UInt16 getDomainType() const;

	const EmaString& getName() const;

	UInt8 getNameType() const;

	UInt32 getServiceId() const;

	const EmaString& getServiceName() const;

	Int32 getId() const;

	UInt32 getFilter() const;

	const EmaBuffer& getHeader() const;

	bool hasQos() const;

	bool hasSeqNum() const;

	bool hasPartNum() const;

	bool hasPermissionData() const;

	bool hasPublisherId() const;

	const OmmState& getState() const;

	const OmmQos& getQos() const;

	UInt32 getSeqNum() const;

	UInt16 getPartNum() const;

	const EmaBuffer& getItemGroup() const;

	const EmaBuffer& getPermissionData() const;

	UInt32 getPublisherIdUserId() const;

	UInt32 getPublisherIdUserAddress() const;

	bool getDoNotCache() const;

	bool getSolicited() const;

	bool getComplete() const;

	bool getClearCache() const;

	bool getPrivateStream() const;

	void setServiceName( const char* , UInt32 );

	const EmaBuffer& getHexBuffer() const;

private :

	void setStateInt() const;

	void setQosInt() const;

	RsslMsg								_rsslMsg;

	RsslMsg*							_pRsslMsg;

	mutable EmaStringInt				_name;

	mutable EmaStringInt				_serviceName;

	mutable EmaBufferInt				_extHeader;

	mutable EmaBufferInt				_permission;

	mutable EmaBufferInt				_itemGroup;

	mutable NoDataImpl					_state;

	mutable NoDataImpl					_qos;

	mutable EmaBufferInt				_hexBuffer;

	mutable bool						_serviceNameSet;
	
	mutable bool						_stateSet;

	mutable bool						_qosSet;

	UInt8								_rsslMajVer;

	UInt8								_rsslMinVer;

	mutable OmmError::ErrorCode			_errorCode;
};

class RefreshMsgDecoderPool : public DecoderPool< RefreshMsgDecoder >
{
public :

	RefreshMsgDecoderPool( unsigned int size = 5 ) : DecoderPool< RefreshMsgDecoder >( size ) {};

	~RefreshMsgDecoderPool() {}

private :

	RefreshMsgDecoderPool( const RefreshMsgDecoderPool& );
	RefreshMsgDecoderPool& operator=( const RefreshMsgDecoderPool& );
};

}

}

}

#endif // __thomsonreuters_ema_access_RefreshMsgDecoder_h