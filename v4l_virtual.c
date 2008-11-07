#include <linux/version.h>  /* >= 2.6.14 LINUX_VERSION_CODE */ 
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>

#define V4L_VIRTUAL_VERSION "0.1"
#define VIDIOCSINVALID  _IO('v',BASE_VIDIOCPRIVATE+1)
#define info(format, arg...) printk(KERN_INFO __FILE__ ": " format "\n" "", ## arg)

#define MAXWIDTH 640
#define MAXHEIGHT 480

struct v4l_virtual_t {
  struct video_device *device;
  unsigned char *buffer;
  unsigned long buflength;
  unsigned int width, height;
  unsigned int palette;
  unsigned long frameswrite;
  unsigned long framesread;
  unsigned long framesdumped;
  unsigned int wopen;
  unsigned int ropen;
  struct semaphore lock;
  wait_queue_head_t wait;
  unsigned int frame;
  unsigned int pid;
  unsigned int zerocopy;
  unsigned long int ioctlnr;
  unsigned int invalid_ioctl; /* 0 .. none invalid; 1 .. invalid */
  unsigned int ioctllength;
  char *ioctldata;
  char *ioctlretdata;
};

static struct v4l_virtual_t * v4l_virtual;

static int v4l_virtual_open(struct inode *inod, struct file *f)
{
  //if (v4l_virtual->ropen || v4l_virtual->wopen)
  //  return -EBUSY;
  v4l_virtual->framesdumped = 0;
  v4l_virtual->frameswrite = 0;
  v4l_virtual->ropen = 1;
  v4l_virtual->zerocopy = 0;
  v4l_virtual->ioctlnr = -1;
  v4l_virtual->pid = current->pid;

  return 0;
}

static int v4l_virtual_release(struct inode *inod, struct file *f)
{
  v4l_virtual->ropen = 0;
  if (v4l_virtual->zerocopy && v4l_virtual->buffer) {
    v4l_virtual->ioctlnr = 0;
    v4l_virtual->ioctllength = 0;
    kill_proc(v4l_virtual->pid, SIGIO, 1);
  }

  return 0;
}

static ssize_t v4l_virtual_write(struct file *f, const char *buf, size_t count, 
    loff_t *offset)
{
  copy_from_user(v4l_virtual->buffer, buf, count);
  return count;
}

static ssize_t v4l_virtual_read(struct file * f, char * buf, size_t count, 
    loff_t *offset)
{
  copy_to_user(buf, v4l_virtual->buffer, count);
  return count;
}

static int v4l_virtual_mmap(struct file *f, struct vm_area_struct *vma)
{
  unsigned long start = (unsigned long)vma->vm_start;
  long size = vma->vm_end - vma->vm_start;
  unsigned long page, pos;

  down(&v4l_virtual->lock);
  if (v4l_virtual->buffer == NULL) {
    up(&v4l_virtual->lock);
    return -EINVAL;
  }

  pos = (unsigned long)v4l_virtual->buffer;
  while (size > 0) {
    page = vmalloc_to_pfn((void *)pos);
    if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
      up(&v4l_virtual->lock);
      return -EAGAIN;
    }
    start += PAGE_SIZE;
    pos += PAGE_SIZE;
    size -= PAGE_SIZE;
  }
  up(&v4l_virtual->lock);

  return 0;
}

