// SPDX-License-Identifier: GPL-2.0
/*
 * myv4l2cam.c —— 一个用来学习 V4L2 摄像头驱动框架的"虚拟摄像头"驱动
 *
 * ============================================================================
 * 先说明白：这个驱动不是用来驱动你那个 USB 摄像头的
 * ============================================================================
 *   你手里的 USB 摄像头遵循 USB Video Class（UVC）协议，内核里已经有
 *   uvcvideo.ko 在处理 USB 枚举、URB、视频流。你 insmod uvcvideo 就是在
 *   "使用一个现成的 V4L2 驱动"，不需要也不能再写一个同名驱动去抢它。
 *
 *   真正需要自己写 V4L2 驱动的场景是：
 *     - CSI / 并口摄像头 sensor（比如 OV5640），要自己写 I2C 寄存器初始化、
 *       时钟/电源管理，再对接 SoC 的 CSI 控制器；
 *     - 特殊采集卡、工业相机；
 *     - 像本文件这样的虚拟/测试设备。
 *
 *   本驱动在软件里"假装"成一个摄像头：注册 /dev/videoN，实现 V4L2 的
 *   全套 ioctl，用 videobuf2 管理 mmap 缓冲区，再用内核定时器每 1/30 秒
 *   往缓冲区里画一幅 YUYV 彩条。它把真实摄像头驱动也要走的 V4L2 骨架
 *   完整走一遍，但不需要任何硬件，最适合先学"驱动怎么写"。
 *
 *   用户态（v4l2-ctl / OpenCV / Qt）看到的和真摄像头没有区别：
 *      open("/dev/videoN") → ioctl 协商格式 → mmap → QBUF → STREAMON
 *      → DQBUF 拿到一帧 → 再 QBUF → ...
 *
 * ============================================================================
 * V4L2 驱动的两层结构（记住这两层，驱动就写出来一半了）
 * ============================================================================
 *   第一层：video_device —— "我是个视频设备"
 *     - 生成 /dev/videoN 字符设备
 *     - file_operations：open/release/mmap/poll/ioctl
 *     - v4l2_ioctl_ops：每个 VIDIOC_xxx 对应一个回调
 *       例：VIDIOC_QUERYCAP → vidioc_querycap
 *           VIDIOC_S_FMT    → vidioc_s_fmt_vid_cap
 *
 *   第二层：videobuf2（简称 vb2）—— "缓冲区我不管，框架帮我管"
 *     - 用户 ioctl(VIDIOC_REQBUFS) 时，vb2 负责分配缓冲区
 *     - 用户 mmap 时，vb2 负责把内核缓冲区映射到用户空间
 *     - 驱动只需要实现 5 个回调（struct vb2_ops）：
 *         queue_setup    ：每帧要多大
 *         buf_prepare    ：缓冲区入队前检查
 *         buf_queue      ：用户 QBUF 了，驱动把 buffer 挂到自己队列
 *         start_streaming：STREAMON，启动采集（本驱动启动定时器）
 *         stop_streaming ：STREAMOFF，停止采集并把没播完的 buffer 还回去
 *     - 驱动填好一帧后调用 vb2_buffer_done()，vb2 就会让用户的 DQBUF 返回。
 *
 * ============================================================================
 * 一次完整的取流时序（用户态视角）
 * ============================================================================
 *   open("/dev/video2")
 *   ioctl(VIDIOC_QUERYCAP)         → 我是谁、支持什么能力
 *   ioctl(VIDIOC_S_FMT)            → 商量格式，比如 640x480 YUYV
 *   ioctl(VIDIOC_REQBUFS, 4)       → 要 4 个缓冲区
 *   for (i = 0; i < 4; i++)
 *       ioctl(VIDIOC_QUERYBUF, i)  → 拿到偏移/长度
 *       mmap(...)                  → 映射到我自己的地址空间
 *       ioctl(VIDIOC_QBUF, i)      → 把空缓冲区还给驱动
 *   ioctl(VIDIOC_STREAMON)         → 开始出图
 *   loop:
 *       select/poll                → 等一帧就绪
 *       ioctl(VIDIOC_DQBUF)        → 取走一帧（驱动已填好）
 *       处理 YUYV 数据
 *       ioctl(VIDIOC_QBUF)         → 把空缓冲区再还回去
 *   ioctl(VIDIOC_STREAMOFF)
 *   munmap / close
 *
 * 目标内核：Linux 4.9.88（100ask IMX6ULL Buildroot SDK）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#define MYCAM_NAME          "myv4l2cam"
#define MYCAM_CARD          "myv4l2 virtual camera"
#define MYCAM_DEFAULT_WIDTH 640
#define MYCAM_DEFAULT_HEIGHT 480
#define MYCAM_DEFAULT_FPS   30
#define MYCAM_MIN_WIDTH     16
#define MYCAM_MAX_WIDTH     1920
#define MYCAM_MIN_HEIGHT    16
#define MYCAM_MAX_HEIGHT    1080

/*
 * 每个 vb2 缓冲区都"藏"着这个结构体。
 * 用户看到的只是一个 struct v4l2_buffer，但 vb2 在内部为每个 buffer
 * 分配 q->buf_struct_size 字节，我们把它定义成 mycam_buffer：
 *
 *   mycam_buffer
 *   ├── vb          : vb2 需要的公共头部（必须放第一个成员）
 *   │   └── vb2_buf : 里面才是真正的缓冲区描述
 *   └── list        : 驱动自己的链表节点，用来挂"已 QBUF 待填充"的缓冲区
 *
 * vb2 回调拿到的是 struct vb2_buffer *，用 container_of() 反推出
 * mycam_buffer，就能同时访问公共头部和我们的 list 了。
 */
