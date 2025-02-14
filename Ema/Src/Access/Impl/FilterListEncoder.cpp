/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#include "FilterListEncoder.h"
#include "ExceptionTranslator.h"
#include "StaticDecoder.h"
#include "FilterList.h"

using namespace thomsonreuters::ema::access;

FilterListEncoder::FilterListEncoder() :
 _containerInitialized( false ),
 _rsslFilterList(),
 _rsslFilterEntry()
{
}

FilterListEncoder::~FilterListEncoder()
{
}

void FilterListEncoder::clear()
{
	Encoder::releaseEncIterator();

	rsslClearFilterList( &_rsslFilterList );
	rsslClearFilterEntry( &_rsslFilterEntry );

	_containerInitialized = false;
}

void FilterListEncoder::initEncode( UInt8 dataType )
{
	_rsslFilterList.containerType = dataType;

	RsslRet retCode = rsslEncodeFilterListInit( &(_pEncodeIter->_rsslEncIter), &_rsslFilterList );

	while ( retCode == RSSL_RET_BUFFER_TOO_SMALL )
	{
		retCode = rsslEncodeFilterListComplete( &(_pEncodeIter->_rsslEncIter), RSSL_FALSE );

		_pEncodeIter->reallocate();

		retCode = rsslEncodeFilterListInit( &(_pEncodeIter->_rsslEncIter), &_rsslFilterList );
	}

	if ( retCode < RSSL_RET_SUCCESS )
	{
		EmaString temp( "Failed to initialize FilterList encoding. Reason='" );
		temp.append( rsslRetCodeToString( retCode ) ).append( "'. " );
		throwIueException( temp );
	}

	_containerInitialized = true;
}

void FilterListEncoder::addEncodedEntry( UInt8 id, UInt8 action, UInt8 dataType, const EmaBuffer& permission, 
	const char* methodName, RsslBuffer& rsslBuffer )
{
	_rsslFilterEntry.flags = RSSL_FTEF_NONE;

	_rsslFilterEntry.encData = rsslBuffer;
	_rsslFilterEntry.containerType = dataType;
	_rsslFilterEntry.id = id;
	_rsslFilterEntry.action = action;

	rsslFilterEntryApplyHasContainerType( &_rsslFilterEntry );

	if ( permission.length() > 0 )
	{
		rsslFilterEntryApplyHasPermData ( &_rsslFilterEntry );
		_rsslFilterEntry.permData.length = permission.length();
		_rsslFilterEntry.permData.data = const_cast<char *>(permission.c_buf());
	}

	RsslRet retCode = rsslEncodeFilterEntry( &_pEncodeIter->_rsslEncIter, &_rsslFilterEntry );
	while ( retCode == RSSL_RET_BUFFER_TOO_SMALL )
	{
		_pEncodeIter->reallocate();

		retCode = rsslEncodeFilterEntry( &_pEncodeIter->_rsslEncIter, &_rsslFilterEntry );
	}

	if ( retCode < RSSL_RET_SUCCESS )
	{
		EmaString temp( "Failed to " );
		temp.append( methodName ).append( " while encoding FilterList. Reason='" );
		temp.append( rsslRetCodeToString( retCode ) ).append( "'. " );
		throwIueException( temp );
	}
}

