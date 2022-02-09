#include <Xm/ComboBox.h>
#include <Xm/CutPaste.h>
#include <Xm/DialogS.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/PanedW.h>
#include <Xm/PushB.h>
#include <Xm/ScrollBar.h>
#include <Xm/SelectioB.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <X11/Xresource.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "ircclient.h"
#include "messagetarget.h"
#include "message.h"
#include "memberlist.h"
#include "prefs.h"

#ifndef MIN
#define MIN(a,b) (a <= b ? a : b)
#endif
#ifndef MAX
#define MAX(a,b) (a >= b ? a : b)
#endif

#pragma set woff 3970

String fallbacks[] = {
	"*sgiMode: true",
	"*useSchemes: all",
	"sgirc*XmList.fontList: -*-screen-medium-r-normal--12-*-*-*-*-*-*-*, -*-screen-bold-r-normal--12-*-*-*-*-*-*-*:UnreadChannel",
	"sgirc*chatFont: -*-screen-medium-r-normal--12-*-*-*-*-*-*-*",
	"sgirc*chatBGColor: black",
	"sgirc*chatTextColor: white",
	"sgirc*chatTimeColor: grey",
	"sgirc*chatNickColor: blue",
	"sgirc*chatBridgeColor: purple",
	"sgirc*chatSelectionColor: orange",
	NULL
};

struct Prefs prefs;

struct MessageTarget *currentTarget;

struct MemberList *MessageTargetMembers[MAX_MESSAGE_TARGETS];
int MessageTargetHasUpdate[MAX_MESSAGE_TARGETS];

static XtAppContext  app;
static Widget window, chatList, channelList, namesList, scrollbar, titleField;
static XFontStruct *chatFontStruct;
static GC chatGC;
static GC selectedGC;
static int chatFontHeight = 12;
static int chatTimestampOffset = 60;
static int chatTextOffset = 116;

static char *altPrefsFile = NULL;

static int LinesPerMessage[MESSAGE_TARGET_CAPACITY];
static int LineBreaksPerMessage[MESSAGE_TARGET_CAPACITY][16];

// Positioning for pixel values of selection
static int selectStartX, selectStartY, selectEndX, selectEndY;
// Positioning for line and character offsets
static int selectStartIndex, selectEndIndex, selectStartLine, selectEndLine;
static int selectStartOffset, selectEndOffset;

static int copyOnNextDraw = 0;
static char selectedText[1024];

static Colormap chatColormap;
static unsigned long chatBGColor;
static unsigned long chatTextColor;
static unsigned long chatTimeColor;
static unsigned long chatNickColor;
static unsigned long chatBridgeColor;
static unsigned long chatSelectionColor;

struct ServerConnection {
	struct IRCConnection *connection;
	struct ServerDetails *serverDetails;
};

static struct ServerConnection *connections = NULL;
static int connectionCount = 0;

struct ChannelListEntry {
	struct MessageTarget *target;
};
static struct ChannelListEntry *channelListEntries = NULL;
static int channelListCount = 0;

