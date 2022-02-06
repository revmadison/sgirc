#ifndef _MESSAGE_H
#define _MESSAGE_H

#define MESSAGE_TYPE_NORMAL 0
#define MESSAGE_TYPE_SERVERMESSAGE 1
#define MESSAGE_TYPE_ENDOFLOG 2

struct IRCConnection;

struct Message {
	struct IRCConnection *connection;
	char *source;	// Can be NULL
	char *target;	// Defaults to SERVER_TARGET
	char *message;
	long timestamp;
	char timestampString[32];
	int type;
};

struct Message *MessageInit(struct IRCConnection *connection, char *source, char *target, char *message);
void MessageFree(struct Message *);	// This also frees the Message * you pass in

#endif