void FilterListEncoder::startEncodingEntry( UInt8 id, UInt8 action, UInt8 dataType, const EmaBuffer& permission, 
	const char* methodName )
{
	_rsslFilterEntry.encData.data = 0;
	_rsslFilterEntry.encData.length = 0;
	_rsslFilterEntry.flags = RSSL_FTEF_NONE;

	_rsslFilterEntry.containerType = dataType;
	_rsslFilterEntry.id = id;
	_rsslFilterEntry.action = action;

	rsslFilterEntryApplyHasContainerType( &_rsslFilterEntry );

	if ( permission.length() > 0 )
	{
		rsslFilterEntryApplyHasPermData ( &_rsslFilterEntry );
		_rsslFilterEntry.permData.length = permission.length();
		_rsslFilterEntry.permData.data = const_cast<char *>(permission.c_buf());
	}

	RsslRet retCode = rsslEncodeFilterEntryInit( &_pEncodeIter->_rsslEncIter, &_rsslFilterEntry, 0 );
	while ( retCode == RSSL_RET_BUFFER_TOO_SMALL )
	{
		_pEncodeIter->reallocate();

		retCode = rsslEncodeFilterEntryInit( &_pEncodeIter->_rsslEncIter, &_rsslFilterEntry, 0 );
	}

	if ( retCode < RSSL_RET_SUCCESS )
	{
		EmaString temp( "Failed to start encoding entry in FilterList::" );
		temp.append( methodName ).append( ". Reason='" );
		temp.append( rsslRetCodeToString( retCode ) ).append( "'. " );
		throwIueException( temp );
	}
}

void FilterListEncoder::endEncodingEntry() const
{
	RsslRet retCode = rsslEncodeFilterEntryComplete( &_pEncodeIter->_rsslEncIter, RSSL_TRUE );
	while ( retCode == RSSL_RET_BUFFER_TOO_SMALL )
	{
		_pEncodeIter->reallocate();

		retCode = rsslEncodeElementEntryComplete( &_pEncodeIter->_rsslEncIter, RSSL_TRUE );
	}

	if ( retCode < RSSL_RET_SUCCESS )
	{
		EmaString temp( "Failed to end encoding entry in FilterList. Reason='" );
		temp.append( rsslRetCodeToString( retCode ) ).append( "'. " );
		throwIueException( temp );
	}
}

void FilterListEncoder::add( UInt8 filterId, FilterEntry::FilterAction action, const ComplexType& complexType, const EmaBuffer& permission )
{
	UInt8 dataType = const_cast<Encoder&>( static_cast<const ComplexType&>(complexType).getEncoder() ).convertDataType( complexType.getDataType() );

	if ( !hasEncIterator() )
	{
		acquireEncIterator();

		initEncode( dataType );
	}

	if ( static_cast<const ComplexType&>(complexType).getEncoder().ownsIterator() )
	{
		// todo ... check if complete was called		
		addEncodedEntry( filterId, action, dataType, permission, "add()", static_cast<const ComplexType&>(complexType).getEncoder().getRsslBuffer() );
	}
	else
	{
		// todo ... check if clear was called
		passEncIterator( const_cast<Encoder&>( static_cast<const ComplexType&>(complexType).getEncoder() ) );
		startEncodingEntry( filterId, action, dataType, permission, "add()" );
	}
}

void FilterListEncoder::complete()
{
	if ( !hasEncIterator() )
	{
		EmaString temp( "Cannot complete an empty FilterList" );
		throwIueException( temp );
		return;
	}

	RsslRet retCode = rsslEncodeFilterListComplete( &(_pEncodeIter->_rsslEncIter), RSSL_TRUE );

	if ( retCode < RSSL_RET_SUCCESS )
	{
		EmaString temp( "Failed to complete FilterList encoding. Reason='" );
		temp.append( rsslRetCodeToString( retCode ) ).append( "'. " );
		throwIueException( temp );
		return;
	}

	_pEncodeIter->setEncodedLength( rsslGetEncodedBufferLength( &(_pEncodeIter->_rsslEncIter) ) );

	if ( !ownsIterator() && _iteratorOwner )
		_iteratorOwner->endEncodingEntry();
}

void FilterListEncoder::totalCountHint( UInt32 totalCountHint )
{
	if ( !_containerInitialized )
	{
		rsslFilterListApplyHasTotalCountHint( &_rsslFilterList );
		_rsslFilterList.totalCountHint = totalCountHint;
	}
	else
	{
		EmaString temp( "Invalid attempt to call totalCountHint() when container is not empty." );
		throwIueException( temp );
	}
}