void SetupNamesList() {
	if(!currentTarget) {
		return;
	}

	XmListDeleteAllItems(namesList);

	struct MemberList *memberList = MessageTargetMembers[currentTarget->index];
	int addAt = 0;

	// TODO: Replace all this with a sort...

	// We'll do this lazily.. first, the ops
	for (int i = 0; i < memberList->memberCount; i++) {
		if(memberList->members[i][0] == '@') {
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}

	// ... and then the voiced ...
	for (int i = 0; i < memberList->memberCount; i++) {
		if(memberList->members[i][0] == '+') {
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}
	// ... and finally the plebes...
	for (int i = 0; i < memberList->memberCount; i++) {
		if(memberList->members[i][0] != '@' && memberList->members[i][0] != '+') {
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}

}
void SetupChannelList()
{
	int index = 0;

	XmListDeleteAllItems(channelList);

	if(connectionCount == 0) {
		if(channelListEntries) {
			free(channelListEntries);
			channelListEntries = NULL;
		}
		channelListCount = 0;
		return;
	}

	if(channelListCount != NumMessageTargets) {
		channelListCount = NumMessageTargets;
		channelListEntries = (struct ChannelListEntry *)realloc(channelListEntries, channelListCount*sizeof(struct ChannelListEntry));
	}

	for(int c = 0; c < NumMessageTargets; c++) {
		if(!strcmp(MessageTargetNames[c], SERVER_TARGET)) {
			channelListEntries[index].target = &MessageTargets[c];

			XmString str = XmStringCreate(MessageTargets[c].title, MessageTargetHasUpdate[c]?"UnreadChannel":"ReadChannel");
			XmListAddItemUnselected(channelList, str, index+1);
			XmStringFree(str);
			index++;

			for(int i = 0; i < NumMessageTargets; i++) {
				if(MessageTargets[i].connection != MessageTargets[c].connection) {
					continue;
				}
				if(!strcmp(MessageTargetNames[i], SERVER_TARGET)) {
					continue;
				}

				char *buffer = (char *)malloc(strlen(MessageTargets[i].title)+4);
				sprintf(buffer, "  %s", MessageTargets[i].title);
				XmString str = XmStringCreate(buffer, MessageTargetHasUpdate[i]?"UnreadChannel":"ReadChannel");
				XmListAddItemUnselected(channelList, str, index+1);
				XmStringFree(str);
				free(buffer);
				channelListEntries[index].target = &MessageTargets[i];
				index++;
			}
		}
	}
}
void RefreshChannelList() {
	for(int c = 0; c < channelListCount; c++) {
		for(int i = 0; i < NumMessageTargets; i++) {
			if(&MessageTargets[i] != channelListEntries[c].target) {
				continue;
			}

			int wasSelected = XmListPosSelected(channelList, c+1);

			XmString str;

			if(!strcmp(MessageTargetNames[i], SERVER_TARGET)) {
				str = XmStringCreate(MessageTargets[i].title, 	MessageTargetHasUpdate[i]?"UnreadChannel":"ReadChannel");
			} else {
				char *buffer = (char *)malloc(strlen(MessageTargets[i].title)+4);
				sprintf(buffer, "  %s", MessageTargets[i].title);
				str = XmStringCreate(buffer, 	MessageTargetHasUpdate[i]?"UnreadChannel":"ReadChannel");
				free(buffer);
			}
			XmListReplaceItemsPos(channelList, &str, 1, c+1);
			XmStringFree(str);

			if(wasSelected) {
				XmListSelectPos(channelList, c+1, FALSE);
			}

		}
	}
}


void forceRedraw() {
	if (chatList && XtDisplay(chatList) && XtWindow(chatList)) {
		XClearArea(XtDisplay(chatList), XtWindow(chatList), 0, 0, 0, 0, True);
	}

	/* Other method, no round trip, requires clear in redraw
	XmDrawingAreaCallbackStruct da_struct;
	da_struct.reason = XmCR_EXPOSE;
	da_struct.event = (XEvent *)NULL;
	da_struct.window = XtWindow(chatList);
	XtCallCallbacks(chatList, XmNexposeCallback, (XtPointer)&da_struct);	
	*/
}

char *removeControlCodes(char *in) {
	if(in == NULL) return strdup("");

	char *out = (char *)malloc(strlen(in)+1);
	int outAt = 0;

	for(int i = 0; i < strlen(in); i++) {
		if(in[i] == 0x03) {
			i+=2;
		} else if(in[i] >= 0x01 && in[i] <= 0x0f) {
			// do nothing
		} else {
			out[outAt] = in[i];
			outAt++;
		}
	}

	out[outAt] = 0;
	return out;
}

int calcMessageBreaks(char *line, int usableWidth, int index) {
	int numLines = 0;

	int linelen = strlen(line);
	int linestart = 0;
	int linewidth = 0;
	int lastspace = -1;
	int endoftext;

	while(linestart < linelen) {
		lastspace = 0;
		linewidth = 0;
		endoftext = 0;

		while(XTextWidth(chatFontStruct, &line[linestart], linewidth) < usableWidth) {
			linewidth++;
			if(line[linestart+linewidth] == ' ') lastspace = linewidth;
			if(linewidth+linestart >= linelen) {
				endoftext = 1;
				break;
			}
		}

		if(endoftext) {
			// do nothing
		} else if(lastspace > 0) {
			linewidth = lastspace+1;
		} else {
			// No space to break on, just go back 1 character...
			linewidth--;
		}

		LineBreaksPerMessage[index][numLines] = linewidth;
		linestart += linewidth;
		numLines++;
		if(numLines > 16) break;		
	}

	LinesPerMessage[index] = numLines;

	return numLines;
}

int recalculateMessageBreaks() {
	Dimension chatWidth;
	int i;
	int totalLines = 0;
	int nameOffset = prefs.showTimestamp ? chatTimestampOffset+8 : 4;
	int textOffset = prefs.showTimestamp ? chatTextOffset+chatTimestampOffset+12 : chatTextOffset+8;
	
	if(currentTarget == NULL) {
		return 0;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;
	char *discordBridgeName = details->discordBridgeName;

	XtVaGetValues(chatList, XmNwidth, &chatWidth, NULL);

	for (i = 0; i < currentTarget->messageAt; i++) {
		int usableWidth = chatWidth - (((currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL)?textOffset:nameOffset)+20);
		char *filteredMessage = removeControlCodes(currentTarget->messages[i]->message);
		int alreadyCounted = 0;

		if(discordBridgeName && !strcmp(currentTarget->messages[i]->source, discordBridgeName)) {
			char *firstSpace = strchr(filteredMessage, ' ');
			if(firstSpace) {
				firstSpace++;
				totalLines += calcMessageBreaks(firstSpace, usableWidth, i);
				alreadyCounted = 1;
			}
		}
		if(!alreadyCounted) {
			totalLines += calcMessageBreaks(filteredMessage, usableWidth, i);
		}

		free(filteredMessage);
	}
	return totalLines;
}

void recalculateBreaksAndScrollBar() {
	Dimension curHeight;
	int windowHeight, chatHeight, totalLines;

	XtVaGetValues(chatList, XmNheight, &curHeight, NULL);
	windowHeight = curHeight;
	totalLines = recalculateMessageBreaks();
	chatHeight = MAX(windowHeight, (totalLines+1)*chatFontHeight);

	//printf("totalLines=%d\tfontHeight=%d\twinH=%d\tchatH=%d\n", totalLines,chatFontHeight,windowHeight,chatHeight);

	XtVaSetValues(scrollbar, XmNmaximum, chatHeight, XmNsliderSize, windowHeight, XmNvalue, chatHeight-windowHeight, XmNpageIncrement, windowHeight>>1, NULL);

	forceRedraw();
}

void textInputCallback(Widget textField, XtPointer client_data, XtPointer call_data) {
	char *newtext = XmTextFieldGetString(textField);

	if(currentTarget == NULL) {
		return;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;

	if (!newtext || !*newtext) {
		XtFree(newtext); /* XtFree() checks for NULL */
		return;
	}

	if(currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL && newtext[0] != '/') {
		char fullmessage[4096];
		snprintf(fullmessage, 4095, "PRIVMSG %s :%s", currentTarget->title, newtext);
		sendIRCCommand(currentTarget->connection, fullmessage);

		struct Message *message = MessageInit(currentTarget->connection, details->nick, currentTarget->title, newtext);
		AddMessageToTarget(currentTarget, message);

		recalculateBreaksAndScrollBar();
	} else if(currentTarget != NULL && currentTarget->type == MESSAGETARGET_WHISPER && newtext[0] != '/') {
		char fullmessage[4096];
		snprintf(fullmessage, 4095, "PRIVMSG %s :%s", currentTarget->title, newtext);
		sendIRCCommand(currentTarget->connection, fullmessage);

		struct Message *message = MessageInit(currentTarget->connection, details->nick, currentTarget->title, newtext);
		AddMessageToTarget(currentTarget, message);

		recalculateBreaksAndScrollBar();
	} else {
		char *start = newtext;
		int skipSendingCommand = 0;
		
		if (*start == '/') start++;

		if(strstr(start, "CLOSE") == start || strstr(start, "close") == start) {
			if (currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL) {
				char buffer[1024];
				snprintf(buffer, 1023, "PART %s", currentTarget->title);
				sendIRCCommand(currentTarget->connection, buffer);
			}

			int removingIndex = currentTarget->index;
			int newCount = RemoveMessageTarget(currentTarget);
			if(newCount >= 0) {
				MemberListFree(MessageTargetMembers[removingIndex]);
				for(int i = removingIndex; i < newCount; i++) {
					MessageTargetMembers[i] = MessageTargetMembers[i+1];
				}
			}
			
			SetupChannelList();
			skipSendingCommand = 1;
		}

		if(strstr(start, "JOIN ") == start || strstr(start, "join ") == start) {
			int index = AddMessageTarget(currentTarget->connection, start+5, start+5, MESSAGETARGET_CHANNEL);
			if (index >= 0) {
				MessageTargetMembers[index] = MemberListInit();
			}
			SetupChannelList();
		}
		if(strstr(start, "PART") == start || strstr(start, "part") == start) {
			if (currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL) {
				char buffer[1024];

				if(start[4] == ' ' && start[5] != ' ' && start[5] != 0 && start[5] != '#' && start[5] != '!' && start[5] != '&') {
					snprintf(buffer, 1023, "PART %s :%s", currentTarget->title, start+5);
				} else {
					snprintf(buffer, 1023, "PART %s", currentTarget->title);
				}
				sendIRCCommand(currentTarget->connection, buffer);

				int removingIndex = currentTarget->index;
				int newCount = RemoveMessageTarget(currentTarget);
				if(newCount >= 0) {
					MemberListFree(MessageTargetMembers[removingIndex]);
					for(int i = removingIndex; i < newCount; i++) {
						MessageTargetMembers[i] = MessageTargetMembers[i+1];
					}
				}
				
				SetupChannelList();
			}
			skipSendingCommand = 1;
		}
		if(strstr(start, "MSG") == start || strstr(start, "msg") == start) {
			
			char *firstSpace = strchr(start+4, ' ');
			char *target = NULL;
			char *text = NULL;
			
			if(firstSpace) {
				struct MessageTarget *msgTarget;
				char fullmessage[4096];

				*firstSpace = 0;
				target = strdup(start+4);
				*firstSpace = ' ';
				text = firstSpace + 1;

				snprintf(fullmessage, 4095, "PRIVMSG %s :%s", target, text);
				sendIRCCommand(currentTarget->connection, fullmessage);
				skipSendingCommand = 1;
				
				msgTarget = FindMessageTargetByName(currentTarget->connection, target);
				if(!msgTarget) {
					int index = AddMessageTarget(currentTarget->connection, target, target, MESSAGETARGET_WHISPER);
					if (index >= 0) {
						MessageTargetMembers[index] = MemberListInit();
						msgTarget = FindMessageTargetByName(currentTarget->connection, target);
						SetupChannelList();
					}
				}
				if(msgTarget) {

					struct Message *message = MessageInit(currentTarget->connection, details->nick, target, text);
					AddMessageToTarget(msgTarget, message);

					if(msgTarget == currentTarget) {
						recalculateBreaksAndScrollBar();
					}
				}	
				free(target);
			}
		
		}

		if(!skipSendingCommand) {
			sendIRCCommand(currentTarget->connection, start);
		}
	}
	
	XtFree(newtext);
	XmTextFieldSetString(textField, "");
}

void switchToMessageTarget(struct MessageTarget *target) {
	currentTarget = target;
	recalculateBreaksAndScrollBar();
	SetupNamesList();
	MessageTargetHasUpdate[target->index] = 0;
	RefreshChannelList();

	XmTextFieldSetString(titleField, target->topic?target->topic:target->title);
}

void channelSelectedCallback(Widget chanList, XtPointer userData, XtPointer callData) {
	for (int i = 0; i < channelListCount; i++) {
		if (XmListPosSelected(chanList, i+1)) {
			switchToMessageTarget(channelListEntries[i].target);
			return;
		}
	}
}


// IRC Client Callbacks
void ircClientUpdateCallback(struct IRCConnection *c, struct Message *message, void *userdata) {
	struct MessageTarget *target;
	char *actualTarget = message->target;
	struct ServerDetails *details = (struct ServerDetails *)c->userData;

	if(!strcmp(actualTarget, details->nick)) {
		actualTarget = message->source;
	}
	target = FindMessageTargetByName(c, actualTarget);
	if(target == NULL && message->target != NULL) {
		int index = AddMessageTarget(c, actualTarget, actualTarget, MESSAGETARGET_WHISPER);
		if (index >= 0) {
			MessageTargetMembers[index] = MemberListInit();
			target = FindMessageTargetByName(c, actualTarget);
			SetupChannelList();
		}
	}

	if(target != NULL) {
		AddMessageToTarget(target, message);

		if(target == currentTarget) {
			recalculateBreaksAndScrollBar();
		} else {
			MessageTargetHasUpdate[target->index] = 1;
			RefreshChannelList();
		}

	} else {
#if DEBUG
		printf("Incoming message for target [%s] %s: %s\n", message->target, message->source, message->message);
#endif
	}
	
}
void ircClientChannelJoinCallback(struct IRCConnection *c, char *channel, char *name, void *userdata) {
	struct MessageTarget *messageTarget = FindMessageTargetByName(c, channel);
	if (messageTarget) {
		AddToMemberList(MessageTargetMembers[messageTarget->index], name);
		if (messageTarget == currentTarget) {
			SetupNamesList();
		}
	}	
}
void ircClientChannelPartCallback(struct IRCConnection *c, char *channel, char *name, char *partMessage, void *userdata) {
	struct MessageTarget *messageTarget = FindMessageTargetByName(c, channel);
	if (messageTarget) {
		char buffer[1024];
		snprintf(buffer, 1023, "** %s has left %s (%s)", name, channel, partMessage?partMessage:"no message");
		struct Message *message = MessageInit(c, name, messageTarget->title, buffer);
		message->type = MESSAGE_TYPE_SERVERMESSAGE;
		AddMessageToTarget(messageTarget, message);

		RemoveFromMemberList(MessageTargetMembers[messageTarget->index], name);
		if (messageTarget == currentTarget) {
			SetupNamesList();
		}
	}	
}
void ircClientChannelQuitCallback(struct IRCConnection *c, char *name, char *quitMessage, void *userdata) {
	for(int i = 0; i < NumMessageTargets; i++) {
		if(RemoveFromMemberList(MessageTargetMembers[i], name)) {
			struct MessageTarget *messageTarget = &MessageTargets[i];
			char buffer[1024];
			snprintf(buffer, 1023, "** %s has quit (%s)", name, quitMessage?quitMessage:"no message");
			struct Message *message = MessageInit(c, name, messageTarget->title, buffer);
			message->type = MESSAGE_TYPE_SERVERMESSAGE;
			AddMessageToTarget(messageTarget, message);

			if (messageTarget == currentTarget) {
				SetupNamesList();
			}
		}	
	}
}
void ircClientChannelTopicCallback(struct IRCConnection *c, char *channel, char *topic, void *userdata) {
	struct MessageTarget *messageTarget = FindMessageTargetByName(c, channel);
	if (messageTarget) {

		SetMessageTargetTopic(messageTarget, topic);
		if (messageTarget == currentTarget) {
			XmTextFieldSetString(titleField, topic);
		}
	}	
}


void updateTimerCallback(XtPointer clientData, XtIntervalId *timer) {
	XtAppContext * app = (XtAppContext *)clientData;

	for(int i = 0; i < connectionCount; i++) {
		updateIRCClient(connections[i].connection, (void *)chatList);
	}

	XtAppAddTimeOut(*app, 50, updateTimerCallback, app);
}
	

void chatListResizeCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	recalculateBreaksAndScrollBar();
}

void scrollbarChangedCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	forceRedraw();
}

static Boolean convertSelectionCallback(Widget widget, Atom *selection, Atom *target,
                                 Atom *type_return, XtPointer *value_return,
                                 unsigned long *length_return,
                                 int *format_return) {
	char *buf;

	if (widget != chatList || *selection != XA_PRIMARY) {
		return (False);
	}

	if (*target != XA_STRING && *target != XInternAtom(XtDisplay(chatList), "TEXT", TRUE)) {
		return False;
	}

	buf = XtMalloc(strlen(selectedText)+1);
	strcpy(buf, selectedText);

	*value_return = (XtPointer)buf;
	*length_return = strlen(selectedText);
	*type_return = XA_STRING;
	*format_return = 8;
	return (True);
}

static void loseSelectionCallback(Widget w, Atom *selection) {
	selectStartX = -1;
	selectStartY = -1;
	selectEndX = -1;
	selectEndY = -1;
	forceRedraw();
}

int processForDiscordBridge(char *buffer, const char *line, int *linestart) {
	if(line[0] != '<') {
		return 0;
	}

	char *nameClose = strstr(line, "> ");
	if(nameClose) {
		if(buffer) {
			nameClose[1] = 0;
			snprintf(buffer, 255, "%s", line);
			nameClose[1] = ' ';
		}
		*linestart = (nameClose-line)+2;
		return 1;
	} else {
		return 0;
	}
}

void updateSelectionIndices() {
	Display *display = XtDisplay(chatList);
	Drawable window = XtWindow(chatList);
	Dimension curWidth = 0, curHeight = 0;
	Position y = chatFontHeight;
	int scrollValue;
	int i;

	selectStartLine = selectStartY / chatFontHeight;
	selectEndLine = selectEndY / chatFontHeight;

	int nameOffset = prefs.showTimestamp ? chatTimestampOffset+8 : 4;
	int textOffset = prefs.showTimestamp ? chatTextOffset+chatTimestampOffset+12 : chatTextOffset+8;

	if(currentTarget == NULL) {
		return;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;
	char *discordBridgeName = details->discordBridgeName;

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	XtVaGetValues(chatList, XmNwidth, &curWidth, XmNheight, &curHeight, NULL);

	y -= scrollValue;

	selectedText[0] = 0;

	for(i = 0; i < currentTarget->messageAt; i++) {
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);

		if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) {
			if(discordBridgeName && !strcmp(currentTarget->messages[i]->source, discordBridgeName)) {
				processForDiscordBridge(NULL, line, &linestart);
			}
		}

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++) {
			int offset = (currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) ? textOffset : nameOffset;
			int yline = (y+scrollValue-chatFontHeight)/chatFontHeight;

			if(yline > selectEndLine) {
				break;
			}

			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX)) {
				int preSel = 0;
				int preSelW = 0;
				int postSel = 0;
				int postSelW = 0;

				if(yline == selectStartLine && yline == selectEndLine) {
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < 	selectStartX && preSel < linewidth) {
						preSel++;
					}
					if(preSel > 0) preSel--;
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);
					postSel = preSel;
					while(offset+XTextWidth(chatFontStruct, &line[linestart], postSel) < selectEndX && postSel < linewidth) {
						postSel++;
					}
					postSelW = XTextWidth(chatFontStruct, &line[linestart], postSel);

					selectStartIndex = preSel;
					selectStartOffset = preSelW;
					selectEndIndex = postSel;
					selectEndOffset = postSelW;
				} else if(yline == selectStartLine) {
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < selectStartX && preSel < linewidth) {
						preSel++;
					}
					if(preSel > 0) preSel--;
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);

					selectStartIndex = preSel;
					selectStartOffset = preSelW;
				} else if(yline == selectEndLine) {
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < selectEndX && preSel < linewidth) {
						preSel++;
					}
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);

					selectEndIndex = preSel;
					selectEndOffset = preSelW;
				}
			}
			linestart += linewidth;
			y += chatFontHeight;
				
		}
		free(line);
	}
}

