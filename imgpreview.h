#ifndef _IMGPREVIEW_H
#define _IMGPREVIEW_H

struct Message;

struct ImagePreviewRequest {
	struct Message *message;
	char *url;
	volatile int started;
	volatile int completed;
	volatile int cancelled;
	struct ImagePreviewRequest *next;

	char *pixmapData;
	int pixmapWidth;
	int pixmapHeight;

	int maxWidth;
	int maxHeight;

	int pngAt;
	unsigned char *pngBuffer;
};

void fetchImagePreview(struct ImagePreviewRequest *request);
struct ImagePreviewRequest *initImagePreviewRequest(struct Message *message, char *url, int maxWidth, int maxHeight);

#endif
