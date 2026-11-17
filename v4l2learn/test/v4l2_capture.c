// SPDX-License-Identifier: GPL-2.0
/*
 * v4l2_capture.c —— 用户态 V4L2 抓帧最小示例（mmap + STREAMING）
 *
 * 这个程序故意不用 OpenCV，把 V4L2 的 ioctl 顺序完整写出来，
 * 方便对照驱动代码看"用户的一次 DQBUF 背后发生了什么"。
 *
 * 用法：
 *   ./v4l2_capture [设备] [帧数] [输出文件]
 *   ./v4l2_capture /dev/video2 30 frames.yuv
 *
 * 输出是原始 YUYV（每像素 2 字节）。转成 PNG 看效果：
 *   ffmpeg -f rawvideo -pix_fmt yuyv422 -s 640x480 -i frames.yuv -frames:v 1 out.png
 *
 * 注意：这个程序对真实的 UVC 摄像头也同样有效，只要把设备换成 /dev/video1。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

struct buffer {
	void *start;
	size_t length;
};

/* ioctl 可能被信号打断，统一重试一次 */
static int xioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret == -1 && errno == EINTR);

	return ret;
}

/* 不同内核可能把能力放在 capabilities 或 device_caps 里 */
static unsigned int device_caps(int fd)
{
	struct v4l2_capability cap;

	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		perror("VIDIOC_QUERYCAP");
		return 0;
	}

	printf("driver : %s\n", cap.driver);
	printf("card   : %s\n", cap.card);
	printf("bus    : %s\n", cap.bus_info);

	if (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
		return cap.device_caps;
	return cap.capabilities;
}

static void enumerate_formats(int fd)
{
	struct v4l2_fmtdesc fmt;

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	printf("支持的像素格式：\n");
	for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; fmt.index++) {
		printf("  [%u] %s (fourcc=0x%08x)\n",
		       fmt.index, fmt.description, fmt.pixelformat);
	}
}

int main(int argc, char **argv)
{
	const char *dev_name = (argc > 1) ? argv[1] : "/dev/video2";
	unsigned int want_frames = (argc > 2) ? (unsigned int)atoi(argv[2]) : 30;
	const char *out_name = (argc > 3) ? argv[3] : "frames.yuv";

	int fd;
	unsigned int caps;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	struct buffer *buffers;
	unsigned int i;
	unsigned int frames = 0;
	FILE *out;
	enum v4l2_buf_type type;

	fd = open(dev_name, O_RDWR);
	if (fd < 0) {
		perror(dev_name);
		return 1;
	}

	/* ① 我是谁？支持采集和 STREAMING 吗？ */
	caps = device_caps(fd);
	if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s 不是视频采集设备\n", dev_name);
		close(fd);
		return 1;
	}
	if (!(caps & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s 不支持 STREAMING\n", dev_name);
		close(fd);
		return 1;
	}

	enumerate_formats(fd);

	/* ② 协商格式：640x480 YUYV */
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 640;
	fmt.fmt.pix.height = 480;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		perror("VIDIOC_S_FMT");
		close(fd);
		return 1;
	}

	printf("实际格式：%ux%u, bytesperline=%u, sizeimage=%u\n",
	       fmt.fmt.pix.width, fmt.fmt.pix.height,
	       fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage);

	/* ③ 申请 4 个 mmap 缓冲区 */
	memset(&req, 0, sizeof(req));
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("VIDIOC_REQBUFS");
		close(fd);
		return 1;
	}
	if (req.count < 2) {
		fprintf(stderr, "缓冲区太少：%u\n", req.count);
		close(fd);
		return 1;
	}

	buffers = calloc(req.count, sizeof(*buffers));
	if (!buffers) {
		perror("calloc");
		close(fd);
		return 1;
	}

	/* ④ 查询每个缓冲区并 mmap 到用户空间 */
	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			perror("VIDIOC_QUERYBUF");
			goto out;
		}

		buffers[i].length = buf.length;
		buffers[i].start = mmap(NULL, buf.length,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					fd, buf.m.offset);
		if (buffers[i].start == MAP_FAILED) {
			perror("mmap");
			goto out;
		}
		printf("buffer[%u]: %zu 字节\n", i, buffers[i].length);
	}

	/* ⑤ 把 4 个空缓冲区全部还给驱动（QBUF） */
	for (i = 0; i < req.count; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			perror("VIDIOC_QBUF");
			goto out;
		}
	}

	/* ⑥ 开始出图 */
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("VIDIOC_STREAMON");
		goto out;
	}

	out = fopen(out_name, "wb");
	if (!out) {
		perror(out_name);
		goto out_streamoff;
	}

	/* ⑦ select + DQBUF 循环 */
	while (frames < want_frames) {
		fd_set fds;
		struct timeval tv;
		struct v4l2_buffer buf;
		int ret;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		ret = select(fd + 1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}
		if (ret == 0) {
			fprintf(stderr, "等帧超时\n");
			break;
		}

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		/* DQBUF 取走一帧（驱动已经填好的那块内存） */
		if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EAGAIN)
				continue;
			perror("VIDIOC_DQBUF");
			break;
		}

		printf("frame %u: index=%u, sequence=%u, bytesused=%u, "
		       "timestamp=%llu\n",
		       frames, buf.index, buf.sequence, buf.bytesused,
		       (unsigned long long)buf.timestamp.tv_sec * 1000000000ULL +
		       (unsigned long long)buf.timestamp.tv_usec * 1000ULL);

		fwrite(buffers[buf.index].start, 1, buf.bytesused, out);
		frames++;

		/* 用完立刻还回去，驱动才能继续往里画下一帧 */
		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			perror("VIDIOC_QBUF(return)");
			break;
		}
	}

	fclose(out);

out_streamoff:
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
		perror("VIDIOC_STREAMOFF");

out:
	for (i = 0; i < req.count; i++) {
		if (buffers[i].start && buffers[i].start != MAP_FAILED)
			munmap(buffers[i].start, buffers[i].length);
	}
	free(buffers);
	close(fd);

	printf("共抓取 %u 帧，写入 %s\n", frames, out_name);
	return (frames == want_frames) ? 0 : 1;
}