void captureSelection() {
	Position y = chatFontHeight;
	int scrollValue;
	int i;
	int curLen = 0;

	if(currentTarget == NULL) {
		return;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;
	char *discordBridgeName = details->discordBridgeName;

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	y -= scrollValue;

	selectedText[0] = 0;

	for(i = 0; i < currentTarget->messageAt; i++) {
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);

		if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) {
			if(discordBridgeName && !strcmp(currentTarget->messages[i]->source, discordBridgeName)) {
				processForDiscordBridge(NULL, line, &linestart);
			}
		}

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++) {
			int yline = (y+scrollValue-chatFontHeight)/chatFontHeight;

			if(yline > selectEndLine) {
				break;
			}

			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX)) {
				if(yline == selectStartLine && yline == selectEndLine) {
					int newLen = selectEndIndex-selectStartIndex;
					if(curLen+newLen < 1023) {
						memcpy(selectedText+curLen, &line[linestart+selectStartIndex], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline == selectStartLine) {
					int newLen = linewidth-selectStartIndex;
					if(curLen+newLen < 1023) {
						memcpy(selectedText+curLen, &line[linestart+selectStartIndex], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline == selectEndLine) {
					int newLen = selectEndIndex;
					if(curLen+newLen < 1023) {
						memcpy(selectedText+curLen, &line[linestart], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline > selectStartLine && yline < selectEndLine){
					int newLen = linewidth;
					if(curLen+newLen < 1023) {
						memcpy(selectedText+curLen, &line[linestart], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				}
			}
			linestart += linewidth;
			y += chatFontHeight;
				
		}
		free(line);
	}

	if(strlen(selectedText) > 1) {
		Time t = XtLastTimestampProcessed(XtDisplay(chatList));

		int result = XtOwnSelection(chatList, XA_PRIMARY, t, convertSelectionCallback, loseSelectionCallback, NULL);
	}
}


void drawChatList() {
	Display *display = XtDisplay(chatList);
	Drawable window = XtWindow(chatList);
	Dimension curWidth = 0, curHeight = 0;
	Position y = chatFontHeight;
	int scrollValue;
	int i;
	GC gc = chatGC;
	char buffer[1024];

	int selectStartLine = selectStartY / chatFontHeight;	// This is due to us using a set font
	int selectEndLine = selectEndY / chatFontHeight;

	int nameOffset = prefs.showTimestamp ? chatTimestampOffset+8 : 4;
	int textOffset = prefs.showTimestamp ? chatTextOffset+chatTimestampOffset+12 : chatTextOffset+8;

	if(currentTarget == NULL) {
		return;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;
	char *discordBridgeName = details->discordBridgeName;

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	XtVaGetValues(chatList, XmNwidth, &curWidth, XmNheight, &curHeight, NULL);

	y -= scrollValue;

	if(copyOnNextDraw) {
		selectedText[0] = 0;
	}

	for(i = 0; i < currentTarget->messageAt; i++) {
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);
		int isBridge = 0;

		if(y > -chatFontHeight) {
			if(prefs.showTimestamp) {
				XSetForeground(display, gc, chatTimeColor);

				XDrawString(display, window, gc, 4, y, currentTarget->messages[i]->timestampString, strlen(currentTarget->messages[i]->timestampString));
			}


			if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) {
				if(discordBridgeName && !strcmp(currentTarget->messages[i]->source, discordBridgeName)) {
					isBridge = processForDiscordBridge(buffer, line, &linestart);
				}
				if(!isBridge) {
					snprintf(buffer, 255, "<%s>", currentTarget->messages[i]->source);
				}

				if(strlen(buffer) > 16) {
					buffer[15] = '>';
					buffer[16] = 0;
				}
				if(isBridge) {
					XSetForeground(display, gc, chatBridgeColor);
				} else {
					XSetForeground(display, gc, chatNickColor);
				}
				XDrawString(display, window, gc, nameOffset, y, buffer, strlen(buffer));
			}
		}

		XSetForeground(display, gc, chatTextColor);

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++) {
			int offset = (currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) ? textOffset : nameOffset;
			int yline = (y+scrollValue-chatFontHeight)/chatFontHeight;
			int drewLine = 0;
			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(y > -chatFontHeight) {
				if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX)) {
					int preSel = selectStartIndex;
					int preSelW = selectStartOffset;
					int postSel = selectEndIndex;
					int postSelW = selectEndOffset;

					if(yline == selectStartLine && yline == selectEndLine) {
						XDrawString(display, window, gc, offset, y, &line[linestart], preSel);
						XDrawImageString(display, window, selectedGC, offset+preSelW, y, 	&line[linestart+preSel], postSel-preSel);
						XDrawString(display, window, gc, offset+postSelW, y, &line[linestart+postSel], linewidth-postSel);
						drewLine = 1;
					} else if(yline == selectStartLine) {
						XDrawString(display, window, gc, offset, y, &line[linestart], preSel);
						XDrawImageString(display, window, selectedGC, offset+preSelW, y, &line[linestart+preSel], linewidth-preSel);
						drewLine = 1;
					} else if(yline == selectEndLine) {
						XDrawImageString(display, window, selectedGC, offset, y, &line[linestart], 	postSel);
						XDrawString(display, window, gc, offset+postSelW, y, &line[linestart+postSel], linewidth-postSel);
						drewLine = 1;
					} else if(yline > selectStartLine && yline < selectEndLine){
						XDrawImageString(display, window, selectedGC, offset, y, &line[linestart], linewidth);
						drewLine = 1;
					}
				}

				if(!drewLine) {
						XDrawString(display, window, gc, offset, y, &line[linestart], MIN(linewidth, strlen(&line[linestart])));
				}
			}
			linestart += linewidth;
			y += chatFontHeight;

			if(y > curHeight+20) {
				break;
			}
				
		}
		free(line);
	}
}

void chatListRedrawCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	drawChatList();
}

