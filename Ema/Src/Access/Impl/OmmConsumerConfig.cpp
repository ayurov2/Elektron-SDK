/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#include "OmmConsumerConfig.h"
#include "OmmConsumerConfigImpl.h"
#include "ExceptionTranslator.h"
#include "ReqMsg.h"
#include <new>

using namespace thomsonreuters::ema::access;

OmmConsumerConfig::OmmConsumerConfig() :
 _pImpl( 0 )
{
	try {
		_pImpl = new OmmConsumerConfigImpl();
	} catch ( std::bad_alloc ) {}

	if ( !_pImpl )
	{
		const char* temp = "Failed to allocate memory for OmmConsumerConfigImpl in OmmConsumerConfig().";
		throwMeeException( temp );
	}
}

OmmConsumerConfig::~OmmConsumerConfig()
{
	if ( _pImpl )
	{
		delete _pImpl;
		_pImpl = 0;
	}
}

OmmConsumerConfig& OmmConsumerConfig::clear()
{
	_pImpl->clear();
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::username( const EmaString& username )
{
	_pImpl->username( username );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::password( const EmaString& password )
{
	_pImpl->password( password );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::position( const EmaString& position )
{
	_pImpl->position( position );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::applicationId( const EmaString& applicationId )
{
	_pImpl->applicationId( applicationId );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::host( const EmaString& host )
{
	_pImpl->host( host );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::operationModel( OperationModel operationModel )
{
	_pImpl->operationModel( operationModel );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::consumerName( const EmaString& consumerName )
{
	_pImpl->consumerName( consumerName );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::config( const Data& config )
{
	_pImpl->config( config );
	return *this;
}

OmmConsumerConfig& OmmConsumerConfig::addAdminMsg( const ReqMsg& reqMsg )
{
	_pImpl->addAdminMsg( reqMsg );
	return *this;
}

OmmConsumerConfigImpl* OmmConsumerConfig::getConfigImpl() const
{
	return _pImpl;
}
