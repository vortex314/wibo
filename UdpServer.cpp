#include "UdpServer.h"

UdpServer::UdpServer(const char* name) : Actor(name),_rxd(300)
{
    _localPort=20000;
}

UdpServer::~UdpServer()
{
}




void UdpServer::setup()
{
    init();
    eb.onEvent(id(),H("signal")).subscribe(this,(MethodHandler)&UdpServer::readMessage);
}

void UdpServer::onEvent(Cbor& msg)
{

}

void UdpServer::init()
{
    _bindSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(_listenFd < 0) {
        LOGF("Cannot open socket" );
    }
    bzero((char*) &_localAddr, sizeof(_localAddr));

    _localAddr.sin_family = AF_INET;
    _localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    _localAddr.sin_port = htons(_localPort);

    //bind socket
    if(bind(_bindSocket, (struct sockaddr *)&_localAddr, sizeof(_localAddr)) < 0) {
        LOGF("Cannot bind" );
    }
}

int UdpServer::fd()
{
    return _bindSocket;
}

void UdpServer::readMessage(Cbor& cbor)
{
    socklen_t  len = sizeof(_remoteAddr);
#define BUFLEN 1024
    uint8_t buf[BUFLEN];
    uint16_t buflen;

    if ( (buflen=recvfrom(_bindSocket, buf, BUFLEN, 0, (sockaddr*) &_remoteAddr, &len)) <0 ) {
        return;
    }
    printf("Received packet from %s:%d\nData: %s\n\n",
           inet_ntoa(_remoteAddr.sin_addr), ntohs(_remoteAddr.sin_port), buf);
    Bytes bytes(buf,buflen);
    eb.event(id(),H("rxd")).addKeyValue(H("data"),bytes);
    eb.send();
}

void UdpServer::setRemote(const char* host,uint16_t port)
{
    _remoteHost = host;
    _remotePort = port;

    struct hostent *server;

    server = gethostbyname(host);

    if(server == NULL) {
        LOGF("Host does not exist" );
    }

    bzero((char *) &_remoteAddr, sizeof(_remoteAddr));
    _remoteAddr.sin_family = AF_INET;

    bcopy((char *) server -> h_addr, (char *) &_remoteAddr.sin_addr.s_addr, server -> h_length);

    _remoteAddr.sin_port = htons(port);

}


void UdpServer::send(Bytes& bytes)
{

    int slen=sizeof(_remoteAddr);
    int s;


    if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
        LOGF("socket create failed ");
        return;
    }
    /*
        memset((char *) &si_other, 0, sizeof(si_other));
        si_other.sin_family = AF_INET;
        si_other.sin_port = htons(PORT);
        if (inet_aton(SRV_IP, &si_other.sin_addr)==0) {
            fprintf(stderr, "inet_aton() failed\n");
            exit(1);
        }
    */

    if (sendto(s, bytes.data(), bytes.length(), 0,(sockaddr*) &_remoteAddr, slen)==-1)
        LOGF("sendto() failed");

    close(s);
}



void UdpServer::send(const char* host,uint16_t port, Bytes& data)
{


    struct sockaddr_in svrAdd;
    struct hostent *server;



    //create client skt
    _listenFd = socket(AF_INET, SOCK_STREAM, 0);

    if(_listenFd < 0) {
        LOGF( "Cannot open socket" );
    }

    server = gethostbyname(host);

    if(server == NULL) {
        LOGF("Host does not exist" );
    }

    bzero((char *) &svrAdd, sizeof(svrAdd));
    svrAdd.sin_family = AF_INET;

    bcopy((char *) server -> h_addr, (char *) &svrAdd.sin_addr.s_addr, server -> h_length);

    svrAdd.sin_port = htons(port);

    int checker = connect(_listenFd,(struct sockaddr *) &svrAdd, sizeof(svrAdd));

    if (checker < 0) {
        LOGF("Cannot connect!" );
    }




    write(_listenFd, data.data(), data.length());


}