void selection(Widget widget, XEvent *event, String *args, Cardinal *num_args) {
	int scrollValue;
	XtVaGetValues (scrollbar, XmNvalue, &scrollValue, NULL);

	if(*num_args != 1) {
		return;
	}

	if(strcmp(args[0], "start")) {
		// If it's not start, it's either move or end
		selectEndX = event->xbutton.x;
		selectEndY = event->xbutton.y+scrollValue;
	} else {
		selectStartX = event->xbutton.x;
		selectStartY = event->xbutton.y+scrollValue;
		selectEndX = event->xbutton.x;
		selectEndY = event->xbutton.y+scrollValue;
	}

	updateSelectionIndices();

	if(!strcmp(args[0], "stop")) {
		captureSelection();
	}

	forceRedraw();
}

char *stringFromXmString(XmString xmString) {
	XmStringContext context;
	char buffer[1024];
	char *text;
	XmStringCharSet charset;
	XmStringDirection direction;
	XmStringComponentType unknownTag;
	unsigned short unknownLen;
	unsigned char *unknownData;
	XmStringComponentType type;

	if(!XmStringInitContext(&context, xmString)) {
		return strdup("");
	}
	buffer[0] = 0;
	while((type = XmStringGetNextComponent(context, &text, &charset, &direction, &unknownTag, &unknownLen, &unknownData)) != XmSTRING_COMPONENT_END) {
		if(type == XmSTRING_COMPONENT_TEXT || type == XmSTRING_COMPONENT_LOCALE_TEXT) {
			if(strlen(buffer)+strlen(text) > 1023) {
				XtFree(text);
				break;
			}
			strcat(buffer, text);
			XtFree(text);
		} else if(type == XmSTRING_COMPONENT_SEPARATOR) {
			if(strlen(buffer) >= 1023) break;

			strcat(buffer, "\n");
		}
	}

	XmStringFreeContext(context);
	return strdup(buffer);
}

