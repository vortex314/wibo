#ifndef UDPSERVER_H
#define UDPSERVER_H
#define __uid_t_defined
#include <EventBus.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <strings.h>
#include <stdlib.h>
#include <string>
#include <pthread.h>


#define UDP_MAX_SIZE 300

class UdpServer : public Actor
{
    uint16_t _localPort;
    struct sockaddr_in _localAddr;
    int _bindSocket;
    
    const char* _remoteHost;
    struct sockaddr_in _remoteAddr;
    uint16_t _remotePort;
    
    
    int _fd;
    int _listenFd;
    int _clientFd;
    struct sockaddr_in _server;
    struct sockaddr_in _client;
    Bytes _rxd;
public:
    UdpServer(const char* name);
    ~UdpServer();
    void readMessage(Cbor& cbor);
    void setRemote(const char* host,uint16_t port);
    void setLocal(const char* host,uint16_t port);
    void setup();
    void init();
    void onEvent(Cbor& cbor);
    bool isConnected() ;
    void loop() ;
    void send(const char* ahost,uint16_t port,Bytes& data);
    void reply(Bytes& bytes);
    int fd();
    void send(Bytes& data);

};

#endif // UDPSERVER_H
