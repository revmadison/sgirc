#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "prefs.h"
#include "thirdparty/cJSON.h"

int LoadPrefs(struct Prefs *prefs, char *prefsFile) {
	char *fileName;

	if(!prefs) {
		return PREFS_LOAD_FAILURE;
	}

	// Setup default prefs first
	prefs->servers = NULL;
	prefs->serverCount = 0;
	prefs->showTimestamp = 1;
	prefs->saveLogs = 1;
	prefs->connectOnLaunch = 0;
	prefs->imagePreviewHeight = 0;

	if(prefsFile) {
		fileName = strdup(prefsFile);
	} else {
		char *homeDir = getenv("HOME");
		if(!homeDir) {
			return PREFS_LOAD_NOFILE;
		}
		fileName = (char *)malloc(strlen(homeDir)+14);
		strcpy(fileName, homeDir);
		strcat(fileName, "/.sgirc/prefs");
	}

	FILE *fp = fopen(fileName, "r");
	free(fileName);

	int fileSize = 0;
	char *fileBuffer;
	if(!fp) {
		return PREFS_LOAD_NOFILE;
	}
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fileBuffer = (char *)malloc(fileSize+1);
	if(!fileBuffer) {
		fclose(fp);
		return PREFS_LOAD_OOM;
	}

	fread(fileBuffer, 1, fileSize, fp);
	fclose(fp);

	cJSON *prefsJSON = cJSON_Parse(fileBuffer);
	free(fileBuffer);

	if(!prefsJSON) {
		return PREFS_LOAD_BADFILE;
	}

	// First parse the servers
	cJSON *servers = cJSON_GetObjectItem(prefsJSON, "servers");
	if(servers && cJSON_IsArray(servers)) {
		prefs->serverCount = cJSON_GetArraySize(servers);
		prefs->servers = (struct ServerDetails *)malloc(prefs->serverCount * sizeof(struct ServerDetails));
		for(int i = 0; i < prefs->serverCount; i++) {
			cJSON *entry = cJSON_GetArrayItem(servers, i);
			cJSON *v;

			prefs->servers[i].serverName = NULL;
			prefs->servers[i].host = NULL;
			prefs->servers[i].port = 0;
			prefs->servers[i].pass = NULL;
			prefs->servers[i].useSSL = 0;
			prefs->servers[i].nick = NULL;
			prefs->servers[i].discordBridgeName = NULL;
			prefs->servers[i].connectCommands = NULL;

			v = cJSON_GetObjectItem(entry, "serverName");
			if(!v || cJSON_IsNull(v)) {
				continue;
			}
			prefs->servers[i].serverName = strdup(cJSON_GetStringValue(v));

			v= cJSON_GetObjectItem(entry, "host");
			if(v&& !cJSON_IsNull(v)) {
				prefs->servers[i].host = strdup(cJSON_GetStringValue(v));
			}
			v = cJSON_GetObjectItem(entry, "port");
			if(v && cJSON_IsNumber(v)) {
				prefs->servers[i].port = (int)cJSON_GetNumberValue(v);
			}
			v = cJSON_GetObjectItem(entry, "useSSL");
			if(v && cJSON_IsBool(v)) {
				prefs->servers[i].useSSL = cJSON_IsTrue(v) ? 1 : 0;
			}
			v = cJSON_GetObjectItem(entry, "pass");
			if(v && !cJSON_IsNull(v)) {
				prefs->servers[i].pass = strdup(cJSON_GetStringValue(v));
			}
			v = cJSON_GetObjectItem(entry, "nick");
			if(v && !cJSON_IsNull(v)) {
				prefs->servers[i].nick = strdup(cJSON_GetStringValue(v));
			}
			v = cJSON_GetObjectItem(entry, "discordBridgeName");
			if(v && !cJSON_IsNull(v)) {
				prefs->servers[i].discordBridgeName = strdup(cJSON_GetStringValue(v));
			}
			v = cJSON_GetObjectItem(entry, "connectCommands");
			if(v && !cJSON_IsNull(v)) {
				prefs->servers[i].connectCommands = strdup(cJSON_GetStringValue(v));
			}
		}
	}



	// Then the app-wide preferences
	cJSON *showTimestamp = cJSON_GetObjectItem(prefsJSON, "showTimestamp");
	if(showTimestamp && cJSON_IsBool(showTimestamp)) {
		prefs->showTimestamp = cJSON_IsTrue(showTimestamp) ? 1 : 0;
	}
	cJSON *saveLogs = cJSON_GetObjectItem(prefsJSON, "saveLogs");
	if(saveLogs && cJSON_IsBool(saveLogs)) {
		prefs->saveLogs = cJSON_IsTrue(saveLogs) ? 1 : 0;
	}
	cJSON *connectOnLaunch = cJSON_GetObjectItem(prefsJSON, "connectOnLaunch");
	if(connectOnLaunch && cJSON_IsBool(connectOnLaunch)) {
		prefs->connectOnLaunch = cJSON_IsTrue(connectOnLaunch) ? 1 : 0;
	}

	cJSON *imagePreviewHeight = cJSON_GetObjectItem(prefsJSON, "imagePreviewHeight");
	if(imagePreviewHeight && cJSON_IsNumber(imagePreviewHeight)) {
		prefs->imagePreviewHeight = cJSON_GetNumberValue(imagePreviewHeight);
	}
	cJSON *imagePreviewQuality = cJSON_GetObjectItem(prefsJSON, "imagePreviewQuality");
	if(imagePreviewQuality && cJSON_IsNumber(imagePreviewQuality)) {
		prefs->imagePreviewQuality = cJSON_GetNumberValue(imagePreviewQuality);
	}

	cJSON_Delete(prefsJSON);

	return PREFS_SUCCESS;
}

