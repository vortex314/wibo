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

void interceptAllSignals();
void loadOptions(int argc, char* argv[]);
struct {
	const char* host;
	uint16_t port;
	const char* binFile;
	uint32_t baudrate;
	const char* device;
	Log::LogLevel logLevel;

} context = { "lmr.ddns.net", 1883, "./firmware.bin", 115200, "/dev/ttyACM0", Log::LOG_DEBUG };

Bytes* binImage=0;

class FileChecker : public Actor
{
    Str _filename;
    int64_t _lastUpdate;
    uint32_t _fileSize;

public:
    FileChecker(const char* name) : Actor(name),_filename(100) {
        _lastUpdate=0;

    }
    void init() {

    }

    void setup() {
        eb.onDst(id()).subscribe(this);
        init();
        timeout(5000);
    }

    void setFile(char* name) {
        _filename=name;
    }

    void onEvent(Cbor& msg) {
        struct stat status;
        timeout(3000);
        if( stat(_filename.c_str(), &status) != 0 ) {
            WARN(" file stat('%s') failed  error %d:%s ",_filename.c_str(),errno,strerror(errno));
        } else {
            if ( status.st_mtime > _lastUpdate ) {
                _lastUpdate = status.st_mtime;
                _fileSize =  status.st_size;
                INFO(" file '%s' changed ",_filename.c_str());

                FILE* fd=fopen(_filename.c_str(),"r");
                if ( fd==NULL) {
                    WARN(" file open('%s') failed  %d:%s",_filename.c_str(),errno,strerror(errno));
                    return;
                }
                if ( binImage ) delete binImage;
                binImage = new Bytes(_fileSize);
                while(!feof(fd)) {
                    uint8_t b;
                    if ( fread(&b,1,1,fd) != 1 ) break;
                    binImage->write(b);
                }
                fclose(fd);
                eb.event(me(),H("update")).addKeyValue(H("file"),_filename);
                eb.send();
            } else {
                INFO(" file '%s' unchanged ",_filename.c_str());
            }

        }

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
    uint16_t _chipId;
    uint16_t _version;
    uint32_t _offset;
    Bytes _block;
    uint32_t _blockSize;

public:
    Programmer(const char* name) : Actor(name),_cmds(32),_block(256) {
        _device = H("wibo");
        _bootloader =  H("bootloader");
        _counter=0;
        _mqtt=H("mqttGtw");
        _state = ST_READY;
        _blockSize=16;
    }
    void init() {

    }

    void setup() {
        eb.onDst(id()).subscribe(this);
        eb.onEvent(H("filechecker"),H("update")).subscribe(this);
        eb.onRemoteSrc(_device,_bootloader).subscribe(this,(MethodHandler)&Programmer::onRemoteEvent);
        init();
        timeout(1000);
    }

    void onRemoteEvent(Cbor& msg) {
        uid_t event;
        if ( msg.getKeyValue(EB_EVENT,event)) {
            if ( event == H("log") ) {
                Str str(1024);
                if ( msg.getKeyValue(H("$data"),str)) {
                    fprintf(stdout," UART : %s\n",str.c_str());
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

    Result setLogLevel(Cbor& msg,int level) {
        uid_t req=H("set");
        uid_t service=H("system");

        if ( _state == ST_READY ) {
            eb.requestRemote(_device,service,req,me())
            .addKeyValue(H("log_level"),level);
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(service,req) ) return R_SUCCESS;
            if (eb.isReply(service,req) ) return R_FAILURE;
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
        uid_t req=H("reset");
        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me()).addKeyValue(H("boot0"),1);
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
            eb.send();
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_bootloader,req) ) return R_SUCCESS;
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;

    }


    Result resetToFlash(Cbor& msg) {
        uid_t req=H("reset");
        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me()).addKeyValue(H("boot0"),0);
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

    Result getInfo(Cbor& msg) {
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
                msg.getKeyValue(H("chipId"),_chipId);
                msg.getKeyValue(H("version"),_version);
                INFO(" found µC - id : 0x%X - version : 0x%X ",_chipId,_version);
                return R_SUCCESS;
            }
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;
    }

    Result eraseAll(Cbor& msg) {
        uid_t req=H("eraseAll");
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
//TODO                INFO(" found µC - id : 0x%X - version : 0x%X ",_chipId,_version);
                return R_SUCCESS;
            }
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;
    }