struct mycam_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

/*
 * 整个驱动只有这一个设备实例（模块可加载一次）。
 * 真实驱动通常把它挂到 platform_device / usb_interface / i2c_client 上。
 */
struct mycam_dev {
	struct v4l2_device v4l2_dev;     /* V4L2 设备的"户口本" */
	struct video_device *vdev;       /* /dev/videoN 字符设备 */
	struct vb2_queue queue;          /* 缓冲区队列（vb2） */

	struct mutex lock;               /* 串行化 ioctl；vb2 的 q->lock 也指向它 */
	spinlock_t qlock;                /* 保护下面这个 queued 链表（定时器也会碰） */
	struct list_head queued;         /* 用户 QBUF 进来、驱动还没填的 buffer */

	struct timer_list timer;         /* 软件"帧率发生器"，模拟摄像头 VSYNC */
	struct v4l2_pix_format fmt;      /* 当前格式：宽、高、像素格式、大小 */
	unsigned int fps;                /* 当前帧率 */
	unsigned int frame_count;        /* 已产生多少帧，也用来让彩条动起来 */
	bool streaming;                  /* 是否正在 STREAMON */
};

/*
 * 8 条标准彩条（RGB），用来生成 YUYV 测试画面。
 * 不需要理解颜色，只要知道每一帧会从这里挑颜色写进缓冲区即可。
 */
static const u8 mycam_bars[8][3] = {
	{255, 255, 255},   /* 白 */
	{255, 255,   0},   /* 黄 */
	{  0, 255, 255},   /* 青 */
	{  0, 255,   0},   /* 绿 */
	{255,   0, 255},   /* 品红 */
	{255,   0,   0},   /* 红 */
	{  0,   0, 255},   /* 蓝 */
	{  0,   0,   0},   /* 黑 */
};

/*
 * 模块参数：insmod 时可以指定设备号
 *   insmod myv4l2cam.ko video_nr=5   → /dev/video5
 *   不指定（-1）则让内核自动找第一个空号
 */
static int video_nr = -1;
module_param(video_nr, int, 0444);
MODULE_PARM_DESC(video_nr, "video device number (-1 = auto)");

/* 全局设备指针，模块卸载时用 */
static struct mycam_dev *mycam_devp;

