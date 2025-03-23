////////////////////////////////////////
/* tled.c                             */
/* copyright (c) 2025, Nathan Gill    */
/* https://github.com/OldUser101/tled */
////////////////////////////////////////

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>

#include "device.h"

#define BUFFER_SIZE 1024

// Global variables to track key states
int capsLock = 0;
int numLock = 0;
int scrLock = 0;

int active_tty = -1;
pthread_attr_t attr;
pthread_mutex_t tty_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t flag_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t device_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t tty_thread, device_thread, led_thread;
int running = 1;

struct pollfd *devices = NULL;
int n_devices = 0;
pthread_cond_t led_cond = PTHREAD_COND_INITIALIZER;

int DEBUG = 0;

void cleanup() {
	running = 0;
}

void signal_handler(int sig) {
	cleanup();
	exit(0);
}

int get_active_tty(int fd) {
	struct vt_stat vt;
	if (ioctl(fd, VT_GETSTATE, &vt) < 0) {
		perror("ioctl VT_GETSTATE");
		return -1;
	}

	return vt.v_active;
}

void setLedState(int fd_tty, int capsLock, int numLock, int scrLock) {
	int state;
	int newState;

	if (ioctl(fd_tty, KDGETLED, &state) < 0) {
		perror("ioctl KDGETLED");
		exit(1);
	}

	newState = state;

	// Check if the flag needs changing
	if (capsLock == 0 && (state & LED_CAP)) {
		newState &= ~LED_CAP;
	} else if (capsLock == 1 && !(state & LED_CAP)) {
		newState |= LED_CAP;
	}

	if (numLock == 0 && (state & LED_NUM)) {
		newState &= ~LED_NUM;
	} else if (numLock == 1 && !(state & LED_NUM)) {
		newState |= LED_NUM;
	}

	if (scrLock == 0 && (state & LED_SCR)) {
		newState &= ~LED_SCR;
	} else if (scrLock == 1 && !(state & LED_SCR)) {
		newState |= LED_SCR;
	}

	int ledState = 0;
	if (newState & LED_SCR) ledState |= 0x01;
	if (newState & LED_NUM) ledState |= 0x02;
	if (newState & LED_CAP) ledState |= 0x04;

	// Change the LED state
	if (ioctl(fd_tty, KDSETLED, ledState) < 0) {
		perror("ioctl KDSETLED");
		exit(1);
	}
}


void refreshLedStates() {
	pthread_mutex_lock(&tty_mutex);

	int keyState = 0;
	char tty_device[16];
	snprintf(tty_device, sizeof(tty_device), "/dev/tty%d", active_tty);

	if (DEBUG) {
		printf("tty: %s\n", tty_device);
	}

	pthread_mutex_unlock(&tty_mutex);

	// Current TTY session
	int fd_tty = open(tty_device, O_RDONLY);
	if (fd_tty == -1) {
		perror("open TTY");
		return;
	}

	if (ioctl(fd_tty, KDGKBLED, &keyState) < 0) {
		perror("ioctl KDGKBLED");
		close(fd_tty);
		return;
	}

	pthread_mutex_lock(&flag_mutex);

	// Set these with the current key states...
	capsLock = (keyState & K_CAPSLOCK) ? 1 : 0;
	numLock = (keyState & K_NUMLOCK) ? 1 : 0;
	scrLock = (keyState & K_SCROLLLOCK) ? 1 : 0;

	int caps = capsLock, num = numLock, scroll = scrLock;

	if (DEBUG) {
		printf("state: %b; caps lock: %d; num lock %d; scroll lock: %d\n", keyState, caps, num, scroll);
	}

	pthread_mutex_unlock(&flag_mutex);

	// ...and update the LEDs
	setLedState(fd_tty, caps, num, scroll);

	close(fd_tty);
}

