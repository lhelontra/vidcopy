/* vidcopy                                                                  */
/*========================================================================= */
/* Copyright (C)2016 Leonardo Lontra <lhe.lontra@gmail.com>                 */
/*                                                                          */
/* This program is distributed under the terms of the GNU                   */
/* General Public License, version 2. You may use, modify,                  */
/* and redistribute it under the terms of this license. A                   */
/* copy should be included with this source.                                */
/* based on https://linuxtv.org/downloads/v4l-dvb-apis/capture-example.html */

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <assert.h>
#include <getopt.h>
#include "vidcopy.h"

static unsigned int running = 1;

static int xioctl(int fd, int request, void *arg) {
	int r;
	do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);
	return r;
}

int v4l2_capabilities(v4l2device *dev) {
	/*
	 * print capabilites of camera
	 */
	struct v4l2_capability caps;
	if (xioctl(dev->fd, VIDIOC_QUERYCAP, &caps) == -1) {
		perror("Querying Capabilities");
		return 1;
	}
	print_debug("Driver Caps:\n"
			"  Driver: \"%s\"\n"
			"  Card: \"%s\"\n"
			"  Bus: \"%s\"\n"
			"  Version: %d.%d\n"
			"  Capabilities: %08x\n",
			caps.driver,
			caps.card,
			caps.bus_info,
			(caps.version>>16)&&0xff,
			(caps.version>>24)&&0xff,
			caps.capabilities);

	struct v4l2_fmtdesc fmtdesc;
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	char c, e;
	char fourcc[5];
	print_debug("  FMT : CE Desc\n--------------------\n");
	while (0 == xioctl(dev->fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
		c = fmtdesc.flags & 1? 'C' : ' ';
		e = fmtdesc.flags & 2? 'E' : ' ';
		print_debug("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
		fmtdesc.index++;
	}
	return 0;
}

int v4l2_set_input(v4l2device *dev) {
	/*
	 * define video input
	 * vfe_v4l2 the driver is forced to input = -1
	 * set as the input = 0, works fine.
	 */
	struct v4l2_input input;
	int count = 0;
	CLEAR(input);
	input.index = count;
	while(!xioctl(dev->fd, VIDIOC_ENUMINPUT, &input)) {
		input.index = ++count;
	}
	count -= 1;
	
	assert(count > -1);
	
	if(xioctl(dev->fd, VIDIOC_S_INPUT, &count) == -1) {
		print_debug("Error selecting input %d", count);
		return 1;
	}
	return 0;
}

int v4l2_set_pixfmt(v4l2device *dev) {
	/*
	 * define pixel format
	 * in gc2035 driver, tested with:
	 * -- 422P/YU12/YV12/NV16/NV12/NV61/NV21/UYVY
	 * others will be ignored
	 * 
	 */
	struct v4l2_format fmt;
	CLEAR(fmt);
	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = dev->width;        // ignored by gc2035 driver
	fmt.fmt.pix.height = dev->height;      // ignored by gc2035 driver
	fmt.fmt.pix.pixelformat = dev->fmt;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
	if (-1 == xioctl(dev->fd, VIDIOC_S_FMT, &fmt)) {
		perror("Setting Pixel Format");
		return 1;
	}
	print_debug("Selected Camera Mode:\n"
			"  Width: %d\n"
			"  Height: %d\n"
			"  PixFmt: %s\n",
			fmt.fmt.pix.width,
			fmt.fmt.pix.height,
			(char *)&fmt.fmt.pix.pixelformat);
	
	dev->width = fmt.fmt.pix.width;
	dev->height = fmt.fmt.pix.height;
	
	if (dev->fmt != fmt.fmt.pix.pixelformat) {
		perror("Pix format not accepted");
		return 1;
	}
	return 0;
}

int v4l2_set_fps(v4l2device *dev) {
	/*
	 * set camera frame rate
	 */
	struct v4l2_streamparm setfps;
	CLEAR(setfps);
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps.parm.capture.timeperframe.numerator = 1;
	setfps.parm.capture.timeperframe.denominator = dev->fps;
	if(xioctl(dev->fd, VIDIOC_S_PARM, &setfps) == -1) {
		perror("Error setting frame rate");
		return 1;
	}
	dev->fps = setfps.parm.capture.timeperframe.denominator;
	return 0;
}

int v4l2_init_mmap(v4l2device *dev) {
	/*
	 * setup memory map mode buffers
	 */
	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = dev->n_buffers;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (-1 == xioctl(dev->fd, VIDIOC_REQBUFS, &req)) {
		perror("Requesting Buffer");
		return 1;
	}
	dev->buffer = malloc(req.count * sizeof(buffer_t));
	if (!dev->buffer) {
		perror("Out of memory");
		return 1;
	}
	unsigned int i;
	for(i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = i;
		if(xioctl(dev->fd, VIDIOC_QUERYBUF, &buf) == -1) {
			perror("VIDIOC_QUERYBUF");
			return 1;
		}
		dev->buffer[i].length = buf.length;
		dev->buffer[i].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
		if(dev->buffer[i].data == MAP_FAILED) {
			perror("Error mapping buffer");
			return 1;
		}
	}
	return 0;
}

int prepare_cap(v4l2device *dev) {
	/*
	 * preparing to capture frames
	 */
	unsigned int i;
	for (i = 0; i < dev->n_buffers; ++i) {
		struct v4l2_buffer buf;
		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (xioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
			perror("VIDIOC_QBUF");
			return 1;
		}
	}
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(dev->fd, VIDIOC_STREAMON, &type) < 0) {
		perror("VIDIOC_STREAMON");
		return 1;
	}
	return 0;
}

int stop_capturing(v4l2device *dev) {
	/*
	 * stopping capture and free nmap buffers
	 */
	enum v4l2_buf_type type;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(dev->fd, VIDIOC_STREAMOFF, &type) < 0) {
		perror("VIDIOC_STREAMOFF");
		return 1;
	}
	unsigned int i;
	for (i = 0; i < dev->n_buffers; ++i) {
		if (munmap(dev->buffer[i].data, dev->buffer[i].length) < 0) {
			perror("munmap");
			return 1;
		}
	}
	return 0;
}

