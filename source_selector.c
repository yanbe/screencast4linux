#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev.h>

#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_FRAME_RATE 10

typedef struct video_input_stream 
{
  struct video_capability capability;
  struct video_picture picture;
  struct video_mmap vmmap;
  struct video_mbuf buffer;
  char * map;
} video_input_stream, * video_input_stream_t;

typedef enum SourceType
{
  SOURCE_WINDOW,
  SOURCE_RECT
} SourceType; 


typedef struct screencast_source
{
  SourceType type;
  Display *display;
  int screen;
  Window window;
  int x;
  int y;
  int width;
  int height;
} screencast_source, * screencast_source_t;

int fd;
video_input_stream *stream = NULL;
screencast_source *source = NULL;

int enable_smoothing = 0;
int enable_mouse_homing = 0;

int init_video_device (int fd, video_input_stream *stream, int width, int height)
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

  stream->vmmap.width = width;
  stream->vmmap.height = height;
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

int update_frame(video_input_stream *stream, XImage *image) 
{
  int n;
  char *p;
  char *map = stream->map+stream->buffer.offsets[0];
  int width = stream->vmmap.width;
  int height = stream->vmmap.height;
  int layer = 3;
  int x, y;

  double scale_x = (double)image->width / width;
  double scale_y = (double)image->height / height;
  double scale = scale_x < scale_y ? scale_y : scale_x;

  int offset_x = (width - width * (scale_y < scale_x ? 1 : scale_x/scale_y)) / 2;
  int offset_y = (height - height * (scale_x < scale_y ? 1 : scale_y/scale_x)) / 2;

  int color;

  // for smothing
  //int dx, dy;
  //int fx, fy;
  //int filter[] = { 255,  8, 255, 
  //                   8,  2,   8,
  //                 255,  8, 255};
  //int findex; 
  for( n = 0, p = map; n < width * height; n++, p += layer ) {
    x = (n - width*(n/width));
    y = n/width;

    p[0] = p[1] = p[2] = 0;

    if ((x < offset_x || x > width - offset_x) || (y < offset_y || y > height - offset_y))
      continue;

    x = (x - offset_x) * scale; 
    y = (y - offset_y) * scale; 

    color = XGetPixel(image, x, y);
    p[0] = (color & 0xFF);
    p[1] = ((color & 0xFF00) >> 8);
    p[2] = ((color & 0xFF0000) >> 16);

    //render with smothing (SLOW)
    //for (dx = -1; dx < 2; dx++) {
    //  for (dy = -1; dy < 2; dy++) {
    //    fx = x + dx;
    //    fx = (fx < 0) ? 0 : (fx > image->width -1 ? image->width - 1 : fx);
    //    fy = y + dy;
    //    fy = (fy < 0) ? 0 : (fy > image->height -1 ? image->height - 1 : fy);
    //    color = XGetPixel(image, fx, fy);
    //    findex = (dy+1)*3+(dx+1);
    //    p[0] += (color & 0xFF) / filter[findex];
    //    p[1] += ((color & 0xFF00) >> 8) / filter[findex];
    //    p[2] += ((color & 0xFF0000) >> 16) / filter[findex];
    //  }
    //}
  }
  return 0;
}

int start_screencast(video_input_stream *stream, screencast_source *source, int wait_nanoseconds) 
{
  XImage *image;
  XWindowAttributes win_info;
  int c = 0;

  printf("frame: %d\r", c++);
  for(;;) {
    if (source->type == SOURCE_WINDOW) {
      if(!XGetWindowAttributes(source->display, source->window, &win_info)) 
        perror("Can't get target window attributes.");
      image = XGetImage(source->display, source->window, 0, 0, 
          win_info.width, win_info.height, AllPlanes, ZPixmap);
    } else {
      image = XGetImage(source->display, source->window, source->x, source->y, 
          source->width, source->height, AllPlanes, ZPixmap);
    }
    update_frame(stream, image);
    XDestroyImage(image);
     
    usleep(wait_nanoseconds);
    fprintf(stderr, "Frame: %d\r", c++);
  }
  fputs("\n", stderr);
  return 0;
}


