using namespace std;
#define __uid_t_defined
#include <iostream>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <EventBus.h>
#include <UdpServer.h>
#include <MqttCbor.h>
#include <MqttGtw.h>
#include <MqttJson.h>
#include <System.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

class FileChecker : public Actor
{
    Str _filename;
    int64_t _lastUpdate;

public:
    FileChecker(const char* name) : Actor(name),_filename("stm32.bin") {
        _lastUpdate=0;

    }
    void init() {

    }

    void setup() {
        eb.onDst(id()).subscribe(this);
        init();
        timeout(5000);
    }

    void onEvent(Cbor& msg) {
        struct stat status;

        if( stat(_filename.c_str(), &status) != 0 ) {
            LOGF(" file '%s' error %d:%s ",_filename.c_str(),errno,strerror(errno));
        } else {
            if ( status.st_mtime > _lastUpdate) {
                _lastUpdate = status.st_mtime;
                LOGF(" file '%s' changed ",_filename.c_str());
                eb.event(me(),H("update"));
                eb.send();
            }

        }
        timeout(5000);
    }
};


class Programmer : public Actor
{
    uid_t  _device;
    uid_t _bootloader;
    uint32_t _counter;
    uint64_t _start=0;
    uid_t _mqtt;
    uint32_t _retries;
    enum { ST_WAIT,ST_READY } _state;
    typedef enum { R_WAIT=0,R_SUCCESS=1,R_FAILURE =2} Result;
    Bytes _cmds;

public:
    Programmer(const char* name) : Actor(name),_cmds(32) {
        _device = H("wibo");
        _bootloader =  H("bootloader");
        _counter=0;
        _mqtt=H("mqttGtw");
        _state = ST_READY;
    }
    void init() {

    }

    void setup() {
        eb.onDst(id()).subscribe(this);
        eb.onRemoteSrc(_device,_bootloader).subscribe(this,(MethodHandler)&Programmer::onRemoteEvent);
        init();
        timeout(5000);
    }

    void onRemoteEvent(Cbor& msg) {
        uid_t event;
        if ( msg.getKeyValue(EB_EVENT,event)) {
            if ( event == H("uart") ) {
                Str str(1024);
                if ( msg.getKeyValue(H("$data"),str)) {
                    fprintf(stdout,"%s",str.c_str());
                }
            }
        }
    }

    Result pingBootloader(Cbor& msg) {
        uid_t req=H("ping");

        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me())
            .addKeyValue(H("counter"),_counter++);
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_bootloader,req) ) return R_SUCCESS;
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;

    }

    Result subscribeBootloaders(Cbor& msg) {
        Str topic(40);
        uid_t req=H("subscribe");
        if ( _state == ST_READY ) {
            topic = "event/+/bootloader/#";
            eb.request(_mqtt, req, me()).addKeyValue(
                H("topic"), topic);
            eb.send();
            timeout(3000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_mqtt,req) ) return R_SUCCESS;
            if (eb.isReply(_mqtt,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;

    }
    Result resetToBootloader(Cbor& msg) {
        uid_t req=H("resetBootloader");
        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me());
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_bootloader,req) ) return R_SUCCESS;
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;
    }

    Result getCmds(Cbor& msg) {
        uid_t req=H("get");
        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me());
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_bootloader,req) ) {
                msg.getKeyValue(H("$cmds"),_cmds);
                return R_SUCCESS;
            }
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;
    }




    void onEvent(Cbor& msg) {
        Str topic(40) ;
        Result r;
        PT_BEGIN();
WAIT_CONNECT : {
            while(true) {
                PT_YIELD_UNTIL ( (r = pingBootloader(msg)) != R_WAIT );
                if ( r!=R_SUCCESS ) continue;
                PT_YIELD_UNTIL ( (r = subscribeBootloaders(msg)) != R_WAIT );
                if ( r==R_SUCCESS ) break;
            }
        }
WAIT_FILE_UPDATE : {
            while(true) {
                timeout(10000);
                PT_YIELD_UNTIL( timeout() || eb.isEvent(H("filechecker"),H("update)")));
                goto PROGRAMMING;
            }
        }
PROGRAMMING : {
            _retries=0;
            while(true) {
// switch to bootloader
                _retries++;
                if ( _retries > 4 ) goto WAIT_CONNECT;
                PT_YIELD_UNTIL ( (r = resetToBootloader(msg)) != R_WAIT );
                if ( r != R_SUCCESS) continue;
                PT_YIELD_UNTIL ( (r = getCmds(msg)) != R_WAIT );
                if ( r != R_SUCCESS) continue;

// load permissible commands
// load file
// program file
// switch to reset
                goto WAIT_FILE_UPDATE;
            }
        }
        PT_END();
    }
};
#define EB_PROGRAMMER H("programmer")
Programmer programmer("programmer");
FileChecker fileChecker("fileChecker");

EventBus eb(10240,300);
Log logger(400);
Uid uid(200);
//MqttCbor router("mqttCbor");
MqttJson router("mqttJson");
MqttGtw mqttGtw("mqttGtw");
System systm;
UdpServer udp("udp");

void poller(int fd1, uid_t id1,int fd2, uid_t id2,uint64_t sleepTill);