void dispatchKey(struct input_event *ev) {
	// Only process key down events
	if (ev->type != EV_KEY || ev->value == 0 || ev->value == 2) {
		return;
	}

	switch (ev->code) {
		case KEY_CAPSLOCK:
		case KEY_NUMLOCK:
		case KEY_SCROLLLOCK:
			break;
		default:
			return;
	}

	pthread_mutex_lock(&flag_mutex);

	if (ev->code == KEY_CAPSLOCK) {
		capsLock = capsLock ? 0 : 1;
		if (DEBUG) {
			printf("caps lock\n");
		}
	}

	if (ev->code == KEY_NUMLOCK) {
		numLock = numLock ? 0 : 1;
		if (DEBUG) {
			printf("num lock\n");
		}
	}

	if (ev->code == KEY_SCROLLLOCK) {
		scrLock = scrLock ? 0 : 1;
		if (DEBUG) {
			printf("scroll lock\n");
		}
	}

	pthread_cond_signal(&led_cond);

	pthread_mutex_unlock(&flag_mutex);
}

void *led_monitor(void *arg) {
	while (running) {
		pthread_mutex_lock(&flag_mutex);

		pthread_cond_wait(&led_cond, &flag_mutex);

		int caps = capsLock, num = numLock, scroll = scrLock;

		pthread_mutex_unlock(&flag_mutex);

		int fd_tty;
		char tty_device[16];

		pthread_mutex_lock(&tty_mutex);

		snprintf(tty_device, sizeof(tty_device), "/dev/tty%d", active_tty);

		pthread_mutex_unlock(&tty_mutex);

		// Current TTY session
		fd_tty = open(tty_device, O_RDWR);
		if (fd_tty == -1) {
			perror("open TTY");
			return NULL;
		}

		setLedState(fd_tty, caps, num, scroll);

		close(fd_tty);
	}
}

void *tty_monitor(void *arg) {
	int fd = open("/dev/console", O_RDONLY);
	if (fd < 0) {
		perror("open /dev/console");
		return NULL;
	}

	while (running) {
		int new_tty = get_active_tty(fd);
		if (new_tty > 0 && new_tty != active_tty) {
			pthread_mutex_lock(&tty_mutex);
			active_tty = new_tty;
			pthread_mutex_unlock(&tty_mutex);
			refreshLedStates();
		}
		sleep(1);
	}

	close(fd);
	return NULL;
}

void *device_monitor(void *arg) {
	device_init(&device_mutex);
	while (running) {
		monitor_input_devices();
		devices = get_fds();
		sleep(1);
	}
	device_destroy();

	return NULL;
}

char *get_conf_path() {
	const char *home = getenv("HOME");
	char* expanded = (char*)malloc(BUFFER_SIZE + 1);
	snprintf(expanded, sizeof(expanded), "%s/.tled", home);
	expanded[BUFFER_SIZE] = '\0';
	return expanded;
}

void check_debug() {
	if (getenv("DEBUG") != NULL) {
		DEBUG = 1;
		printf("set debug to on\n");
	}
}

void keyboard_monitor() {
	while (devices == NULL && running) {}

	struct input_event event;
	while (running) {
		int n_devices = get_n_fds();
		int ret = poll(devices, n_devices, -1);
		if (ret < 0) {
			if (!running) {
				break;
			}
			perror("poll");
			return;
		}

		for (int i = 0; i < n_devices; i++) {
			if (devices[i].revents & POLLIN) {
				ssize_t bytesRead = read(devices[i].fd, &event, sizeof(event));
				if (bytesRead < (ssize_t)sizeof(event)) {
					perror("read");
					return;
				}
				dispatchKey(&event);
			}
		}
	}
}

int main(int argc, char *argv[]) {
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	check_debug();

	pthread_attr_init(&attr);

	pthread_attr_setstacksize(&attr, 512 * 1024);

	pthread_create(&tty_thread, &attr, tty_monitor, NULL);
	pthread_detach(tty_thread);

	pthread_create(&device_thread, &attr, device_monitor, NULL);
	pthread_detach(device_thread);

	pthread_create(&led_thread, &attr, led_monitor, NULL);
	pthread_detach(led_thread);

	keyboard_monitor();

	cleanup();

	pthread_join(led_thread, NULL);
	pthread_join(tty_thread, NULL);
	pthread_join(device_thread, NULL);

	pthread_attr_destroy(&attr);

	pthread_mutex_destroy(&flag_mutex);
	pthread_mutex_destroy(&tty_mutex);
	pthread_mutex_destroy(&device_mutex);
	pthread_cond_destroy(&led_cond);
	return 0;
}

