/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#ifndef __thomsonreuters_ema_access_OmmSystemException_h
#define __thomsonreuters_ema_access_OmmSystemException_h

/**
	@class thomsonreuters::ema::access::OmmSystemException OmmSystemException.h "Access/Include/OmmSystemException.h"
	@brief OmmSystemException represents exceptions thrown by operating system.

	\remark All methods in this class are \ref SingleThreaded.

	@see OmmException,
		OmmConsumerErrorClient
*/

#include "Access/Include/OmmException.h"

namespace thomsonreuters {

namespace ema {

namespace access {

class EMA_ACCESS_API OmmSystemException : public OmmException
{
public :

	///@name Accessors
	//@{	
	/** Returns ExceptionType.
		@return OmmException::OmmSystemExceptionEnum
	*/
	ExceptionType getExceptionType() const;

	/** Returns Text.
		@return EmaString with exception text information
	*/
	const EmaString& getText() const;

	/** Returns a string representation of the class instance.
		@return string representation of the class instance
	*/
	const EmaString& toString() const;

	/** Returns SystemExceptionCode.
		@return code of the system exception
	*/
	Int64 getSystemExceptionCode() const;

	/** Returns SystemExceptionAddress.
		@return address of the system exception
	*/
	void* getSystemExceptionAddress() const;
	//@}

protected :

	Int64		_exceptionCode;
	void*		_exceptionAddress;

	OmmSystemException();
	virtual ~OmmSystemException();

	OmmSystemException( const OmmSystemException& );
	OmmSystemException& operator=( const OmmSystemException& );
};

}

}

}

#endif // __thomsonreuters_ema_access_OmmSystemException_h
