#ifndef _PREFS_H
#define _PREFS_H

#define PREFS_SUCCESS 0

#define PREFS_LOAD_FAILURE 1
#define PREFS_LOAD_NOFILE 2
#define PREFS_LOAD_BADFILE 3
#define PREFS_LOAD_OOM 4

#define PREFS_SAVE_FAILURE 1
#define PREFS_SAVE_CANTOPEN 5

struct Prefs {
	int showTimestamp;
	char *defaultServer;
	int defaultPort;
	char *defaultPass;
	char *defaultNick;
	int saveLogs;
	char *discordBridgeName;
	int connectOnLaunch;
};

int LoadPrefs(struct Prefs *prefs, char *prefsFile);
int SavePrefs(struct Prefs *prefs, char *prefsFile);

#endif

