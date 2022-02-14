#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <jpeglib.h>
#include <png.h>

#include "imgpreview.h"
#include "net.h"
#include "prefs.h"

#pragma set woff 3970

extern struct Prefs prefs;

struct jpeg_error_mgr err;

void init_mem_source(struct jpeg_decompress_struct *jd){}
void term_source(struct jpeg_decompress_struct *jd) {}
boolean fill_mem_input_buffer(struct jpeg_decompress_struct *jd) {
	static const JOCTET mybuffer[4] = {
		(JOCTET)0xff, (JOCTET)JPEG_EOI, 0, 0
	};
	jd->src->next_input_byte = mybuffer;
	jd->src->bytes_in_buffer = 2;
	return 1;
}
void skip_input_data(struct jpeg_decompress_struct *jd, long len) {
	struct jpeg_source_mgr *src = jd->src;
	if(len > 0) {
		while(len > (long)src->bytes_in_buffer) {
			len -= (long)src->bytes_in_buffer;
			(void)(*src->fill_input_buffer)(jd);
		}
		src->next_input_byte += len;
		src->bytes_in_buffer -= len;
	}
}

// NOTE: This was taken from the source of a newer version of libjpeg
// github.com/winlibs/libjpeg/blob/master/jdatasrc.c
void jpeg_mem_src(struct jpeg_decompress_struct *jd, const unsigned char *buf, const int size) {
	struct jpeg_source_mgr *src;
	src = jd->src;
	if(!src) {
		printf("jpeg_source_mgr is null!");
fflush(stdout);
	}
	src->init_source = init_mem_source;
	src->fill_input_buffer = fill_mem_input_buffer;
	src->skip_input_data = skip_input_data;
	src->resync_to_restart = jpeg_resync_to_restart;
	src->term_source = term_source;
	src->bytes_in_buffer = size;
	src->next_input_byte = (const JOCTET *)buf;
}

int scaledImageFromPixbufNice(char *pixbuf, int w, int h, struct ImagePreviewRequest *request) {
	if(!pixbuf || !w || !h) {
		return 0;
	}

	int scaleCount = 0;
	int neww = w;
	int newh = h;
	int scaler;
	while((neww > request->maxWidth) || (newh > request->maxHeight)) {
		scaleCount++;
		neww>>=1;
		newh>>=1;
	};

	for(scaler = 0; scaler < scaleCount; scaler++) {
		neww = w>>1;
		newh = h;
		char *newpixbuf = (char *)malloc(neww*newh*4);
		for(int y = 0; y < h; y++) {
			for(int x = 0; x < w-1; x+=2) {
				int dstoff = (y*neww+(x>>1))<<2;
				int srcoff = (y*w+x)<<2;
				newpixbuf[dstoff+0] = 
					(pixbuf[srcoff+0]>>1) +
					(pixbuf[srcoff+4]>>1) +
					((pixbuf[srcoff+0]>>1)&(pixbuf[srcoff+4]>>1)&1);
				newpixbuf[dstoff+1] = 
					(pixbuf[srcoff+1]>>1) +
					(pixbuf[srcoff+5]>>1) +
					((pixbuf[srcoff+1]>>1)&(pixbuf[srcoff+5]>>1)&1);
				newpixbuf[dstoff+2] = 
					(pixbuf[srcoff+2]>>1) +
					(pixbuf[srcoff+6]>>1) +
					((pixbuf[srcoff+2]>>1)&(pixbuf[srcoff+6]>>1)&1);
				newpixbuf[dstoff+3] = 
					(pixbuf[srcoff+3]>>1) +
					(pixbuf[srcoff+7]>>1) +
					((pixbuf[srcoff+3]>>1)&(pixbuf[srcoff+7]>>1)&1);
			}
		}
		free(pixbuf);
		pixbuf = newpixbuf;
		w = neww;
	}
	for(scaler = 0; scaler < scaleCount; scaler++) {
		neww = w;
		newh = h>>1;
		char *newpixbuf = (char *)malloc(neww*newh*4);
		for(int y = 0; y < h-1; y+=2) {
			for(int x = 0; x < w; x++) {
				int dstoff = ((y>>1)*neww+x)<<2;
				int srcoff1 = (y*w+x)<<2;
				int srcoff2 = ((y+1)*w+x)<<2;
				newpixbuf[dstoff+0] = 
					(pixbuf[srcoff1+0]>>1) +
					(pixbuf[srcoff2+0]>>1) +
					((pixbuf[srcoff1+0]>>1)&(pixbuf[srcoff2+0]>>1)&1);
				newpixbuf[dstoff+1] = 
					(pixbuf[srcoff1+1]>>1) +
					(pixbuf[srcoff2+1]>>1) +
					((pixbuf[srcoff1+1]>>1)&(pixbuf[srcoff2+1]>>1)&1);
				newpixbuf[dstoff+2] = 
					(pixbuf[srcoff1+2]>>1) +
					(pixbuf[srcoff2+2]>>1) +
					((pixbuf[srcoff1+2]>>1)&(pixbuf[srcoff2+2]>>1)&1);
				newpixbuf[dstoff+3] = 
					(pixbuf[srcoff1+3]>>1) +
					(pixbuf[srcoff2+3]>>1) +
					((pixbuf[srcoff1+3]>>1)&(pixbuf[srcoff2+3]>>1)&1);
			}
		}
		free(pixbuf);
		pixbuf = newpixbuf;
		h = newh;
	}

	request->pixmapData = pixbuf;
	request->pixmapWidth = w;
	request->pixmapHeight = h;
	return 1;
}

