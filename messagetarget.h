#ifndef _MESSAGETARGET_H
#define _MESSAGETARGET_H

#define MAX_MESSAGE_TARGETS 256
#define MESSAGE_TARGET_CAPACITY 1024

#define MESSAGETARGET_SERVER 0
#define MESSAGETARGET_CHANNEL 1
#define MESSAGETARGET_WHISPER 2

struct Message;
struct IRCConnection;

struct MessageTarget {
	struct IRCConnection *connection;
	
	char *title;
	char *topic;
	struct Message * * messages;
	int messageCapacity;
	int messageAt;
	int type;	// 0 = server, 1 = channel, 2 = whisper
	int index;
};

extern char *  MessageTargetNames[MAX_MESSAGE_TARGETS];
extern struct MessageTarget MessageTargets[MAX_MESSAGE_TARGETS];
extern int NumMessageTargets;

int AddMessageTarget(struct IRCConnection *connection, char *targetName, char *title, int type);
struct MessageTarget * FindMessageTargetByName(struct IRCConnection *connection, char *name);

int RemoveMessageTarget(struct MessageTarget *target);
void RemoveAllMessageTargets();
int RemoveAllMessageTargetsForConnection(struct IRCConnection *connection);

void AddMessageToTarget(struct MessageTarget *target, struct Message *message);

void SetMessageTargetTopic(struct MessageTarget *target, char *topic);

#endif

