/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#ifndef __thomsonreuters_ema_access_OmmDateTime_h
#define __thomsonreuters_ema_access_OmmDateTime_h

/**
	@class thomsonreuters::ema::access::OmmDateTime OmmDateTime.h "Access/Include/OmmDateTime.h"
	@brief OmmDateTime represents DateTime info in Omm.
	
	OmmDateTime encapsulates year, month, day, hour, minute, second, millisecond,
	microsecond and nanosecond information.

	The following code snippet shows extraction of DateTime from ElementList.

	\code

	void decodeElementList( const ElementList& eList )
	{
		while ( eList.forth() )
		{
			const ElementEntry& eEntry = eList.getEntry();

			if ( eEntry.getCode() != Data::BlankEnum )
				switch ( eEntry.getDataType() )
				{
					case DataType::DateTimeEnum :
						const OmmDateTime& ommDateTime = eEntry.getDateTime();
						UInt16 year = ommDateTime.getYear();
						...
						UInt8 hour = ommDateTime.getHour();
						break;
				}
		}
	}

	\endcode
	
	\remark OmmDateTime is a read only class.
	\remark This class is used for extraction of DateTime info only.
	\remark All methods in this class are \ref SingleThreaded.

	@see Data,
		EmaString,
		EmaBuffer
*/

#include "Access/Include/Data.h"

namespace thomsonreuters {

namespace ema {

namespace access {

class OmmDateTimeDecoder;

class EMA_ACCESS_API OmmDateTime : public Data
{
public :

	///@name Accessors
	//@{
	/** Returns the DataType, which is the type of Omm data. Results in this class type.
		@return DataType::DateTimeEnum
	*/
	DataType::DataTypeEnum getDataType() const;

	/** Returns the Code, which indicates a special state of a DataType.
		@return Data::BlankEnum if received data is blank; Data::NoCodeEnum otherwise
	*/
	Data::DataCode getCode() const;

	/** Returns a buffer that in turn provides an alphanumeric null-terminated hexadecimal string representation.
		@return EmaBuffer with the object hex information
	*/
	const EmaBuffer& getAsHex() const;

	/** Returns a string representation of the class instance.
		@return string representation of the class instance
	*/
	const EmaString& toString() const;

	/** Returns Year.
		@return value of year
	*/
	UInt16 getYear() const;

	/** Returns Month.
		@return value of month
	*/
	UInt8 getMonth() const;

	/** Returns Day.
		@return value of day
	*/
	UInt8 getDay() const;

	/** Returns Hour.
		@return value of hour
	*/
	UInt8 getHour() const;

	/** Returns Minute.
		@return value of minute
	*/
	UInt8 getMinute() const;

	/** Returns Second.
		@return value of second
	*/
	UInt8 getSecond() const;

	/** Returns Millisecond.
		@return value of millisecond
	*/
	UInt16 getMillisecond() const;

	/** Returns Microsecond.
		@return value of microsecond
	*/
	UInt16 getMicrosecond() const;

	/** Returns Nanosecond.
		@return value of nanosecond
	*/
	UInt16 getNanosecond() const;
	//@}

private :

	friend class Decoder;
	friend class StaticDecoder;

	Decoder& getDecoder();

	const EmaString& toString( UInt64 ) const;

	const Encoder& getEncoder() const;

	OmmDateTime();
	virtual ~OmmDateTime();
	OmmDateTime( const OmmDateTime& );
	OmmDateTime& operator=( const OmmDateTime& );

	OmmDateTimeDecoder*			_pDecoder;
	UInt64						_space[13];
};

}

}

}

#endif // __thomsonreuters_ema_access_OmmDateTime_h
