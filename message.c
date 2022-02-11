#include <stdlib.h>
#include <time.h>

#include "message.h"

struct IRCConnection;

extern void FreeImagePreview(Pixmap preview);

struct Message *MessageInit(struct IRCConnection *connection, char *source, char *target, char *messageBody) {
	struct Message *message = (struct Message *)malloc(sizeof(struct Message));

	message->connection = connection;

	if(source) {
		message->source = strdup(source);
	} else {
		message->source = NULL;
	}	

	if(target) {
		message->target =  strdup(target);
	} else {
		message->target = strdup("");
	}

	if(messageBody) {
		message->message = strdup(messageBody);
	} else {
		message->message = strdup("");
	}	

	message->timestamp = time(NULL);
	cftime(message->timestampString, "%T", &message->timestamp);

	message->type = MESSAGE_TYPE_NORMAL;

	message->brokenWidth = -1;
	message->lineCount = 0;
	message->lineBreaks = NULL;
	message->display = NULL;
	message->url = NULL;
	message->imagePreview = None;
	message->imagePreviewHeight = 0;
	message->cancelImageFetch = NULL;
	return message;
}

void MessageFree(struct Message *message) {
	if(message->cancelImageFetch) {
		*message->cancelImageFetch = 1;
		message->cancelImageFetch = NULL;
	}

	if(message->message) {
		free(message->message);
	}
	if(message->target) {
		free(message->target);
	}
	if(message->source) {
		free(message->source);
	}
	if(message->lineBreaks) {
		free(message->lineBreaks);
	}
	if(message->display) {
		free(message->display);
	}
	if(message->url) {
		free(message->url);
	}
	if(message->imagePreview != None) {
		FreeImagePreview(message->imagePreview);
	}
	free(message);
}