static int v4l_virtual_ioctl(struct inode *inod, struct file *f, unsigned int cmd, 
    unsigned long arg)
{
  switch(cmd)
  {
    /* Get capabilities */
    case VIDIOCGCAP:
    {
      struct video_capability *vidcap=(struct video_capability *)arg;
      sprintf(vidcap->name, "Jeroen's dummy v4l driver");
      //sprintf(vidcap->name, "v4l virtual driver");
      vidcap->type = VID_TYPE_CAPTURE;
      vidcap->channels=1;
      vidcap->audios=0;
      vidcap->maxwidth=MAXWIDTH;
      vidcap->maxheight=MAXHEIGHT;
      vidcap->minwidth=128;
      vidcap->minheight=96;
      return 0;
    }
    /* Get channel info (sources) */
    case VIDIOCGCHAN:
    {
      struct video_channel *vidchan=(struct video_channel *)arg;
      if(vidchan->channel!=0)
        ;
      vidchan->channel=0;
      vidchan->flags=0;
      vidchan->tuners=0;
      vidchan->norm=0;
      vidchan->type = VIDEO_TYPE_CAMERA;
      strcpy(vidchan->name, "Virtual device");

      return 0;
    }
    /* Set channel   */
    case VIDIOCSCHAN:
    {
      int *v=(int *)arg;
      
      if (v[0]!=0)
        return 1;
      return 0;
    }
    /* Get tuner abilities */
    case VIDIOCGTUNER:
    {
      struct video_tuner *v = (struct video_tuner *)arg;

      if(v->tuner) {
        //printf("VIDIOCGTUNER: Invalid Tuner, was %d\n", v->tuner);
        //return -EINVAL;
      }
      v->tuner=0;
      strcpy(v->name, "Format");
      v->rangelow=0;
      v->rangehigh=0;
      v->flags=0;
      v->mode=VIDEO_MODE_AUTO;
      return 1;
    }
    /* Get picture properties */
    case VIDIOCGPICT:
    {
      struct video_picture *vidpic=(struct video_picture *)arg;

      vidpic->colour=0x8000;
      vidpic->hue=0x8000;
      vidpic->brightness=0x8000;
      vidpic->contrast=0x8000;
      vidpic->whiteness=0x8000;
      vidpic->depth=0x8000;
      vidpic->palette=v4l_virtual->palette;
      return 0;

    }
    /* Set picture properties */
    case VIDIOCSPICT:
    {
      struct video_picture *vidpic=(struct video_picture *)arg;
      v4l_virtual->palette=vidpic->palette;
      return 0;
    }
    /* Get the video overlay window */
    case VIDIOCGWIN:
    {

      struct video_window *vidwin=(struct video_window *)arg;
      vidwin->x=0;
      vidwin->y=0;
      vidwin->width=v4l_virtual->width;
      vidwin->height=v4l_virtual->height;
      vidwin->chromakey=0;
      vidwin->flags=0;
      vidwin->clipcount=0;
      return 0;
    }
    /* Set the video overlay window - passes clip list for hardware smarts , chromakey etc */
    case VIDIOCSWIN:
    {
      struct video_window *vidwin=(struct video_window *)arg;
      
      if (vidwin->width > MAXWIDTH ||
          vidwin->height > MAXHEIGHT )
        return 1;
      if (vidwin->flags)
        return 1;
      v4l_virtual->width=vidwin->width;
      v4l_virtual->height=vidwin->height;
      v4l_virtual->buflength=vidwin->width*vidwin->height*3;
      return 0;
    }
    /* Memory map buffer info */
    case VIDIOCGMBUF:
    {
      struct video_mbuf *vidmbuf=(struct video_mbuf *)arg;
      int i;

      vidmbuf->size=v4l_virtual->buflength;
      vidmbuf->frames=1;
      for (i=0; i<vidmbuf->frames; i++)
        vidmbuf->offsets[i]=i*vidmbuf->size;
      return 0;
    }
    /* Grab frames */
    case VIDIOCMCAPTURE:
    {
      struct video_mmap *vidmmap=(struct video_mmap *)arg;

      //return 0;
      if (vidmmap->height>MAXHEIGHT ||
          vidmmap->width>MAXWIDTH ||
          vidmmap->format!=v4l_virtual->palette )
        return 1;
      if (vidmmap->height!=v4l_virtual->height ||
          vidmmap->width!=v4l_virtual->width) {
        v4l_virtual->height=vidmmap->height;
        v4l_virtual->width=vidmmap->width;
        v4l_virtual->buflength=vidmmap->width*vidmmap->height*3;
      }

      return 0;
    }
    /* Sync with mmap grabbing */
    case VIDIOCSYNC:
    {
      return 0;
    }
    default:
      return -ENOTTY;
  }
  return 0;
}