int SavePrefs(struct Prefs *prefs, char *prefsFile) {
	char *fileName;

	if(!prefs) {
		return PREFS_SAVE_FAILURE;
	}

	if(prefsFile) {
		fileName = strdup(prefsFile);
	} else {
		char *homeDir = getenv("HOME");
		if(!homeDir) {
			return PREFS_LOAD_NOFILE;
		}
		fileName = (char *)malloc(strlen(homeDir)+14);
		strcpy(fileName, homeDir);
		strcat(fileName, "/.sgirc");

		mkdir(fileName, 00700);
		strcat(fileName, "/prefs");
	}

	cJSON *prefsJSON = cJSON_CreateObject();

	if(prefs->serverCount > 0) {
		cJSON *servers = cJSON_CreateArray();

		for(int i = 0; i < prefs->serverCount; i++) {
			if(!prefs->servers[i].serverName || !prefs->servers[i].host) {
				continue;
			}

			cJSON *entry = cJSON_CreateObject();
			cJSON_AddStringToObject(entry, "serverName", prefs->servers[i].serverName);
			cJSON_AddStringToObject(entry, "host", prefs->servers[i].host);
			cJSON_AddNumberToObject(entry, "port", prefs->servers[i].port);

			if(prefs->servers[i].pass) {
				cJSON_AddStringToObject(entry, "pass", prefs->servers[i].pass);
			} else {
				cJSON_AddNullToObject(entry, "pass");
			}
			cJSON_AddBoolToObject(entry, "useSSL", prefs->servers[i].useSSL?1:0);
			if(prefs->servers[i].nick) {
				cJSON_AddStringToObject(entry, "nick", prefs->servers[i].nick);
			} else {
				cJSON_AddNullToObject(entry, "nick");
			}
			if(prefs->servers[i].discordBridgeName) {
				cJSON_AddStringToObject(entry, "discordBridgeName", prefs->servers[i].discordBridgeName);
			} else {
				cJSON_AddNullToObject(entry, "discordBridgeName");
			}
			if(prefs->servers[i].connectCommands) {
				cJSON_AddStringToObject(entry, "connectCommands", prefs->servers[i].connectCommands);
			} else {
				cJSON_AddNullToObject(entry, "connectCommands");
			}

			cJSON_AddItemToArray(servers, entry);
		}

		cJSON_AddItemToObject(prefsJSON, "servers", servers);
	}

	cJSON_AddBoolToObject(prefsJSON, "showTimestamp", prefs->showTimestamp?1:0);
	cJSON_AddBoolToObject(prefsJSON, "saveLogs", prefs->saveLogs?1:0);
	cJSON_AddBoolToObject(prefsJSON, "connectOnLaunch", prefs->connectOnLaunch?1:0);
	cJSON_AddNumberToObject(prefsJSON, "imagePreviewHeight", prefs->imagePreviewHeight);
	cJSON_AddNumberToObject(prefsJSON, "imagePreviewQuality", prefs->imagePreviewQuality);

	char *prefsString = cJSON_Print(prefsJSON);
	cJSON_Delete(prefsJSON);

	if(!prefsString) {
		return PREFS_SAVE_FAILURE;
	}

	FILE *fp = fopen(fileName, "w");
	free(fileName);
	if(!fp) {
		free(prefsString);
		return PREFS_SAVE_CANTOPEN;
	}

	fwrite(prefsString, 1, strlen(prefsString), fp);
	fclose(fp);
	
	return PREFS_SUCCESS;
}