void connectAndSetNick(struct IRCConnection *connection, char *server, int port, char *nick, char *pass) {
	int i = AddMessageTarget(connection, SERVER_TARGET, server, MESSAGETARGET_SERVER);
	currentTarget = FindMessageTargetByName(connection, SERVER_TARGET);
	SetupChannelList();
	if(i >= 0) {
		MessageTargetMembers[i] = MemberListInit();
	}

	printf("Attempting to connect to %s:%d\n", server, port);
	connectToServer(connection, server, port);

	char connbuffer[1024];
	if(pass && strlen(pass) > 0) {
		snprintf(connbuffer, 1023, "PASS %s", pass);
		sendIRCCommand(connection, connbuffer);
	}
	snprintf(connbuffer, 1023, "NICK %s", nick);
	sendIRCCommand(connection, connbuffer);
	snprintf(connbuffer, 1023, "USER %s 0 * :%s", nick, nick);
	sendIRCCommand(connection, connbuffer);
}
void addServerConnection(struct ServerDetails *serverDetails) {
	struct IRCConnection *connection = (struct IRCConnection *)malloc(sizeof(struct IRCConnection));

	connectionCount++;
	connections = (struct ServerConnection *)realloc(connections, connectionCount*sizeof(struct ServerConnection));

	connections[connectionCount-1].connection = connection;
	connections[connectionCount-1].serverDetails = serverDetails;

	initIRCConnection(connection);
	connection->messageCallback = ircClientUpdateCallback;
	connection->joinCallback = ircClientChannelJoinCallback;
	connection->partCallback = ircClientChannelPartCallback;
	connection->quitCallback = ircClientChannelQuitCallback;
	connection->topicCallback = ircClientChannelTopicCallback;
	connection->userData = serverDetails;
	
	connectAndSetNick(connection, serverDetails->host, serverDetails->port>0?serverDetails->port:6667, serverDetails->nick, serverDetails->pass);
}

void setNickCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	XmSelectionBoxCallbackStruct *cbs;
	cbs = (XmSelectionBoxCallbackStruct *)call_data;
	char *newnick = stringFromXmString(cbs->value);

	if(currentTarget == NULL) {
		return;
	}

	struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;

	printf("Changing nick from %s to %s\n", details->nick, newnick);
	free(details->nick);
	details->nick = newnick;

	char connbuffer[1024];
	snprintf(connbuffer, 1023, "NICK %s", newnick);
	sendIRCCommand(currentTarget->connection, connbuffer);
}

void connectToServerCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	Widget dialog = (Widget)client_data;
	Widget form = XtNameToWidget(dialog, "dialogForm");
	Widget nameField = XtNameToWidget(form, "connName");
	Widget serverField = XtNameToWidget(form, "server");
	Widget passField = XtNameToWidget(form, "pass");
	Widget nickField = XtNameToWidget(form, "nick");
	Widget bridgeField = XtNameToWidget(form, "bridge");
	Widget save = XtNameToWidget(form, "saveConn");

	char *t = XmTextFieldGetString(serverField);
	char *server = strdup(t);
	char *portstr = strchr(server, ':');
	char *connName = XmTextFieldGetString(nameField);
	char *nick = XmTextFieldGetString(nickField);
	char *pass = XmTextFieldGetString(passField);
	char *bridge = XmTextFieldGetString(bridgeField);
	int port = 6667;
	if(portstr) {
		*portstr = 0;
		portstr++;
		port = atoi(portstr);
	}

	if(!server || !strlen(server) || !nick || !strlen(nick)) {
		return;
	}

	struct ServerDetails *serverDetails = (struct ServerDetails *)malloc(sizeof(struct ServerDetails));;
	serverDetails->serverName = strdup(connName?connName:"New connection");
	serverDetails->host = strdup(server?server:"");
	serverDetails->port = port;
	serverDetails->pass = pass?strdup(pass):NULL;
	serverDetails->useSSL = 0;
	serverDetails->nick = strdup(nick?nick:"");
	serverDetails->discordBridgeName = bridge?strdup(bridge):NULL;

	if(save && XmToggleButtonGetState(save)) {
		StoreServerDetails(&prefs, serverDetails);
		SavePrefs(&prefs, altPrefsFile);
	}

	addServerConnection(serverDetails);

	XtFree(t);
	XtFree(nick);
	XtFree(pass);
	XtFree(bridge);
	free(server);

	XtDestroyWidget(dialog);
}

void closeDialog(Widget widget, XtPointer client_data, XtPointer call_data) {
	XtDestroyWidget((Widget)client_data);
}

void selectConnection(Widget widget, XtPointer client_data, XtPointer call_data) {
	Widget dialog = (Widget)client_data;
	Widget form = XtNameToWidget(dialog, "dialogForm");
	Widget nameField = XtNameToWidget(form, "connName");
	Widget serverField = XtNameToWidget(form, "server");
	Widget passField = XtNameToWidget(form, "pass");
	Widget nickField = XtNameToWidget(form, "nick");
	Widget bridgeField = XtNameToWidget(form, "bridge");

	XmComboBoxCallbackStruct *cb = (XmComboBoxCallbackStruct *)call_data;
	int index = cb->item_position;
	if(index == 0) {
		return;
	}
	if(!prefs.servers) {
		return;
	}

	for(int i = 0; i < prefs.serverCount; i++) {
		if(index == 1) {
			char *server = prefs.servers[i].host?prefs.servers[i].host:"";
			char *buffer = (char *)malloc(strlen(server)+32);
			strcpy(buffer, server);
			if(prefs.servers[i].port > 0 && prefs.servers[i].port != 6667) {
				sprintf(buffer, "%s:%d", server, prefs.servers[i].port);
			} else {
				strcpy(buffer, server);
			}
			// This is me!
			XmTextFieldSetString(nameField, prefs.servers[i].serverName);
			XmTextFieldSetString(serverField, buffer);
			XmTextFieldSetString(passField, prefs.servers[i].pass?prefs.servers[i].pass:"");
			XmTextFieldSetString(nickField, prefs.servers[i].nick?prefs.servers[i].nick:"");
			XmTextFieldSetString(bridgeField, prefs.servers[i].discordBridgeName?prefs.servers[i].discordBridgeName:"");
			free(buffer);
			return;
		} else {
			if(prefs.servers[i].serverName && strlen(prefs.servers[i].serverName) > 0) {
				index--;
			}
		}
	}
}