int scaledImageFromPixbufFast(char *pixbuf, int w, int h, struct ImagePreviewRequest *request) {
	if(!pixbuf || !w || !h) {
		return 0;
	}
	int scale, neww, newh;

	scale = 0;
	do {
		scale++;
		neww = w/scale;
		newh = h/scale;
	} while((neww > request->maxWidth) || (newh > request->maxHeight));
	if(neww < 1) neww = 1;
	if(newh < 1) newh = 1;
	if(scale > 1) {
printf("Resizing image to %dx%d\n", neww, newh);
		char *newpixbuf = (char *)malloc(neww*newh*4);
		for(int y = 0; y < newh; y++) {
			for(int x = 0; x < neww; x++) {
				newpixbuf[((y*neww+x)*4)+0] = 	pixbuf[(((y*scale*w)+(x*scale))*4)+0];
				newpixbuf[((y*neww+x)*4)+1] = pixbuf[(((y*scale*w)+(x*scale))*4)+1];
				newpixbuf[((y*neww+x)*4)+2] = pixbuf[(((y*scale*w)+(x*scale))*4)+2];
				newpixbuf[((y*neww+x)*4)+3] = 	pixbuf[(((y*scale*w)+(x*scale))*4)+3];
			}
		}
		free(pixbuf);
		pixbuf = newpixbuf;
		w = neww;
		h = newh;
	}

	request->pixmapData = pixbuf;
	request->pixmapWidth = w;
	request->pixmapHeight = h;
	return 1;
}

int scaledImageFromPixbuf(char *pixbuf, int w, int h, struct ImagePreviewRequest *request) {
	if(prefs.imagePreviewQuality > 0) {
		return scaledImageFromPixbufNice(pixbuf, w, h, request);
	} else {
		return scaledImageFromPixbufFast(pixbuf, w, h, request);
	}
} 

int decompressJPEG(const unsigned char *buffer, const int size, struct ImagePreviewRequest *request) {
	int rc, w, h, comp, stride;
	unsigned char *bmp;
	struct jpeg_decompress_struct jpgdec;
	struct jpeg_source_mgr src;

//printf("decompressJPEG with buffer %x of size %d\n", buffer, size);
	jpgdec.err = jpeg_std_error(&err);
	jpeg_create_decompress(&jpgdec);
	jpgdec.src = &src;

	jpeg_mem_src(&jpgdec, buffer, size);

	rc = jpeg_read_header(&jpgdec, TRUE);
	if(rc != 1) {
		return 0;
	}

	jpeg_start_decompress(&jpgdec);

	w = jpgdec.output_width;
	h = jpgdec.output_height;
	comp = jpgdec.output_components;

	printf("Image is %dx%d\n", w, h);
	bmp = (unsigned char *)malloc(w*h*comp);
	stride = w*comp;

	while(jpgdec.output_scanline < h) {
			unsigned char *arr[1];
			arr[0] = bmp+(jpgdec.output_scanline*stride);
		jpeg_read_scanlines(&jpgdec, arr, 1);
	}

	jpeg_finish_decompress(&jpgdec);
	jpeg_destroy_decompress(&jpgdec);

	// Now I have to convert this to a Pixmap compatible buffer...
	char *pixbuf = (char *)malloc(w*h*4);
	for(int y = 0; y < h; y++) {
		for(int x = 0; x < w; x++) {
			pixbuf[((y*w)+x)*4+0] = 0xff;
			pixbuf[((y*w)+x)*4+1] = bmp[((y*w)+x)*3+2];
			pixbuf[((y*w)+x)*4+2] = bmp[((y*w)+x)*3+1];
			pixbuf[((y*w)+x)*4+3] = bmp[((y*w)+x)*3+0];
		}
	}
	free(bmp);
	return scaledImageFromPixbuf(pixbuf, w, h, request);
}

static int pngAt;
static unsigned char *pngBuffer;

void pngReadData(png_structp png_ptr, png_bytep data, png_size_t len) {
	memcpy(data, pngBuffer+pngAt, len);
	pngAt += len;
}

