#include "Sys.h"

#include <malloc.h>
#include <unistd.h> // gethostname
#include <string.h>
#include <time.h>
#include <Log.h>


char Sys::_hostname[30] ;

uint64_t Sys::_upTime=0;
uint64_t Sys::_boot_time=0;


#ifdef __linux__
#include <time.h>

uint64_t Sys::millis()   // time in msec since boot, only increasing
{
    struct timespec deadline;
    clock_gettime((int)CLOCK_MONOTONIC, &deadline);
    Sys::_upTime= deadline.tv_sec*1000 + deadline.tv_nsec/1000000;
    return _upTime;
}


#endif

void Sys::init()
{
    gethostname(_hostname,30);
}



void Sys::delay(uint32_t time)
{
    usleep(time*1000);
}

uint64_t Sys::now()
{
    return _boot_time+Sys::millis();
}

void Sys::setNow(uint64_t n)
{
    _boot_time = n-Sys::millis();
}

void Sys::hostname(const char* hn)
{
    return;
    LOGF("%s:%d",hn,strlen(hn));
    delay(100);
    strncpy(_hostname , hn,strlen(hn));
}

const char* Sys::hostname()
{

    return _hostname;
}
