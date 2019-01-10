#include<linux/videodev2.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ioctl.h>
#include<errno.h>
#include<string.h>
#include<unistd.h>
#include<sys/mman.h>
#include<fcntl.h>
#include<assert.h>
#include<sys/stat.h>
#include<fstream>
using namespace std;
#include<QImage>
#define MIN_BUFFERS_NEEDED 8
#define NUM_BUFFERS 8
#define WIDTH 640
#define HEIGHT 480

void yuvtorgb(uchar * yuyv_image, uchar * rgb_image) {

int y;
int cr;
int cb;

double r;
double g;
double b;

for (int i = 0, j = 0; i < WIDTH * HEIGHT * 3; i+=6, j+=4) {
    //first pixel
    y = yuyv_image[j];
    cb = yuyv_image[j+1];
    cr = yuyv_image[j+3];

    r = y + (1.4065 * (cr - 128));
    g = y - (0.3455 * (cb - 128)) - (0.7169 * (cr - 128));
    b = y + (1.7790 * (cb - 128));

    //This prevents colour distortions in your rgb image
    if (r < 0) r = 0;
    else if (r > 255) r = 255;
    if (g < 0) g = 0;
    else if (g > 255) g = 255;
    if (b < 0) b = 0;
    else if (b > 255) b = 255;

    rgb_image[i] = (uchar)r;
    rgb_image[i+1] = (uchar)g;
    rgb_image[i+2] = (uchar)b;

    //second pixel
    y = yuyv_image[j+2];
    cb = yuyv_image[j+1];
    cr = yuyv_image[j+3];

    r = y + (1.4065 * (cr - 128));
    g = y - (0.3455 * (cb - 128)) - (0.7169 * (cr - 128));
    b = y + (1.7790 * (cb - 128));

    if (r < 0) r = 0;
    else if (r > 255) r = 255;
    if (g < 0) g = 0;
    else if (g > 255) g = 255;
    if (b < 0) b = 0;
    else if (b > 255) b = 255;

    rgb_image[i+3] = (uchar)r;
    rgb_image[i+4] = (uchar)g;
    rgb_image[i+5] = (uchar)b;
}

}




typedef struct {
    void *start;
    size_t length;
} Buf;

static Buf * buffers;

static int devicefd;