static unsigned int v4l_virtual_poll(struct file *f, struct poll_table_struct *wait)
{
  return 0;
}

static struct file_operations fileops_template=
{
  owner:    THIS_MODULE,
  open:    v4l_virtual_open,
  release:  v4l_virtual_release,
  read:    v4l_virtual_read,
  write:    v4l_virtual_write,
  poll:    v4l_virtual_poll,
  ioctl:    v4l_virtual_ioctl,
  mmap:    v4l_virtual_mmap,
};

static struct video_device v4l_virtual_template=
{
  owner:    THIS_MODULE,
  name:    "Virtual Video",
  type:    VID_TYPE_CAPTURE,
  fops:    &fileops_template,
  release:  video_device_release,
};

static int create_v4l_virtual(void) 
{
  int ret;

  v4l_virtual = kmalloc(sizeof(struct v4l_virtual_t), GFP_KERNEL);
  if (!v4l_virtual)
    return -ENOMEM;

  v4l_virtual->device = video_device_alloc();
  if (v4l_virtual->device == NULL) 
    return -ENOMEM;

  *v4l_virtual->device = v4l_virtual_template;
  
  v4l_virtual->width = MAXWIDTH;
  v4l_virtual->height = MAXHEIGHT;
  
  //v4l_virtual->palette = VIDEO_PALETTE_YUV420P;
  v4l_virtual->buflength = v4l_virtual->width*v4l_virtual->height*3;
  v4l_virtual->buffer = vmalloc(v4l_virtual->buflength);
  v4l_virtual->frameswrite = 0;
  v4l_virtual->framesread = 0;
  v4l_virtual->framesdumped = 0;
  v4l_virtual->wopen = 0;
  v4l_virtual->ropen = 0;
  v4l_virtual->frame = 0;

  v4l_virtual->device->type = VID_TYPE_CAPTURE;
  sprintf(v4l_virtual->device->name, "v4l virtual device %d", 0);

  init_waitqueue_head(&v4l_virtual->wait);
  init_MUTEX(&v4l_virtual->lock);

  ret = video_register_device(v4l_virtual->device, VFL_TYPE_GRABBER, -1); //autoassign

  if ((ret == -1) || (ret == -23)) {
    info("error registering device %s", v4l_virtual->device->name);
    kfree(v4l_virtual->device);
    kfree(v4l_virtual);
    v4l_virtual = NULL;
    return ret;
  }

  v4l_virtual->ioctldata=kmalloc(1024, GFP_KERNEL);
  v4l_virtual->ioctlretdata=kmalloc(1024, GFP_KERNEL);
  return 0;
}

MODULE_AUTHOR("Yusuke Yanbe (y.yanbe@gmail.com)");
MODULE_DESCRIPTION("Video4Linux virtual device");
MODULE_LICENSE("GPL");
MODULE_VERSION(V4L_VIRTUAL_VERSION);

static int __init v4l_virtual_init(void)
{
  int ret;
  info("Registering Video4Linux virtual devices");
  ret = create_v4l_virtual();
  if (ret == 0) {
    info("Virtual video device video%d registered", v4l_virtual->device->minor);
  } else {
    return ret;
  }
  return 0;
}

static void __exit cleanup_v4l_virtual_module(void)
{
  info("Unregistering Video4Linux virtual device");
  video_unregister_device(v4l_virtual->device);
  if (v4l_virtual->buffer) vfree(v4l_virtual->buffer);
  kfree(v4l_virtual->ioctldata);
  kfree(v4l_virtual->ioctlretdata);
  kfree(v4l_virtual);
}

module_init(v4l_virtual_init);
module_exit(cleanup_v4l_virtual_module);
