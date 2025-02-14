/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#ifndef __thomsonreuters_ema_access_OmmInvalidConfigurationExceptionImpl_h
#define __thomsonreuters_ema_access_OmmInvalidConfigurationExceptionImpl_h

#include "OmmInvalidConfigurationException.h"

namespace thomsonreuters {

namespace ema {

namespace access {

class EMA_ACCESS_API OmmInvalidConfigurationExceptionImpl : public OmmInvalidConfigurationException
{
public :

	static void throwException( const EmaString& text );

	OmmInvalidConfigurationExceptionImpl();

	virtual ~OmmInvalidConfigurationExceptionImpl();

private :

	OmmInvalidConfigurationExceptionImpl( const OmmInvalidConfigurationExceptionImpl& );
	OmmInvalidConfigurationExceptionImpl& operator=( const OmmInvalidConfigurationExceptionImpl& );
};

}

}

}

#endif // __thomsonreuters_ema_access_OmmInvalidConfigurationExceptionImpl_h
