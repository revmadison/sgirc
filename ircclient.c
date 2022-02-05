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

int activeSocket;
int connected;

char * readBuffer;
int readBufferSize;
int readBufferAt;

char * commandBuffer;
int commandBufferSize;
int commandBufferAt;

void initIRCClient() {
	activeSocket = -1;
	connected = 0;

	readBuffer = (char *)malloc(4096);
	readBufferSize = 4096;
	readBufferAt = 0;

	commandBuffer = (char *)malloc(4096);
	commandBufferSize = 4096;
	commandBufferAt = 0;
}

void sendIRCCommand(const char *command) {
	if (!connected) {
		return;
	}
#if DEBUG
	printf("Sending: %s\n", command);
#endif
	send(activeSocket, command, strlen(command), 0);
	send(activeSocket, "\r\n", 2, 0);
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


struct Message *parseServerMessage(char * srvmessage, IRCChannelJoinCallback joinCallback, IRCChannelPartCallback partCallback, IRCChannelQuitCallback quitCallback, IRCChannelTopicCallback topicCallback, void *userdata) {
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
		send(activeSocket, "PONG ", 5, 0);
		send(activeSocket, params, strlen(params), 0);
		send(activeSocket, "\r\n", 2, 0);
	} else if(!strcasecmp(command, "PRIVMSG")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
		char *messageBody = params+paramOffset;
		if(*messageBody == ':') {
			messageBody++;
		}

		message = MessageInit(source, target, messageBody);	
		free(target);
	} else if(!strcasecmp(command, "JOIN")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
	
		joinCallback(target, source, userdata);
		free(target);
	} else if (!strcasecmp(command, "PART")) {
		int paramOffset = 0;
		char *target = parseFirstWord(params, &paramOffset);
		char *partMessage = params+paramOffset;
		if (*partMessage == ':') {
			partMessage++;
		}
	
		partCallback(target, source, partMessage, userdata);
		free(target);
	} else if (!strcasecmp(command, "QUIT")) {
		int paramOffset = 0;
		char *partMessage = params+paramOffset;
		if (*partMessage == ':') {
			partMessage++;
		}
	
		quitCallback(source, partMessage, userdata);
	} else if (!strcasecmp(command, "332")) {
		int paramOffset = 0;
		char *forwho = parseFirstWord(params, &paramOffset);
		char *target = parseFirstWord(params, &paramOffset);
		char *topic = params+paramOffset;
		if (*topic == ':') {
			topic++;
		}
	
		topicCallback(target, topic, userdata);
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
				joinCallback(target, list, userdata);
				list += strlen(list);
			} else {
				*nextName = 0;
				joinCallback(target, list, userdata);
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
		message = MessageInit(source, SERVER_TARGET, fullCommandAndParams);
	}

	if (source) free(source);
	if (command) free(command);

	return message;
}

void updateIRCClient(IRCUpdateCallback callback, IRCChannelJoinCallback joinCallback, IRCChannelPartCallback partCallback, IRCChannelQuitCallback quitCallback, IRCChannelTopicCallback topicCallback, void *userdata) {
	if (!connected) {
		return;
	}

	int n = recv(activeSocket, &readBuffer[readBufferAt], readBufferSize-readBufferAt, MSG_DONTWAIT);
	if (n < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			//printf("Error reading from socket: %d!\n", errno);
			printf("Connection has been closed.");
			exit(1);
		}
	}

	if (n > 0) {
		readBufferAt += n;
	}

	int commandFound;
	do {
		int copied = 0;
		commandFound = 0;
		while (commandBufferAt < commandBufferSize && copied < readBufferAt) {
			commandBuffer[commandBufferAt] = readBuffer[copied++];

			if (commandBuffer[commandBufferAt] == '\n') {
				commandBuffer[commandBufferAt] = 0;
				if(commandBufferAt > 0 && commandBuffer[commandBufferAt-1] == '\r') {
					commandBuffer[commandBufferAt-1] = 0;
				}

				commandFound = 1;
				break;
			} else {
				commandBufferAt++;
			}
		}

		if (commandFound) {
			//printf("commandBuffer: %s\n", commandBuffer);
			struct Message *message = parseServerMessage(commandBuffer, joinCallback, partCallback, quitCallback, topicCallback, userdata);
			if (message != NULL) {
				callback(message, userdata);
			}
			commandBufferAt = 0;
		} else if(commandBufferAt >= commandBufferSize) {
#if DEBUG
			printf("Command buffer overflowing?!?\n");
#endif
		}


		if (copied > 0) {
			for (int i = 0; i < readBufferAt-copied; i++) {
				readBuffer[i] = readBuffer[i+copied];
			}
		
			readBufferAt -= copied;	
		}
	} while(commandFound);

}

int connectToServer(const char *server, int port) {
	struct sockaddr_in serv_addr;
	struct hostent *server_ent;

	if (connected) {
		connected = 0;
	}
	if (activeSocket >= 0) {
		close(activeSocket);
	}

	activeSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(activeSocket < 0) {
		printf("Failed to create socket\n");
		return 1;
	}

	server_ent = gethostbyname(server);
	if (server_ent == NULL) {
		printf("Failed to lookup server %s\n", server);
		close(activeSocket);
		activeSocket = -1;
		return 1;
	}


	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server_ent->h_addr, server_ent->h_length);
	serv_addr.sin_port = htons(port);

	if (connect(activeSocket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Failed to connect to %s:%d with error %d\n", server, port, errno);
		close(activeSocket);
		activeSocket = -1;
		return 1;
	}

	connected = 1;

	return 0;
}

int disconnectFromServer() {
	int wasConnected = connected;

	if (connected) connected = 0;
	if (activeSocket >= 0) close(activeSocket);

	return wasConnected;
}
