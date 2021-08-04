#ifndef _MEMBERLIST_H
#define _MEMBERLIST_H

struct MemberList {
	char **members;
	int memberCount;
	int memberCapacity;
};

struct MemberList *MemberListInit()
{
	struct MemberList *list = (struct MemberList *)malloc(sizeof(struct MemberList));
	list->memberCount = 0;
	list->memberCapacity = 256;
	list->members = (char **)malloc(list->memberCapacity*sizeof(char *));
	return list;
}

void MemberListFree(struct MemberList *list)
{
	for (int i = 0; i < list->memberCount; i++)
	{
		free(list->members[i]);
	}
	free(list->members);
	free(list);
}

void AddToMemberList(struct MemberList *list, char *name)
{
	char *actualName = name;
	if (*actualName == '@' || *actualName == '+') actualName++;

	for (int i = 0; i < list->memberCount; i++)
	{
		char *otherName = list->members[i];
		if (*otherName == '@' || *otherName == '+') otherName++;
		if(!strcasecmp(otherName, actualName))
		{
			// Name exists, replace and be done
			free(list->members[i]);
			list->members[i] = strdup(name);
			return;
		}
	}

	if (list->memberCount == list->memberCapacity)
	{
		list->memberCapacity *= 2;
		list->members = (char **)realloc(list->members, list->memberCapacity*sizeof(char *));
	}
	list->members[list->memberCount] = strdup(name);
	list->memberCount++;
}

int RemoveFromMemberList(struct MemberList *list, char *name)
{
	char *actualName = name;
	if (*actualName == '@' || *actualName == '+') actualName++;

	for (int i = 0; i < list->memberCount; i++)
	{
		char *otherName = list->members[i];
		if (*otherName == '@' || *otherName == '+') otherName++;
		if(!strcasecmp(otherName, actualName))
		{
			free(list->members[i]);
			if (i < list->memberCount-1)
			{
				list->members[i] = list->members[list->memberCount-1];
			}
			list->memberCount--;
			return 1;
		}
	}
	return 0;
}


#endif

