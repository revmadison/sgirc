#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "prefs.h"
#include "thirdparty/cJSON.h"

int LoadPrefs(struct Prefs *prefs)
{
	if(!prefs) return PREFS_LOAD_FAILURE;

	// Setup default prefs first
	prefs->showTimestamp = 1;
	prefs->defaultServer = NULL;
	prefs->defaultPort = -1;
	prefs->defaultNick = NULL;
	prefs->saveLogs = 1;
	prefs->discordBridgeName = NULL;

	char *homeDir = getenv("HOME");
	if(!homeDir)
	{
		return PREFS_LOAD_NOFILE;
	}
	char *fileName = (char *)malloc(strlen(homeDir)+14);
	strcpy(fileName, homeDir);
	strcat(fileName, "/.sgirc/prefs");
	FILE *fp = fopen(fileName, "r");
	free(fileName);
	int fileSize = 0;
	char *fileBuffer;
	if(!fp)
	{
		return PREFS_LOAD_NOFILE;
	}
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fileBuffer = (char *)malloc(fileSize+1);
	if(!fileBuffer)
	{
		fclose(fp);
		return PREFS_LOAD_OOM;
	}

	fread(fileBuffer, 1, fileSize, fp);
	fclose(fp);

	cJSON *prefsJSON = cJSON_Parse(fileBuffer);
	free(fileBuffer);

	if(!prefsJSON)
	{
		return PREFS_LOAD_BADFILE;
	}

	cJSON *showTimestamp = cJSON_GetObjectItem(prefsJSON, "showTimestamp");
	if(showTimestamp && cJSON_IsBool(showTimestamp))
	{
		prefs->showTimestamp = cJSON_IsTrue(showTimestamp) ? 1 : 0;
	}
	cJSON *defaultServer = cJSON_GetObjectItem(prefsJSON, "defaultServer");
	if(defaultServer && !cJSON_IsNull(defaultServer))
	{
		prefs->defaultServer = strdup(cJSON_GetStringValue(defaultServer));
	}
	cJSON *defaultPort = cJSON_GetObjectItem(prefsJSON, "defaultPort");
	if(defaultPort && cJSON_IsNumber(defaultPort))
	{
		prefs->defaultPort = (int)cJSON_GetNumberValue(defaultPort);
	}
	cJSON *defaultNick = cJSON_GetObjectItem(prefsJSON, "defaultNick");
	if(defaultNick && !cJSON_IsNull(defaultNick))
	{
		prefs->defaultNick = strdup(cJSON_GetStringValue(defaultNick));
	}
	cJSON *saveLogs = cJSON_GetObjectItem(prefsJSON, "saveLogs");
	if(saveLogs && cJSON_IsBool(saveLogs))
	{
		prefs->saveLogs = cJSON_IsTrue(saveLogs) ? 1 : 0;
	}
	cJSON *discordBridgeName = cJSON_GetObjectItem(prefsJSON, "discordBridgeName");
	if(discordBridgeName && !cJSON_IsNull(discordBridgeName))
	{
		prefs->discordBridgeName = strdup(cJSON_GetStringValue(discordBridgeName));
	}

	cJSON_Delete(prefsJSON);

	return PREFS_SUCCESS;
}

int SavePrefs(struct Prefs *prefs)
{
	if(!prefs) return PREFS_SAVE_FAILURE;

	char *homeDir = getenv("HOME");
	if(!homeDir)
	{
		return PREFS_LOAD_NOFILE;
	}
	char *fileName = (char *)malloc(strlen(homeDir)+14);
	strcpy(fileName, homeDir);
	strcat(fileName, "/.sgirc");

	mkdir(fileName, 00700);
	strcat(fileName, "/prefs");

	cJSON *prefsJSON = cJSON_CreateObject();
	cJSON_AddBoolToObject(prefsJSON, "showTimestamp", prefs->showTimestamp?1:0);
	if(prefs->defaultServer)
	{
		cJSON_AddStringToObject(prefsJSON, "defaultServer", prefs->defaultServer);
	} else {
		cJSON_AddNullToObject(prefsJSON, "defaultServer");
	}
	cJSON_AddNumberToObject(prefsJSON, "defaultPort", prefs->defaultPort);
	if(prefs->defaultNick)
	{
		cJSON_AddStringToObject(prefsJSON, "defaultNick", prefs->defaultNick);
	} else {
		cJSON_AddNullToObject(prefsJSON, "defaultNick");
	}
	cJSON_AddBoolToObject(prefsJSON, "saveLogs", prefs->saveLogs?1:0);
	if(prefs->discordBridgeName)
	{
		cJSON_AddStringToObject(prefsJSON, "discordBridgeName", prefs->discordBridgeName);
	} else {
		cJSON_AddNullToObject(prefsJSON, "discordBridgeName");
	}

	char *prefsString = cJSON_Print(prefsJSON);
	cJSON_Delete(prefsJSON);

	if(!prefsString)
	{
		return PREFS_SAVE_FAILURE;
	}

	FILE *fp = fopen(fileName, "w");
	free(fileName);
	if(!fp)
	{
		free(prefsString);
		return PREFS_SAVE_CANTOPEN;
	}

	fwrite(prefsString, 1, strlen(prefsString), fp);
	fclose(fp);
	
	return PREFS_SUCCESS;
}