void err_exit(const char * s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

int xioctl(int fh, int request, void * arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

void init_mmap(const char * device) {
       struct stat st;

        if (-1 == stat(device, &st)) {
                fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                         device, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", device);
                exit(EXIT_FAILURE);
        }

        devicefd = open(device, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == devicefd) {
                fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         device, errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
	 struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        unsigned int min;

        if (-1 == xioctl(devicefd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "No V4L2 device\n");
                        exit(EXIT_FAILURE);
                } else {
                        err_exit("VIDIOC_QUERYCAP");
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                fprintf(stderr, "No video capture device\n");
                exit(EXIT_FAILURE);
        }

                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        fprintf(stderr, "Does not support streaming i/o\n");
                        exit(EXIT_FAILURE);
                }


        /* Select video input, video standard and tune here. */


        memset(&cropcap, 0, sizeof(cropcap));

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (0 == xioctl(devicefd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl(devicefd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {
                /* Errors ignored. */
        }

        memset(&fmt, 0, sizeof(fmt));

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (1) {
                fmt.fmt.pix.width       = WIDTH;
                fmt.fmt.pix.height      = HEIGHT;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
                fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

                if (-1 == xioctl(devicefd, VIDIOC_S_FMT, &fmt))
                        err_exit("VIDIOC_S_FMT");

                /* Note VIDIOC_S_FMT may change width and height. */
        } else {
                /* Preserve original settings as set by v4l2-ctl for example */
                if (-1 == xioctl(devicefd, VIDIOC_G_FMT, &fmt))
                        err_exit("VIDIOC_G_FMT");
        }

        /* Buggy driver paranoia. */
        min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min)
                fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min)
                fmt.fmt.pix.sizeimage = min;
        
    struct v4l2_requestbuffers reqbuf;

    // Set request buf options
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // Set to multiplane buffers
    reqbuf.memory = V4L2_MEMORY_MMAP; // For fastest video capture rate
    reqbuf.count = NUM_BUFFERS;
    if (xioctl(devicefd, VIDIOC_REQBUFS, &reqbuf) == -1) {
        err_exit("ioctl reqbuf");
    }

    if (reqbuf.count < MIN_BUFFERS_NEEDED) {
        char msg[] = "Not enough buffer memory\n";
        write(2, msg, sizeof(msg));
        exit(EXIT_FAILURE);
    }

    buffers = (Buf*) malloc(reqbuf.count * sizeof(*buffers));
    assert(buffers != NULL);

    // Memory map all the buffers
    for (unsigned i = 0; i < reqbuf.count; i++) {
        struct v4l2_buffer buffer;

        memset(&buffer, 0, sizeof(buffer));
        buffer.type = reqbuf.type;
        buffer.memory = reqbuf.memory;
        buffer.index = i;

        if (ioctl(devicefd, VIDIOC_QUERYBUF, &buffer) < 0) {
            err_exit("ioctl querybuf");
        }
	buffers[i].length = buffer.length;

	buffers[i].start = mmap(NULL, buffer.length,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED,
                                       devicefd, buffer.m.offset);
	if (MAP_FAILED == buffers[i].start) {
            err_exit("mmap");
        }
    }
}

void cleanup() {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    if (buffers) free(buffers);
    close(devicefd);
}

void start_capture() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(devicefd, VIDIOC_STREAMON, &type) == -1) {
        err_exit("VIDIOC_STREAMON");
    }
}

void stop_capture() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(xioctl(devicefd, VIDIOC_STREAMOFF, &type) == -1) {
        err_exit("VIDIOC_STREAMOFF");
    }
}

// Return the newly filled buffer
void dequeue_frame(struct v4l2_buffer * frame) {
    memset(frame, 0, sizeof(*frame));
    frame->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frame->memory = V4L2_MEMORY_MMAP;
	BEFORE:
    if (xioctl(devicefd, VIDIOC_DQBUF, frame) == -1) {
        switch (errno) {
        case EAGAIN:
            goto BEFORE;
        default:
            err_exit("VIDIOC_DQBUF");
        }

    }
    assert(frame->index < NUM_BUFFERS);

}

// Re input the buffer
void enqueue_frame(struct v4l2_buffer * frame) {
    if (xioctl(devicefd, VIDIOC_QBUF, frame) == -1) {
        err_exit("VIDIOC_QBUF");
    }
}

void process_frame(struct v4l2_buffer * frame) {
    printf("Processed frame with %d bytes and %lu total bytes\n", frame->bytesused, buffers[frame->index].length);
    printf("At index %d\n", frame->index);
	static uchar rgbarr[WIDTH * HEIGHT * 3];
	yuvtorgb((uchar*)buffers[frame->index].start, rgbarr);
  
  //QImage img(rgbarr, WIDTH, HEIGHT, QImage::Format_RGB888);
  //img.save("test.png");
}

int main() {
    const char * c = "/dev/video2";
    init_mmap(c);
    start_capture();
    struct v4l2_buffer buf;
	
	// Queue all the frames
	for (int i = 0; i < NUM_BUFFERS; i++) {
    	memset(&buf, 0, sizeof(buf));
		buf.index = i;
    	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	buf.memory = V4L2_MEMORY_MMAP;
		enqueue_frame(&buf);
	}
	
    while (1) {
        // Clear the frame, then process the dequeued buffer
        memset(&buf, 0, sizeof(buf));
        dequeue_frame(&buf);
        process_frame(&buf);
        enqueue_frame(&buf);
    }
}
