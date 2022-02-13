#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include <netinet/in.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "ircclient.h"
#include "message.h"

//#define LOG_ALL 1

void initIRCConnection(struct IRCConnection *c) {
	c->activeSocket = -1;
	c->connected = 0;

	c->readBuffer = (char *)malloc(4096);
	c->readBufferSize = 4096;
	c->readBufferAt = 0;

	c->commandBuffer = (char *)malloc(4096);
	c->commandBufferSize = 4096;
	c->commandBufferAt = 0;
}

void sendIRCCommand(struct IRCConnection *c, const char *command) {
	if (!c->connected) {
		return;
	}
#if DEBUG
	printf("Sending: %s\n", command);
#endif
	send(c->activeSocket, command, strlen(command), 0);
	send(c->activeSocket, "\r\n", 2, 0);
}

char *parseFirstWord(char *str, int *offset) {
	char *start = str+*offset;
	char *firstWord;
	int newoff;

	char *firstSpace = strchr(start, ' ');
	if (firstSpace) {
		*firstSpace = 0;
		firstWord = strdup(start);
		*firstSpace = ' ';
	
		newoff = (firstSpace-start)+1;
	} else {
		firstWord = strdup(start);
		newoff = strlen(firstWord);
	}

	*offset = *offset + newoff;
	return firstWord;
}

char *parseCommandFromMessage(char *srvmessage, int *offset) {
	return parseFirstWord(srvmessage, offset);
}

char *parseSourceFromMessage(char *srvmessage, int *offset) {
	char *start = srvmessage+*offset;
	char *source = NULL;

	if (*start == ':') {
		char *firstBang = strchr(start+1, '!');
		char *firstSpace = strchr(start+1, ' ');

		if (firstBang != NULL && firstSpace != NULL) {
			if (firstBang < firstSpace) {
				*firstBang = 0;
				source = strdup(start+1);
				*firstBang = '!';
			} else {
				*firstSpace = 0;
				source = strdup(start+1);
				*firstSpace = ' ';
			}
			int newoff = (firstSpace-start)+1;
			*offset = *offset + newoff;
		} else if(firstSpace != NULL) {
			*firstSpace = 0;
			source = strdup(start+1);
			*firstSpace = ' ';
			int newoff = (firstSpace-start)+1;
			*offset = *offset + newoff;
		}
	}
	return source;
}


struct Message *parseServerMessage(struct IRCConnection *c, char *srvmessage, void *userdata) {
#if LOG_ALL
	FILE *fp = fopen("logfile.txt", "wa");
	fprintf(fp, "%s", srvmessage);
	fclose(fp);
#endif

	int offset = 0;
	struct Message *message = NULL;

	if (strlen(srvmessage) == 0) {
		return NULL;
	}

	char *source = parseSourceFromMessage(srvmessage, &offset);	// needs to be freed

	char *fullCommandAndParams = srvmessage+offset;

	char *command = parseCommandFromMessage(srvmessage, &offset);	// needs to be freed

	char *params = srvmessage+offset;

	if (command == NULL || strlen(command) == 0) {
#if DEBUG
		printf("Failed parsing server message: [%s]\n", srvmessage);
#endif
	}

