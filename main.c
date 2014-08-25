/* 
 * PNG2RLE , standalone cross-platform png to rle converter.
 * Copyright (C) 2014 Alireza Forouzandeh Nezhad <alirezafn@gmx.us> <http://alirezafn.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include "png.h"

#define to565(r,g,b)                                            \
    ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

#define from565_r(x) ((((x) >> 11) & 0x1f) * 255 / 31)
#define from565_g(x) ((((x) >> 5) & 0x3f) * 255 / 63)
#define from565_b(x) (((x) & 0x1f) * 255 / 31)

typedef FILE file;

static long get_file_size(file *f) {
	long current_offset=ftell(f);
	fseek(f,0,SEEK_END);
	long size=ftell(f);
	fseek(f,current_offset,SEEK_SET);
	return size;
}

static char *load_file_to_mem(char *path) {
	file *f=fopen(path,"r");
	long size=get_file_size(f);
	char *mem=malloc((int)size);
	fread(mem,1,size,f);
	fclose(f);
	return mem;
}

static void write_mem_to_file(char *path,char *mem,unsigned int size) {
	file *f=fopen(path,"w");
	fwrite(mem,1,size,f);
	fclose(f);
	return;
}

// log functions for later usage

static void log_info(char *m,...) {
	va_list arg;
	va_start(arg,m);
	int s_size=strlen(m)+8;
	char *s=malloc(s_size);
	strcpy(s,"[INFO]\t");
	strcat(s,m);
	vfprintf(stdout,s,arg);
	va_end(arg);
	free(s);
}

static void log_warn(char *m,...) {
	va_list arg;
	va_start(arg,m);
	int s_size=strlen(m)+8;
	char *s=malloc(s_size);
	strcpy(s,"[WARNING]\t");
	strcat(s,m);
	vfprintf(stdout,s,arg);
	va_end(arg);
	free(s);
}

static void log_err(char *m,...) {
	va_list arg;
	va_start(arg,m);
	int s_size=strlen(m)+8;
	char *s=malloc(s_size);
	strcpy(s,"[ERROR]\t");
	strcat(s,m);
	vfprintf(stdout,s,arg);
	va_end(arg);
	free(s);
}

typedef struct {
	void (*warn)(char *,...);
	void (*err)(char *,...);
	void (*info)(char *,...);
} log_ops;

static log_ops log = {
	.info=log_info,
	.err=log_err,
	.info=log_info,
};

typedef struct {
	char *start; // where memblk memory start
	int size; // sizeof memblk memory
	int offset; // current position
} memblk;

static memblk *memopen(void *mem,int size) {
	memblk *m=malloc(sizeof(memblk));
	m->start=mem;
	m->size=size;
	m->offset=0;
	return m;
}

// simulate a POSIX read function but in memory instead of file

static int memread(memblk *f,void *buffer,int size) {
	int read_len=size;
	if(f->offset >= f->size) {
		return 0;
	}
	if((size+f->offset) > f->size) {
		read_len=f->size-(f->offset+size);
	}
	memcpy(buffer,f->start+f->offset,read_len);
	f->offset+=read_len;
	return read_len;
}

// simulate a POSIX close function but in memory instead of file

static void memclose(memblk *f) {
	free(f);
}

// simulate a POSIX write function but in memory instead of file
// dynamicly expand memblk memory like when file size expands while writing

static int memwrite(memblk *f,void *buffer,int size) {
	f->start=realloc(f->start,size+f->size);
	if(!f->start) {
		return -1;
	}
	f->size+=size;
	memcpy(f->start+f->offset,buffer,size);
	f->offset+=size;
	return size;
}

// simulate a POSIX lseek function but in memory instead of file

static int memseek(memblk *f,unsigned int offset,int whence) {
	if(whence == 0) {
		f->offset=offset;
	} else if(whence == 1) {
		f->offset+=offset;
	} else if(whence == 2) {
		f->offset=f->size+offset;
		// if offset is > 0 then we are trying to go out of range of
		// f->start so set f->size to current offset to tell memwrite 
		// to expand the memory allocated
		if(offset > 0) {
			f->size=f->offset;
		}

	} else {
		return -1;
	}
}

// image pixel formats

typedef enum {
	IMG_RGB888=0,
	IMG_RGBA8888=1,
	IMG_RGB565=2,
	IMG_RLE565=3,
	IMG_UNKOWN=4,
} img_format;

// a struct to hold image info

typedef struct {
	unsigned char *mem; // ptr to where the image has been loaded
	unsigned int height;
	unsigned int width;
	img_format format;
	unsigned int error; // if there where an error keep it here
	unsigned size;
} image;

static image *new_image(unsigned int width,unsigned int height,img_format format) {
	image *i=(image *)malloc(sizeof(image));
	i->height=height;
	i->width=width;
	i->format=format;
	i->error=0;
	if(format == IMG_RGBA8888) {
		i->mem=malloc(width*height*4);
	} else if(format == IMG_RGB888) {
		i->mem=malloc(width*height*3);
	} else if(format == IMG_RGB565) {
		i->mem=malloc(width*height*2);
	} else {
		i->mem=0;
	}
	return i;
}

// read a png from file and write it in memory allocated image

static image *read_png_file_into_image(const char *path) {
	image *i=(image *)malloc(sizeof(image));
	i->error=lodepng_decode32_file(&i->mem, &i->width, &i->height, path);
	i->format=IMG_RGBA8888;
	return i;
}

// convert an image to rgb image

static image *convert_to_rgb(image *in) {
	if(in->format!=IMG_RGBA8888) {
		return 0;
	}
	int j;
	image *o=new_image(in->width,in->height,IMG_RGB888);
  	for(j=0;j<in->width*in->height;j++) {
  		o->mem[j*3+0]=in->mem[j*4+0];
  		o->mem[j*3+1]=in->mem[j*4+1];
  		o->mem[j*3+2]=in->mem[j*4+2];
  	}
  	return o;
}

static memblk *memopen_image(image *i) {
	int size=0;
	if(i->format == IMG_RGBA8888) {
		size=i->height*i->width*4;
	} else if(i->format == IMG_RGB888) {
		size=i->height*i->width*3;
	} else if(i->format == IMG_RGB565) {
		size=i->height*i->width*2;
	} else {
		return 0;
	}
	return memopen(i->mem,size);
}

static image *convert_to_rle(image *ini) {
	// currently only rgb888 is supported !
	if(ini->format!=IMG_RGB888) {
		return 0;
	}
	memblk *m=memopen_image(ini);
	memblk *outfd=memopen(malloc(0),0);
	unsigned char in[3];
    unsigned short last, color, count;
    unsigned total = 0;
    count = 0;
    while(memread(m, in, 3) == 3) {
        color = to565(in[0],in[1],in[2]);
        if (count) {
            if ((color == last) && (count != 65535)) {
                count++;
                continue;
            } else {
                memwrite(outfd, &count, 2);
                memwrite(outfd, &last, 2);
                total += count;
            }
        }
        last = color;
        count = 1;
    }
    if (count) {
        memwrite(outfd, &count, 2);
        memwrite(outfd, &last, 2);
        total += count;
    }
    image *i=malloc(sizeof(image));
    i->format=IMG_RLE565;
    i->mem=outfd->start;
    i->size=outfd->size;
    return i;

}

static int write_image_to_file(char *path,image *i) {
	write_mem_to_file(path,i->mem,i->size);
}


int main(int argc,char **argv) {
	if(argc<3 || !strcmp(argv[1],"--help")) { printf("PNG to RLE converter\nusage: %s input_png output_rle\ncopyright 2014 Alireza7991 <alirezafn@gmx.us> <http://alirezafn.net>\n",argv[0]);
	exit(0); }
  	write_image_to_file(argv[2],convert_to_rle(convert_to_rgb(read_png_file_into_image(argv[1]))));
  	exit(0);
}