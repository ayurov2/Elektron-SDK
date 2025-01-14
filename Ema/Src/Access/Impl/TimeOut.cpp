/*|-----------------------------------------------------------------------------
 *|            This source code is provided under the Apache 2.0 license      --
 *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
 *|                See the project's LICENSE.md for details.                  --
 *|           Copyright Thomson Reuters 2015. All rights reserved.            --
 *|-----------------------------------------------------------------------------
 */

#include "OmmConsumerImpl.h"
#include "TimeOut.h"
#include "Utilities.h"

using namespace thomsonreuters::ema::access;

#ifdef WIN32
TimeOutTimeType TimeOut::frequency = {0,0};
#endif

TimeOut::TimeOut( OmmConsumerImpl & consumer, Int64 lengthInMicroSeconds, void (* functor)( void * ), void * args )
	: theConsumer( consumer), _lengthInMicroSeconds( lengthInMicroSeconds ), _functor( functor ), _args( args ), canceled( false )
{
#ifdef WIN32
	if ( ! frequency.QuadPart )
		QueryPerformanceFrequency( &frequency );

	QueryPerformanceCounter( &setAt );
	timeoutTime.QuadPart = setAt.QuadPart + (frequency.QuadPart * lengthInMicroSeconds)/1000000;
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	setAt = ts.tv_sec * static_cast<int>(1E9) + ts.tv_nsec;
	timeoutTime = setAt + lengthInMicroSeconds * 1000;
#endif				

	theConsumer.getTimeOutList().insert( this );

	theConsumer.installTimeOut();
}

bool
TimeOut::operator<( const TimeOut & rhs ) const
{
#ifdef WIN32
	return timeoutTime.QuadPart < rhs.timeoutTime.QuadPart;
#else
	return timeoutTime < rhs.timeoutTime;
#endif
}

bool
TimeOut::getTimeOutInMicroSeconds( OmmConsumerImpl & consumer, Int64 & value )
{
	MutexLocker ml( consumer.getTimeOutMutex() );

	EmaList< TimeOut > & theTimeOuts( consumer.getTimeOutList() );

	if ( theTimeOuts.empty() )
		return false;

	TimeOut * p( theTimeOuts.front() );
	while( p->canceled )
	{
		TimeOut * toBeDeleted( p );
		p = p->next();
		theTimeOuts.remove( toBeDeleted );
		delete toBeDeleted;
		if ( ! p )
			return false;
	}
	TimeOutTimeType current;
#ifdef WIN32
	QueryPerformanceCounter( &current );
	if ( p->timeoutTime.QuadPart < current.QuadPart )
		value = 0;
	else
		value = static_cast< Int64> ( ( p->timeoutTime.QuadPart - current.QuadPart ) * 1000000 / frequency.QuadPart );
#else
	struct timespec ts;
	clock_gettime( CLOCK_REALTIME, &ts );
	current = ts.tv_sec * static_cast<int>( 1E9 ) + ts.tv_nsec;
	if ( p->timeoutTime < current )
		value = 0;
	else
		value = static_cast< Int64> ( ( p->timeoutTime - current ) / 1000 );
#endif
	return true;
}

void
TimeOut::execute( OmmConsumerImpl & consumer, EmaList<TimeOut> & theTimeOuts )
{
	MutexLocker ml( consumer.getTimeOutMutex() );

	TimeOutTimeType current;
#ifdef WIN32
	QueryPerformanceCounter( &current );
#else
	struct timespec ts;
	clock_gettime( CLOCK_REALTIME, &ts );
	current = ts.tv_sec * static_cast<int>( 1E9 ) + ts.tv_nsec;
#endif

	TimeOut * p( theTimeOuts.front() );
	while( p )
	{
#ifdef WIN32
		if ( current.QuadPart >= p->timeoutTime.QuadPart )
#else
		if ( current >= p->timeoutTime )
#endif
		{
			if ( ! p->canceled )
				(*p)();
			delete theTimeOuts.pop_front();
			p = theTimeOuts.front();
		}
		else
			return;
	}
}
