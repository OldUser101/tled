////////////////////////////////////////
/* device.c                           */
/* copyright (c) 2025, Nathan Gill    */
/* https://github.com/OldUser101/tled */
////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>

#include "device.h"

#define INPUT_DIR "/dev/input"
#define EVENT_PREFIX "event"
#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

typedef struct {
	struct pollfd *fds;
	size_t size;
	size_t capacity;
} DeviceDescriptorArray;

DeviceDescriptorArray dda = {NULL, 0, 0};

pthread_mutex_t *device_mutex_;
int fd = -1;
int wd = -1;

void add_device(int fd) {
	pthread_mutex_lock(device_mutex_);
	if (dda.size >= dda.capacity) {
		dda.capacity = dda.capacity ? dda.capacity * 2 : sizeof(struct pollfd);
		dda.fds = realloc(dda.fds, dda.capacity * sizeof(struct pollfd));
		if (!dda.fds) {
			perror("realloc failed");
			exit(1);
		}
	}
	dda.fds[dda.size++].fd = fd;
	dda.fds[dda.size].events = POLLIN;
	pthread_mutex_unlock(device_mutex_);
}

void remove_device(int index) {
	pthread_mutex_lock(device_mutex_);
	if (index < 0 || index >= dda.size) {
		return;
	}
	close(dda.fds[index].fd);
	for (size_t i = index; i < dda.size - 1; i++) {
		dda.fds[i] = dda.fds[i + 1];
	}
	dda.size--;
	pthread_mutex_unlock(device_mutex_);
}

void remove_device_fd(int fd) {
	for (size_t i = 0; i < dda.size; i++) {
		if (dda.fds[i].fd == fd) {
			remove_device(i);
			return;
		}
	}
}

int open_device(const char *dev_name) {
	char dev_path[256];
	snprintf(dev_path, sizeof(dev_path), "%s/%s", INPUT_DIR, dev_name);
	int fd = open(dev_path, O_RDONLY);
	if (fd == -1) {
		return -1;
	} else {
		add_device(fd);
	}
	return fd;
}

void update_new_devices() {
	struct dirent *entry;
	DIR *dp = opendir(INPUT_DIR);
	if (!dp) {
		perror("opendir");
		return;
	}

	while ((entry = readdir(dp)) != NULL) {
		if (strncmp(entry->d_name, EVENT_PREFIX, strlen(EVENT_PREFIX)) == 0) {
			int is_open = 0;
			for (size_t i = 0; i < dda.size; i++) {
				char dev_path[1024];
				snprintf(dev_path, 1023, "%s/%s", INPUT_DIR, entry->d_name);
				if (dda.fds[i].fd == open(dev_path, O_RDONLY)) {
					is_open = 1;
					break;
				}
			}
			if (!is_open) {
				open_device(entry->d_name);
				if (DEBUG) {
					printf("added device '%s'\n", entry->d_name);
				}
			}
		}
	}
	closedir(dp);
}

void device_init(pthread_mutex_t *mutex) {
	device_mutex_ = mutex;

	fd = inotify_init1(IN_NONBLOCK);
	if (fd < 0) {
		perror("inotify_init");
		return;
	}

	wd = inotify_add_watch(fd, INPUT_DIR, IN_CREATE | IN_DELETE);
	if (wd < 0) {
		perror("inotify_add_watch");
		close(fd);
		return;
	}

	update_new_devices();
}

void monitor_input_devices() {
	char buf[BUF_LEN];
	int length = read(fd, buf, BUF_LEN);
	if (length < 0) {
		if (errno == EAGAIN) {
			usleep(500000);
			return;
		}
		perror("read");
		return;
	}

	for (char *ptr = buf; ptr < buf + length;) {
		struct inotify_event *event = (struct inotify_event*)ptr;
		if (event->len > 0 && strstr(event->name, EVENT_PREFIX)) {
			if (event->mask & IN_CREATE) {
				int new_fd = 0;
				if ((new_fd = open_device(event->name)) != -1) {
					add_device(new_fd);
					if (DEBUG) {
						printf("added device '%s'\n", event->name);
					}
				}
			}
			else {
				continue; // We should probably remove the device here
			}
		}
		ptr += sizeof(struct inotify_event) + event->len;
	}
}

struct pollfd *get_fds() {
	return dda.fds;
}

int get_n_fds() {
	return dda.size;
}

void device_destroy() {
	for (size_t i = 0; i < dda.size; i++) {
		close(dda.fds[i].fd);
	}
	close(fd);
	free(dda.fds);
}
