#include <stdlib.h>
#include <string.h>

#include "messagetarget.h"
#include "message.h"

char *  MessageTargetNames[MAX_MESSAGE_TARGETS];
struct MessageTarget MessageTargets[MAX_MESSAGE_TARGETS];
int NumMessageTargets;

int AddMessageTarget(char *targetName, char *title, int type)
{
	if (NumMessageTargets >= MAX_MESSAGE_TARGETS) return -1;

	MessageTargetNames[NumMessageTargets] = strdup(targetName);
	MessageTargets[NumMessageTargets].title = strdup(title);
	MessageTargets[NumMessageTargets].messages = (struct Message **)malloc(MESSAGE_TARGET_CAPACITY * sizeof(struct Message *));
	for (int i = 0; i < MESSAGE_TARGET_CAPACITY; i++)
	{
		MessageTargets[NumMessageTargets].messages[i] = NULL;
	}

	MessageTargets[NumMessageTargets].messageCapacity = MESSAGE_TARGET_CAPACITY;
	MessageTargets[NumMessageTargets].messageAt = 0;
	MessageTargets[NumMessageTargets].type = type;
	MessageTargets[NumMessageTargets].index = NumMessageTargets;

	NumMessageTargets++;

	return NumMessageTargets-1;
}

void AddMessageToTarget(struct MessageTarget *target, struct Message *message)
{

	if (target->messageAt >= target->messageCapacity)
	{
		int halfcap = target->messageCapacity>>1;
		for (int i = 0; i < halfcap; i++)
		{
			MessageFree(target->messages[i]);
			target->messages[i] = target->messages[halfcap+i];
		}
		target->messageAt = halfcap;
	}

	target->messages[target->messageAt] = message;
	target->messageAt++;
}

struct MessageTarget * FindMessageTargetByName(char *name)
{
	for(int i = 0; i < NumMessageTargets; i++)
	{
		if(!strcasecmp(MessageTargetNames[i], name))
		{
			return &MessageTargets[i];
		}
	}
	return NULL;
}

int RemoveMessageTarget(struct MessageTarget *target)
{
	for(int i = 0; i < NumMessageTargets; i++)
	{
		if(target == &MessageTargets[i])
		{
			for(int m = 0; m < target->messageAt; m++)
			{
				MessageFree(target->messages[m]);
			}
			free(target->messages);
			free(target->title);

			for(int l = i; l < NumMessageTargets-1; l++)
			{
				MessageTargets[l] = MessageTargets[l+1];
				MessageTargets[l].index = l;
			}
			NumMessageTargets--;
			return NumMessageTargets;
		}
	}
	return -1;
}

void RemoveAllMessageTargets()
{
	for(int i = 0; i < NumMessageTargets; i++)
	{
		struct MessageTarget *target = &MessageTargets[i];
		for(int m = 0; m < target->messageAt; m++)
		{
			MessageFree(target->messages[m]);
		}
		free(target->messages);
		free(target->title);
	}
	NumMessageTargets = 0;
}
	