    Result writeMemory(Cbor& msg,Bytes& block,uint32_t offset) {

        uid_t req=H("writeMemory");
        if ( _state == ST_READY ) {
            eb.requestRemote(_device,_bootloader,req,me())
            .addKeyValue(H("$data"),block)
            .addKeyValue(H("address"),  0x8000000+offset);
            eb.send();
            timeout(1000);
            _state=ST_WAIT;
            return R_WAIT;
        } else if ( _state==ST_WAIT ) {
            _state=ST_READY;
            if ( timeout()) return R_FAILURE;
            if (eb.isReplyCorrect(_bootloader,req) ) {
//TODO                INFO(" found µC - id : 0x%X - version : 0x%X ",_chipId,_version);
                return R_SUCCESS;
            }
            if (eb.isReply(_bootloader,req) ) return R_FAILURE;
        }
        _state=ST_WAIT;
        return R_WAIT;
    }



    void onEvent(Cbor& msg) {
        Str topic(40) ;
        Str line(600);
        Result r;
        uint32_t count;
        PT_BEGIN();
WAIT_CONNECT : {
            while(true) {
                timeout(1000);
                PT_YIELD_UNTIL(timeout());
                PT_WAIT_UNTIL ( (r = pingBootloader(msg)) != R_WAIT );
                if ( r!=R_SUCCESS ) continue;
                PT_WAIT_UNTIL ( (r = setLogLevel(msg,Log::LOG_INFO)) != R_WAIT );
                if ( r!=R_SUCCESS ) continue;
                PT_WAIT_UNTIL ( (r = subscribeBootloaders(msg)) != R_WAIT );
                if ( r!=R_SUCCESS ) continue;
                PT_WAIT_UNTIL ( (r = setLogLevel(msg,Log::LOG_NONE)) != R_WAIT );
                if ( r!=R_SUCCESS ) continue;
                PT_WAIT_UNTIL ( (r = resetToBootloader(msg)) != R_WAIT );
                if ( r != R_SUCCESS) continue;
                PT_WAIT_UNTIL ( (r = getInfo(msg)) != R_WAIT );
                if ( r != R_SUCCESS) continue;
                PT_WAIT_UNTIL ( (r = resetToFlash(msg)) != R_WAIT );
                if ( r == R_SUCCESS) goto WAIT_FILE_UPDATE;


            }
        }
WAIT_FILE_UPDATE : {
            while(true) {
                timeout(100000);
                INFO(" wait file update, capturing log uart. ");
                PT_YIELD_UNTIL( timeout()
                                || eb.isEvent(H("filechecker"),H("update")));
                if ( timeout()) continue ;
                INFO(" file updated ");
                goto PROGRAMMING;
            }
        }
PROGRAMMING : {
            INFO(" Programming %d bytes.....",binImage->length());
            _retries=0;
            _offset=0;
//            _blockSize=20;

// switch to bootloader
            PT_WAIT_UNTIL ( (r = resetToBootloader(msg)) != R_WAIT );
            if ( r != R_SUCCESS) goto WAIT_CONNECT;
// program file
            PT_WAIT_UNTIL ( (r = eraseAll(msg)) != R_WAIT );
            if ( r != R_SUCCESS) {
                WARN(" rease Flash µC failed. ");
                goto WAIT_CONNECT;
            }
            while(_offset < binImage->length()) {
                _block.clear();
                binImage->offset(_offset);
                count=0;
//                _blockSize += 4;
                while ( binImage->hasData() && _block.hasSpace(1) ) {
                    if ( ++count > _blockSize ) break;
                    _block.write(binImage->read());
                }
                fprintf(stdout,".");
                fflush(stdout);
                line.clear();
                _block.toHex(line);
                DEBUG("%X:%s\n",_offset,line.c_str());
                PT_WAIT_UNTIL ( (r = writeMemory(msg,_block,_offset)) != R_WAIT );
                if ( r != R_SUCCESS) {
                    WARN(" writing block 0x%X failed. ",_offset);
                    goto WAIT_CONNECT;
                }
                _offset += _block.length();

            }
            PT_WAIT_UNTIL ( (r = resetToFlash(msg)) != R_WAIT );
            fprintf(stdout,"\n");
            if ( r != R_SUCCESS) {
                WARN(" reset µC failed. ");
                goto WAIT_CONNECT;
            }
            INFO(" Programming done. Running.");
            goto WAIT_FILE_UPDATE;


// load permissible commands
// load file

// switch to reset
            goto WAIT_FILE_UPDATE;
        }
        PT_END();
    }
};

