#include <Xm/CutPaste.h>
#include <Xm/DrawingA.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MainW.h>
#include <Xm/ScrollBar.h>
#include <Xm/SelectioB.h>
#include <Xm/Text.h>
#include <Xm/TextF.h>

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


#define MAX(a,b) (a >= b ? a : b)

#pragma set woff 3970

String fallbacks[] = {
	"*sgiMode: true",
	"*useSchemes: all",
	"sgirc*XmList.fontList: -*-screen-medium-r-normal--12-*-*-*-*-*-*-*, -*-screen-bold-r-normal--12-*-*-*-*-*-*-*:UnreadChannel",
	"sgirc*chatFont: -*-screen-medium-r-normal--12-*-*-*-*-*-*-*",
	NULL
};

struct Prefs prefs;

struct MessageTarget *currentTarget;

struct MemberList *MessageTargetMembers[MAX_MESSAGE_TARGETS];
int MessageTargetHasUpdate[MAX_MESSAGE_TARGETS];

static XtAppContext  app;
static Widget window, chatList, channelList, namesList, scrollbar;
static XFontStruct *chatFontStruct;
static GC chatGC;
static GC selectedGC;
static char *nick;

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

void SetupNamesList()
{
	XmListDeleteAllItems(namesList);

	struct MemberList *memberList = MessageTargetMembers[currentTarget->index];
	int addAt = 0;

	// We'll do this lazily.. first, the ops
	for (int i = 0; i < memberList->memberCount; i++)
	{
		if(memberList->members[i][0] == '@')
		{
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}

	// ... and then the voiced ...
	for (int i = 0; i < memberList->memberCount; i++)
	{
		if(memberList->members[i][0] == '+')
		{
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}
	// ... and finally the plebes...
	for (int i = 0; i < memberList->memberCount; i++)
	{
		if(memberList->members[i][0] != '@' && memberList->members[i][0] != '+')
		{
			XmString str = XmStringCreate(memberList->members[i], "MSG");
			XmListAddItemUnselected(namesList, str, addAt+1);
			XmStringFree(str);
			addAt++;
		}
	}

}
void SetupChannelList()
{
	XmListDeleteAllItems(channelList);

	for (int i = 0; i < NumMessageTargets; i++)
	{
		XmString str = XmStringCreate(MessageTargets[i].title, MessageTargetHasUpdate[i]?"UnreadChannel":"ReadChannel");
		XmListAddItemUnselected(channelList, str, i+1);
		XmStringFree(str);
	}

}
void RefreshChannelList()
{
	for (int i = 0; i < NumMessageTargets; i++)
	{
		int wasSelected = XmListPosSelected(channelList, i+1);

		XmString str = XmStringCreate(MessageTargets[i].title, MessageTargetHasUpdate[i]?"UnreadChannel":"ReadChannel");
		XmListReplaceItemsPos(channelList, &str, 1, i+1);
		XmStringFree(str);

		if(wasSelected)
		{
			XmListSelectPos(channelList, i+1, FALSE);
		}
	}
}


void forceRedraw()
{
	if (chatList && XtDisplay(chatList) && XtWindow(chatList))
	{
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

char *removeControlCodes(char *in)
{
	if(in == NULL) return strdup("");

	char *out = (char *)malloc(strlen(in)+1);
	int outAt = 0;

	for(int i = 0; i < strlen(in); i++)
	{
		if(in[i] == 0x03)
		{
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

int calcMessageBreaks(char *line, int usableWidth, int index)
{
	int numLines = 0;

	int linelen = strlen(line);
	int linestart = 0;
	int linewidth = 0;
	int lastspace = -1;
	int endoftext;

	while(linestart < linelen)
	{
		lastspace = 0;
		linewidth = 0;
		endoftext = 0;

		while(XTextWidth(chatFontStruct, &line[linestart], linewidth) < usableWidth)
		{
			linewidth++;
			if(line[linestart+linewidth] == ' ') lastspace = linewidth;
			if(linewidth+linestart >= linelen) {
				endoftext = 1;
				break;
			}
		}

		if(endoftext)
		{
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

int recalculateMessageBreaks()
{
	Dimension chatWidth;
	int i;
	int totalLines;
	int nameOffset = prefs.showTimestamp ? 64 : 4;
	int textOffset = prefs.showTimestamp ? 180 : 120;

	XtVaGetValues(chatList, XmNwidth, &chatWidth, NULL);

	if(currentTarget == NULL)
	{
		return 0;
	}

	for (i = 0; i < currentTarget->messageAt; i++)
	{
		int usableWidth = chatWidth - (((currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL)?textOffset:nameOffset)+20);
		char *filteredMessage = removeControlCodes(currentTarget->messages[i]->message);
		int alreadyCounted = 0;

		if(prefs.discordBridgeName && !strcmp(currentTarget->messages[i]->source, prefs.discordBridgeName))
		{
			char *firstSpace = strchr(filteredMessage, ' ');
			if(firstSpace)
			{
				firstSpace++;
				totalLines += calcMessageBreaks(firstSpace, usableWidth, i);
				alreadyCounted = 1;
			}
		}
		if(!alreadyCounted)
		{
			totalLines += calcMessageBreaks(filteredMessage, usableWidth, i);
		}

		free(filteredMessage);
	}
	return totalLines;
}

void recalculateBreaksAndScrollBar()
{
	Dimension curHeight;
	int windowHeight, chatHeight, totalLines;

	XtVaGetValues(chatList, XmNheight, &curHeight, NULL);
	windowHeight = curHeight;
	totalLines = recalculateMessageBreaks();
	chatHeight = MAX(windowHeight, (totalLines+1)*12);

	XtVaSetValues(scrollbar, XmNmaximum, chatHeight, XmNsliderSize, windowHeight, XmNvalue, chatHeight-windowHeight, XmNpageIncrement, windowHeight>>1, NULL);

	forceRedraw();
}

void textInputCallback(Widget textField, XtPointer client_data, XtPointer call_data)
{
	char *newtext = XmTextFieldGetString(textField);

	if (!newtext || !*newtext) {
		XtFree(newtext); /* XtFree() checks for NULL */
		return;
	}

	if(currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL && newtext[0] != '/')
	{
		char fullmessage[4096];
		snprintf(fullmessage, 4095, "PRIVMSG %s :%s", currentTarget->title, newtext);
		sendIRCCommand(fullmessage);

		struct Message *message = MessageInit(nick, currentTarget->title, newtext);
		AddMessageToTarget(currentTarget, message);

		recalculateBreaksAndScrollBar();
	} else if(currentTarget != NULL && currentTarget->type == MESSAGETARGET_WHISPER && newtext[0] != '/') {
		char fullmessage[4096];
		snprintf(fullmessage, 4095, "PRIVMSG %s :%s", currentTarget->title, newtext);
		sendIRCCommand(fullmessage);

		struct Message *message = MessageInit(nick, currentTarget->title, newtext);
		AddMessageToTarget(currentTarget, message);

		recalculateBreaksAndScrollBar();
	} else {
		char *start = newtext;
		int skipSendingCommand = 0;
		
		if (*start == '/') start++;

		if(strstr(start, "CLOSE") == start || strstr(start, "close") == start)
		{
			if (currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL)
			{
				char buffer[1024];
				snprintf(buffer, 1023, "PART %s", currentTarget->title);
				sendIRCCommand(buffer);
			}

			int removingIndex = currentTarget->index;
			int newCount = RemoveMessageTarget(currentTarget);
			if(newCount >= 0)
			{
				MemberListFree(MessageTargetMembers[removingIndex]);
				for(int i = removingIndex; i < newCount; i++)
				{
					MessageTargetMembers[i] = MessageTargetMembers[i+1];
				}
			}
			
			SetupChannelList();
			skipSendingCommand = 1;
		}

		if(strstr(start, "JOIN ") == start || strstr(start, "join ") == start)
		{
			int index = AddMessageTarget(start+5, start+5, MESSAGETARGET_CHANNEL);
			if (index >= 0)
			{
				MessageTargetMembers[index] = MemberListInit();
			}
			SetupChannelList();
		}
		if(strstr(start, "PART") == start || strstr(start, "part") == start)
		{
			if (currentTarget != NULL && currentTarget->type == MESSAGETARGET_CHANNEL)
			{
				char buffer[1024];

				if(start[4] == ' ' && start[5] != ' ' && start[5] != 0 && start[5] != '#' && start[5] != '!' && start[5] != '&')
				{
					snprintf(buffer, 1023, "PART %s :%s", currentTarget->title, start+5);
				} else {
					snprintf(buffer, 1023, "PART %s", currentTarget->title);
				}
				sendIRCCommand(buffer);

				int removingIndex = currentTarget->index;
				int newCount = RemoveMessageTarget(currentTarget);
				if(newCount >= 0)
				{
					MemberListFree(MessageTargetMembers[removingIndex]);
					for(int i = removingIndex; i < newCount; i++)
					{
						MessageTargetMembers[i] = MessageTargetMembers[i+1];
					}
				}
				
				SetupChannelList();
			}
			skipSendingCommand = 1;
		}
		if(strstr(start, "MSG") == start || strstr(start, "msg") == start)
		{
			
			char *firstSpace = strchr(start+4, ' ');
			char *target = NULL;
			char *text = NULL;
			
			if(firstSpace)
			{
				struct MessageTarget *msgTarget;
				char fullmessage[4096];

				*firstSpace = 0;
				target = strdup(start+4);
				*firstSpace = ' ';
				text = firstSpace + 1;

				snprintf(fullmessage, 4095, "PRIVMSG %s :%s", target, text);
				sendIRCCommand(fullmessage);
				skipSendingCommand = 1;
				
				msgTarget = FindMessageTargetByName(target);
				if(!msgTarget)
				{
					int index = AddMessageTarget(target, target, MESSAGETARGET_WHISPER);
					if (index >= 0)
					{
						MessageTargetMembers[index] = MemberListInit();
						msgTarget = FindMessageTargetByName(target);
						SetupChannelList();
					}
				}
				if(msgTarget)
				{

					struct Message *message = MessageInit(nick, target, text);
					AddMessageToTarget(msgTarget, message);

					if(msgTarget == currentTarget)
					{
						recalculateBreaksAndScrollBar();
					}
				}	
				free(target);
			}
		
		}

		if(!skipSendingCommand)
		{
			sendIRCCommand(start);
		}
	}
	
	XtFree(newtext);
	XmTextFieldSetString(textField, "");
}

void switchToMessageTarget(struct MessageTarget *target)
{
	currentTarget = target;
	recalculateBreaksAndScrollBar();
	SetupNamesList();
	MessageTargetHasUpdate[target->index] = 0;
	RefreshChannelList();
}

void channelSelectedCallback(Widget chanList, XtPointer userData, XtPointer callData)
{
	for (int i = 0; i < NumMessageTargets; i++)
	{
		if (XmListPosSelected(chanList, i+1))
		{
			switchToMessageTarget(&MessageTargets[i]);
			return;
		}
	}

}


// IRC Client Callbacks
void ircClientUpdateCallback(struct Message *message, void *userdata)
{
	struct MessageTarget *target;
	char *actualTarget = message->target;
	if(!strcmp(actualTarget, nick))
	{
		actualTarget = message->source;
	}
	target = FindMessageTargetByName(actualTarget);
	if(target == NULL && message->target != NULL)
	{
		int index = AddMessageTarget(actualTarget, actualTarget, MESSAGETARGET_WHISPER);
		if (index >= 0)
		{
			MessageTargetMembers[index] = MemberListInit();
			target = FindMessageTargetByName(actualTarget);
			SetupChannelList();
		}
	}

	if(target != NULL)
	{
		AddMessageToTarget(target, message);

		if(target == currentTarget) 
		{
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
void ircClientChannelJoinCallback(char *channel, char *name, void *userdata)
{
	struct MessageTarget *messageTarget = FindMessageTargetByName(channel);
	if (messageTarget)
	{
		AddToMemberList(MessageTargetMembers[messageTarget->index], name);
		if (messageTarget == currentTarget) SetupNamesList();
	}	
}
void ircClientChannelPartCallback(char *channel, char *name, char *partMessage, void *userdata)
{
	struct MessageTarget *messageTarget = FindMessageTargetByName(channel);
	if (messageTarget)
	{
		char buffer[1024];
		snprintf(buffer, 1023, "** %s has left %s (%s)", name, channel, partMessage?partMessage:"no message");
		struct Message *message = MessageInit(name, messageTarget->title, buffer);
		message->type = MESSAGE_TYPE_SERVERMESSAGE;
		AddMessageToTarget(messageTarget, message);

		RemoveFromMemberList(MessageTargetMembers[messageTarget->index], name);
		if (messageTarget == currentTarget) SetupNamesList();
	}	
}
void ircClientChannelQuitCallback(char *name, char *quitMessage, void *userdata)
{
	for(int i = 0; i < NumMessageTargets; i++)
	{
		if(RemoveFromMemberList(MessageTargetMembers[i], name))
		{
			struct MessageTarget *messageTarget = &MessageTargets[i];
			char buffer[1024];
			snprintf(buffer, 1023, "** %s has quit (%s)", name, quitMessage?quitMessage:"no message");
			struct Message *message = MessageInit(name, messageTarget->title, buffer);
			message->type = MESSAGE_TYPE_SERVERMESSAGE;
			AddMessageToTarget(messageTarget, message);

			if (messageTarget == currentTarget) SetupNamesList();
		}	
	}
}

void updateTimerCallback(XtPointer clientData, XtIntervalId *timer)
{
	XtAppContext * app = (XtAppContext *)clientData;

	updateIRCClient(ircClientUpdateCallback, ircClientChannelJoinCallback, ircClientChannelPartCallback, ircClientChannelQuitCallback, (void *)chatList);

	XtAppAddTimeOut(*app, 50, updateTimerCallback, app);
}
	

void chatListResizeCallback(Widget widget, XtPointer client_data, XtPointer call_data)
{
	recalculateBreaksAndScrollBar();
}

void scrollbarChangedCallback(Widget widget, XtPointer client_data, XtPointer call_data)
{
	forceRedraw();
}

static Boolean convertSelectionCallback(Widget widget, Atom *selection, Atom *target,
                                 Atom *type_return, XtPointer *value_return,
                                 unsigned long *length_return,
                                 int *format_return)
{
	char *buf;

	if (widget != chatList || *selection != XA_PRIMARY)
	{
		return (False);
	}

	if (*target != XA_STRING && *target != XInternAtom(XtDisplay(chatList), "TEXT", TRUE))
	{
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

static void loseSelectionCallback(Widget w, Atom *selection)
{
	selectStartX = -1;
	selectStartY = -1;
	selectEndX = -1;
	selectEndY = -1;
	forceRedraw();
}

int processForDiscordBridge(char *buffer, const char *line, int *linestart)
{
	if(line[0] != '<')
	{
		return 0;
	}

	char *nameClose = strstr(line, "> ");
	if(nameClose)
	{
		if(buffer)
		{
			nameClose[1] = 0;
			snprintf(buffer, 255, "<%s>", line);
			nameClose[1] = ' ';
		}
		*linestart = (nameClose-line)+2;
		return 1;
	} else {
		return 0;
	}
}

void updateSelectionIndices()
{
	Display *display = XtDisplay(chatList);
	Drawable window = XtWindow(chatList);
	Dimension curWidth = 0, curHeight = 0;
	Position y = 12;
	int scrollValue;
	int i;

	selectStartLine = selectStartY / 12;	// This is due to us using a set font that's 12 pixels high...
	selectEndLine = selectEndY / 12;

	int nameOffset = prefs.showTimestamp ? 64 : 4;
	int textOffset = prefs.showTimestamp ? 180 : 120;

	if(currentTarget == NULL)
	{
		return;
	}

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	XtVaGetValues(chatList, XmNwidth, &curWidth, XmNheight, &curHeight, NULL);

	y -= scrollValue;

	selectedText[0] = 0;

	for(i = 0; i < currentTarget->messageAt; i++)
	{
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);

		if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL)
		{
			if(prefs.discordBridgeName && !strcmp(currentTarget->messages[i]->source, prefs.discordBridgeName))
			{
				processForDiscordBridge(NULL, line, &linestart);
			}
		}

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++)
		{
			int offset = (currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) ? textOffset : nameOffset;
			int yline = (y+scrollValue-12)/12;

			if(yline > selectEndLine)
			{
				break;
			}

			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX))
			{
				int preSel = 0;
				int preSelW = 0;
				int postSel = 0;
				int postSelW = 0;

				if(yline == selectStartLine && yline == selectEndLine)
				{
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < 	selectStartX && preSel < linewidth)
					{
						preSel++;
					}
					if(preSel > 0) preSel--;
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);
					postSel = preSel;
					while(offset+XTextWidth(chatFontStruct, &line[linestart], postSel) < selectEndX && postSel < linewidth)
					{
						postSel++;
					}
					postSelW = XTextWidth(chatFontStruct, &line[linestart], postSel);

					selectStartIndex = preSel;
					selectStartOffset = preSelW;
					selectEndIndex = postSel;
					selectEndOffset = postSelW;
				} else if(yline == selectStartLine) {
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < selectStartX && preSel < linewidth)
					{
						preSel++;
					}
					if(preSel > 0) preSel--;
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);

					selectStartIndex = preSel;
					selectStartOffset = preSelW;
				} else if(yline == selectEndLine) {
					while(offset+XTextWidth(chatFontStruct, &line[linestart], preSel) < selectEndX && preSel < linewidth)
					{
						preSel++;
					}
					preSelW = XTextWidth(chatFontStruct, &line[linestart], preSel);

					selectEndIndex = preSel;
					selectEndOffset = preSelW;
				}
			}
			linestart += linewidth;
			y += 12;
				
		}
		free(line);
	}
}

void captureSelection()
{
	Position y = 12;
	int scrollValue;
	int i;
	int curLen = 0;

	if(currentTarget == NULL)
	{
		return;
	}

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	y -= scrollValue;

	selectedText[0] = 0;

	for(i = 0; i < currentTarget->messageAt; i++)
	{
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);

		if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL)
		{
			if(prefs.discordBridgeName && !strcmp(currentTarget->messages[i]->source, prefs.discordBridgeName))
			{
				processForDiscordBridge(NULL, line, &linestart);
			}
		}

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++)
		{
			int yline = (y+scrollValue-12)/12;

			if(yline > selectEndLine)
			{
				break;
			}

			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX))
			{
				if(yline == selectStartLine && yline == selectEndLine)
				{
					int newLen = selectEndIndex-selectStartIndex;
					if(curLen+newLen < 1023)
					{
						memcpy(selectedText+curLen, &line[linestart+selectStartIndex], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline == selectStartLine) {
					int newLen = linewidth-selectStartIndex;
					if(curLen+newLen < 1023)
					{
						memcpy(selectedText+curLen, &line[linestart+selectStartIndex], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline == selectEndLine) {
					int newLen = selectEndIndex;
					if(curLen+newLen < 1023)
					{
						memcpy(selectedText+curLen, &line[linestart], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				} else if(yline > selectStartLine && yline < selectEndLine){
					int newLen = linewidth;
					if(curLen+newLen < 1023)
					{
						memcpy(selectedText+curLen, &line[linestart], newLen);
						selectedText[curLen+newLen] = 0;
						curLen += newLen;
					}
				}
			}
			linestart += linewidth;
			y += 12;
				
		}
		free(line);
	}

	if(strlen(selectedText) > 1)
	{
//		int status;
//		long id = 0;
		Time t = XtLastTimestampProcessed(XtDisplay(chatList));

		int result = XtOwnSelection(chatList, XA_PRIMARY, t, convertSelectionCallback, loseSelectionCallback, NULL);
	}
}


void drawChatList()
{
	Display *display = XtDisplay(chatList);
	Drawable window = XtWindow(chatList);
	Dimension curWidth = 0, curHeight = 0;
	Position y = 12;
	int scrollValue;
	int i;
	GC gc = chatGC;
	char buffer[1024];

	int selectStartLine = selectStartY / 12;	// This is due to us using a set font
	int selectEndLine = selectEndY / 12;

	int nameOffset = prefs.showTimestamp ? 64 : 4;
	int textOffset = prefs.showTimestamp ? 180 : 120;

	if(currentTarget == NULL)
	{
		return;
	}

	XtVaGetValues(scrollbar, XmNvalue, &scrollValue, NULL);
	XtVaGetValues(chatList, XmNwidth, &curWidth, XmNheight, &curHeight, NULL);

	y -= scrollValue;

	if(copyOnNextDraw)
	{
		selectedText[0] = 0;
	}

	for(i = 0; i < currentTarget->messageAt; i++)
	{
		int linestart = 0;
		int linewidth = 0;
		char *line = removeControlCodes(currentTarget->messages[i]->message);
		int linelen = strlen(line);
		int isBridge = 0;

		if(y > -12)
		{
			if(prefs.showTimestamp)
			{
				XDrawString(display, window, gc, 4, y, currentTarget->messages[i]->timestampString, strlen(currentTarget->messages[i]->timestampString));
			}


			if(currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL)
			{
				if(prefs.discordBridgeName && !strcmp(currentTarget->messages[i]->source, prefs.discordBridgeName))
				{
					isBridge = processForDiscordBridge(buffer, line, &linestart);
				}
				if(!isBridge)
				{
					snprintf(buffer, 255, "<%s>", currentTarget->messages[i]->source);
				}

				if(strlen(buffer) > 16)
				{
					if(isBridge)
					{
						buffer[14] = '>';
					}
					buffer[15] = '>';
					buffer[16] = 0;
				}
				XDrawString(display, window, gc, nameOffset, y, buffer, strlen(buffer));
			}
		}

		for(int lineOfMessage = 0; lineOfMessage < LinesPerMessage[i]; lineOfMessage++)
		{
			int offset = (currentTarget->messages[i]->type == MESSAGE_TYPE_NORMAL) ? textOffset : nameOffset;
			int yline = (y+scrollValue-12)/12;
			int drewLine = 0;
			linewidth = LineBreaksPerMessage[i][lineOfMessage];

			if(y > -12)
			{
				if(selectEndY>selectStartY || (selectEndY==selectStartY && selectEndX>selectStartX))
				{
					int preSel = selectStartIndex;
					int preSelW = selectStartOffset;
					int postSel = selectEndIndex;
					int postSelW = selectEndOffset;

					if(yline == selectStartLine && yline == selectEndLine)
					{
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
						XDrawString(display, window, gc, offset, y, &line[linestart], linewidth);
				}
			}
			linestart += linewidth;
			y += 12;

			if(y > curHeight+20)
			{
				break;
			}
				
		}
		free(line);
	}
}

void chatListRedrawCallback(Widget widget, XtPointer client_data, XtPointer call_data)
{
	drawChatList();
}

void selection(Widget widget, XEvent *event, String *args, Cardinal *num_args)
{
	int scrollValue;
	XtVaGetValues (scrollbar, XmNvalue, &scrollValue, NULL);

	if(*num_args != 1)
	{
		return;
	}

	if(strcmp(args[0], "start"))
	{
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

	if(!strcmp(args[0], "stop"))
	{
		captureSelection();
	}

	forceRedraw();
}

char *stringFromXmString(XmString xmString)
{
	XmStringContext context;
	char buffer[1024];
	char *text;
	XmStringCharSet charset;
	XmStringDirection direction;
	XmStringComponentType unknownTag;
	unsigned short unknownLen;
	unsigned char *unknownData;
	XmStringComponentType type;

	if(!XmStringInitContext(&context, xmString))
	{
		return strdup("");
	}
	buffer[0] = 0;
	while((type = XmStringGetNextComponent(context, &text, &charset, &direction, &unknownTag, &unknownLen, &unknownData)) != XmSTRING_COMPONENT_END)
	{
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

void connectAndSetNick(char *server, int port, char *nick)
{
	disconnectFromServer();
	RemoveAllMessageTargets();
	AddMessageTarget(SERVER_TARGET, server, MESSAGETARGET_SERVER);
	MessageTargetMembers[0] = MemberListInit();
	currentTarget = FindMessageTargetByName(SERVER_TARGET);
	SetupChannelList();

	printf("Attempting to connect to %s:%d\n", server, port);
	connectToServer(server, port);

	char connbuffer[1024];
	snprintf(connbuffer, 1023, "NICK %s", nick);
	sendIRCCommand(connbuffer);
	snprintf(connbuffer, 1023, "USER %s 0 * :%s", nick, nick);
	sendIRCCommand(connbuffer);
}

void connectToServerCallback(Widget widget, XtPointer client_data, XtPointer call_data) 
{
	XmSelectionBoxCallbackStruct *cbs;
	cbs = (XmSelectionBoxCallbackStruct *)call_data;
	char *server = stringFromXmString(cbs->value);
	char *portstr = strchr(server, ':');
	int port = 6667;
	if(portstr) {
		*portstr = 0;
		portstr++;
		port = atoi(portstr);
	}
	if(prefs.defaultServer) {
		free(prefs.defaultServer);
	}
	prefs.defaultServer = strdup(server);
	prefs.defaultPort = port;
	SavePrefs(&prefs, altPrefsFile);

	connectAndSetNick(server, port, nick);
	free(server);
}

void setNickCallback(Widget widget, XtPointer client_data, XtPointer call_data) 
{
	XmSelectionBoxCallbackStruct *cbs;
	cbs = (XmSelectionBoxCallbackStruct *)call_data;
	char *newnick = stringFromXmString(cbs->value);

	printf("Changing nick from %s to %s\n", nick, newnick);
	free(nick);
	nick = newnick;

	if(prefs.defaultNick) {
		free(prefs.defaultNick);
	}
	prefs.defaultNick = strdup(newnick);
	SavePrefs(&prefs, altPrefsFile);

	char connbuffer[1024];
	snprintf(connbuffer, 1023, "NICK %s", nick);
	sendIRCCommand(connbuffer);

}

void closeDialogCallback(Widget widget, XtPointer client_data, XtPointer call_data)
{
	XtDestroyWidget(XtParent(widget));
}

void fileMenuSimpleCallback(Widget widget, XtPointer client_data, XtPointer call_data)
{
	// client_data is an int for index of menu item
	switch((int)client_data) {
	case 0:	// Connect...
		{
			Widget dialog;
			XmString str = XmStringCreateLocalized("Enter server[:port]");
			char buffer[1024];
			Arg args[5];
			int n = 0;
			buffer[0] = 0;
			if(prefs.defaultServer) {
				strcat(buffer, prefs.defaultServer);
			}
			if(prefs.defaultPort > 0 && prefs.defaultPort != 6667) {
				char ports[16];
				sprintf(ports, ":%d", prefs.defaultPort);
				strcat(buffer, ports);
			}
			XmString cur = XmStringCreate(buffer, "CUR_SERVER");		
			XtSetArg(args[n], XmNselectionLabelString, str); n++;
			XtSetArg(args[n], XmNautoUnmanage, True); n++;
			XtSetArg(args[n], XmNtextString, cur); n++;
			dialog = (Widget)XmCreatePromptDialog(window, "server_prompt", args, n);
			XmStringFree(str);
			XmStringFree(cur);
			XtAddCallback(dialog, XmNokCallback, connectToServerCallback, NULL);
			XtAddCallback(dialog, XmNcancelCallback, closeDialogCallback, NULL);
			XtSetSensitive(XtNameToWidget(dialog, "Help"), False);
			XtManageChild(dialog);
		}
		break;
	case 1:	// Set nick...
		{
			Widget dialog;
			XmString str = XmStringCreateLocalized("Enter your nick:");
			XmString cur = XmStringCreate(nick, "CUR_NICK");
			Arg args[5];
			int n = 0;
			XtSetArg(args[n], XmNselectionLabelString, str); n++;
			XtSetArg(args[n], XmNautoUnmanage, True); n++;
			XtSetArg(args[n], XmNtextString, cur); n++;
			dialog = (Widget)XmCreatePromptDialog(window, "nick_prompt", args, n);
			XmStringFree(str);
			XmStringFree(cur);
			XtAddCallback(dialog, XmNokCallback, setNickCallback, NULL);
			XtAddCallback(dialog, XmNcancelCallback, closeDialogCallback, NULL);
			XtSetSensitive(XtNameToWidget(dialog, "Help"), False);
			XtManageChild(dialog);
		}
		break;
	case 2:	// Exit
		XtAppSetExitFlag(app);
		break;
	}
}

void printUsage()
{
	printf("Usage: sgirc [-n <nick>] [-s <server>] [-p <port>] [-f <prefs file>] [-c]\n");
	printf("\t-n: Set nick to use\n");
	printf("\t-s: Set server to connect to\n");
	printf("\t-p: Set port to connect to\n");
	printf("\t-f: Specify alternate preferences file\n");
	printf("\t-c: Connect at startup\n");
	printf("\n");
}

void addServerConnection(char *server, int port, char *nick)
{
	connectAndSetNick(server,  port>0?port:6667, nick);
}


int main(int argc, char** argv) 
{
	Widget      	textField, formLayout, mainWindow, menubar;
	Arg         	args[32];
	int         	n = 0;
	XGCValues		gcv;
	String			translations = "<Btn1Down>: selection(start) ManagerGadgetArm()\n<Btn1Up>: selection(stop) ManagerGadgetActivate()\n<Btn1Motion>: selection(move) ManagerGadgetButtonMotion()";
	XtActionsRec	actions;

	// Command line args
	char *cmdServer = NULL;
	char *cmdPort = NULL;
	char *cmdNick = NULL;
	int cmdAutoConnect = 0;
	int cmdOption;

	selectStartX = -1;
	selectStartY = -1;
	selectEndX = -1;
	selectEndY = -1;

	while((cmdOption = getopt(argc, argv, "s:p:n:f:c")) != -1)
	{
		switch(cmdOption)
		{
		case 's':
			cmdServer = strdup(optarg);
			break;
		case 'p':
			cmdPort = strdup(optarg);
			break;
		case 'n':
			cmdNick = strdup(optarg);
			break;
		case 'f':
			altPrefsFile = strdup(optarg);	// This one is static for the run
			break;
		case 'c':
			cmdAutoConnect = 1;
			break;
		case '?':
			printUsage();
			return 0;
		}
	}
	LoadPrefs(&prefs, altPrefsFile);
	SavePrefs(&prefs, altPrefsFile);

	NumMessageTargets = 0;

	if(cmdNick)
	{
		nick = strdup(cmdNick);
	} else if(prefs.defaultNick) {
		nick = strdup(prefs.defaultNick);
	} else {
		nick = strdup("def_sgirc_n");
	}
	printf("Default nick: %s\n", nick);

	initIRCClient();

	XtSetLanguageProc(NULL, NULL, NULL);

	window = XtVaAppInitialize(&app, "SgIRC", NULL, 0, &argc, argv, fallbacks, NULL);

	XtAppAddTimeOut(app, 100, updateTimerCallback, &app);

	mainWindow = (Widget)XmCreateMainWindow(window, "main_window", NULL, 0);
	XtManageChild(mainWindow);

	formLayout = XtVaCreateWidget("formLayout", xmFormWidgetClass, mainWindow, NULL);

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

	XtVaSetValues(mainWindow, XmNmenuBar, menubar, XmNworkWindow, formLayout, NULL);

	actions.string = "selection";
	actions.proc = selection;
	XtAppAddActions(app, &actions, 1);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNwidth, 150); n++;
	channelList = XmCreateScrolledList(formLayout, "channelList", args, n);
	XtManageChild(channelList);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNwidth, 150); n++;
	namesList = XmCreateScrolledList(formLayout, "namesList", args, n);
	XtManageChild(namesList);

	textField = XtVaCreateManagedWidget("textField", xmTextFieldWidgetClass, formLayout, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, channelList, XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, namesList, XmNbottomAttachment, XmATTACH_FORM, NULL);

	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, textField); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNrightWidget, namesList); n++;
	XtSetArg(args[n], XmNresizable, 0); n++;
	XtSetArg(args[n], XmNincrement, 6); n++;
	scrollbar = XmCreateScrollBar(formLayout, "scrollbar", args, n);
	XtManageChild(scrollbar);


	n = 0;
	XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNbottomWidget, textField); n++;
	XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
	XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNleftWidget, channelList); n++;
	XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
	XtSetArg(args[n], XmNrightWidget, scrollbar); n++;
	XtSetArg(args[n], XmNresizable, 1); n++;
	XtSetArg(args[n], XmNresizePolicy, XmRESIZE_ANY); n++;
	XtSetArg(args[n], XmNtranslations, XtParseTranslationTable(translations)); n++;
	XtSetArg(args[n], XmNwidth, 400); n++;
	XtSetArg(args[n], XmNminWidth, 300); n++;
	XtSetArg(args[n], XmNheight, 250); n++;


	chatList = XmCreateDrawingArea(formLayout, "chatList", args, n);
	XtManageChild(chatList);

	XtAddCallback(chatList, XmNexposeCallback, chatListRedrawCallback, NULL);
	XtAddCallback(chatList, XmNresizeCallback, chatListResizeCallback, NULL);
	XtAddCallback(scrollbar, XmNvalueChangedCallback, scrollbarChangedCallback, NULL);
	XtAddCallback(textField, XmNactivateCallback, textInputCallback, chatList);
	XtAddCallback(channelList, XmNbrowseSelectionCallback, channelSelectedCallback,  NULL);


	{
		XrmInitialize();
		XrmDatabase xrdb = XrmGetDatabase(XtDisplay(formLayout));
		if(!xrdb)
		{
			printf("NULL xrdb\n");
			return 0;
		}
		char *strtype = NULL;
		XrmValue value;
		if(XrmGetResource(xrdb, "sgirc.chatList.chatFont", "Sgirc.Chatlist.ChatFont", &strtype, &value))
		{
			chatFontStruct = XLoadQueryFont(XtDisplay(chatList), value.addr);
		} else {
			chatFontStruct = XLoadQueryFont(XtDisplay(chatList), "-*-screen-medium-r-normal--12-*-*-*-*-*-*-*");
		}
	}
	gcv.foreground = BlackPixelOfScreen(XtScreen(chatList));
	chatGC = XCreateGC(XtDisplay(chatList), RootWindowOfScreen(XtScreen(chatList)), GCForeground, &gcv);
	XSetFont(XtDisplay(chatList), chatGC, chatFontStruct->fid);
	gcv.background = BlackPixelOfScreen(XtScreen(chatList));
	gcv.foreground = WhitePixelOfScreen(XtScreen(chatList));
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

	AddMessageTarget(SERVER_TARGET, "Server", MESSAGETARGET_SERVER);
	MessageTargetMembers[0] = MemberListInit();
	currentTarget = FindMessageTargetByName(SERVER_TARGET);
	SetupChannelList();

	if(cmdAutoConnect || prefs.connectOnLaunch)
	{
		if(cmdServer)
		{
			addServerConnection(cmdServer, cmdPort?atoi(cmdPort):0, nick);
		} else if(prefs.defaultServer) {
			addServerConnection(prefs.defaultServer, prefs.defaultPort>0?prefs.defaultPort:6667, nick);
		}
	}
	XtAppMainLoop(app);

	return 0;
}


    
				    