void StoreServerDetails(struct Prefs *prefs, struct ServerDetails *details) {
	int i;
	for(i = 0; i < prefs->serverCount; i++) {
		if(prefs->servers[i].serverName && details->serverName && !strcmp(details->serverName, prefs->servers[i].serverName)) {
			// Replacing an existing setting!
			if(details->host && strlen(details->host) > 0) {
				if(prefs->servers[i].host) {
					free(prefs->servers[i].host);
				}
				prefs->servers[i].host = strdup(details->host);
			}
			if(details->nick && strlen(details->nick) > 0) {
				if(prefs->servers[i].nick) {
					free(prefs->servers[i].nick);
				}
				prefs->servers[i].nick = strdup(details->nick);
			}
			prefs->servers[i].port = details->port;
			prefs->servers[i].useSSL = details->useSSL;
			if(prefs->servers[i].pass) {
				free(prefs->servers[i].pass);
				prefs->servers[i].pass = NULL;
			}
			if(details->pass && strlen(details->pass) > 0) {
				prefs->servers[i].pass = strdup(details->pass);
			}
			if(prefs->servers[i].discordBridgeName) {
				free(prefs->servers[i].discordBridgeName);
				prefs->servers[i].discordBridgeName = NULL;
			}
			prefs->servers[i].discordBridgeName = details->discordBridgeName ? strdup(details->discordBridgeName) : NULL;
			if(prefs->servers[i].connectCommands) {
				free(prefs->servers[i].connectCommands);
				prefs->servers[i].connectCommands = NULL;
			}
			prefs->servers[i].connectCommands = details->connectCommands ? strdup(details->connectCommands) : NULL;

			return;
		}
	}

	if(!prefs->servers) {
		prefs->serverCount = 1;
		prefs->servers = (struct ServerDetails *)malloc(prefs->serverCount * sizeof(struct ServerDetails));
		i = 0;
	} else {
		i = prefs->serverCount;
		prefs->serverCount++;
		prefs->servers = (struct ServerDetails *)realloc(prefs->servers, prefs->serverCount * sizeof(struct ServerDetails));
	}

	prefs->servers[i].serverName = details->serverName ? strdup(details->serverName) : strdup("Default connection");
	prefs->servers[i].host = details->host ? strdup(details->host) : NULL;
	prefs->servers[i].port = details->port;
	prefs->servers[i].pass = (details->pass && strlen(details->pass) > 0) ? strdup(details->pass) : NULL;
	prefs->servers[i].useSSL = details->useSSL;
	prefs->servers[i].nick = details->nick ? strdup(details->nick) : NULL;
	prefs->servers[i].discordBridgeName = details->discordBridgeName ? strdup(details->discordBridgeName) : NULL;
	prefs->servers[i].connectCommands = details->connectCommands ? strdup(details->connectCommands) : NULL;

}