	if(!strcasecmp(command, "PING")) {
		// Background processing, no need to generate a Message from this
		send(c->activeSocket, "PONG ", 5, 0);
		send(c->activeSocket, params, strlen(params), 0);
		send(c->activeSocket, "\r\n", 2, 0);
	} else if(!strcasecmp(command, "PRIVMSG")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
		char *messageBody = params+paramOffset;
		if(*messageBody == ':') {
			messageBody++;
		}

		message = MessageInit(c, source, target, messageBody);	
		free(target);
	} else if(!strcasecmp(command, "JOIN")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
	
		c->joinCallback(c, target, source, userdata);
		free(target);
	} else if (!strcasecmp(command, "PART")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
		char *partMessage = params+paramOffset;
		if (*partMessage == ':') {
			partMessage++;
		}
	
		c->partCallback(c, target, source, partMessage, userdata);
		free(target);
	} else if (!strcasecmp(command, "QUIT")) {
		int paramOffset = 0;
		char *partMessage = params+paramOffset;
		if (*partMessage == ':') {
			partMessage++;
		}
	
		c->quitCallback(c, source, partMessage, userdata);
	} else if (!strcasecmp(command, "NICK")) {
		int paramOffset = 0;
		char *newNick = params+paramOffset;
		if (*newNick == ':') {
			newNick++;
		}
	
		c->nickCallback(c, source, newNick, userdata);
	} else if (!strcasecmp(command, "332")) {
		int paramOffset = 0;
		char *forwho = parseFirstWord(params, &paramOffset);
		char *target = parseFirstWord(params, &paramOffset);
		char *topic = params+paramOffset;
		if (*topic == ':') {
			topic++;
		}
	
		c->topicCallback(c, target, topic, userdata);
		free(forwho);
		free(target);
	} else if(!strcmp(command, "353")) {
		// NAMES reply, format is '<symbol> <target> :list of user names
		int paramOffset = 0;
		char * forwho = parseFirstWord(params, &paramOffset);
		char * symbol = parseFirstWord(params, &paramOffset);
		char * target = parseFirstWord(params, &paramOffset);
		char * list = params+paramOffset;
		if (*list == ':') {
			list++;
		}

		//printf("Parsing [%s] into symbol [%s] channel [%s] names list: [%s]\n", params, symbol, target, list);
		while(*list) {
			char *nextName = strchr(list, ' ');
			if (!nextName) {
				c->joinCallback(c, target, list, userdata);
				list += strlen(list);
			} else {
				*nextName = 0;
				c->joinCallback(c, target, list, userdata);
				*nextName = ' ';
				list = nextName+1;
			}
		}
	
		free(forwho);
		free(symbol);
		free(target);	
	} else {
#if DEBUG
		printf("Unknown command [%s] from source [%s] in [%s]\n", command, source?source:"NULL", srvmessage);
#endif
		message = MessageInit(c, source, SERVER_TARGET, fullCommandAndParams);
	}

	if (source) free(source);
	if (command) free(command);

	return message;
}

void updateIRCClient(struct IRCConnection *c, void *userdata) {
	if (!c->connected) {
		return;
	}

	int n = recv(c->activeSocket, &c->readBuffer[c->readBufferAt], c->readBufferSize-c->readBufferAt, MSG_DONTWAIT);
	if (n < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			//printf("Error reading from socket: %d!\n", errno);
			printf("Connection has been closed.");
			exit(1);
		}
	}

	if (n > 0) {
		c->readBufferAt += n;
	}

	int commandFound;
	do {
		int copied = 0;
		commandFound = 0;
		while (c->commandBufferAt < c->commandBufferSize && copied < c->readBufferAt) {
			c->commandBuffer[c->commandBufferAt] = c->readBuffer[copied++];

			if (c->commandBuffer[c->commandBufferAt] == '\n') {
				c->commandBuffer[c->commandBufferAt] = 0;
				if(c->commandBufferAt > 0 && c->commandBuffer[c->commandBufferAt-1] == '\r') {
					c->commandBuffer[c->commandBufferAt-1] = 0;
				}

				commandFound = 1;
				break;
			} else {
				c->commandBufferAt++;
			}
		}

		if (commandFound) {
			//printf("commandBuffer: %s\n", commandBuffer);
			struct Message *message = parseServerMessage(c, c->commandBuffer, userdata);
			if (message != NULL) {
				c->messageCallback(c, message, userdata);
			}
			c->commandBufferAt = 0;
		} else if(c->commandBufferAt >= c->commandBufferSize) {
#if DEBUG
			printf("Command buffer overflowing?!?\n");
#endif
		}


		if (copied > 0) {
			for (int i = 0; i < c->readBufferAt-copied; i++) {
				c->readBuffer[i] = c->readBuffer[i+copied];
			}
		
			c->readBufferAt -= copied;	
		}
	} while(commandFound);

}

int connectToServer(struct IRCConnection *c, const char *server, int port) {
	struct sockaddr_in serv_addr;
	struct hostent *server_ent;

	if (c->connected) {
		c->connected = 0;
	}
	if (c->activeSocket >= 0) {
		close(c->activeSocket);
	}

	c->activeSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(c->activeSocket < 0) {
		printf("Failed to create socket\n");
		return 1;
	}

	server_ent = gethostbyname(server);
	if (server_ent == NULL) {
		printf("Failed to lookup server %s\n", server);
		close(c->activeSocket);
		c->activeSocket = -1;
		return 1;
	}


	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server_ent->h_addr, server_ent->h_length);
	serv_addr.sin_port = htons(port);

	if (connect(c->activeSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Failed to connect to %s:%d with error %d\n", server, port, errno);
		close(c->activeSocket);
		c->activeSocket = -1;
		return 1;
	}

	c->connected = 1;

	return 0;
}

int disconnectFromServer(struct IRCConnection *c) {
	int wasConnected = c->connected;

	if (c->connected) c->connected = 0;
	if (c->activeSocket >= 0) close(c->activeSocket);

	return wasConnected;
}