Str str(300);
void logCbor(Cbor& msg)
{
    eb.log(str,msg);
    LOGF("%s",str.c_str());
}
#include "main_labels.h"
void setup()
{
    Sys::init();
    uid.add(labels,LABEL_COUNT);
    eb.setup();
    eb.onAny().subscribe(logCbor);
    router.setMqttId(mqttGtw.id());
    router.setup();
    mqttGtw.setup();
    systm.setup();
    programmer.setup();
    fileChecker.setup();
    logger.level(Log::LOG_INFO);

    eb.onDst(H("wibo")).subscribe([](Cbor& msg) {
        router.ebToMqtt(msg);
    });

}

void loop()
{
//    LOGF("wait for io , timeout :%d",Actor::lowestTimeout()-Sys::millis());
    poller(mqttGtw.fd(),mqttGtw.id(),0,H("zero"), Actor::lowestTimeout());
    eb.eventLoop();
}

int main(int argc, char **argv)
{
    LOGF("version : " __DATE__ " " __TIME__ " " __FILE__ " ");
    logger.level(Log::LOG_INFO);
    setup();
    while(1)
        loop();
}


//_______________________________________________________________________________________
//
// simulates RTOS generating events into queue : Timer::TICK,Serial::RXD,Serial::CONNECTED,...
//_______________________________________________________________________________________

void poller(int fd1,uid_t id1,int fd2,uid_t id2,uint64_t sleepTill)
{
    /*   Cbor cbor(1024);
       Bytes bytes(1024);  */
    uint8_t buffer[1024];
    fd_set rfds;
    fd_set wfds;
    fd_set efds;
    struct timeval tv;
    int retval;
    uint64_t start = Sys::millis();
    //    uint64_t delta=1000;
    if(fd1 == 0 && fd2 == 0) {
        usleep(1000);
        //        eb.publish(H("sys"),H("tick"));
    } else {

        // Wait up to 1000 msec.
        uint64_t delta = 5000;
        if(sleepTill > Sys::millis()) {
            delta = sleepTill - Sys::millis();
        } else {
            DEBUG(" now : %llu  next timeout : %llu  ", Sys::millis(), sleepTill);
            for (Actor* cursor=Actor::first(); cursor; cursor=cursor->next()) {
                if ( cursor->nextTimeout()==sleepTill ) {
                    LOGF(" awaiting Actor : %s",cursor->name());
                    break;
                }
            }
            delta = 0;
        }
        //   delta+=1; // timeout tests for higher then
        tv.tv_sec = delta / 1000;
        tv.tv_usec = (delta * 1000) % 1000000;

        // Watch fd1 and fd2  to see when it has input.
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
        if(fd1)
            FD_SET(fd1, &rfds);
        if(fd2)
            FD_SET(fd2, &rfds);
        if(fd1)
            FD_SET(fd1, &efds);
        if(fd2)
            FD_SET(fd2, &efds);
        int maxFd = fd1 < fd2 ? fd2 : fd1;
        maxFd += 1;

        start = Sys::millis();
//        LOGF(" before select() wait %d ",delta);

        retval = select(maxFd, &rfds, NULL, &efds, &tv);

        //LOGF(" after select() rc=%d ",retval);

        uint64_t waitTime = Sys::millis() - start;
        if(waitTime == 0) {
            TRACE(" waited %ld/%ld msec.", waitTime, delta);
        }

        if(retval < 0) {
            WARN(" select() : error : %s (%d)", strerror(errno), errno);
        } else if(retval > 0) { // one of the fd was set
            if(FD_ISSET(fd1, &rfds)) {
                ::read(fd1, buffer, sizeof(buffer)); // empty event pipe
                eb.event(id1,H("signal"));
                eb.send();
            }
            if(FD_ISSET(fd2, &rfds)) {
                ::read(fd2, buffer, sizeof(buffer)); // empty event pipe
                eb.event(id2,H("signal"));
                eb.send();
            }
            if(FD_ISSET(fd1, &efds)) {
                eb.event(id1, H("err"))
                .addKeyValue(H("error"), errno)
                .addKeyValue(H("error_msg"), strerror(errno));
                eb.send();
            }
            if(FD_ISSET(fd2, &efds)) {
                eb.event(id2, H("err"))
                .addKeyValue(H("error"), errno)
                .addKeyValue(H("error_msg"), strerror(errno));
                eb.send();
            }
        } else {
            TRACE(" timeout %u", delta);
        }
    }
}
/* EXAMPLE TEMPLATE
EventBus eb(10240,300);
Log logger(256);
Uid uid(200);
UdpServer udp("udp");

void poller(int fd1, uid_t id1,int fd2, uid_t id2,uint64_t sleepTill);

Str str(300);
void logCbor(Cbor& msg)
{
    eb.log(str,msg);
    LOGF("%s",str.c_str());
}
#include "main_labels.h"
void setup()
{
    uid.add(labels,LABEL_COUNT);
    eb.setup();
    eb.onAny().subscribe(logCbor);
    udp.setup();
}

void loop()
{

    poller(udp.fd(),H("udp"),0,H("zero"), Actor::lowestTimeout());
    LOGF("");
    eb.eventLoop();

}

int main(int argc, char **argv)
{
    LOGF("version : " __DATE__ " " __TIME__ " " __FILE__ " ");
    setup();
    while(1)
        loop();
}*/
