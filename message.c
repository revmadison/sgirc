#include <stdlib.h>
#include <time.h>

#include "message.h"

struct IRCConnection;

struct Message *MessageInit(struct IRCConnection *connection, char *source, char *target, char *messageBody) {
	struct Message *message = (struct Message *)malloc(sizeof(struct Message));

	message->connection = connection;

	if (source) {
		message->source = strdup(source);
	} else {
		message->source = NULL;
	}	

	if (target) {
		message->target =  strdup(target);
	} else {
		message->target = strdup("");
	}

	if (messageBody) {
		message->message = strdup(messageBody);
	} else {
		message->message = strdup("");
	}	

	message->timestamp = time(NULL);
	cftime(message->timestampString, "%T", &message->timestamp);

	message->type = MESSAGE_TYPE_NORMAL;

	return message;
}

void MessageFree(struct Message *message) {
	if (message->message) {
		free(message->message);
	}
	if (message->target) {
		free(message->target);
	}
	if (message->source) {
		free(message->source);
	}
	free(message);
}