/* ------------------------------------------------------------------------- */
/* 小工具                                                                    */
/* ------------------------------------------------------------------------- */

static inline u8 mycam_clamp_u8(int v)
{
	if (v < 0)
		return 0;
	if (v > 255)
		return 255;
	return (u8)v;
}

/*
 * RGB → YUV（BT.601 整数近似）。
 * YUYV 是 4:2:2 打包格式：每两个水平像素共用一组 U/V，内存排列为
 *   Y0 U Y1 V
 * 每个像素占 2 字节，所以 640x480 的一帧是 640*480*2 = 614400 字节。
 */
static void mycam_rgb_to_yuv(u8 r, u8 g, u8 b, u8 *y, u8 *u, u8 *v)
{
	*y = mycam_clamp_u8(((66 * r + 129 * g + 25 * b) >> 8) + 16);
	*u = mycam_clamp_u8(((-38 * r - 74 * g + 112 * b) >> 8) + 128);
	*v = mycam_clamp_u8(((112 * r - 94 * g - 18 * b) >> 8) + 128);
}

/* 把格式字段一次性填好；s_fmt/try_fmt 和初始化都会用到 */
static void mycam_fill_format(struct v4l2_pix_format *pix, u32 w, u32 h)
{
	pix->width = w;
	pix->height = h;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = w * 2;              /* YUYV 每像素 2 字节 */
	pix->sizeimage = pix->bytesperline * h;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
}

/* 用户给的宽高可能很离谱，这里夹到合理范围，并强制成偶数宽高 */
static void mycam_clamp_format(struct v4l2_pix_format *pix)
{
	if (pix->width < MYCAM_MIN_WIDTH)
		pix->width = MYCAM_MIN_WIDTH;
	if (pix->width > MYCAM_MAX_WIDTH)
		pix->width = MYCAM_MAX_WIDTH;
	if (pix->height < MYCAM_MIN_HEIGHT)
		pix->height = MYCAM_MIN_HEIGHT;
	if (pix->height > MYCAM_MAX_HEIGHT)
		pix->height = MYCAM_MAX_HEIGHT;

	pix->width &= ~1U;      /* YUYV 两个像素一组，宽度必须是偶数 */
	pix->height &= ~1U;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
}

/*
 * 真正"画"一帧到 vb2 缓冲区里。
 *
 * vb2_plane_vaddr() 返回这块缓冲区在内核里的虚拟地址。
 * 我们的 mem_ops 用的是 vb2_vmalloc_memops，所以这块内存是 vmalloc 出来的，
 * 在定时器（软中断）上下文里也能安全访问。
 *
 * 画面内容：
 *   - 8 条彩条，随帧号横向移动 → 能直观看出画面是活的；
 *   - 每隔 60 行一条白线，也随帧号移动 → 第二个"活"的证据。
 */
static int mycam_fill_frame(struct mycam_dev *dev, struct mycam_buffer *buf)
{
	u8 *base = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	unsigned int w = dev->fmt.width;
	unsigned int h = dev->fmt.height;
	unsigned int stride = dev->fmt.bytesperline;
	unsigned int bar_width = w / 8 + 1;
	unsigned int x, y;

	if (!base)
		return -EFAULT;

	for (y = 0; y < h; y++) {
		u8 *line = base + y * stride;

		for (x = 0; x < w; x += 2) {
			/* 彩条随帧号平移，产生"横向滚动"的效果 */
			unsigned int bar0 = ((x + dev->frame_count * 4) / bar_width) % 8;
			unsigned int bar1 = ((x + 1 + dev->frame_count * 4) / bar_width) % 8;
			u8 y0, u0, v0, y1, u1, v1;

			mycam_rgb_to_yuv(mycam_bars[bar0][0], mycam_bars[bar0][1],
					 mycam_bars[bar0][2], &y0, &u0, &v0);
			mycam_rgb_to_yuv(mycam_bars[bar1][0], mycam_bars[bar1][1],
					 mycam_bars[bar1][2], &y1, &u1, &v1);

			/* YUYV: Y0 U Y1 V（U/V 取两个像素的平均） */
			line[x * 2 + 0] = y0;
			line[x * 2 + 1] = (u8)((u0 + u1) / 2);
			line[x * 2 + 2] = y1;
			line[x * 2 + 3] = (u8)((v0 + v1) / 2);
		}

		/* 一条移动的白线（Y=235, U=V=128 就是标准白） */
		if ((y + dev->frame_count * 2) % 60 == 0) {
			for (x = 0; x < w; x += 2) {
				line[x * 2 + 0] = 235;
				line[x * 2 + 1] = 128;
				line[x * 2 + 2] = 235;
				line[x * 2 + 3] = 128;
			}
		}
	}

	return 0;
}

