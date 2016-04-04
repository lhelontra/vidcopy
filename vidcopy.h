/*
 * vidcopy header
 * Author: leonardo lontra
 */
#ifndef __VIDCOPY__H__
#define __VIDCOPY__H__

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define DEBUG 1
#if(DEBUG == 1)
#define print_debug(x,arg...) fprintf(stderr, x, ##arg)
#else
#define print_debug(x,arg...)
#endif
	
typedef struct {
	unsigned char *data;
	size_t length;
} buffer_t;

typedef struct {
	int             fd;               // device file descriptor
	int             fdout_devname;    // virtual device file descriptor
	int             fmt;
	unsigned int    timeout;
	unsigned int    width;
	unsigned int    height;
	unsigned int    fps;
	unsigned int    n_buffers;
	char            *in_devname;
	char            *out_devname;
	buffer_t        *buffer;
} v4l2device;

static int xioctl(int fd, int request, void *arg);
/* show device apabilities */
int v4l2_capabilities(v4l2device *dev);
/* set input */
int v4l2_set_input(v4l2device *dev);
/* set pixel format */
int v4l2_set_pixfmt(v4l2device *dev);
/* set fps */
int v4l2_set_fps(v4l2device *dev);
/* setup buffers */
int v4l2_init_mmap(v4l2device *dev);
/* preparing capture */
int prepare_cap(v4l2device *dev);
/* stopping capture and free buffers */
int stop_capturing(v4l2device *dev);
/* grab frame and processing data */
int grabFrame(v4l2device *dev);
/* configure virtual video device */
int v4l2loopbackDevice(v4l2device *dev);
/* handler signals */
void sighandler(int signo);
/* help */
void usage(char *app);
/* processing input args */
void process_args(int argc, char* argv[], v4l2device *dev);

#endif //__VIDCOPY__H__ 
