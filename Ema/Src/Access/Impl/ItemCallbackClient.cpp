/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#include "ItemCallbackClient.h"
#include "ChannelCallbackClient.h"
#include "DirectoryCallbackClient.h"
#include "DictionaryCallbackClient.h"
#include "LoginCallbackClient.h"
#include "OmmConsumerImpl.h"
#include "OmmConsumerClient.h"
#include "ReqMsg.h"
#include "PostMsg.h"
#include "GenericMsg.h"
#include "StaticDecoder.h"
#include "Decoder.h"
#include "ReqMsgEncoder.h"
#include "GenericMsgEncoder.h"
#include "PostMsgEncoder.h"
#include "OmmState.h"
#include "OmmConsumerErrorClient.h"
#include "Utilities.h"
#include "RdmUtilities.h"
#include "ExceptionTranslator.h"
#include "TunnelStreamRequest.h"
#include "TunnelStreamLoginReqMsgImpl.h"

#include <new>
#include <limits.h>

using namespace thomsonreuters::ema::access;
using namespace thomsonreuters::ema::rdm;

const EmaString ItemCallbackClient::_clientName( "ItemCallbackClient" );
const EmaString SingleItem::_clientName( "SingleItem" );
const EmaString ItemList::_clientName( "ItemList" );
const EmaString BatchItem::_clientName( "BatchItem" );
const EmaString TunnelItem::_clientName( "TunnelItem" );
const EmaString SubItem::_clientName( "SubItem" );

const EmaString SingleItemString( "SingleItem" );
const EmaString BatchItemString( "BatchItem" );
const EmaString loginItemString( "LoginItem" );
const EmaString DirectoryItemString( "DirectoryItem" );
const EmaString DictionaryItemString( "DictionaryItem" );
const EmaString TunnelItemString( "TunnelItem" );
const EmaString SubItemString( "SubItem" );
const EmaString UnknownItemString( "UnknownItem" );

Item::Item( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure, Item* parent ) :
 _domainType( 0 ),
 _streamId( 0 ),
 _closure( closure ),
 _parent( parent ),
 _ommConsClient( ommConsClient ),
 _ommConsImpl( ommConsImpl )
{
}

Item::~Item()
{
	_ommConsImpl.getItemCallbackClient().removeFromMap( this );
}

void Item::destroy( Item*& pItem )
{
	if ( pItem )
	{
		delete pItem;
		pItem = 0;
	}
}

const EmaString& Item::getTypeAsString()
{
	switch ( getType() )
	{
	case Item::SingleItemEnum :
		return SingleItemString;
	case Item::BatchItemEnum :
		return BatchItemString;
	case Item::LoginItemEnum :
		return loginItemString;
	case Item::DirectoryItemEnum :
		return DirectoryItemString;
	case Item::DictionaryItemEnum :
		return DictionaryItemString;
	case Item::TunnelItemEnum :
		return TunnelItemString;
	case Item::SubItemEnum :
		return SubItemString;
	default :
		return UnknownItemString;
	}
}

OmmConsumerClient& Item::getClient() const
{
	return _ommConsClient;
}

void* Item::getClosure() const
{
	return _closure;
}

Item* Item::getParent() const
{
	return _parent;
}

OmmConsumerImpl& Item::getOmmConsumerImpl()
{
	return _ommConsImpl;
}

SingleItem* SingleItem::create( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure, Item* batchItem )
{
	SingleItem* pItem = 0;

	try {
		pItem = new SingleItem( ommConsImpl, ommConsClient, closure, batchItem );
	}
	catch( std::bad_alloc ) {}

	if ( !pItem )
	{
		const char* temp = "Failed to create SingleItem";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( ommConsImpl.hasOmmConnsumerErrorClient() )
			ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );
	}

	return pItem;
}

SingleItem::SingleItem( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure, Item* batchItem ) :
 Item( ommConsImpl, ommConsClient, closure, batchItem ),
 _pDirectory( 0 ),
 _closedStatusInfo( 0 )
{
}

SingleItem::~SingleItem()
{
	_ommConsImpl.getItemCallbackClient().removeFromList( this );

	if ( _closedStatusInfo )
	{
		delete _closedStatusInfo;
		_closedStatusInfo = 0;
	}
}

Item::ItemType SingleItem::getType() const 
{
	return Item::SingleItemEnum;
}

const Directory* SingleItem::getDirectory()
{
	return _pDirectory;
}

bool SingleItem::open( const ReqMsg& reqMsg )
{
	const ReqMsgEncoder& reqMsgEncoder = static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() );
	
	const Directory* pDirectory = 0;

	if ( reqMsgEncoder.hasServiceName() )
	{
		pDirectory = _ommConsImpl.getDirectoryCallbackClient().getDirectory( reqMsgEncoder.getServiceName() );
		if ( !pDirectory )
		{
			EmaString temp( "Service name of '" );
			temp.append( reqMsgEncoder.getServiceName() ).
				append( "' is not found." );
			scheduleItemClosedStatus( reqMsgEncoder, temp );

			return true;
		}
	}
	else
	{
		if ( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.flags & RSSL_MKF_HAS_SERVICE_ID )
			pDirectory = _ommConsImpl.getDirectoryCallbackClient().getDirectory( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.serviceId );
		else
		{
			EmaString temp( "Passed in request message does not identify any service." );
			scheduleItemClosedStatus( reqMsgEncoder, temp );

			return true;
		}

		if ( !pDirectory )
		{
			EmaString temp( "Service id of '" );
			temp.append( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.serviceId ).
				append( "' is not found." );
			scheduleItemClosedStatus( reqMsgEncoder, temp );

			return true;
		}
	}

	_pDirectory = pDirectory;

	return submit( reqMsgEncoder.getRsslRequestMsg() );
}

bool SingleItem::modify( const ReqMsg& reqMsg )
{
	return submit( static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() ).getRsslRequestMsg() );
}

bool SingleItem::close()
{
	RsslCloseMsg rsslCloseMsg;
	rsslClearCloseMsg( &rsslCloseMsg );

	rsslCloseMsg.msgBase.containerType = RSSL_DT_NO_DATA;
	rsslCloseMsg.msgBase.domainType = _domainType;

	bool retCode = submit( &rsslCloseMsg );

	remove();

	return retCode;
}

bool SingleItem::submit( const PostMsg& postMsg )
{
	return submit( static_cast<const PostMsgEncoder&>( postMsg.getEncoder() ).getRsslPostMsg() );
}

bool SingleItem::submit( const GenericMsg& genMsg )
{
	return submit( static_cast<const GenericMsgEncoder&>( genMsg.getEncoder() ).getRsslGenericMsg() );
}

void SingleItem::remove()
{
	if ( getType() != Item::BatchItemEnum )
	{
		if ( _parent )
		{
			if ( _parent->getType() == Item::BatchItemEnum )
				static_cast<BatchItem*>(_parent)->decreaseItemCount();
		}

		delete this;
	}
}