/* ------------------------------------------------------------------------- */
/* 定时器：模拟摄像头的"每帧中断"                                             */
/* ------------------------------------------------------------------------- */

/*
 * 注意上下文：定时器回调运行在软中断里，不能睡眠！
 *   - 不能调用 mutex_lock / kmalloc(GFP_KERNEL) / copy_to_user
 *   - 可以用 spin_lock_irqsave、vmalloc 内存读写、vb2_buffer_done
 *
 * 每次回调：
 *   1. 从 queued 链表取一个用户已经 QBUF 的缓冲区；
 *   2. 把彩条画进去；
 *   3. 调用 vb2_buffer_done() 告诉 vb2："这一帧好了"；
 *      vb2 会把 buffer 移到 done_list，并唤醒正在 DQBUF 等着的用户进程；
 *   4. 重新装定时器，30fps 后再来一次。
 *
 * 如果没有可用的 buffer（用户还没来得及 QBUF），就空转一次继续等。
 */
static void mycam_timer_cb(unsigned long data)
{
	struct mycam_dev *dev = (struct mycam_dev *)data;
	struct mycam_buffer *buf = NULL;
	unsigned long flags;
	bool streaming;

	spin_lock_irqsave(&dev->qlock, flags);
	streaming = dev->streaming;
	if (streaming && !list_empty(&dev->queued)) {
		buf = list_first_entry(&dev->queued, struct mycam_buffer, list);
		list_del_init(&buf->list);   /* 用 _init 版本，节点之后还能安全再删 */
	}
	spin_unlock_irqrestore(&dev->qlock, flags);

	if (buf) {
		if (mycam_fill_frame(dev, buf) < 0) {
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		} else {
			/* 告诉 vb2 这一帧有多少有效字节 */
			vb2_set_plane_payload(&buf->vb.vb2_buf, 0,
					      dev->fmt.sizeimage);
			/* 时间戳：用户态 DQBUF 后能在 v4l2_buffer.timestamp 看到 */
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			buf->vb.sequence = dev->frame_count++;
			buf->vb.field = V4L2_FIELD_NONE;
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		}
	}

	if (streaming)
		mod_timer(&dev->timer,
			  jiffies + msecs_to_jiffies(1000 / dev->fps));
}

/* ------------------------------------------------------------------------- */
/* 第二层：videobuf2 回调（5 个）                                             */
/* ------------------------------------------------------------------------- */

/*
 * 用户 REQBUFS 时，vb2 问驱动："每个缓冲区要多大、要几个 plane？"
 * 我们只有单平面（single-planar）图像，所以 num_planes = 1。
 */
static int mycam_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			     unsigned int *num_planes, unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct mycam_dev *dev = vb2_get_drv_priv(q);

	(void)num_buffers;
	(void)alloc_devs;

	if (*num_planes) {
		/* 用户已经指定了 plane 大小，只要不小于我们的需求就同意 */
		if (sizes[0] < dev->fmt.sizeimage)
			return -EINVAL;
		return 0;
	}

	*num_planes = 1;
	sizes[0] = dev->fmt.sizeimage;
	return 0;
}

/*
 * vb2 每分配一个 buffer 都会调一次 buf_init，相当于"构造函数"。
 * 我们在这里把链表节点初始化成自环，这样后面无论在哪条路径上调用
 * list_del_init() 都是安全的。
 */
