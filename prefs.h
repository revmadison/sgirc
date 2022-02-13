#ifndef _PREFS_H
#define _PREFS_H

#define PREFS_SUCCESS 0

#define PREFS_LOAD_FAILURE 1
#define PREFS_LOAD_NOFILE 2
#define PREFS_LOAD_BADFILE 3
#define PREFS_LOAD_OOM 4

#define PREFS_SAVE_FAILURE 1
#define PREFS_SAVE_CANTOPEN 5

struct ServerDetails {
	char *serverName;
	char *host;
	int port;
	char *pass;
	int useSSL;
	char *nick;
	char *discordBridgeName;
	char *connectCommands;
};

struct Prefs {
	struct ServerDetails *servers;
	int serverCount;

	int showTimestamp;
	int saveLogs;
	int connectOnLaunch;

	int imagePreviewHeight;
	int imagePreviewQuality;
};

int LoadPrefs(struct Prefs *prefs, char *prefsFile);
int SavePrefs(struct Prefs *prefs, char *prefsFile);

void StoreServerDetails(struct Prefs *prefs, struct ServerDetails *details);

#endif

