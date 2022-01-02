#ifndef _IRCCLIENT_H
#define _IRCCLIENT_H

#define SERVER_TARGET "SERVER_TARGET"

struct Message;

typedef void (*IRCUpdateCallback)(struct Message *message, void *userdata);				// general message updates
typedef void (*IRCChannelJoinCallback)(char *channel, char *name, void *userdata);			// name joined #chanel
typedef void (*IRCChannelPartCallback)(char *channel, char *name, char *message, void *userdata);	// name left #channel because #message
typedef void (*IRCChannelQuitCallback)(char *name, char *message, void *userdata);	// name left #channel because #message

void initIRCClient();
int connectToServer(const char *server, int port);
int disconnectFromServer();
void updateIRCClient(IRCUpdateCallback, IRCChannelJoinCallback, IRCChannelPartCallback, IRCChannelQuitCallback, void *);
void sendIRCCommand(const char *command);

#endif