static int mycam_buf_init(struct vb2_buffer *vb)
{
	struct mycam_buffer *buf =
		container_of(vb, struct mycam_buffer, vb.vb2_buf);

	INIT_LIST_HEAD(&buf->list);
	return 0;
}

/*
 * buf_cleanup 是 buf_init 的反操作，vb2 释放 buffer 之前调用。
 *
 * 为什么要它？考虑这条路径：用户 REQBUFS + QBUF 之后没 STREAMON
 * 就 close() 了。此时 q->start_streaming_called 是 false，
 * vb2 不会调用 stop_streaming，但 buffer 内存照样要被释放。
 * 如果某个 buffer 还挂在 dev->queued 上，链表里就留下了悬空指针，
 * 下次 open/STREAMON 时就会 use-after-free。
 *
 * 在这里把它从链表摘掉（list_del_init 对已摘除的节点也安全），
 * 无论 vb2 从哪条路径释放 buffer，我们的链表都不会脏。
 */
static void mycam_buf_cleanup(struct vb2_buffer *vb)
{
	struct mycam_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct mycam_buffer *buf =
		container_of(vb, struct mycam_buffer, vb.vb2_buf);
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	list_del_init(&buf->list);
	spin_unlock_irqrestore(&dev->qlock, flags);
}

/* 用户 QBUF 时，vb2 先调这里做一次检查 */
static int mycam_buf_prepare(struct vb2_buffer *vb)
{
	struct mycam_dev *dev = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < dev->fmt.sizeimage)
		return -EINVAL;
	return 0;
}

/* 用户 QBUF 成功后，vb2 调这里。驱动把 buffer 挂到自己的待填充链表。 */
static void mycam_buf_queue(struct vb2_buffer *vb)
{
	struct mycam_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct mycam_buffer *buf =
		container_of(vb, struct mycam_buffer, vb.vb2_buf);
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	list_add_tail(&buf->list, &dev->queued);
	spin_unlock_irqrestore(&dev->qlock, flags);
}

/* 用户 STREAMON：启动"帧率发生器" */
static int mycam_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mycam_dev *dev = vb2_get_drv_priv(q);
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	dev->streaming = true;
	dev->frame_count = 0;
	spin_unlock_irqrestore(&dev->qlock, flags);

	mod_timer(&dev->timer,
		  jiffies + msecs_to_jiffies(1000 / dev->fps));

	pr_info(MYCAM_NAME ": streaming start (%u buffers)\n", count);
	return 0;
}

/*
 * 用户 STREAMOFF / 关闭设备：停止采集。
 * vb2 要求驱动把所有"用户已经 QBUF 但还没还回去"的 buffer 归还，
 * 否则用户进程会永远卡在 DQBUF。这里用 VB2_BUF_STATE_ERROR 表示
 * "这些帧没采到"。
 */
