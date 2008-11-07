#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev.h>

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

typedef unsigned long Pixel;

typedef struct video_input_stream 
{
  struct video_capability capability;
  struct video_picture picture;
  struct video_mmap vmmap;
  struct video_mbuf buffer;
  char * map;
} video_input_stream, * video_input_stream_t;

int init_video_device (int fd, video_input_stream *stream)
{
  if (ioctl (fd, VIDIOCGCAP, &stream->capability) == -1) {
    perror("Error getting device capabilities");
  }
  if (ioctl (fd, VIDIOCGPICT, &stream->picture) < 0) {
    perror("Error getting picture settings");
  } else {
    stream->picture.palette = VIDEO_PALETTE_RGB24;
    if (ioctl (fd, VIDIOCSPICT, &stream->picture) < 0)
      perror("Error setting picture settings");
  }
  if (ioctl (fd, VIDIOCGMBUF, &stream->buffer) < 0) {
    perror("ioctl(VIDIOCGMBUF)");
    return -1; 
  }

  if ((stream->map = mmap(0, stream->buffer.size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == (char *)-1) {
    perror("mmap");
    return -1;
  }

  stream->vmmap.width = 320;
  stream->vmmap.height = 240;
  stream->vmmap.format = VIDEO_PALETTE_RGB24;
  stream->vmmap.frame = 0;

  if (ioctl(fd, VIDIOCMCAPTURE, &stream->vmmap ) < 0) {
    perror("ioctl(VIDIOCMCAPTURE)");
    return -1;
  }

  if (ioctl(fd, VIDIOCSYNC, &stream->vmmap.frame) < 0) {
    perror("ioctl(VIDIOCSYNC)");
    return -1;
  }

  return 0;
}

int start_screencast(video_input_stream *stream, Display *display, Window window) 
{
  XImage *image;
  XWindowAttributes win_info;
  int c = 0;

  for(;;) {
    if(!XGetWindowAttributes(display, window, &win_info)) 
      perror("Can't get target window attributes.");
    image = XGetImage(display, window, 0, 0, win_info.width, win_info.height, AllPlanes, ZPixmap);
    update_frame(stream, image);
    XDestroyImage(image);
    usleep(100*1000); //10 FPS
    //printf("frame: %d\n", c++);
  }
  return 0;
}

int update_frame(video_input_stream *stream, XImage *image) 
{
  char *map = stream->map+stream->buffer.offsets[0];
  int width = stream->vmmap.width;
  int height = stream->vmmap.height;
  int layer = 3;
  int n;
  int color;
  int x, y;
  int dx, dy;
  int fx, fy;
  double scale_x, scale_y;
  char *p;
  int filter[] = { 255, 8, 255, 
                   8, 2, 8,
                   255, 8, 255};
  int findex; 
  scale_x = (double)image->width / width;
  scale_y = (double)image->height / height;
  for( n = 0, p = map; n < width * height; n++, p += layer ){
    x = (int)((n - width*(n/width)) * scale_x);
    y = (int)(n/width * scale_y);

		p[0] = p[1] = p[2] = 0;
    for (dx = -1; dx < 2; dx++) {
      for (dy = -1; dy < 2; dy++) {
				fx = x + dx;
				fx = (fx < 0) ? 0 : (fx > image->width -1 ? image->width - 1 : fx);
				fy = y + dy;
				fy = (fy < 0) ? 0 : (fy > image->height -1 ? image->height - 1 : fy);
				color = XGetPixel(image, fx, fy);
        findex = (dy+1)*3+(dx+1);
				p[0] += (color & 0xFF) / filter[findex];
				p[1] += ((color & 0xFF00) >> 8) / filter[findex];
				p[2] += ((color & 0xFF0000) >> 16) / filter[findex];
			}
		}
		//color = XGetPixel(image, x, y);
		//p[0] = color & 0xFF;
		//p[1] = (color & 0xFF00) >> 8;
		//p[2] = (color & 0xFF0000) >> 16;
  }

  return 0;
}

Window Select_Window(Display *display, int screen)
{
  int status;
  Cursor cursor;
  XEvent event;
  Window target_win = None, root = RootWindow(display, screen);
  int buttons = 0;

  /* Make the target cursor */
  cursor = XCreateFontCursor(display, XC_crosshair);

  /* Grab the pointer using target cursor, letting it room all over */
  status = XGrabPointer(display, root, False,
			ButtonPressMask|ButtonReleaseMask, GrabModeSync,
			GrabModeAsync, root, cursor, CurrentTime);
  if (status != GrabSuccess) perror("Can't grab the mouse.");

  /* Let the user select a window... */
  while ((target_win == None) || (buttons != 0)) {
    /* allow one more event */
    XAllowEvents(display, SyncPointer, CurrentTime);
    XWindowEvent(display, root, ButtonPressMask|ButtonReleaseMask, &event);
    switch (event.type) {
    case ButtonPress:
      if (target_win == None) {
	target_win = event.xbutton.subwindow; /* window selected */
	if (target_win == None) target_win = root;
      }
      buttons++;
      break;
    case ButtonRelease:
      if (buttons > 0) /* there may have been some down before we started */
	buttons--;
       break;
    }
  } 

  XUngrabPointer(display, CurrentTime);      /* Done with pointer */

  return(target_win);
}

int main (int argc, char **argv)
{
  int fd;
  char * device_fname = "/dev/video0";

  char * display_name = NULL;
  video_input_stream *stream;
  Display *display = XOpenDisplay(NULL);
  int screen = XDefaultScreen(display);
  Window window = Select_Window(display, screen);

  if ((fd=open(device_fname, O_RDWR)) == -1) {
    perror("Error opening video device");
    close(fd);
    return -1;
  }

  stream = malloc(sizeof(video_input_stream));
  if ((init_video_device(fd, stream)) < 0) {
    perror ("init_video_device()");
    free(stream);
    close(fd);
    return -1;
  }

  if (start_screencast(stream, display, window) < -1) { 
    perror ("start_screencast()");
    free(stream);
    close(fd);
    return -1;
  }
  free(stream);
  close(fd);

  return 0; 
}