Window Select_Window(screencast_source *source)
{
  static int xfd = 0;
  static int fdsize = 0;
  XEvent ev;
  fd_set fdset;
  int count = 0, done = 0;
  int rx = 0, ry = 0, rw = 0, rh = 0, btn_pressed = 0;
  int rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
  Cursor cursor, cursor2;
  Window target = None, root = RootWindow(source->display, source->screen);
  GC gc;
  XGCValues gcval;
  char *window_name;
  XWindowAttributes win_info;

  xfd = ConnectionNumber(source->display);
  fdsize = xfd + 1;

  cursor = XCreateFontCursor(source->display, XC_left_ptr);
  cursor2 = XCreateFontCursor(source->display, XC_lr_angle);

  gcval.foreground = XWhitePixel(source->display, 0);
  gcval.function = GXxor;
  gcval.background = XBlackPixel(source->display, 0);
  gcval.plane_mask = gcval.background ^ gcval.foreground;
  gcval.subwindow_mode = IncludeInferiors;

  gc =
    XCreateGC(source->display, root,
              GCFunction | GCForeground | GCBackground | GCSubwindowMode,
              &gcval);

  if ((XGrabPointer
       (source->display, root, False,
        ButtonMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
        GrabModeAsync, root, cursor, CurrentTime) != GrabSuccess))
    perror("couldn't grab pointer:");

  if ((XGrabKeyboard
       (source->display, root, False, GrabModeAsync, GrabModeAsync,
        CurrentTime) != GrabSuccess))
    perror("couldn't grab keyboard:");

  fprintf(stderr, "Select source with mouse:\n");
  fprintf(stderr, "  Left Click: Window (supports resizing)\n");
  fprintf(stderr, "  Left Drag: Rectangle area (free size, does not support resizing)\n");
  fprintf(stderr, "  Right Click: Rectangle area (fit size to video resolution, does not support resizing)\n");
  while (1) {
    /* handle events here */
    while (!done && XPending(source->display)) {
      XNextEvent(source->display, &ev);
      switch (ev.type) {
        case MotionNotify:
          if (btn_pressed) {
            if (rect_w) {
              /* re-draw the last rect to clear it */
              XDrawRectangle(source->display, root, gc, rect_x, rect_y, rect_w, rect_h);
            } else {
              /* Change the cursor to show we're selecting a region */
              XChangeActivePointerGrab(source->display,
                                       ButtonMotionMask | ButtonReleaseMask,
                                       cursor2, CurrentTime);
            }

            rect_x = rx;
            rect_y = ry;
            rect_w = ev.xmotion.x - rect_x;
            rect_h = ev.xmotion.y - rect_y;

            if (rect_w < 0) {
              rect_x += rect_w;
              rect_w = 0 - rect_w;
            }
            if (rect_h < 0) {
              rect_y += rect_h;
              rect_h = 0 - rect_h;
            }
            /* draw rectangle */
            XDrawRectangle(source->display, root, gc, rect_x, rect_y, rect_w, rect_h);
            XFlush(source->display);
          }
          break;
        case ButtonPress:
          btn_pressed = ev.xbutton.button;
          rx = ev.xbutton.x;
          ry = ev.xbutton.y;
          target = ev.xbutton.subwindow;
          if (target == None)
            target = root;
          break;
        case ButtonRelease:
          done = 1;
          break;
        default:
          break;
      }
    }
    if (done)
      break;

    /* now block some */
    FD_ZERO(&fdset);
    FD_SET(xfd, &fdset);
    errno = 0;
    count = select(fdsize, &fdset, NULL, NULL, NULL);
  }

  if (rect_w) {
    XDrawRectangle(source->display, root, gc, rect_x, rect_y, rect_w, rect_h);
    XFlush(source->display);
  }
  XUngrabPointer(source->display, CurrentTime);
  XUngrabKeyboard(source->display, CurrentTime);
  XFreeCursor(source->display, cursor);
  XFreeGC(source->display, gc);
  XSync(source->display, True);

  if (done < 2) {
    //scrot_do_delay();
    if (rect_w > 5) {
      /* if a rect has been drawn, it's an area selection */
      rw = ev.xbutton.x - rx;
      rh = ev.xbutton.y - ry;

      if (rw < 0) {
        rx += rw;
        rw = 0 - rw;
      }
      if (rh < 0) {
        ry += rh;
        rh = 0 - rh;
      }
    }

    /* clip rectangle nicely */
    if (rx < 0) {
      rw += rx;
      rx = 0;
    }
    if (ry < 0) {
      rh += ry;
      ry = 0;
    }
  }

  if (rw > 5) {
    source->type = SOURCE_RECT;
    source->x = rx;
    source->y = ry;
    source->width = rw;
    source->height = rh;
    fprintf(stderr, "target: Point(%d, %d)\nsize: %dx%d\n", rx, ry, rect_w, rect_h);
    return root;
  } else if (btn_pressed == 3) {
    source->type = SOURCE_RECT;
    source->x = rx;
    source->y = ry;
    fprintf(stderr, "target: Point(%d, %d)\nsize: %dx%d\n", rx, ry, source->width, source->height);
    return root;
  } else {
    source->type = SOURCE_WINDOW;
    XFetchName(source->display, target, &window_name);
    //if (window_name == NULL) {
    //  XGetWindowAttributes(source->display, root, &win_info);
    //  fprintf(stderr, "target: Desktop\nsize: %dx%d\n", win_info.width, win_info.height);
    //  return root;
    //} else {
    XGetWindowAttributes(source->display, target, &win_info);
    //fprintf(stderr, "target: Window(\"%s\")\n(initial)size: %dx%d\n", window_name, win_info.width, win_info.height);
    fprintf(stderr, "target: Window\nsize: %dx%d\n", win_info.width, win_info.height);
    return target;
    //}
  }
}