static void mycam_stop_streaming(struct vb2_queue *q)
{
	struct mycam_dev *dev = vb2_get_drv_priv(q);
	struct mycam_buffer *buf, *tmp;
	LIST_HEAD(pending);
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	dev->streaming = false;
	list_splice_init(&dev->queued, &pending);
	spin_unlock_irqrestore(&dev->qlock, flags);

	/* 等可能正在运行的定时器回调退出，之后再还 buffer 才安全 */
	del_timer_sync(&dev->timer);

	list_for_each_entry_safe(buf, tmp, &pending, list) {
		list_del_init(&buf->list);   /* 留成自环，后面 buf_cleanup 再删也安全 */
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	pr_info(MYCAM_NAME ": streaming stop\n");
}

static const struct vb2_ops mycam_vb2_ops = {
	.queue_setup     = mycam_queue_setup,
	.buf_init        = mycam_buf_init,
	.buf_prepare     = mycam_buf_prepare,
	.buf_queue       = mycam_buf_queue,
	.buf_cleanup     = mycam_buf_cleanup,
	.start_streaming = mycam_start_streaming,
	.stop_streaming  = mycam_stop_streaming,
};

/* ------------------------------------------------------------------------- */
/* 第一层：V4L2 ioctl 回调                                                    */
/*                                                                            */
/* 每个函数对应用户态的一个 ioctl。名字里的 _vid_cap 表示 video capture。      */
/* ------------------------------------------------------------------------- */

static int mycam_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	(void)file;
	(void)priv;

	strlcpy(cap->driver, MYCAM_NAME, sizeof(cap->driver));
	strlcpy(cap->card, MYCAM_CARD, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform:" MYCAM_NAME, sizeof(cap->bus_info));
	cap->version = LINUX_VERSION_CODE;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_STREAMING |
			   V4L2_CAP_READWRITE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* VIDIOC_ENUM_FMT：我支持哪些像素格式？这里只有 YUYV。 */
static int mycam_enum_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	(void)file;
	(void)priv;

	if (f->index != 0)
		return -EINVAL;

	f->flags = 0;
	f->pixelformat = V4L2_PIX_FMT_YUYV;
	strlcpy(f->description, "YUYV 4:2:2", sizeof(f->description));
	return 0;
}

/* VIDIOC_G_FMT：当前格式是什么？ */
static int mycam_g_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct mycam_dev *dev = video_drvdata(file);

	(void)priv;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix = dev->fmt;
	return 0;
}

/* VIDIOC_TRY_FMT：这个格式你能不能接受？（只修改传进来的结构体，不生效） */
static int mycam_try_fmt_vid_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	(void)file;
	(void)priv;

	mycam_clamp_format(&f->fmt.pix);
	return 0;
}

/* VIDIOC_S_FMT：就按这个格式来。缓冲区已分配/正在出图时不允许改。 */
static int mycam_s_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct mycam_dev *dev = video_drvdata(file);

	(void)priv;

	if (vb2_is_busy(&dev->queue))
		return -EBUSY;

	mycam_clamp_format(&f->fmt.pix);
	dev->fmt = f->fmt.pix;
	return 0;
}

/* 输入源：本驱动只有一个"摄像头输入"。 */
static int mycam_enum_input(struct file *file, void *priv,
			    struct v4l2_input *inp)
{
	(void)file;
	(void)priv;

	if (inp->index != 0)
		return -EINVAL;

	memset(inp, 0, sizeof(*inp));
	inp->index = 0;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(inp->name, "myv4l2 camera", sizeof(inp->name));
	return 0;
}

static int mycam_g_input(struct file *file, void *priv, unsigned int *i)
{
	(void)file;
	(void)priv;

	*i = 0;
	return 0;
}

static int mycam_s_input(struct file *file, void *priv, unsigned int i)
{
	(void)file;
	(void)priv;

	return (i == 0) ? 0 : -EINVAL;
}

/* 支持的分辨率列表（v4l2-ctl --list-framesizes 会用到） */
static int mycam_enum_framesizes(struct file *file, void *priv,
				 struct v4l2_frmsizeenum *fsize)
{
	(void)file;
	(void)priv;

	if (fsize->index > 1)
		return -EINVAL;
	if (fsize->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	if (fsize->index == 0) {
		fsize->discrete.width = 320;
		fsize->discrete.height = 240;
	} else {
		fsize->discrete.width = MYCAM_DEFAULT_WIDTH;
		fsize->discrete.height = MYCAM_DEFAULT_HEIGHT;
	}
	return 0;
}

/* 支持的帧率（本驱动固定 30fps 一档，够演示了） */
static int mycam_enum_frameintervals(struct file *file, void *priv,
				     struct v4l2_frmivalenum *fie)
{
	(void)file;
	(void)priv;

	if (fie->index != 0)
		return -EINVAL;
	if (fie->pixel_format != V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	fie->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fie->discrete.numerator = 1;
	fie->discrete.denominator = MYCAM_DEFAULT_FPS;
	return 0;
}

static int mycam_g_parm(struct file *file, void *priv,
			struct v4l2_streamparm *p)
{
	struct mycam_dev *dev = video_drvdata(file);