int decompressPNG(const unsigned char *buffer, const int size, struct ImagePreviewRequest *request) {
	unsigned char header[8];
	int w, h, comp, stride;
	unsigned char *bmp;

	pngBuffer = (unsigned char *)buffer;

	pngAt = 0;
	memcpy(header, &buffer[pngAt], 8);
	pngAt += 8;
	if (png_sig_cmp(header, 0, 8)) {
		//printf("Not a PNG image\n");
		return 0;
	}

	png_structp png_ptr;
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if(!png_ptr) {
		//printf("Failed to create read struct\n");
		return 0;
	}

	png_infop info_ptr;
	info_ptr = png_create_info_struct(png_ptr);

	png_set_read_fn(png_ptr, NULL, pngReadData);

	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	w = png_get_image_width(png_ptr, info_ptr);
	h = png_get_image_height(png_ptr, info_ptr);
	comp = png_get_bit_depth(png_ptr, info_ptr);
	int channels = png_get_channels(png_ptr, info_ptr);
	int colorType = png_get_color_type(png_ptr, info_ptr);

	printf("Image is %dx%d with %dbpp w/ %d channels and color type %d\n", w, h, comp, channels, colorType);

	switch(colorType) {
	case PNG_COLOR_TYPE_PALETTE:
		png_set_palette_to_rgb(png_ptr);
		channels = 3;
		break;
	case PNG_COLOR_TYPE_GRAY:
		if(comp < 8) {
			png_set_gray_1_2_4_to_8(png_ptr);
			comp = 8;
		}
	}

	if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png_ptr);
		channels++;
	}
	if(comp == 16) {
		png_set_strip_16(png_ptr);
		comp = 8;
	}

	png_read_update_info(png_ptr, info_ptr);

	bmp = (unsigned char *)malloc(w*h*4);
	stride = (w*(comp>>3)*channels);

	png_bytep *rows = (png_bytep *)malloc(h * sizeof(png_bytep));
	for(int y = 0; y < h; y++) {
		rows[y] = &bmp[y*stride];
	}

	png_read_image(png_ptr, rows);

	free(rows);
	png_destroy_read_struct(&png_ptr, &info_ptr, 0);

	// Now I have to convert this to a Pixmap compatible buffer...
	char *pixbuf = (char *)malloc(w*h*4);
	for(int y = 0; y < h; y++) {
		for(int x = 0; x < w; x++) {
			if(channels == 3) {
				pixbuf[((y*w)+x)*4+0] = 0xff;
				pixbuf[((y*w)+x)*4+1] = bmp[((y*w)+x)*3+2];
				pixbuf[((y*w)+x)*4+2] = bmp[((y*w)+x)*3+1];
				pixbuf[((y*w)+x)*4+3] = bmp[((y*w)+x)*3+0];
			} else if(channels == 4) {
				pixbuf[((y*w)+x)*4+0] = bmp[((y*w)+x)*4+3];
				pixbuf[((y*w)+x)*4+1] = bmp[((y*w)+x)*4+2];
				pixbuf[((y*w)+x)*4+2] = bmp[((y*w)+x)*4+1];
				pixbuf[((y*w)+x)*4+3] = bmp[((y*w)+x)*4+0];
			} else if(channels == 1) {
				pixbuf[((y*w)+x)*4+0] = 0xff;
				pixbuf[((y*w)+x)*4+1] = bmp[((y*w)+x)];
				pixbuf[((y*w)+x)*4+2] = bmp[((y*w)+x)];
				pixbuf[((y*w)+x)*4+3] = bmp[((y*w)+x)];
			}
		}
	}
	free(bmp);
	return scaledImageFromPixbuf(pixbuf, w, h, request);
}

static void *fetchThread(void *arg) {
	struct ImagePreviewRequest *request = (struct ImagePreviewRequest *)arg;

	printf("Trying to fetch image preview %s\n", request->url);

	int size;
	unsigned char *imgdata = fetchURL(request->url, "", &size);
	if(imgdata) {
		int ret;
		printf("Fetched image of %d bytes\n", size);
		ret = decompressPNG(imgdata, size, request);
		if(!ret) {
			decompressJPEG(imgdata, size, request);
		}
		free(imgdata);
	}

	request->completed = 1;
	return NULL;
}

void fetchImagePreview(struct ImagePreviewRequest *request) {
	pthread_t tid;
	request->started = 1;
	int ret = pthread_create(&tid, NULL, fetchThread, request);
}

struct ImagePreviewRequest *initImagePreviewRequest(struct Message *message, char *url, int maxWidth, int maxHeight) {
	struct ImagePreviewRequest *req = (struct ImagePreviewRequest *)malloc(sizeof(struct ImagePreviewRequest));
	req->message = message;
	req->url = strdup(url);
	req->started = 0;
	req->completed = 0;
	req->cancelled = 0;
	req->next = NULL;
	req->pixmapData = NULL;
	req->pixmapWidth = 0;
	req->pixmapHeight = 0;
	req->maxWidth = maxWidth;
	req->maxHeight = maxHeight;
	req->pngAt = 0;
	req->pngBuffer = NULL;
	return req;
}