bool SingleItem::submit( RsslRequestMsg* pRsslRequestMsg )
{
	RsslReactorSubmitMsgOptions submitMsgOpts;
	rsslClearReactorSubmitMsgOptions( &submitMsgOpts );

	pRsslRequestMsg->msgBase.msgKey.flags &= ~RSSL_MKF_HAS_SERVICE_ID;	
	
	if ( !( pRsslRequestMsg->flags & RSSL_RQMF_HAS_QOS ) )
	{
		pRsslRequestMsg->qos.timeliness = RSSL_QOS_TIME_REALTIME;
		pRsslRequestMsg->qos.rate = RSSL_QOS_RATE_TICK_BY_TICK;
		pRsslRequestMsg->worstQos.rate = RSSL_QOS_RATE_TIME_CONFLATED;
		pRsslRequestMsg->worstQos.timeliness = RSSL_QOS_TIME_DELAYED_UNKNOWN;
		pRsslRequestMsg->worstQos.rateInfo = 65535;
		pRsslRequestMsg->flags |= (RSSL_RQMF_HAS_QOS | RSSL_RQMF_HAS_WORST_QOS);
	}	

	pRsslRequestMsg->flags |= _ommConsImpl.getActiveConfig().channelConfig->msgKeyInUpdates ? RSSL_RQMF_MSG_KEY_IN_UPDATES : 0;
	submitMsgOpts.pRsslMsg = (RsslMsg*)pRsslRequestMsg;

	RsslBuffer serviceNameBuffer;
	serviceNameBuffer.data = (char*)_pDirectory->getName().c_str();
	serviceNameBuffer.length = _pDirectory->getName().length();
	submitMsgOpts.pServiceName = &serviceNameBuffer;

	submitMsgOpts.majorVersion = _pDirectory->getChannel()->getRsslChannel()->majorVersion;
	submitMsgOpts.minorVersion = _pDirectory->getChannel()->getRsslChannel()->minorVersion;

	submitMsgOpts.requestMsgOptions.pUserSpec = (void*)this;

	if ( !_streamId )
	{
		if ( pRsslRequestMsg->flags & RSSL_RQMF_HAS_BATCH )
		{
			submitMsgOpts.pRsslMsg->msgBase.streamId = _pDirectory->getChannel()->getNextStreamId();
			_streamId = submitMsgOpts.pRsslMsg->msgBase.streamId;

			const EmaVector<SingleItem*>& singleItemList = static_cast<BatchItem &>(*this).getSingleItemList();

			for( UInt32 i = 1; i < singleItemList.size(); ++i )
			{
				singleItemList[i]->_streamId = _pDirectory->getChannel()->getNextStreamId();
				singleItemList[i]->_pDirectory = _pDirectory;
				singleItemList[i]->_domainType = submitMsgOpts.pRsslMsg->msgBase.domainType;
			}
		}
		else
		{
			if ( !submitMsgOpts.pRsslMsg->msgBase.streamId )
				submitMsgOpts.pRsslMsg->msgBase.streamId = _pDirectory->getChannel()->getNextStreamId();
			_streamId = submitMsgOpts.pRsslMsg->msgBase.streamId;
		}
	}
	else
		submitMsgOpts.pRsslMsg->msgBase.streamId = _streamId;

	if ( !_domainType )
		_domainType = submitMsgOpts.pRsslMsg->msgBase.domainType;
	else
		submitMsgOpts.pRsslMsg->msgBase.domainType = _domainType;

	RsslErrorInfo rsslErrorInfo;
	RsslRet ret;
	if ( ( ret = rsslReactorSubmitMsg( _pDirectory->getChannel()->getRsslReactor(),
										_pDirectory->getChannel()->getRsslChannel(),
										&submitMsgOpts, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error: rsslReactorSubmitMsg() failed in SingleItem::submit( RsslRequestMsg* )" );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		EmaString text( "Failed to open or modify item request. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	return true;
}

bool SingleItem::submit( RsslCloseMsg* pRsslCloseMsg )
{
	RsslReactorSubmitMsgOptions submitMsgOpts;
	rsslClearReactorSubmitMsgOptions( &submitMsgOpts );

	submitMsgOpts.pRsslMsg = (RsslMsg*)pRsslCloseMsg;

	submitMsgOpts.majorVersion = _pDirectory->getChannel()->getRsslChannel()->majorVersion;
	submitMsgOpts.minorVersion = _pDirectory->getChannel()->getRsslChannel()->minorVersion;
	submitMsgOpts.pRsslMsg->msgBase.streamId = _streamId;

	RsslErrorInfo rsslErrorInfo;
	RsslRet ret;
	if ( ( ret = rsslReactorSubmitMsg( _pDirectory->getChannel()->getRsslReactor(),
										_pDirectory->getChannel()->getRsslChannel(),
										&submitMsgOpts, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error. rsslReactorSubmitMsg() failed in SingleItem::submit( RsslCloseMsg* )" );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		EmaString text( "Failed to close item request. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	return true;
}

bool SingleItem::submit( RsslGenericMsg* pRsslGenericMsg )
{
	RsslReactorSubmitMsgOptions submitMsgOpts;
	rsslClearReactorSubmitMsgOptions( &submitMsgOpts );

	submitMsgOpts.pRsslMsg = (RsslMsg*)pRsslGenericMsg;

	submitMsgOpts.majorVersion = _pDirectory->getChannel()->getRsslChannel()->majorVersion;
	submitMsgOpts.minorVersion = _pDirectory->getChannel()->getRsslChannel()->minorVersion;
	submitMsgOpts.pRsslMsg->msgBase.streamId = _streamId;
	submitMsgOpts.pRsslMsg->msgBase.domainType = _domainType;

	RsslErrorInfo rsslErrorInfo;
	RsslRet ret;
	if ( ( ret = rsslReactorSubmitMsg( _pDirectory->getChannel()->getRsslReactor(),
										_pDirectory->getChannel()->getRsslChannel(),
										&submitMsgOpts, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error. rsslReactorSubmitMsg() failed in SingleItem::submit( RsslGenericMsg* )" );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );

			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		EmaString text( "Failed to submit GenericMsg on item stream. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	return true;
}

bool SingleItem::submit( RsslPostMsg* pRsslPostMsg )
{
	RsslReactorSubmitMsgOptions submitMsgOpts;
	rsslClearReactorSubmitMsgOptions( &submitMsgOpts );

	submitMsgOpts.pRsslMsg = (RsslMsg*)pRsslPostMsg;

	submitMsgOpts.majorVersion = _pDirectory->getChannel()->getRsslChannel()->majorVersion;
	submitMsgOpts.minorVersion = _pDirectory->getChannel()->getRsslChannel()->minorVersion;
	submitMsgOpts.pRsslMsg->msgBase.streamId = _streamId;
	submitMsgOpts.pRsslMsg->msgBase.domainType = _domainType;

	RsslErrorInfo rsslErrorInfo;
	RsslRet ret;
	if ( ( ret = rsslReactorSubmitMsg( _pDirectory->getChannel()->getRsslReactor(),
										_pDirectory->getChannel()->getRsslChannel(),
										&submitMsgOpts, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error: rsslReactorSubmitMsg() failed in SingleItem::submit( RsslPostMsg* ) " );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		EmaString text( "Failed to submit PostMsg on item stream. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	return true;
}

void ItemCallbackClient::sendItemClosedStatus( void* pInfo )
{
	if ( !pInfo ) return;

	RsslStatusMsg rsslStatusMsg;
	rsslClearStatusMsg( &rsslStatusMsg );

	ClosedStatusInfo* pClosedStatusInfo = static_cast<ClosedStatusInfo*>( pInfo );

	rsslStatusMsg.msgBase.streamId = pClosedStatusInfo->getStreamId();

	rsslStatusMsg.msgBase.domainType = (UInt8)pClosedStatusInfo->getDomainType();

	rsslStatusMsg.state.code = RSSL_SC_SOURCE_UNKNOWN;
	rsslStatusMsg.state.dataState = RSSL_DATA_SUSPECT;
	rsslStatusMsg.state.streamState = RSSL_STREAM_CLOSED;
	rsslStatusMsg.state.text.data = (char*)pClosedStatusInfo->getStatusText().c_str();
	rsslStatusMsg.state.text.length = pClosedStatusInfo->getStatusText().length();

	rsslStatusMsg.msgBase.msgKey = *(pClosedStatusInfo->getRsslMsgKey());

	rsslStatusMsg.msgBase.containerType = RSSL_DT_NO_DATA;

	rsslStatusMsg.flags |= RSSL_STMF_HAS_STATE | RSSL_STMF_HAS_MSG_KEY;

	if ( pClosedStatusInfo->getPrivateStream() )
		rsslStatusMsg.flags |= RSSL_STMF_PRIVATE_STREAM;

	StatusMsg statusMsg;

	StaticDecoder::setRsslData( &statusMsg, (RsslMsg*)&rsslStatusMsg, RSSL_RWF_MAJOR_VERSION, RSSL_RWF_MINOR_VERSION, 0 );

	statusMsg.getDecoder().setServiceName( pClosedStatusInfo->getServiceName().c_str(), pClosedStatusInfo->getServiceName().length() );

	OmmConsumerEvent event;

	event._pItem = pClosedStatusInfo->getItem();

	event._pItem->getClient().onAllMsg( statusMsg, event );
	event._pItem->getClient().onStatusMsg( statusMsg, event );

	event._pItem->remove();
}

void SingleItem::scheduleItemClosedStatus( const ReqMsgEncoder& reqMsgEncoder, const EmaString& text )
{
	if ( _closedStatusInfo ) return;

	_closedStatusInfo = new ClosedStatusInfo( this, reqMsgEncoder, text );

	new TimeOut( _ommConsImpl, 1000, ItemCallbackClient::sendItemClosedStatus, _closedStatusInfo );
}

ClosedStatusInfo::ClosedStatusInfo( Item* pItem, const ReqMsgEncoder& reqMsgEncoder, const EmaString& text ) :
 _msgKey(),
 _statusText( text ),
 _serviceName(),
 _domainType( reqMsgEncoder.getRsslRequestMsg()->msgBase.domainType ),
 _streamId( reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId ),
 _pItem( pItem ),
 _privateStream( false )
{
	rsslClearMsgKey( &_msgKey );

	if ( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
	{
		_msgKey.name.data = (char*)malloc( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.name.length );

		if ( !_msgKey.name.data )
		{
			const char* text = "Failed to allocate memory in ClosedStatusInfo( Item* , const ReqMsgEncoder& , const EmaString& ).";
			throwMeeException( text );
		}

		_msgKey.name.length = reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.name.length;
	}

	if ( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.flags & RSSL_MKF_HAS_ATTRIB )
	{
		_msgKey.encAttrib.data = (char*) malloc( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.encAttrib.length + 1 );
		_msgKey.encAttrib.length = reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.encAttrib.length + 1;
	}

	rsslCopyMsgKey( &_msgKey, &reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey );

	if ( reqMsgEncoder.hasServiceName() )
		_serviceName = reqMsgEncoder.getServiceName();

	_privateStream = reqMsgEncoder.getPrivateStream();
}

ClosedStatusInfo::ClosedStatusInfo( Item* pItem, const TunnelStreamRequest& tunnelStreamRequest, const EmaString& text ) :
 _msgKey(),
 _statusText( text ),
 _serviceName(),
 _domainType( tunnelStreamRequest.getDomainType() ),
 _streamId( 0 ),
 _pItem( pItem ),
 _privateStream( true )
{
	rsslClearMsgKey( &_msgKey );

	if ( tunnelStreamRequest.hasName() )
	{
		_msgKey.name.data = (char*)malloc( tunnelStreamRequest.getName().length() );
		if ( !_msgKey.name.data )
		{
			const char* text = "Failed to allocate memory in ClosedStatusInfo( Item* , const TunnelStreamRequest& , const EmaString& ).";
			throwMeeException( text );
		}

		memcpy( _msgKey.name.data, tunnelStreamRequest.getName().c_str(), tunnelStreamRequest.getName().length() );

		_msgKey.name.length = tunnelStreamRequest.getName().length();

		_msgKey.flags |= RSSL_MKF_HAS_NAME;
	}


	if ( tunnelStreamRequest.hasServiceId() )
	{
		_msgKey.serviceId = tunnelStreamRequest.getServiceId();
		_msgKey.flags |= RSSL_MKF_HAS_SERVICE_ID;
	}

	if ( tunnelStreamRequest.hasServiceName() )
		_serviceName = tunnelStreamRequest.getServiceName();
}

ClosedStatusInfo::~ClosedStatusInfo()
{
	if ( _msgKey.name.data )
		free( _msgKey.name.data );

	if ( _msgKey.encAttrib.data )
		free( _msgKey.encAttrib.data );
}

BatchItem* BatchItem::create( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure )
{
	BatchItem* pItem = 0;

	try {
		pItem = new BatchItem( ommConsImpl, ommConsClient, closure );
	}
	catch( std::bad_alloc ) {}

	if ( !pItem )
	{
		const char* temp = "Failed to create BatchItem.";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( ommConsImpl.hasOmmConnsumerErrorClient() )
			ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );
	}

	return pItem;
}

BatchItem::BatchItem( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure ) :
 SingleItem( ommConsImpl, ommConsClient, closure, 0 ),
 _singleItemList( 10 ),
 _itemCount( 1 )
{
	_singleItemList.push_back( 0 );
}

BatchItem::~BatchItem()
{
	_singleItemList.clear();
}

Item::ItemType BatchItem::getType() const
{
	return Item::BatchItemEnum; 
}

bool BatchItem::open( const ReqMsg& reqMsg )
{
	return SingleItem::open( reqMsg );
}

bool BatchItem::modify( const ReqMsg& reqMsg )
{
	EmaString temp( "Invalid attempt to modify batch stream. " );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool BatchItem::close()
{
	EmaString temp( "Invalid attempt to close batch stream. " );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool BatchItem::submit( const PostMsg& )
{
	EmaString temp( "Invalid attempt to submit PostMsg on batch stream. " );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool BatchItem::submit( const GenericMsg& )
{
	EmaString temp( "Invalid attempt to submit GenericMsg on batch stream. " );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );
	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool BatchItem::addBatchItems( const EmaVector<EmaString>& batchItemList )
{
	SingleItem* item = 0;

	for( UInt32 i = 0 ; i < batchItemList.size() ; i++ )
	{
		item = SingleItem::create( _ommConsImpl, _ommConsClient, 0, this );

		if ( item )
			_singleItemList.push_back( item );
		else
			return false;
	}
	
	_itemCount = _singleItemList.size() - 1;

	return true;
}

const EmaVector<SingleItem*> & BatchItem::getSingleItemList()
{
	return _singleItemList;
}

SingleItem* BatchItem::getSingleItem( Int32 streamId )
{
	return (streamId == _streamId) ? this : _singleItemList[ streamId - _streamId ];
}

void BatchItem::decreaseItemCount()
{
	_itemCount--;
	
	if ( _itemCount == 0 )
		delete this;
}

TunnelItem* TunnelItem::create( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure )
{
	TunnelItem* pItem = 0;

	try {
		pItem = new TunnelItem( ommConsImpl, ommConsClient, closure );
	}
	catch( std::bad_alloc ) {}

	if ( !pItem )
	{
		const char* temp = "Failed to create TunnelItem.";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( ommConsImpl.hasOmmConnsumerErrorClient() )
			ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );
	}

	return pItem;
}

TunnelItem::TunnelItem( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure ) :
 Item( ommConsImpl, ommConsClient, closure, 0 ),
 _pDirectory( 0 ),
 _pRsslTunnelStream( 0 ),
 _closedStatusInfo( 0 )
{
}

TunnelItem::~TunnelItem()
{
	_ommConsImpl.getItemCallbackClient().removeFromList( this );

	if ( _closedStatusInfo )
	{
		delete _closedStatusInfo;
		_closedStatusInfo = 0;
	}
}

const Directory* TunnelItem::getDirectory()
{
	return _pDirectory;
}

Item::ItemType TunnelItem::getType() const
{
	return Item::TunnelItemEnum;
}

UInt32 TunnelItem::addSubItem( Item* pSubItem, UInt32 streamId )
{
	if ( streamId == 0 )
	{
		UInt32 position = 0;
		UInt32 size = _subItemList.size();

		for ( ; position < size; ++position )
			if ( !_subItemList[position ] )
				break;

		if ( position > INT_MAX )
		{
			EmaString temp( "Attempt to open too many concurent sub streams." );
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
			else
				throwIueException( temp );

			return 0;
		}

		if ( position == size )
			_subItemList.push_back( pSubItem );
		else
			_subItemList[ position ] = pSubItem;

		return position + _startingStreamId;
	}
	else
	{
		if ( streamId < _startingStreamId )
		{
			EmaString temp( "Invalid attempt to open a sub stream with streamId smaller than starting stream id. Passed in stream id is " );
			temp.append( streamId );

			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
			else
				throwIueException( temp );

			return 0;
		}

		if ( streamId - _startingStreamId < _subItemList.size() )
		{
			if ( _subItemList[ streamId - _startingStreamId ] )
			{
				EmaString temp( "Invalid attempt to open a sub stream with streamId already in use. Passed in stream id is " );
				temp.append( streamId );

				if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
					_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

				if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
					_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
				else
					throwIueException( temp );

				return 0;
			}

			_subItemList[ streamId - _startingStreamId ] = pSubItem;
		}
		else
		{
			while ( _subItemList.size() <= streamId - _startingStreamId )
				_subItemList.push_back( 0 );

			_subItemList[ streamId - _startingStreamId ] = pSubItem;
		}

		return streamId;
	}
}

void TunnelItem::removeSubItem( UInt32 streamId )
{
	if ( streamId < _startingStreamId )
	{
		if ( streamId > 0 )
		{
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Internal error. Current stream Id in TunnelItem::removeSubItem( UInt32 ) is less than the starting stream id." );
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
			}
		}
		return;
	}

	if ( streamId - _startingStreamId >= _subItemList.size() )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error. Current stream Id in TunnelItem::removeSubItem( UInt32 ) is greater than the list size." );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
		}
		return;
	}

	_subItemList[ streamId - _startingStreamId ] = 0;
}

Item* TunnelItem::getSubItem( UInt32 streamId )
{
	if ( streamId < _startingStreamId )
	{
		if ( streamId > 0 )
		{
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Internal error. Current stream Id in TunnelItem::getSubItem( UInt32 ) is less than the starting stream id." );
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
			}
		}
		return 0;
	}

	if ( streamId - _startingStreamId >= _subItemList.size() )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error. Current stream Id in TunnelItem::getSubItem( UInt32 ) is greater than the list size." );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
		}
		return 0;
	}

	return _subItemList[ streamId - _startingStreamId ];	
}

bool TunnelItem::open( const TunnelStreamRequest& tunnelStreamRequest )
{
	const Directory* pDirectory = 0;

	if ( tunnelStreamRequest.hasServiceName() )
	{
		pDirectory = _ommConsImpl.getDirectoryCallbackClient().getDirectory( tunnelStreamRequest.getServiceName() );
		if ( !pDirectory )
		{
			EmaString temp( "Service name of '" );
			temp.append( tunnelStreamRequest.getServiceName() ).
				append( "' is not found." );

			scheduleItemClosedStatus( tunnelStreamRequest, temp );

			return true;
		}
	}
	else if ( tunnelStreamRequest.hasServiceId() )
	{
		pDirectory = _ommConsImpl.getDirectoryCallbackClient().getDirectory( tunnelStreamRequest.getServiceId() );

		if ( !pDirectory )
		{
			EmaString temp( "Service id of '" );
			temp.append( tunnelStreamRequest.getServiceId() ).
				append( "' is not found." );

			scheduleItemClosedStatus( tunnelStreamRequest, temp );

			return true;
		}
	}

	_pDirectory = pDirectory;

	return submit( tunnelStreamRequest );
}

void TunnelItem::scheduleItemClosedStatus( const TunnelStreamRequest& tunnelStreamRequest, const EmaString& text )
{
	if ( _closedStatusInfo ) return;

	_closedStatusInfo = new ClosedStatusInfo( this, tunnelStreamRequest, text );

	new TimeOut( _ommConsImpl, 1000, ItemCallbackClient::sendItemClosedStatus, _closedStatusInfo );
}

void TunnelItem::rsslTunnelStream( RsslTunnelStream* pRsslTunnelStream )
{
	_pRsslTunnelStream = pRsslTunnelStream;
}

bool TunnelItem::open( const ReqMsg& )
{
	EmaString temp( "Invalid attempt to open tunnel stream using ReqMsg." );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool TunnelItem::modify( const ReqMsg& )
{
	EmaString temp( "Invalid attempt to reissue tunnel stream using ReqMsg." );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool TunnelItem::submit( const PostMsg& )
{
	EmaString temp( "Invalid attempt to submit PostMsg on tunnel stream." );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool TunnelItem::submit( const GenericMsg& )
{
	EmaString temp( "Invalid attempt to submit GenericMsg on tunnel stream." );
	temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

	if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

	if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
		_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
	else
		throwIueException( temp );

	return false;
}

bool TunnelItem::submit( const TunnelStreamRequest& tunnelStreamRequest )
{
	_domainType = (UInt8)tunnelStreamRequest.getDomainType();
	_streamId = _pDirectory->getChannel()->getNextStreamId();

	RsslTunnelStreamOpenOptions tsOpenOptions;
	rsslClearTunnelStreamOpenOptions( &tsOpenOptions );

	tsOpenOptions.domainType = _domainType;
	tsOpenOptions.name = tunnelStreamRequest.hasName() ? (char*)tunnelStreamRequest.getName().c_str() : 0;

	RsslDecodeIterator dIter;
	rsslClearDecodeIterator( &dIter );

	RsslBuffer rsslBuffer;
	rsslBuffer.data = (char*)malloc( sizeof( char ) * 4096 );

	if ( !rsslBuffer.data )
	{
		const char* temp = "Failed to allocate memory in TunnelItem::submit( const TunnelStreamRequest& ).";

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );

		return false;
	}

	rsslBuffer.length = 4096;
	
	RsslRDMLoginMsg rsslRdmLoginMsg;
	rsslClearRDMLoginMsg( &rsslRdmLoginMsg );

	RsslErrorInfo rsslErrorInfo;

	if ( tunnelStreamRequest.hasLoginReqMsg() )
	{
		if ( RSSL_RET_SUCCESS != rsslSetDecodeIteratorRWFVersion( &dIter, _pDirectory->getChannel()->getRsslChannel()->majorVersion, _pDirectory->getChannel()->getRsslChannel()->minorVersion ) )
		{
			free( rsslBuffer.data );

			EmaString text( "Internal Error. Failed to set decode iterator version in TunnelItem::submit( const TunnelStreamRequest& )" );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
			else
				throwIueException( text );

			return false;
		}

		if ( RSSL_RET_SUCCESS != rsslSetDecodeIteratorBuffer( &dIter, tunnelStreamRequest._pImpl->getRsslBuffer() ) )
		{
			free( rsslBuffer.data );

			EmaString text( "Internal Error. Failed to set decode iterator buffer in TunnelItem::submit( const TunnelStreamRequest& )" );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
			else
				throwIueException( text );

			return false;
		}

		RsslMsg* pRsslMsg = tunnelStreamRequest._pImpl->getRsslMsg();

		RsslRet retCode = rsslDecodeRDMLoginMsg( &dIter, pRsslMsg, &rsslRdmLoginMsg, &rsslBuffer, &rsslErrorInfo );

		while ( retCode == RSSL_RET_BUFFER_TOO_SMALL )
		{
			free( rsslBuffer.data );

			rsslBuffer.length += rsslBuffer.length;

			rsslBuffer.data = (char*)malloc( sizeof( char ) * rsslBuffer.length );

			if ( !rsslBuffer.data )
			{
				const char* temp = "Failed to allocate memory in TunnelItem::submit( const TunnelStreamRequest& ).";

				if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
					_ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
				else
					throwMeeException( temp );

				return false;
			}

			retCode = rsslDecodeRDMLoginMsg( &dIter, pRsslMsg, &rsslRdmLoginMsg, &rsslBuffer, &rsslErrorInfo );
		}

		if ( RSSL_RET_SUCCESS != retCode )
		{
			free( rsslBuffer.data );

			EmaString text( "Internal Error. Failed to decode login request in TunnelItem::submit( const TunnelStreamRequest& )" );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
			else
				throwIueException( text );

			return false;
		}

		tsOpenOptions.pAuthLoginRequest = &rsslRdmLoginMsg.request;
	}
	
	tsOpenOptions.guaranteedOutputBuffers = tunnelStreamRequest.getGuaranteedOutputBuffers();
	tsOpenOptions.responseTimeout = tunnelStreamRequest.getResponseTimeOut();
	tsOpenOptions.serviceId = (UInt16)_pDirectory->getId();
	tsOpenOptions.streamId = _streamId;

	tsOpenOptions.userSpecPtr = this;

	tsOpenOptions.defaultMsgCallback = OmmConsumerImpl::tunnelStreamDefaultMsgCallback;
	tsOpenOptions.queueMsgCallback = OmmConsumerImpl::tunnelStreamQueueMsgCallback;
	tsOpenOptions.statusEventCallback = OmmConsumerImpl::tunnelStreamStatusEventCallback;

	tsOpenOptions.classOfService.common.maxMsgSize = tunnelStreamRequest.getClassOfService().getCommon().getMaxMsgSize();

	tsOpenOptions.classOfService.authentication.type = (RDMClassOfServiceAuthenticationType)tunnelStreamRequest.getClassOfService().getAuthentication().getType();

	tsOpenOptions.classOfService.dataIntegrity.type = (RsslUInt)tunnelStreamRequest.getClassOfService().getDataIntegrity().getType();

	tsOpenOptions.classOfService.flowControl.recvWindowSize = tunnelStreamRequest.getClassOfService().getFlowControl().getRecvWindowSize();
	tsOpenOptions.classOfService.flowControl.sendWindowSize = tunnelStreamRequest.getClassOfService().getFlowControl().getSendWindowSize();
	tsOpenOptions.classOfService.flowControl.type = (RsslUInt)tunnelStreamRequest.getClassOfService().getFlowControl().getType();

	tsOpenOptions.classOfService.guarantee.type = (RsslUInt)tunnelStreamRequest.getClassOfService().getGuarantee().getType();
	tsOpenOptions.classOfService.guarantee.persistLocally = tunnelStreamRequest.getClassOfService().getGuarantee().getPersistLocally() ? RSSL_TRUE : RSSL_FALSE;
	tsOpenOptions.classOfService.guarantee.persistenceFilePath = (char*)tunnelStreamRequest.getClassOfService().getGuarantee().getPersistenceFilePath().c_str();

	RsslRet ret;
	if ( ( ret = rsslReactorOpenTunnelStream( _pDirectory->getChannel()->getRsslChannel(), &tsOpenOptions, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error: rsslReactorOpenTunnelStream() failed in TunnelItem::submit( const TunnelStreamRequest& )" );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		free( rsslBuffer.data );

		EmaString text( "Failed to open tunnel stream request. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	free( rsslBuffer.data );

	return true;
}

bool TunnelItem::close()
{
	RsslTunnelStreamCloseOptions tunnelStreamCloseOptions;
	rsslClearTunnelStreamCloseOptions( &tunnelStreamCloseOptions );

	RsslErrorInfo rsslErrorInfo;
	RsslRet ret;
	if ( ( ret = rsslReactorCloseTunnelStream( _pRsslTunnelStream, &tunnelStreamCloseOptions, &rsslErrorInfo ) ) != RSSL_RET_SUCCESS )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Internal error. rsslReactorCloseTunnelStream() failed in TunnelItem::close()" );
			temp.append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( rsslErrorInfo.rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( rsslErrorInfo.rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( rsslErrorInfo.rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( rsslErrorInfo.errorLocation ).append( CR )
				.append( "Error Text " ).append( rsslErrorInfo.rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		EmaString text( "Failed to close tunnel stream request. Reason: " );
		text.append( rsslRetCodeToString( ret ) )
			.append( ". Error text: " )
			.append( rsslErrorInfo.rsslError.text );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( text );
		else
			throwIueException( text );

		return false;
	}

	remove();

	return true;
}

void TunnelItem::remove()
{
	UInt32 subItemCount = _subItemList.size();

	for ( UInt32 pos = 0; pos < subItemCount; ++pos )
	{
		if ( !_subItemList[pos] ) continue;

		_subItemList[pos]->remove();
	}

	delete this;
}

bool TunnelItem::submitSubItemMsg( RsslMsg* pRsslMsg )
{
	RsslTunnelStreamGetBufferOptions rsslGetBufferOpts;
	rsslClearTunnelStreamGetBufferOptions( &rsslGetBufferOpts );
	rsslGetBufferOpts.size = 256;

	RsslErrorInfo rsslErrorInfo;
	RsslBuffer* pRsslBuffer = rsslTunnelStreamGetBuffer( _pRsslTunnelStream, &rsslGetBufferOpts, &rsslErrorInfo );

	if ( !pRsslBuffer )
	{
		EmaString temp( "Internal Error. Failed to allocate RsslTunnelStreamBuffer in TunnelItem::submitSubItemMsg( RsslMsg* )." );
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
		else
			throwIueException( temp );

		return false;
	}

	RsslEncodeIterator eIter;
	rsslClearEncodeIterator( &eIter );

	if ( rsslSetEncodeIteratorRWFVersion( &eIter, _pRsslTunnelStream->pReactorChannel->majorVersion, _pRsslTunnelStream->pReactorChannel->minorVersion ) != RSSL_RET_SUCCESS )
	{
		EmaString temp( "Internal Error. Failed to set encode iterator version in TunnelItem::submitSubItemMsg( RsslMsg* )." );
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
		else
			throwIueException( temp );

		return false;
	}

	if ( rsslSetEncodeIteratorBuffer( &eIter, pRsslBuffer ) != RSSL_RET_SUCCESS )
	{
		EmaString temp( "Internal Error. Failed to set encode iterator buffer in TunnelItem::submitSubItemMsg( RsslMsg* )." );
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
		else
			throwIueException( temp );

		return false;
	}

	RsslRet retCode;
	while ( ( retCode = rsslEncodeMsg( &eIter, pRsslMsg ) ) == RSSL_RET_BUFFER_TOO_SMALL )
	{
		rsslTunnelStreamReleaseBuffer( pRsslBuffer, &rsslErrorInfo );

		rsslGetBufferOpts.size += rsslGetBufferOpts.size;

		pRsslBuffer = rsslTunnelStreamGetBuffer( _pRsslTunnelStream, &rsslGetBufferOpts, &rsslErrorInfo );

		if ( !pRsslBuffer )
		{
			EmaString temp( "Internal Error. Failed to allocate RsslTunnelStreamBuffer in TunnelItem::submitSubItemMsg( RsslMsg* )." );
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
			else
				throwIueException( temp );

			return false;
		}

		rsslClearEncodeIterator( &eIter );

		if ( rsslSetEncodeIteratorRWFVersion( &eIter, _pRsslTunnelStream->pReactorChannel->majorVersion, _pRsslTunnelStream->pReactorChannel->minorVersion ) != RSSL_RET_SUCCESS )
		{
			EmaString temp( "Internal Error. Failed to set encode iterator version in TunnelItem::submitSubItemMsg( RsslMsg* )." );
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
			else
				throwIueException( temp );

			return false;
		}

		if ( rsslSetEncodeIteratorBuffer( &eIter, pRsslBuffer ) != RSSL_RET_SUCCESS )
		{
			EmaString temp( "Internal Error. Failed to set encode iterator buffer in TunnelItem::submitSubItemMsg( RsslMsg* )." );
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
			else
				throwIueException( temp );

			return false;
		}
	}

	if ( retCode != RSSL_RET_SUCCESS )
	{
		rsslTunnelStreamReleaseBuffer( pRsslBuffer, &rsslErrorInfo );

		EmaString temp( "Internal Error. Failed to encode message in TunnelItem::submitSubItemMsg( RsslMsg* )." );
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
		else
			throwIueException( temp );

		return false;
	}

	pRsslBuffer->length = rsslGetEncodedBufferLength( &eIter );

	RsslTunnelStreamSubmitOptions rsslTunnelStreamSubmitOptions;
	rsslClearTunnelStreamSubmitOptions( &rsslTunnelStreamSubmitOptions );
	rsslTunnelStreamSubmitOptions.containerType = RSSL_DT_MSG;

	retCode = rsslTunnelStreamSubmit( _pRsslTunnelStream, pRsslBuffer, &rsslTunnelStreamSubmitOptions, &rsslErrorInfo );

	if ( retCode != RSSL_RET_SUCCESS )
	{
		rsslTunnelStreamReleaseBuffer( pRsslBuffer, &rsslErrorInfo );

		EmaString temp( "Internal Error. Failed to submit message in TunnelItem::submitSubItemMsg( RsslMsg* )." );
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
		else
			throwIueException( temp );

		return false;
	}

	return true;
}

SubItem* SubItem::create( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure, Item* parent )
{
	SubItem* pItem = 0;

	try {
		pItem = new SubItem( ommConsImpl, ommConsClient, closure, parent );
	}
	catch( std::bad_alloc ) {}

	if ( !pItem )
	{
		const char* temp = "Failed to create SubItem";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( ommConsImpl.hasOmmConnsumerErrorClient() )
			ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );
	}

	return pItem;
}

SubItem::SubItem( OmmConsumerImpl& ommConsImpl, OmmConsumerClient& ommConsClient, void* closure, Item* parent ) :
 Item( ommConsImpl, ommConsClient, closure, parent ),
 _closedStatusInfo( 0 )
{
}

SubItem::~SubItem()
{
	static_cast<TunnelItem*>( _parent )->removeSubItem( _streamId );

	_ommConsImpl.getItemCallbackClient().removeFromList( this );

	if ( _closedStatusInfo )
	{
		delete _closedStatusInfo;
		_closedStatusInfo = 0;
	}
}

Item::ItemType SubItem::getType() const 
{
	return Item::SubItemEnum;
}

const Directory* SubItem::getDirectory()
{
	return 0;
}

void SubItem::scheduleItemClosedStatus( const ReqMsgEncoder& reqMsgEncoder, const EmaString& text )
{
	if ( _closedStatusInfo ) return;

	_closedStatusInfo = new ClosedStatusInfo( this, reqMsgEncoder, text );

	new TimeOut( _ommConsImpl, 1000, ItemCallbackClient::sendItemClosedStatus, _closedStatusInfo );
}

bool SubItem::open( const ReqMsg& reqMsg )
{
	const ReqMsgEncoder& reqMsgEncoder = static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() );
	
	if ( reqMsgEncoder.hasServiceName() )
	{
		EmaString temp( "Invalid attempt to open sub stream using serviceName." );
		scheduleItemClosedStatus( reqMsgEncoder, temp );

		return true;
	}

	if ( !reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId )
	{
		_streamId = static_cast<TunnelItem*>( _parent )->addSubItem( this );
		reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId = _streamId;
	}
	else
	{
		if ( reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId < 0 )
		{
			EmaString temp( "Invalid attempt to assign negative streamId to a sub stream." );
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			{
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
				return false;
			}
			else
			{
				throwIueException( temp );
				return false;
			}
		}
		else
		{
			_streamId = static_cast<TunnelItem*>( _parent )->addSubItem( this, reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId );
		}
	}

	_domainType = (UInt8)reqMsgEncoder.getRsslRequestMsg()->msgBase.domainType;

	return static_cast<TunnelItem*>( _parent )->submitSubItemMsg( (RsslMsg*)reqMsgEncoder.getRsslRequestMsg() );
}

bool SubItem::modify( const ReqMsg& reqMsg )
{
	const ReqMsgEncoder& reqMsgEncoder = static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() );

	reqMsgEncoder.getRsslRequestMsg()->msgBase.streamId = _streamId;

	return static_cast<TunnelItem*>( _parent )->submitSubItemMsg( (RsslMsg*)reqMsgEncoder.getRsslRequestMsg() );
}

bool SubItem::submit( const PostMsg& postMsg )
{
	const PostMsgEncoder& postMsgEncoder = static_cast<const PostMsgEncoder&>( postMsg.getEncoder() );

	postMsgEncoder.getRsslPostMsg()->msgBase.streamId = _streamId;

	return static_cast<TunnelItem*>( _parent )->submitSubItemMsg( (RsslMsg*)postMsgEncoder.getRsslPostMsg() );
}

bool SubItem::submit( const GenericMsg& genMsg )
{
	const GenericMsgEncoder& genMsgEncoder = static_cast<const GenericMsgEncoder&>( genMsg.getEncoder() );

	genMsgEncoder.getRsslGenericMsg()->msgBase.streamId = _streamId;

	return static_cast<TunnelItem*>( _parent )->submitSubItemMsg( (RsslMsg*)genMsgEncoder.getRsslGenericMsg() );
}

bool SubItem::close()
{
	RsslCloseMsg rsslCloseMsg;
	rsslClearCloseMsg( &rsslCloseMsg );

	rsslCloseMsg.msgBase.containerType = RSSL_DT_NO_DATA;
	rsslCloseMsg.msgBase.domainType = _domainType;
	rsslCloseMsg.msgBase.streamId = _streamId;

	bool retCode = static_cast<TunnelItem*>( _parent )->submitSubItemMsg( (RsslMsg*)&rsslCloseMsg );

	remove();

	return retCode;
}

void SubItem::remove()
{
	delete this;
}

ItemList* ItemList::create( OmmConsumerImpl& ommConsImpl )
{
	ItemList* pItemList = 0;

	try {
		pItemList = new ItemList( ommConsImpl );
	}
	catch( std::bad_alloc ) {}

	if ( !pItemList )
	{
		const char* temp = "Failed to create ItemList";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( ommConsImpl.hasOmmConnsumerErrorClient() )
			ommConsImpl.getOmmConsumerErrorClient().onMemoryExhaustion( temp );
		else
			throwMeeException( temp );
	}

	return pItemList;
}

void ItemList::destroy( ItemList*& pItemList )
{
	if ( pItemList )
	{
		delete pItemList;
		pItemList = 0;
	}
}

ItemList::ItemList( OmmConsumerImpl& ommConsImpl ) :
 _ommConsImpl( ommConsImpl )
{
}

ItemList::~ItemList()
{
	Item* temp = _list.back();

	while ( temp )
	{
		Item::destroy( temp );
		temp = _list.back();
	}
}

Int32 ItemList::addItem( Item* pItem )
{
	_list.push_back( pItem );

	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		EmaString temp( "Added Item " );
		temp.append( ptrToStringAsHex( pItem ) ).append( " to ItemList" ).append( CR )
			.append( "OmmConsumer name " ).append( _ommConsImpl .getConsumerName() );
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
	}

	return _list.size();
}

void ItemList::removeItem( Item* pItem )
{
	_list.remove( pItem );

	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		EmaString temp( "Removed Item " );
		temp.append( ptrToStringAsHex( pItem ) ).append( " from ItemList" ).append( CR )
			.append( "OmmConsumer name " ).append( _ommConsImpl .getConsumerName() );
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
	}
}

ItemCallbackClient::ItemCallbackClient( OmmConsumerImpl& ommConsImpl ) :
 _refreshMsg(),
 _updateMsg(),
 _statusMsg(),
 _genericMsg(),
 _ackMsg(),
 _event(),
 _ommConsImpl( ommConsImpl ),
 _itemMap( ommConsImpl.getActiveConfig().itemCountHint )
{
    _itemList = ItemList::create( ommConsImpl );

	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		EmaString temp( "Created ItemCallbackClient." );
		temp.append( " OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
	}
}

ItemCallbackClient::~ItemCallbackClient()
{
	ItemList::destroy( _itemList );

	if ( OmmLoggerClient::VerboseEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
	{
		EmaString temp( "Destroyed ItemCallbackClient." );
		temp.append( " OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );
		_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::VerboseEnum, temp );
	}
}

ItemCallbackClient* ItemCallbackClient::create( OmmConsumerImpl& ommConsImpl )
{
	ItemCallbackClient* pClient = 0;

	try {
		pClient = new ItemCallbackClient( ommConsImpl );
	}
	catch ( std::bad_alloc ) {}

	if ( !pClient )
	{
		const char* temp = "Failed to create ItemCallbackClient";
		if ( OmmLoggerClient::ErrorEnum >= ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		throwMeeException( temp );
	}

	return pClient;
}

void ItemCallbackClient::destroy( ItemCallbackClient*& pClient )
{
	if ( pClient )
	{
		delete pClient;
		pClient = 0;
	}
}

void ItemCallbackClient::initialize()
{
}

RsslReactorCallbackRet ItemCallbackClient::processCallback( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamStatusEvent* pTunnelStreamStatusEvent )
{
	if ( !pRsslTunnelStream )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream status event without the pRsslTunnelStream in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamStatusEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamStatusEvent->pReactorChannel->pRsslChannel ) );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	if ( !pRsslTunnelStream->userSpecPtr )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream status event without the userSpecPtr in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamStatusEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamStatusEvent->pReactorChannel->pRsslChannel ) );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	RsslStatusMsg rsslStatusMsg;
	rsslClearStatusMsg( &rsslStatusMsg );

	rsslStatusMsg.flags |= RSSL_STMF_PRIVATE_STREAM | RSSL_STMF_CLEAR_CACHE | RSSL_STMF_HAS_MSG_KEY;

	rsslStatusMsg.msgBase.containerType = RSSL_DT_NO_DATA;
	rsslStatusMsg.msgBase.domainType = pRsslTunnelStream->domainType;
	rsslStatusMsg.msgBase.streamId = pRsslTunnelStream->streamId;

	if ( pTunnelStreamStatusEvent->pState )
	{
		rsslStatusMsg.state = *pTunnelStreamStatusEvent->pState;
		rsslStatusMsg.flags |= RSSL_STMF_HAS_STATE;
	}

	rsslStatusMsg.msgBase.msgKey.flags = RSSL_MKF_HAS_NAME;
	rsslStatusMsg.msgBase.msgKey.name.data = pRsslTunnelStream->name;
	rsslStatusMsg.msgBase.msgKey.name.length = (UInt32)strlen( pRsslTunnelStream->name );

	rsslStatusMsg.msgBase.msgKey.flags |= RSSL_MKF_HAS_SERVICE_ID;
	rsslStatusMsg.msgBase.msgKey.serviceId = pRsslTunnelStream->serviceId;

	StaticDecoder::setRsslData( &_statusMsg, (RsslMsg*)&rsslStatusMsg,
		pTunnelStreamStatusEvent->pReactorChannel->majorVersion,
		pTunnelStreamStatusEvent->pReactorChannel->minorVersion,
 		static_cast<Channel*>( pTunnelStreamStatusEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	static_cast<TunnelItem*>( pRsslTunnelStream->userSpecPtr )->rsslTunnelStream( pRsslTunnelStream );

	_event._pItem = (Item*)( pRsslTunnelStream->userSpecPtr );

	_statusMsg.getDecoder().setServiceName( _event._pItem->getDirectory()->getName().c_str(),
											_event._pItem->getDirectory()->getName().length() );

	_event._pItem->getClient().onAllMsg( _statusMsg, _event );
	_event._pItem->getClient().onStatusMsg( _statusMsg, _event );

	if ( pTunnelStreamStatusEvent->pState )
		if ( _statusMsg.getState().getStreamState() != OmmState::OpenEnum )
			_event._pItem->remove();

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processCallback( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	if ( !pRsslTunnelStream )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream message event without the pRsslTunnelStream in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamMsgEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	if ( !pRsslTunnelStream->userSpecPtr )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream message event without the userSpecPtr in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamMsgEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) ).append( CR )
				.append( "Error Id " ).append( pTunnelStreamMsgEvent->pErrorInfo->rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( pTunnelStreamMsgEvent->pErrorInfo->rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( pTunnelStreamMsgEvent->pErrorInfo->errorLocation ).append( CR )
				.append( "Error Text " ).append( pTunnelStreamMsgEvent->pErrorInfo->rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	if ( pTunnelStreamMsgEvent->containerType != RSSL_DT_MSG )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream message event containing an unsupported data type of " );
			temp += DataType( dataType[ pTunnelStreamMsgEvent->containerType ] ).toString();

			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) ).append( CR )
				.append( "Tunnel Stream Handle " ).append( (UInt64)pRsslTunnelStream->userSpecPtr ).append( CR )
				.append( "Tunnel Stream name " ).append( pRsslTunnelStream->name ).append( CR )
				.append( "Tunnel Stream serviceId " ).append( pRsslTunnelStream->serviceId ).append( CR )
				.append( "Tunnel Stream streamId " ).append( pRsslTunnelStream->streamId );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}
	else if ( !pTunnelStreamMsgEvent->pRsslMsg )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream message event containing no sub stream message." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) ).append( CR )
				.append( "Tunnel Stream Handle " ).append( (UInt64)pRsslTunnelStream->userSpecPtr ).append( CR )
				.append( "Tunnel Stream name " ).append( pRsslTunnelStream->name ).append( CR )
				.append( "Tunnel Stream serviceId " ).append( pRsslTunnelStream->serviceId ).append( CR )
				.append( "Tunnel Stream streamId " ).append( pRsslTunnelStream->streamId );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	_event._pItem = static_cast<TunnelItem*>( pRsslTunnelStream->userSpecPtr )->getSubItem( pTunnelStreamMsgEvent->pRsslMsg->msgBase.streamId );
	
	if ( !_event._pItem )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream message event containing sub stream message with unknown streamId " );
			temp.append( pTunnelStreamMsgEvent->pRsslMsg->msgBase.streamId ).append( ". Message is dropped." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) ).append( CR )
				.append( "Tunnel Stream Handle " ).append( (UInt64)pRsslTunnelStream->userSpecPtr ).append( CR )
				.append( "Tunnel Stream name " ).append( pRsslTunnelStream->name ).append( CR )
				.append( "Tunnel Stream serviceId " ).append( pRsslTunnelStream->serviceId ).append( CR )
				.append( "Tunnel Stream streamId " ).append( pRsslTunnelStream->streamId );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	switch ( pTunnelStreamMsgEvent->pRsslMsg->msgBase.msgClass )
	{
	default :
		{
			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			{
				EmaString temp( "Received a tunnel stream message event containing an unsupported message type of " );
				temp += DataType( msgDataType[ pTunnelStreamMsgEvent->pRsslMsg->msgBase.msgClass ] ).toString();

				temp.append( CR )
					.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
					.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamMsgEvent->pReactorChannel->pRsslChannel ) );
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
			}
		}
		break;
	case RSSL_MC_GENERIC :
		return processGenericMsg( pRsslTunnelStream, pTunnelStreamMsgEvent );
	case RSSL_MC_ACK :
		return processAckMsg( pRsslTunnelStream, pTunnelStreamMsgEvent );
	case RSSL_MC_REFRESH :
		return processRefreshMsg( pRsslTunnelStream, pTunnelStreamMsgEvent );
	case RSSL_MC_UPDATE :
		return processUpdateMsg( pRsslTunnelStream, pTunnelStreamMsgEvent );
	case RSSL_MC_STATUS :
		return processStatusMsg( pRsslTunnelStream, pTunnelStreamMsgEvent );
	};

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processCallback( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamQueueMsgEvent* pTunnelStreamQueueMsgEvent )
{
	if ( !pRsslTunnelStream )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream queue message event without the pRsslTunnelStream in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamQueueMsgEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamQueueMsgEvent->base.pReactorChannel->pRsslChannel ) );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	if ( !pRsslTunnelStream->userSpecPtr )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received a tunnel stream queue message event without the userSpecPtr in ItemCallbackClient::processCallback( RsslTunnelStream* , RsslTunnelStreamQueueMsgEvent* )." );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pTunnelStreamQueueMsgEvent->base.pReactorChannel->pRsslChannel ) ).append( CR )
				.append( "Error Id " ).append( pTunnelStreamQueueMsgEvent->base.pErrorInfo->rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( pTunnelStreamQueueMsgEvent->base.pErrorInfo->rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( pTunnelStreamQueueMsgEvent->base.pErrorInfo->errorLocation ).append( CR )
				.append( "Error Text " ).append( pTunnelStreamQueueMsgEvent->base.pErrorInfo->rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processAckMsg( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	StaticDecoder::setRsslData( &_ackMsg, pTunnelStreamMsgEvent->pRsslMsg,
		pTunnelStreamMsgEvent->pReactorChannel->majorVersion,
		pTunnelStreamMsgEvent->pReactorChannel->minorVersion,
		static_cast<Channel*>( pTunnelStreamMsgEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem->getClient().onAllMsg( _ackMsg, _event );
	_event._pItem->getClient().onAckMsg( _ackMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processGenericMsg( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	StaticDecoder::setRsslData( &_genericMsg, pTunnelStreamMsgEvent->pRsslMsg,
		pTunnelStreamMsgEvent->pReactorChannel->majorVersion,
		pTunnelStreamMsgEvent->pReactorChannel->minorVersion,
		static_cast<Channel*>( pTunnelStreamMsgEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem->getClient().onAllMsg( _genericMsg, _event );
	_event._pItem->getClient().onGenericMsg( _genericMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processRefreshMsg( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	StaticDecoder::setRsslData( &_refreshMsg, pTunnelStreamMsgEvent->pRsslMsg,
		pTunnelStreamMsgEvent->pReactorChannel->majorVersion,
		pTunnelStreamMsgEvent->pReactorChannel->minorVersion,
		static_cast<Channel*>( pTunnelStreamMsgEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem->getClient().onAllMsg( _refreshMsg, _event );
	_event._pItem->getClient().onRefreshMsg( _refreshMsg, _event );

	if ( _refreshMsg.getState().getStreamState() == OmmState::NonStreamingEnum )
	{
		if ( _refreshMsg.getComplete() )
			_event._pItem->remove();
	}
	else if ( _refreshMsg.getState().getStreamState() != OmmState::OpenEnum )
	{
		_event._pItem->remove();
	}

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processUpdateMsg( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	StaticDecoder::setRsslData( &_updateMsg, pTunnelStreamMsgEvent->pRsslMsg,
		pTunnelStreamMsgEvent->pReactorChannel->majorVersion,
		pTunnelStreamMsgEvent->pReactorChannel->minorVersion,
		static_cast<Channel*>( pTunnelStreamMsgEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem->getClient().onAllMsg( _updateMsg, _event );
	_event._pItem->getClient().onUpdateMsg( _updateMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processStatusMsg( RsslTunnelStream* pRsslTunnelStream, RsslTunnelStreamMsgEvent* pTunnelStreamMsgEvent )
{
	StaticDecoder::setRsslData( &_statusMsg, pTunnelStreamMsgEvent->pRsslMsg,
		pTunnelStreamMsgEvent->pReactorChannel->majorVersion,
		pTunnelStreamMsgEvent->pReactorChannel->minorVersion,
		static_cast<Channel*>( pTunnelStreamMsgEvent->pReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem->getClient().onAllMsg( _statusMsg, _event );
	_event._pItem->getClient().onStatusMsg( _statusMsg, _event );

	if ( pTunnelStreamMsgEvent->pRsslMsg->statusMsg.flags & RSSL_STMF_HAS_STATE )
		if ( _statusMsg.getState().getStreamState() != OmmState::OpenEnum )
			_event._pItem->remove();

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processCallback( RsslReactor* pRsslReactor,
															RsslReactorChannel* pRsslReactorChannel,
															RsslMsgEvent* pEvent )
{
	RsslMsg* pRsslMsg = pEvent->pRsslMsg;

	if ( !pRsslMsg )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			RsslErrorInfo* pError = pEvent->pErrorInfo;

			EmaString temp( "Received an item event without RsslMsg message" );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
				.append( "RsslChannel " ).append( ptrToStringAsHex( pError->rsslError.channel ) ).append( CR )
				.append( "Error Id " ).append( pError->rsslError.rsslErrorId ).append( CR )
				.append( "Internal sysError " ).append( pError->rsslError.sysError ).append( CR )
				.append( "Error Location " ).append( pError->errorLocation ).append( CR )
				.append( "Error Text " ).append( pError->rsslError.text );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp.trimWhitespace() );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	if ( pRsslMsg->msgBase.streamId != 1 && 
		( !pEvent->pStreamInfo || !pEvent->pStreamInfo->pUserSpec ) )
	{
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received an item event without user specified pointer or stream info" );
			temp.append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
				.append( "RsslReactorChannel " ).append( ptrToStringAsHex( pRsslReactorChannel ) ).append( CR )
				.append( "RsslSocket " ).append( (UInt64)pRsslReactorChannel->socketId );

			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
		}

		return RSSL_RC_CRET_SUCCESS;
	}

	switch ( pRsslMsg->msgBase.msgClass )
	{
	case RSSL_MC_ACK :
		if ( pRsslMsg->msgBase.streamId == 1 )
			return _ommConsImpl.getLoginCallbackClient().processAckMsg( pRsslMsg, pRsslReactorChannel, 0 );
		else
			return processAckMsg( pRsslMsg, pRsslReactorChannel, pEvent );
	case RSSL_MC_GENERIC :
		return processGenericMsg( pRsslMsg, pRsslReactorChannel, pEvent );
	case RSSL_MC_REFRESH :
		return processRefreshMsg( pRsslMsg, pRsslReactorChannel, pEvent );
	case RSSL_MC_STATUS :
		return processStatusMsg( pRsslMsg, pRsslReactorChannel, pEvent );
	case RSSL_MC_UPDATE :
		return processUpdateMsg( pRsslMsg, pRsslReactorChannel, pEvent );
	default :
		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
		{
			EmaString temp( "Received an item event with message containing unhandled message class" );
			temp.append( CR )
				.append( "Rssl Message Class " ).append( pRsslMsg->msgBase.msgClass ).append( CR )
				.append( "Consumer Name " ).append( _ommConsImpl.getConsumerName() ).append( CR )
				.append( "RsslReactor " ).append( ptrToStringAsHex( pRsslReactor ) ).append( CR )
				.append( "RsslReactorChannel " ).append( ptrToStringAsHex( pRsslReactorChannel ) ).append( CR )
				.append( "RsslSocket" ).append( (UInt64)pRsslReactorChannel->socketId );
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );
		}
		break;
	}

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processRefreshMsg( RsslMsg* pRsslMsg, RsslReactorChannel* pRsslReactorChannel, RsslMsgEvent* pEvent )
{
	StaticDecoder::setRsslData( &_refreshMsg, pRsslMsg,
		pRsslReactorChannel->majorVersion,
		pRsslReactorChannel->minorVersion,
 		static_cast<Channel*>( pRsslReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem = (Item*)( pEvent->pStreamInfo->pUserSpec );

	if ( _event._pItem->getType() == Item::BatchItemEnum )
		_event._pItem = static_cast<BatchItem *>(_event._pItem)->getSingleItem( pRsslMsg->msgBase.streamId );
	
	_refreshMsg.getDecoder().setServiceName( _event._pItem->getDirectory()->getName().c_str(),
											_event._pItem->getDirectory()->getName().length() );

	_event._pItem->getClient().onAllMsg( _refreshMsg, _event );
	_event._pItem->getClient().onRefreshMsg( _refreshMsg, _event );

	if ( _refreshMsg.getState().getStreamState() == OmmState::NonStreamingEnum )
	{
		if ( _refreshMsg.getComplete() )
			_event._pItem->remove();
	}
	else if ( _refreshMsg.getState().getStreamState() != OmmState::OpenEnum )
	{
		_event._pItem->remove();
	}

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processUpdateMsg( RsslMsg* pRsslMsg, RsslReactorChannel* pRsslReactorChannel, RsslMsgEvent* pEvent )
{
	StaticDecoder::setRsslData( &_updateMsg, pRsslMsg,
		pRsslReactorChannel->majorVersion,
		pRsslReactorChannel->minorVersion,
 		static_cast<Channel*>( pRsslReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem = (Item*)( pEvent->pStreamInfo->pUserSpec );

	if ( _event._pItem->getType() == Item::BatchItemEnum )
		_event._pItem = static_cast<BatchItem *>(_event._pItem)->getSingleItem( pRsslMsg->msgBase.streamId );
	
	_updateMsg.getDecoder().setServiceName( _event._pItem->getDirectory()->getName().c_str(),
											_event._pItem->getDirectory()->getName().length() );

	_event._pItem->getClient().onAllMsg( _updateMsg, _event );
	_event._pItem->getClient().onUpdateMsg( _updateMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processStatusMsg( RsslMsg* pRsslMsg, RsslReactorChannel* pRsslReactorChannel, RsslMsgEvent* pEvent )
{
	StaticDecoder::setRsslData( &_statusMsg, pRsslMsg,
		pRsslReactorChannel->majorVersion,
		pRsslReactorChannel->minorVersion,
 		static_cast<Channel*>( pRsslReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem = (Item*)( pEvent->pStreamInfo->pUserSpec );

	if ( _event._pItem->getType() == Item::BatchItemEnum )
		_event._pItem = static_cast<BatchItem *>(_event._pItem)->getSingleItem( pRsslMsg->msgBase.streamId );
	
	_statusMsg.getDecoder().setServiceName( _event._pItem->getDirectory()->getName().c_str(),
											_event._pItem->getDirectory()->getName().length() );

	_event._pItem->getClient().onAllMsg( _statusMsg, _event );
	_event._pItem->getClient().onStatusMsg( _statusMsg, _event );

	if ( pRsslMsg->statusMsg.flags & RSSL_STMF_HAS_STATE )
		if ( _statusMsg.getState().getStreamState() != OmmState::OpenEnum )
			_event._pItem->remove();

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processGenericMsg( RsslMsg* pRsslMsg, RsslReactorChannel* pRsslReactorChannel, RsslMsgEvent* pEvent )
{
	StaticDecoder::setRsslData( &_genericMsg, pRsslMsg,
		pRsslReactorChannel->majorVersion,
		pRsslReactorChannel->minorVersion,
		static_cast<Channel*>( pRsslReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem = (Item*)( pEvent->pStreamInfo->pUserSpec );

	if ( _event._pItem->getType() == Item::BatchItemEnum )
		_event._pItem  = static_cast<BatchItem *>(_event._pItem )->getSingleItem( pRsslMsg->msgBase.streamId );

	_event._pItem->getClient().onAllMsg( _genericMsg, _event );
	_event._pItem->getClient().onGenericMsg( _genericMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

RsslReactorCallbackRet ItemCallbackClient::processAckMsg( RsslMsg* pRsslMsg, RsslReactorChannel* pRsslReactorChannel, RsslMsgEvent* pEvent )
{
	StaticDecoder::setRsslData( &_ackMsg, pRsslMsg,
		pRsslReactorChannel->majorVersion,
		pRsslReactorChannel->minorVersion,
		static_cast<Channel*>( pRsslReactorChannel->userSpecPtr )->getDictionary()->getRsslDictionary() );

	_event._pItem = (Item*)( pEvent->pStreamInfo->pUserSpec );

	if ( _event._pItem->getType() == Item::BatchItemEnum )
		_event._pItem = static_cast<BatchItem *>(_event._pItem)->getSingleItem( pRsslMsg->msgBase.streamId );
	
	_ackMsg.getDecoder().setServiceName( _event._pItem->getDirectory()->getName().c_str(),
										_event._pItem->getDirectory()->getName().length() );

	_event._pItem->getClient().onAllMsg( _ackMsg, _event );
	_event._pItem->getClient().onAckMsg( _ackMsg, _event );

	return RSSL_RC_CRET_SUCCESS;
}

UInt64 ItemCallbackClient::registerClient( const ReqMsg& reqMsg, OmmConsumerClient& ommConsClient,
											void* closure, UInt64 parentHandle )
{
	if ( !parentHandle )
	{
		const ReqMsgEncoder& reqMsgEncoder = static_cast<const ReqMsgEncoder&>( reqMsg.getEncoder() );

		switch ( reqMsgEncoder.getRsslRequestMsg()->msgBase.domainType )
		{
		case RSSL_DMT_LOGIN :
			{
				SingleItem* pItem = _ommConsImpl.getLoginCallbackClient().getLoginItem( reqMsg, ommConsClient, closure );

				if ( pItem )
				{
					addToList( pItem );
					addToMap( pItem );
				}

				return (UInt64)pItem;
			}
		case RSSL_DMT_DICTIONARY :
			{
				if ( ( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.nameType != INSTRUMENT_NAME_UNSPECIFIED ) &&
					( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.nameType != rdm::INSTRUMENT_NAME_RIC ) )
				{
					EmaString temp( "Invalid ReqMsg's name type : " );
					temp.append( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.nameType );
					temp.append( ". OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

					if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
						_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

					if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
						_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
					else
						throwIueException( temp );

					return 0;
				}

				if ( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.flags & RSSL_MKF_HAS_NAME )
				{
					EmaString name( reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.name.data, reqMsgEncoder.getRsslRequestMsg()->msgBase.msgKey.name.length );

					if ( ( name != "RWFFld" ) && ( name != "RWFEnum" ) )
					{
						EmaString temp( "Invalid ReqMsg's name : " );
						temp.append( name );
						temp.append( "\nReqMsg's name must be \"RWFFld\" or \"RWFEnum\" for MMT_DICTIONARY domain type. ");
						temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

						if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
							_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

						if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
							_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
						else
							throwIueException( temp );

						return 0;
					}
				}
				else
				{
					EmaString temp( "ReqMsg's name is not defined. " );
					temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

					if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
						_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

					if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
						_ommConsImpl.getOmmConsumerErrorClient().onInvalidUsage( temp );
					else
						throwIueException( temp );

					return 0;
				}

				DictionaryItem* pItem = DictionaryItem::create( _ommConsImpl, ommConsClient, closure );

				if ( pItem )
				{
					if ( !pItem->open( reqMsg ) )
					{
						Item::destroy( (Item*&)pItem );
					}
					else
					{
						addToList( pItem );
						addToMap( pItem );
					}
				}

				return (UInt64)pItem;
			}
		case RSSL_DMT_SOURCE :
			{
				const ChannelList& channels( _ommConsImpl.getChannelCallbackClient().getChannelList() );
				const Channel* c( channels.front() );
				while( c )
				{
					DirectoryItem* pItem = DirectoryItem::create( _ommConsImpl, ommConsClient, closure, c );

					if ( pItem )
					{
						try {
							if ( !pItem->open( reqMsg ) )
								Item::destroy( (Item*&)pItem );
							else
							{
								addToList( pItem );
								addToMap( pItem );
							}
						}
						catch ( ... )
						{
							Item::destroy( (Item*&)pItem );
							throw;
						}
					}

					return (UInt64)pItem;
				
					c = c->next();
				}

				return 0;
			}
		default :
			{
				SingleItem* pItem  = 0;

				if ( reqMsgEncoder.getRsslRequestMsg()->flags & RSSL_RQMF_HAS_BATCH )
				{
					BatchItem* pBatchItem = BatchItem::create( _ommConsImpl, ommConsClient, closure );

					if ( pBatchItem )
					{
						try {
							pItem = pBatchItem;
							pBatchItem->addBatchItems( reqMsgEncoder.getBatchItemList() );

							if ( !pBatchItem->open( reqMsg ) )
							{
								for ( UInt32 i = 1 ; i < pBatchItem->getSingleItemList().size() ; i++ )
								{
									Item::destroy( (Item*&)pBatchItem->getSingleItemList()[i] );
								}

								Item::destroy( (Item*&)pItem );
							}
							else
							{
								addToList( pBatchItem );
								addToMap( pBatchItem );

								for ( UInt32 i = 1 ; i < pBatchItem->getSingleItemList().size() ; i++ )
								{
									addToList( pBatchItem->getSingleItemList()[i] );
									addToMap( pBatchItem->getSingleItemList()[i] );
								}
							}
						}
						catch ( ... )
						{
							for ( UInt32 i = 1 ; i < pBatchItem->getSingleItemList().size() ; i++ )
							{
								Item::destroy( (Item*&)pBatchItem->getSingleItemList()[i] );
							}

							Item::destroy( (Item*&)pItem );

							throw;
						}
					}
				}
				else
				{
					pItem = SingleItem::create( _ommConsImpl, ommConsClient, closure, 0 );

					if ( pItem )
					{
						try {
							if ( !pItem->open( reqMsg ) )
								Item::destroy( (Item*&)pItem );
							else
							{
								addToList( pItem );
								addToMap( pItem );
							}
						}
						catch ( ... )
						{
							Item::destroy( (Item*&)pItem );
							throw;
						}
					}
				}

				return (UInt64)pItem;
			}
		}
	}
	else
	{

		if ( !_itemMap.find( parentHandle ) )
		{
			EmaString temp( "Attempt to use invalid parentHandle on registerClient(). " );
			temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidHandle( parentHandle, temp );
			else
				throwIheException( parentHandle, temp );

			return 0;
		}

		if ( ((Item*)parentHandle)->getType() != Item::TunnelItemEnum )
		{
			EmaString temp( "Invalid attempt to use " );
			temp += ((Item*)parentHandle)->getTypeAsString();
			temp.append( " as parentHandle on registerClient(). " );
			temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

			if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
				_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

			if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
				_ommConsImpl.getOmmConsumerErrorClient().onInvalidHandle( parentHandle, temp );
			else
				throwIheException( parentHandle, temp );

			return 0;
		}

		SubItem* pItem = SubItem::create( _ommConsImpl, ommConsClient, closure, (Item*)parentHandle );

		if ( pItem )
		{
			try {
				if ( !pItem->open( reqMsg ) )
					Item::destroy( (Item*&)pItem );
				else
				{
					addToList( pItem );
					addToMap( pItem );
				}
			}
			catch ( ... )
			{
				Item::destroy( (Item*&)pItem );
				throw;
			}
		}

		return (UInt64)pItem;
	}
}

UInt64 ItemCallbackClient::registerClient( const TunnelStreamRequest& tunnelStreamRequest, OmmConsumerClient& ommConsClient, void* closure )
{
	TunnelItem* pItem = TunnelItem::create( _ommConsImpl, ommConsClient, closure );

	if ( pItem )
	{
		try {
			if ( !pItem->open( tunnelStreamRequest ) )
				Item::destroy( (Item*&)pItem );
			else
			{
				addToList( pItem );
				addToMap( pItem );
			}
		}
		catch ( ... )
		{
			Item::destroy( (Item*&)pItem );
			throw;
		}
	}

	return (UInt64)pItem;
}

void ItemCallbackClient::reissue( const ReqMsg& reqMsg, UInt64 handle )
{
	if ( !_itemMap.find( handle ) )
	{
		EmaString temp( "Attempt to use invalid Handle on reissue(). " );
		temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidHandle( handle, temp );
		else
			throwIheException( handle, temp );

		return;
	}

	((Item*)handle)->modify( reqMsg );
}

void ItemCallbackClient::unregister( UInt64 handle )
{
	if ( !_itemMap.find( handle ) ) return;

	((Item*)handle)->close();
}

void ItemCallbackClient::submit( const PostMsg& postMsg, UInt64 handle )
{
	if ( !_itemMap.find( handle ) )
	{
		EmaString temp( "Attempt to use invalid Handle on submit( const PostMsg& ). " );
		temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidHandle( handle, temp );
		else
			throwIheException( handle, temp );

		return;
	}

	((Item*)handle)->submit( postMsg );
}

void ItemCallbackClient::submit( const GenericMsg& genericMsg, UInt64 handle )
{
	if ( !_itemMap.find( handle ) )
	{
		EmaString temp( "Attempt to use invalid Handle on submit( const GenericMsg& ). " );
		temp.append( "OmmConsumer name='" ).append( _ommConsImpl .getConsumerName() ).append( "'." );

		if ( OmmLoggerClient::ErrorEnum >= _ommConsImpl.getActiveConfig().loggerConfig.minLoggerSeverity )
			_ommConsImpl.getOmmLoggerClient().log( _clientName, OmmLoggerClient::ErrorEnum, temp );

		if ( _ommConsImpl.hasOmmConnsumerErrorClient() )
			_ommConsImpl.getOmmConsumerErrorClient().onInvalidHandle( handle, temp );
		else
			throwIheException( handle, temp );

		return;
	}

	((Item*)handle)->submit( genericMsg );
}

size_t ItemCallbackClient::UInt64rHasher::operator()( const UInt64& value ) const
{
	return value;
}

bool ItemCallbackClient::UInt64Equal_To::operator()( const UInt64& x, const UInt64& y ) const
{
	return x == y;
}

void ItemCallbackClient::addToList( Item* pItem )
{
	_itemList->addItem( pItem );
}

void ItemCallbackClient::removeFromList( Item* pItem )
{
	_itemList->removeItem( pItem );
}

void ItemCallbackClient::addToMap( Item* pItem )
{
	_itemMap.insert( (UInt64)pItem, pItem );
}

void ItemCallbackClient::removeFromMap( Item* pItem )
{
	_itemMap.erase( (UInt64)pItem );
}