void makeConnectionDialog() {
	Widget dialog, form, w, actions;
	XmString str;
	Arg args[16];
	int n = 0;

	n = 0;
	XtSetArg(args[n], XmNdeleteResponse, XmDESTROY); n++;
	dialog = (Widget)XmCreateDialogShell(window, "Connect", args, n);
	form = XtVaCreateWidget("dialogForm", xmFormWidgetClass, dialog, NULL);

	XmStringTable connectionsList;
	int count = 1;
	connectionsList = (XmStringTable)XtMalloc((prefs.serverCount+1)*sizeof(XmString *));
	connectionsList[0] = XmStringCreateLocalized("New connection...");

	for(n = 0; n < prefs.serverCount; n++) {
		if(!prefs.servers[n].serverName || !strlen(prefs.servers[n].serverName)) {
			continue;
		}
		connectionsList[1+n] = XmStringCreateLocalized(prefs.servers[n].serverName);
		count++;
	}
	n = 0;
	XtSetArg(args[n], XmNitems, connectionsList); n++;
	XtSetArg(args[n], XmNitemCount, count); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	XtSetArg(args[n], XmNpositionMode, XmZERO_BASED); n++;
	w = XmCreateDropDownList(form, "conns", args, n);
	XtManageChild(w);
	XtAddCallback(w, XmNselectionCallback, selectConnection, dialog);

	str = XmStringCreateLocalized("Connection name:");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	w = XmCreateLabel(form, "connNameLabel", args, n);
	XtManageChild(w);
	XmStringFree(str);
	w = XtVaCreateManagedWidget("connName", xmTextFieldWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, w, XmNleftOffset, 4, XmNrightOffset, 4, NULL);
	XmTextFieldSetString(w, "New Connection");

	str = XmStringCreateLocalized("Server:");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	w = XmCreateLabel(form, "serverLabel", args, n);
	XtManageChild(w);
	XmStringFree(str);
	w = XtVaCreateManagedWidget("server", xmTextFieldWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, w, XmNleftOffset, 4, XmNrightOffset, 4, NULL);

	str = XmStringCreateLocalized("Use SSL");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	w = XmCreateToggleButton(form, "useSSL", args, n);
	XtManageChild(w);
	XtSetSensitive(w, False);
	XmStringFree(str);

	str = XmStringCreateLocalized("Pass (if required):");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	w = XmCreateLabel(form, "passLabel", args, n);
	XtManageChild(w);
	XmStringFree(str);
	w = XtVaCreateManagedWidget("pass", xmTextFieldWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, w, XmNleftOffset, 4, XmNrightOffset, 4, NULL);

	str = XmStringCreateLocalized("Preferred Nick:");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	w = XmCreateLabel(form, "nickLabel", args, n);
	XtManageChild(w);
	XmStringFree(str);
	w = XtVaCreateManagedWidget("nick", xmTextFieldWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, w, XmNleftOffset, 4, XmNrightOffset, 4, NULL);

	str = XmStringCreateLocalized("Discord bridge (if used):");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	w = XmCreateLabel(form, "bridgeLabel", args, n);
	XtManageChild(w);
	XmStringFree(str);
	w = XtVaCreateManagedWidget("bridge", xmTextFieldWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, w, XmNleftOffset, 4, XmNrightOffset, 4, NULL);

	str = XmStringCreateLocalized("Save this connection");
	n = 0;
	XtSetArg(args[n], XmNlabelString, str); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, w); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNleftOffset, 4); n++;
	XtSetArg(args[n], XmNrightOffset, 4); n++;
	XtSetArg(args[n], XmNtopOffset, 4); n++;
	XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
	XtSetArg(args[n], XmNset, True); n++;
	w = XmCreateToggleButton(form, "saveConn", args, n);
	XtManageChild(w);
	XmStringFree(str);

	actions = XtVaCreateWidget("actionSection", xmFormWidgetClass, form, 
		XmNtopAttachment, XmATTACH_WIDGET,
		XmNtopWidget, w,
		XmNleftAttachment, XmATTACH_FORM,
		XmNrightAttachment, XmATTACH_FORM,
		XmNbottomAttachment, XmATTACH_FORM,
		XmNfractionBase, 5,
		XmNtopOffset, 8,
		XmNbottomOffset, 8,
		NULL);

	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg(args[n], XmNleftPosition, 1); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg(args[n], XmNrightPosition, 2); n++;
	w = XmCreatePushButton(actions, "OK", args, n);
	XtManageChild(w);
	XtAddCallback(w, XmNactivateCallback, connectToServerCallback, dialog);
	Widget focus = w;

	n = 0;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_POSITION); n++;
	XtSetArg(args[n], XmNleftPosition, 3); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_POSITION); n++;
	XtSetArg(args[n], XmNrightPosition, 4); n++;
	w = XmCreatePushButton(actions, "Cancel", args, n);
	XtManageChild(w);
	XtAddCallback(w, XmNactivateCallback, closeDialog, dialog);

	XtVaSetValues(dialog, XmNinitialFocus, form, NULL);
	XtVaSetValues(form, XmNinitialFocus, actions, NULL);
	XtVaSetValues(actions, XmNinitialFocus, focus, NULL);

	XtManageChild(actions);
	XtManageChild(form);
	XtManageChild(dialog);
}

void fileMenuSimpleCallback(Widget widget, XtPointer client_data, XtPointer call_data) {
	// client_data is an int for index of menu item
	switch((int)client_data) {
	case 0:	// Connect...
		makeConnectionDialog();
		break;
	case 1:	// Set nick...
		{
			Widget dialog;
			XmString str = XmStringCreateLocalized("Enter your nick:");
			XmString cur;
			Arg args[5];
			int n = 0;
			XtSetArg(args[n], XmNselectionLabelString, str); n++;
			XtSetArg(args[n], XmNautoUnmanage, True); n++;
			if(currentTarget != NULL) {
				struct ServerDetails *details = (struct ServerDetails *)currentTarget->connection->userData;
				cur = XmStringCreate(details->nick, "CUR_NICK");
				XtSetArg(args[n], XmNtextString, cur); n++;
			}
			dialog = (Widget)XmCreatePromptDialog(window, "nick_prompt", args, n);
			XmStringFree(str);
			if(currentTarget != NULL) {
				XmStringFree(cur);
			}
			XtAddCallback(dialog, XmNokCallback, setNickCallback, NULL);
			XtAddCallback(dialog, XmNcancelCallback, closeDialog, dialog);
			XtSetSensitive(XtNameToWidget(dialog, "Help"), False);
			XtManageChild(dialog);
		}
		break;
	case 2:	// Exit
		XtAppSetExitFlag(app);
		break;
	}
}

void printUsage() {
	printf("Usage: sgirc [-n <nick>] [-s <server>] [-p <port>] [-f <prefs file>]\n");
	printf("\t-n: set Nick to use\n");
	printf("\t-s: set Server to connect to\n");
	printf("\t-p: set Port to connect to\n");
	printf("\t-w: set passWord for server\n");
	printf("\t-f: specify alternate preferences File\n");
	printf("\n");
}