#define EB_PROGRAMMER H("programmer")
Programmer programmer("programmer");
FileChecker fileChecker("filechecker");

EventBus eb(10240,1024);
Log logger(1024);
Uid uid(200);
//MqttCbor router("mqttCbor");
MqttJson router("mqttJson",1024);
MqttGtw mqttGtw("mqttGtw");
System systm;

UdpServer udp("udp");

void poller(int fd1, uid_t id1,int fd2, uid_t id2,uint64_t sleepTill);

Str str(1024);
void logCbor(Cbor& msg)
{
    eb.log(str,msg);
    DEBUG("%s",str.c_str());
}
#include "main_labels.h"
void setup()
{
    interceptAllSignals();
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
    INFO("version : " __DATE__ " " __TIME__ " " __FILE__ " ");
    loadOptions(argc, argv);
    logger.level(context.logLevel);
    setup();
    fileChecker.setFile((char*)context.binFile);
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
                    WARN(" awaiting Actor : %s",cursor->name());
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

/*_______________________________________________________________________________

 loadOptions  role :
 - parse commandline otions
 h : host of mqtt server
 p : port
 d : the serial device "/dev/ttyACM*"
 b : the baudrate set ( only usefull for a serial2serial box or a real serial port )
 ________________________________________________________________________________*/

#include "Log.h"

void loadOptions(int argc, char* argv[])
{
	int c;
	while((c = getopt(argc, argv, "h:p:d:b:l:f:")) != -1)
		switch(c) {
		case 'h':
			context.host =  optarg;
			break;
		case 'p':
			context.port = atoi(optarg);
			break;
		case 'f':
			context.binFile = optarg;
			break;
		case 'd':
			context.device = optarg;
			break;
		case 'b':
			context.baudrate = atoi(optarg);
			break;
		case 'l':
			if(strcmp(optarg, "DEBUG") == 0)
				context.logLevel = Log::LOG_DEBUG;
			if(strcmp(optarg, "INFO") == 0)
				context.logLevel = Log::LOG_INFO;
			if(strcmp(optarg, "WARN") == 0)
				context.logLevel = Log::LOG_WARN;
			if(strcmp(optarg, "ERROR") == 0)
				context.logLevel = Log::LOG_ERROR;
			if(strcmp(optarg, "FATAL") == 0)
				context.logLevel = Log::LOG_FATAL;
			break;
		case '?':
			if(optopt == 'c')
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
			else if(isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
			return;
		default:
			abort();
			break;
		}
}

#include <execinfo.h>
#include <signal.h>

void SignalHandler(int signal_number)
{
	void* array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "Error: signal %d:%s \n", signal_number, strsignal(signal_number));
	fprintf(stderr, "Error: errno %d:%s \n", errno, strerror(errno));
	backtrace_symbols_fd(array, size, STDERR_FILENO);
	exit(1);
}

void interceptAllSignals()
{
	signal(SIGFPE, SignalHandler);
	signal(SIGILL, SignalHandler);
	signal(SIGINT, SignalHandler);
	signal(SIGSEGV, SignalHandler);
	signal(SIGTERM, SignalHandler);
}