////////////////////////////////////////
/* device.h                           */
/* copyright (c) 2025, Nathan Gill    */
/* https://github.com/OldUser101/tled */
////////////////////////////////////////

#pragma once
#ifndef _DEVICE_H
#define _DEVICE_H

extern int running;
extern int DEBUG;

/* Monitor currently open input devices */
void monitor_input_devices();

/* Get file descriptor array */
struct pollfd *get_fds();

/* Get number of file descriptors */
int get_n_fds();

/* Initialise and destroy device handler */
void device_init(pthread_mutex_t *mutex);
void device_destroy();

#endif