int main(int argc, char** argv) {
	Widget      	textField, formLayout, mainWindow, menubar, panes;
	Arg         	args[32];
	int         	n = 0;
	XGCValues		gcv;
	String			translations = "<Btn1Down>: selection(start) ManagerGadgetArm()\n<Btn1Up>: selection(stop) ManagerGadgetActivate()\n<Btn1Motion>: selection(move) ManagerGadgetButtonMotion()";
	XtActionsRec	actions;

	selectStartX = -1;
	selectStartY = -1;
	selectEndX = -1;
	selectEndY = -1;

	LoadPrefs(&prefs, altPrefsFile);
	SavePrefs(&prefs, altPrefsFile);

	NumMessageTargets = 0;

	XtSetLanguageProc(NULL, NULL, NULL);

	window = XtVaAppInitialize(&app, "Sgirc", NULL, 0, &argc, argv, fallbacks, NULL);

	XtAppAddTimeOut(app, 100, updateTimerCallback, &app);

	mainWindow = (Widget)XmCreateMainWindow(window, "main_window", NULL, 0);
	XtManageChild(mainWindow);

	n = 0;
	XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
	panes = XmCreatePanedWindow(mainWindow, "panes", args, n);
	XtManageChild(panes);

	n = 0;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNwidth, 150); n++;
	XtSetArg(args[n], XmNpaneMinimum, 100); n++;
	XtSetArg(args[n], XmNskipAdjust, 1); n++;
	channelList = XmCreateScrolledList(panes, "channelList", args, n);
	XtManageChild(channelList);

	formLayout = XtVaCreateWidget("formLayout", xmFormWidgetClass, panes, XmNpaneMinimum, 250, NULL);

	XmString file = XmStringCreateLocalized("File");
	menubar = XmVaCreateSimpleMenuBar(mainWindow, "menubar", 
		XmVaCASCADEBUTTON, file, 'F',
		NULL);
	XmStringFree(file);
	XtManageChild(menubar);

	XmString connect = XmStringCreateLocalized("Connect...");
	XmString setNick = XmStringCreateLocalized("Set nick...");
	XmString exit = XmStringCreateLocalized("Exit");
	XmVaCreateSimplePulldownMenu(menubar, "fileMenu", 0, fileMenuSimpleCallback,
		XmVaPUSHBUTTON, connect, 'C', NULL, NULL,
		XmVaPUSHBUTTON, setNick, 'n', NULL, NULL,
		XmVaSEPARATOR,
		XmVaPUSHBUTTON, exit, 'x', NULL, NULL,
		NULL);
	XmStringFree(connect);
	XmStringFree(setNick);
	XmStringFree(exit);

	XtVaSetValues(mainWindow, XmNmenuBar, menubar, XmNworkWindow, panes, NULL);

	actions.string = "selection";
	actions.proc = selection;
	XtAppAddActions(app, &actions, 1);


	n = 0;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNwidth, 150); n++;
	XtSetArg(args[n], XmNpaneMinimum, 100); n++;
	XtSetArg(args[n], XmNskipAdjust, 1); n++;
	namesList = XmCreateScrolledList(panes, "namesList", args, n);
	XtManageChild(namesList);

	titleField = XtVaCreateManagedWidget("titleField", xmTextFieldWidgetClass, formLayout, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNtopAttachment, XmATTACH_FORM, XmNeditable, 0, NULL);

	textField = XtVaCreateManagedWidget("textField", xmTextFieldWidgetClass, formLayout, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, NULL);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, textField); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, titleField); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNincrement, 6); n++;
	scrollbar = XmCreateScrollBar(formLayout, "scrollbar", args, n);
	XtManageChild(scrollbar);

	Display *display = XtDisplay(formLayout);
	Screen *screen = XtScreen(formLayout);
	chatColormap = XCreateColormap(display, RootWindowOfScreen(screen), DefaultVisualOfScreen(screen), AllocNone);
	{

		XrmInitialize();
		XrmDatabase xrdb = XrmGetDatabase(display);
		if(!xrdb) {
			printf("NULL xrdb\n");
			return 0;
		}
		char *strtype = NULL;
		XrmValue value;

		
		if(XrmGetResource(xrdb, "sgirc.chatList.chatFont", "Sgirc.Chatlist.ChatFont", &strtype, &value)) {
			chatFontStruct = XLoadQueryFont(display, value.addr);
		} else {
			chatFontStruct = XLoadQueryFont(display, "-*-screen-medium-r-normal--12-*-*-*-*-*-*-*");
		}

		chatFontHeight = chatFontStruct->ascent+chatFontStruct->descent;
		chatTimestampOffset = XTextWidth(chatFontStruct, "88:88:88", 8);
		chatTextOffset = XTextWidth(chatFontStruct, "<WWWWabcdefighi>", 16);
		//printf("Font:\n\tasc: %d\n\tdes: %d", chatFontStruct->ascent, chatFontStruct->descent);
		//printf("\n\tTso: %d\n\tTxt: %d\n", chatTimestampOffset, chatTextOffset);
		chatBGColor = BlackPixelOfScreen(screen);
		chatTextColor = WhitePixelOfScreen(screen);
		chatTimeColor = WhitePixelOfScreen(screen);
		chatNickColor = WhitePixelOfScreen(screen);
		chatBridgeColor = WhitePixelOfScreen(screen);
		chatSelectionColor = WhitePixelOfScreen(screen);

		XColor c, e;
		if(XrmGetResource(xrdb, "sgirc.chatList.chatBGColor", "Sgirc.Chatlist.ChatBackgroundColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatBGColor = c.pixel;
			}
		}
		if(XrmGetResource(xrdb, "sgirc.chatList.chatTextColor", "Sgirc.Chatlist.ChatTextColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatTextColor = c.pixel;
			}
		}
		if(XrmGetResource(xrdb, "sgirc.chatList.chatTimeColor", "Sgirc.Chatlist.ThatTimeColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatTimeColor = c.pixel;
			}
		}
		if(XrmGetResource(xrdb, "sgirc.chatList.chatNickColor", "Sgirc.Chatlist.ChatNickColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatNickColor = c.pixel;
			}
		}
		if(XrmGetResource(xrdb, "sgirc.chatList.chatBridgeColor", "Sgirc.Chatlist.ChatBridgeColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatBridgeColor = c.pixel;
			}
		}
		if(XrmGetResource(xrdb, "sgirc.chatList.chatSelectionColor", "Sgirc.Chatlist.ChatSelectionColor", &strtype, &value)) {
			if(XAllocNamedColor(display, chatColormap, value.addr, &c, &e)) {
				chatSelectionColor = c.pixel;
			}
		}
	}

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, textField); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNtopWidget, titleField); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNrightWidget, scrollbar); n++;
	XtSetArg(args[n], XmNresizable, 1); n++;
	XtSetArg(args[n], XmNresizePolicy, XmRESIZE_ANY); n++;
	XtSetArg(args[n], XmNtranslations, XtParseTranslationTable(translations)); n++;
	XtSetArg(args[n], XmNwidth, 400); n++;
	XtSetArg(args[n], XmNminWidth, 300); n++;
	XtSetArg(args[n], XmNheight, 250); n++;
	XtSetArg(args[n], XmNcolormap, chatColormap); n++;
	XtSetArg(args[n], XmNbackground, chatBGColor); n++;

	chatList = XmCreateDrawingArea(formLayout, "chatList", args, n);
	XtManageChild(chatList);

	XtAddCallback(chatList, XmNexposeCallback, chatListRedrawCallback, NULL);
	XtAddCallback(chatList, XmNresizeCallback, chatListResizeCallback, NULL);
	XtAddCallback(scrollbar, XmNvalueChangedCallback, scrollbarChangedCallback, NULL);
	XtAddCallback(textField, XmNactivateCallback, textInputCallback, chatList);
	XtAddCallback(channelList, XmNbrowseSelectionCallback, channelSelectedCallback,  NULL);

	gcv.foreground = chatTextColor;
	gcv.background = chatBGColor;
	chatGC = XCreateGC(XtDisplay(chatList), RootWindowOfScreen(XtScreen(chatList)), GCForeground|GCBackground, &gcv);
	XSetFont(XtDisplay(chatList), chatGC, chatFontStruct->fid);
	gcv.background = chatSelectionColor;
	gcv.foreground = chatBGColor;
	selectedGC = XCreateGC(XtDisplay(chatList), RootWindowOfScreen(XtScreen(chatList)), GCForeground|GCBackground, &gcv);
	XSetFont(XtDisplay(chatList), selectedGC, chatFontStruct->fid);

	XtManageChild(formLayout);
	XtRealizeWidget(window);

	// Force redraw events even on shrinking
	{
		XSetWindowAttributes attrs;
		attrs.bit_gravity = ForgetGravity;
		XChangeWindowAttributes(XtDisplay(chatList), XtWindow(chatList), CWBitGravity, &attrs);
	}

	SetupChannelList();

	XtAppMainLoop(app);

	return 0;
}