int grabFrame(v4l2device *dev) {
	/*
	 * read and processing image and write into output
	 */
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(dev->fd, &fds);
	struct timeval tv;
	CLEAR(tv);
	tv.tv_sec = dev->timeout;
	tv.tv_usec = 0;
	int r = select(dev->fd + 1, &fds, NULL, NULL, &tv);
	if(-1 == r) {
		perror("Waiting for Frame");
		return 1;
	}
	struct v4l2_buffer buf;
	unsigned int i;
	CLEAR(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	// dequeue buffer
	if (xioctl(dev->fd, VIDIOC_DQBUF, &buf) < 0) {
		perror("VIDIOC_DQBUF");
		return 1;
	}
	assert(buf.index < dev->n_buffers);
	// processing buffer
	if (dev->fdout_devname == -1) {
		fwrite(dev->buffer[buf.index].data, buf.bytesused, 1, stdout);
		fflush(stdout);
	} else {
		write(dev->fdout_devname, dev->buffer[buf.index].data, buf.bytesused);
	}
	// put buffer
	if (xioctl(dev->fd, VIDIOC_QBUF, &buf) < 0) {
		perror("VIDIOC_QBUF");
		return 1;
	}
    return 0;
}

int v4l2loopbackDevice(v4l2device *dev) {
	/*
	 * configure dst video device 
	 */
	dev->fdout_devname = open(dev->out_devname, O_RDWR | O_NONBLOCK, 0);
	if (dev->fdout_devname == -1) {
		perror("v4l2loopback: Opening virtual video device");
		return 1;
	}
	struct v4l2_capability vid_caps;
	struct v4l2_format vid_format;
	CLEAR(vid_format);
	
	if (xioctl(dev->fdout_devname, VIDIOC_QUERYCAP, &vid_caps) < 0) {
		perror("v4l2loopback: VIDIOC_QUERYCAP");
		return 1;
	}
	vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vid_format.fmt.pix.width = dev->width;
	vid_format.fmt.pix.height = dev->height;
	vid_format.fmt.pix.pixelformat = dev->fmt;
	vid_format.fmt.pix.field = V4L2_FIELD_ANY;
	if (xioctl(dev->fdout_devname, VIDIOC_S_FMT, &vid_format) < 0) {
		perror("v4l2loopback: VIDIOC_S_FMT");
		return 1;
	}
	
	struct v4l2_streamparm setfps;
	CLEAR(setfps);
	setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps.parm.capture.timeperframe.numerator = 1;
	setfps.parm.capture.timeperframe.denominator = dev->fps;
	if(xioctl(dev->fdout_devname, VIDIOC_S_PARM, &setfps) == -1) {
		perror("v4l2loopback: Error setting frame rate");
		return 1;
	}
	return 0;
}

void sighandler(int signo) {
	/*
	 * exit gracefully when you are receiving a signal
	 */
	if (signo == SIGINT || signo == SIGTERM || signo == SIGPIPE) {
		print_debug("%d received. Exiting...\n", signo);
		running = 0;
	}
}

void usage(char *app) {
	printf("usage %s [-w <width>] [-h <height>] [-r <fps>] [-i <input device>] [-o <output device>] [-f <pixformat>]\n" 
	"\t -w <width>                  set height of frame\n"
	"\t -h <height>                 set with of frame\n"
	"\t -r <framerate>              set frame rate\n"
	"\t -i <input device>           set input device.  default: /dev/video0\n"
	"\t -o <output device>          set output device. default: stdout\n" 
	"\t                             for copying to another device, using v4l2loopback\n"
	"\t -f <pixformat>              Fourcc format default: YUYV\n"
	"\t                             for working with gc2035 module, works with: UYVY/YV12/YU12\n"
	, app);
	exit(1);
}

void process_args(int argc, char* argv[], v4l2device *dev) {
	/*
	 * processing input args
	 */
	int opt = 0;
	int fourcc_len = 0;
	while((opt = getopt(argc, argv, "w:h:i:o:r:f:")) != -1) {
		switch (opt){
			case 'w':
				dev->width = atoi(optarg);
				if (!dev->width) goto WRONG_ARG;
				break;
				
			case 'h':
				dev->height = atoi(optarg);
				if (!dev->height) goto WRONG_ARG;
				break;
				
			case 'r':
				dev->fps = atoi(optarg);
				if (!dev->fps) goto WRONG_ARG;
				break;
				
			case 'i':
				dev->in_devname = optarg;
				if (!dev->in_devname) goto WRONG_ARG;
				break;
				
			case 'o':
				dev->out_devname = optarg;
				if (!dev->out_devname) goto WRONG_ARG;
				break;
				
			case 'f':
				fourcc_len = strlen(optarg);
				if (fourcc_len != 4) goto WRONG_ARG;
				int pixfmt;
				pixfmt = v4l2_fourcc(toupper(optarg[0]), toupper(optarg[1]), toupper(optarg[2]), toupper(optarg[3]));
				dev->fmt = pixfmt;
				if (!dev->fmt) goto WRONG_ARG;
				break;
			
			WRONG_ARG:
			default:
				usage(argv[0]);
		}
	}
}

int main(int argc, char* argv[]) {
	v4l2device *dev;
	dev = malloc(sizeof(v4l2device));
	CLEAR(*dev);
	unsigned int retcode = 0;
	
	// default values
	dev->fd = -1;
	dev->fdout_devname = -1;
	dev->width = 320;
	dev->height = 240;
	dev->fps = 30;
	dev->n_buffers = 4;
	dev->timeout = 5;
	dev->in_devname = "/dev/video0";
	dev->out_devname = "-";
	dev->fmt = V4L2_PIX_FMT_YUYV;
	process_args(argc, argv, dev);
	
	// print args
	print_debug("Required width: %d\n", dev->width);
	print_debug("Required height: %d\n", dev->height);
	print_debug("Required FPS: %d\n", dev->fps);
	print_debug("input device: %s\n", dev->in_devname);
	print_debug("output device: %s\n", dev->out_devname);
	
	dev->fd = open(dev->in_devname, O_RDWR | O_NONBLOCK, 0);
	if (dev->fd == -1) {
		perror("Opening video device");
		retcode = 1;
		goto FINISH;
	}
	
// 	signal handling
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);
// 	check device capabilites 
	v4l2_capabilities(dev);
// 	set input
	retcode = v4l2_set_input(dev);
	if (retcode) goto FINISH;
// 	set pixformat
	retcode = v4l2_set_pixfmt(dev);
	if (retcode) goto FINISH;
// 	set framerate
	retcode = v4l2_set_fps(dev);
	if (retcode) goto FINISH;
// 	allocate buffers
	retcode = v4l2_init_mmap(dev);
	if (retcode) goto FINISH;
// 	prepare to grab frames
	retcode = prepare_cap(dev);
	if (retcode) goto FINISH;
	// configure virtual video device
	if (*dev->out_devname != '-') {
		retcode = v4l2loopbackDevice(dev);
		if (retcode) goto FINISH;
	}
	print_debug("capturing..\n");
	while (running) {
		retcode = grabFrame(dev);
		if (retcode) running = 0;
	}
	stop_capturing(dev);
	FINISH:
	close(dev->fd);
	if (dev->fdout_devname > 0) close(dev->fdout_devname);
	free(dev);
	return retcode;
}