#ifndef _IRCCLIENT_H
#define _IRCCLIENT_H

#define SERVER_TARGET "SERVER_TARGET"

struct Message;
struct IRCConnection;

typedef void (*IRCUpdateCallback)(struct IRCConnection *c, struct Message *message, void *userdata);				// general message updates
typedef void (*IRCChannelJoinCallback)(struct IRCConnection *c, char *channel, char *name, void *userdata);			// name joined #chanel
typedef void (*IRCChannelPartCallback)(struct IRCConnection *c, char *channel, char *name, char *message, void *userdata);	// name left #channel because #message
typedef void (*IRCChannelQuitCallback)(struct IRCConnection *c, char *name, char *message, void *userdata);	// name left #channel because #message
typedef void (*IRCChannelTopicCallback)(struct IRCConnection *c, char *channel, char *topic, void *userdata);			// name joined #chanel

struct IRCConnection {
	int activeSocket;
	int connected;

	char * readBuffer;
	int readBufferSize;
	int readBufferAt;

	char * commandBuffer;
	int commandBufferSize;
	int commandBufferAt;

	IRCUpdateCallback messageCallback;
	IRCChannelJoinCallback joinCallback;
	IRCChannelPartCallback partCallback;
	IRCChannelQuitCallback quitCallback;
	IRCChannelTopicCallback topicCallback;
};

void initIRCConnection(struct IRCConnection *c);
int connectToServer(struct IRCConnection *c, const char *server, int port);
int disconnectFromServer(struct IRCConnection *c);
void updateIRCClient(struct IRCConnection *c, void *);
void sendIRCCommand(struct IRCConnection *c, const char *command);

#endif