void release_resources_and_exit(int no) 
{
  free(source);
  free(stream);
  close(fd);
  exit(no);
}

void register_signal_handler()
{
  sigset_t block, oblock;
  struct sigaction sa;
  
  sigemptyset(&block);
  sigemptyset(&oblock);
  sigaddset(&block, SIGINT);
  sigaddset(&block, SIGTERM);

  sa.sa_handler = release_resources_and_exit;
  sa.sa_flags = SA_RESTART;
  sa.sa_mask = block;
  sigaction(SIGINT, &sa, 0);
  sigaction(SIGTERM, &sa, 0);
}

char * get_device_fname() {
  struct dirent *dp;
  char * path = "/dev";
  char * fname = (char *)malloc(sizeof(char)*500);
  DIR *dir;
  if ((dir=opendir(path))==NULL) {
    perror("opendir");
    exit(-1);
  }
  for (dp=readdir(dir);dp!=NULL;dp=readdir(dir)) {
    if (strstr(dp->d_name, "video") && dp->d_name[5] > fname[10]) {
      sprintf(fname, "%s/%s", path, dp->d_name);
    }
  }
  return fname;
}

int main (int argc, char **argv)
{
  char * device_fname = get_device_fname();
  
  int width, height;
  int frame_rate;
  fprintf(stderr, "Using device: %s\n", device_fname);
  register_signal_handler();

  source = malloc(sizeof(screencast_source));
  source->display = XOpenDisplay(NULL);
  source->screen = XDefaultScreen(source->display);

  if (argc > 2) {
    source->width = width = atoi(argv[1]);
    source->height = height = atoi(argv[2]);
  } else {
    source->width = width = DEFAULT_WIDTH;
    source->height = height = DEFAULT_HEIGHT;
  }
  frame_rate = (argc == 4) ? atoi(argv[3]) : DEFAULT_FRAME_RATE;
  fprintf(stderr, "Video resolution: %dx%d\nFrame rate: %d\n", width, height, frame_rate);

  if ((fd=open(device_fname, O_RDWR)) == -1) {
    perror("opening video device");
    release_resources_and_exit(-1);
  }

  source->window = Select_Window(source);

  stream = malloc(sizeof(video_input_stream));
  if ((init_video_device(fd, stream, width, height)) < 0) {
    perror ("init_video_device()");
    release_resources_and_exit(-1);
  }

  fputs("CTRL-C to exit\n", stderr);
  if (start_screencast(stream, source, (int)((double)1/frame_rate*1000*1000)) < -1) { 
    perror ("start_screencast()");
    release_resources_and_exit(-1);
  }

  release_resources_and_exit(0);
	return 0;
}