	(void)priv;

	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	p->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	p->parm.capture.timeperframe.numerator = 1;
	p->parm.capture.timeperframe.denominator = dev->fps;
	p->parm.capture.readbuffers = 2;
	return 0;
}

static int mycam_s_parm(struct file *file, void *priv,
			struct v4l2_streamparm *p)
{
	struct mycam_dev *dev = video_drvdata(file);
	unsigned int num, den, fps;

	(void)priv;

	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	num = p->parm.capture.timeperframe.numerator;
	den = p->parm.capture.timeperframe.denominator;
	if (num && den) {
		fps = den / num;
		if (fps < 1)
			fps = 1;
		if (fps > 60)
			fps = 60;
		dev->fps = fps;
	}

	return mycam_g_parm(file, priv, p);
}

/* VIDIOC_LOG_STATUS：把当前状态打到 dmesg，调试很方便 */
static int mycam_log_status(struct file *file, void *priv)
{
	struct mycam_dev *dev = video_drvdata(file);

	(void)priv;

	v4l2_info(&dev->v4l2_dev,
		  "streaming=%u, %ux%u YUYV, fps=%u, frames=%u\n",
		  dev->streaming, dev->fmt.width, dev->fmt.height,
		  dev->fps, dev->frame_count);
	return 0;
}

/*
 * ioctl 总表。
 * vb2_ioctl_xxx 是 vb2 提供的现成包装函数，直接赋值即可；
 * 它们内部会调用 vb2_reqbufs/vb2_qbuf/... 并处理非阻塞标志。
 */
static const struct v4l2_ioctl_ops mycam_ioctl_ops = {
	.vidioc_querycap              = mycam_querycap,
	.vidioc_enum_fmt_vid_cap      = mycam_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap         = mycam_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap       = mycam_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap         = mycam_s_fmt_vid_cap,
	.vidioc_enum_input            = mycam_enum_input,
	.vidioc_g_input               = mycam_g_input,
	.vidioc_s_input               = mycam_s_input,
	.vidioc_enum_framesizes       = mycam_enum_framesizes,
	.vidioc_enum_frameintervals   = mycam_enum_frameintervals,
	.vidioc_g_parm                = mycam_g_parm,
	.vidioc_s_parm                = mycam_s_parm,
	.vidioc_reqbufs               = vb2_ioctl_reqbufs,
	.vidioc_querybuf              = vb2_ioctl_querybuf,
	.vidioc_qbuf                  = vb2_ioctl_qbuf,
	.vidioc_dqbuf                 = vb2_ioctl_dqbuf,
	.vidioc_create_bufs           = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf           = vb2_ioctl_prepare_buf,
	.vidioc_expbuf                = vb2_ioctl_expbuf,
	.vidioc_streamon              = vb2_ioctl_streamon,
	.vidioc_streamoff             = vb2_ioctl_streamoff,
	.vidioc_log_status            = mycam_log_status,
};

/*
 * file_operations。
 *   v4l2_fh_open        : 分配一个 v4l2_fh（文件句柄），ioctl 核心要用
 *   vb2_fop_release     : 释放 fh，并在最后一个用户关闭时释放缓冲区
 *   vb2_fop_read        : 支持 read() 取流（本驱动也允许，虽然不常用）
 *   vb2_fop_poll        : 支持 select/poll，OpenCV 就是靠它等帧
 *   video_ioctl2        : V4L2 ioctl 总入口，按 cmd 分发到 mycam_ioctl_ops
 *   vb2_fop_mmap        : 支持 mmap，把内核缓冲区映射给用户
 */
static const struct v4l2_file_operations mycam_fops = {
	.owner          = THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll           = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

/* ------------------------------------------------------------------------- */
/* 模块加载 / 卸载                                                            */
/* ------------------------------------------------------------------------- */

static int __init mycam_init(void)
{
	struct mycam_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->lock);
	spin_lock_init(&dev->qlock);
	INIT_LIST_HEAD(&dev->queued);
	dev->fps = MYCAM_DEFAULT_FPS;
	mycam_fill_format(&dev->fmt, MYCAM_DEFAULT_WIDTH, MYCAM_DEFAULT_HEIGHT);
	setup_timer(&dev->timer, mycam_timer_cb, (unsigned long)dev);

	/*
	 * 1. 注册 v4l2_device。
	 *    第一个参数是父 device；虚拟设备没有父设备，传 NULL。
	 *    此时 v4l2_dev.name 必须自己填好，否则内核会 WARN。
	 */
	strlcpy(dev->v4l2_dev.name, MYCAM_NAME, sizeof(dev->v4l2_dev.name));
	ret = v4l2_device_register(NULL, &dev->v4l2_dev);
	if (ret)
		goto err_free;

	/*
	 * 2. 初始化 vb2 队列。
	 *    - io_modes：本驱动支持 mmap 和 read
	 *    - mem_ops ：用 vmalloc 分配普通内存（不需要 DMA）
	 *    - lock    ：和 video_device 共用一把锁，所有 ioctl 串行执行
	 *    - timestamp_flags：时间戳用单调时钟
	 */
	dev->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dev->queue.io_modes = VB2_MMAP | VB2_READ;
	dev->queue.drv_priv = dev;
	dev->queue.buf_struct_size = sizeof(struct mycam_buffer);
	dev->queue.ops = &mycam_vb2_ops;
	dev->queue.mem_ops = &vb2_vmalloc_memops;
	dev->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	dev->queue.lock = &dev->lock;
	ret = vb2_queue_init(&dev->queue);
	if (ret)
		goto err_v4l2;

	/*
	 * 3. 创建并注册 /dev/videoN。
	 *    video_device_alloc() 分配结构体，release 回调负责释放它。
	 */
	dev->vdev = video_device_alloc();
	if (!dev->vdev) {
		ret = -ENOMEM;
		goto err_v4l2;
	}

	dev->vdev->v4l2_dev = &dev->v4l2_dev;
	dev->vdev->queue = &dev->queue;   /* vb2_ioctl_* / vb2_fop_* 都从这里找队列 */
	strlcpy(dev->vdev->name, MYCAM_CARD, sizeof(dev->vdev->name));
	dev->vdev->fops = &mycam_fops;
	dev->vdev->ioctl_ops = &mycam_ioctl_ops;
	dev->vdev->release = video_device_release;
	dev->vdev->lock = &dev->lock;
	dev->vdev->vfl_dir = VFL_DIR_RX;   /* 接收方向：摄像头 → 内存 */
	dev->vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE |
				 V4L2_CAP_STREAMING |
				 V4L2_CAP_READWRITE;
	video_set_drvdata(dev->vdev, dev);

	ret = video_register_device(dev->vdev, VFL_TYPE_GRABBER, video_nr);
	if (ret) {
		video_device_release(dev->vdev);
		goto err_v4l2;
	}

	mycam_devp = dev;
	pr_info(MYCAM_NAME ": registered as /dev/video%d (%ux%u YUYV, %ufps)\n",
		dev->vdev->num, dev->fmt.width, dev->fmt.height, dev->fps);
	return 0;

err_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);
err_free:
	kfree(dev);
	return ret;
}

static void __exit mycam_exit(void)
{
	struct mycam_dev *dev = mycam_devp;

	if (!dev)
		return;

	/* 顺序和初始化相反：先摘设备节点，再释放队列、注销 v4l2_device */
	video_unregister_device(dev->vdev);
	vb2_queue_release(&dev->queue);
	v4l2_device_unregister(&dev->v4l2_dev);
	kfree(dev);
	mycam_devp = NULL;

	pr_info(MYCAM_NAME ": removed\n");
}

module_init(mycam_init);
module_exit(mycam_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("learn V4L2");
MODULE_DESCRIPTION("A virtual V4L2 camera for learning the driver framework");
MODULE_VERSION("1.0");
