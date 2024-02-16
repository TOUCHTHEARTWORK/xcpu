/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <pthread.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

enum {
	TblModified	= 1,
	Notified	= 2,
	ChunkSize	= 4,
};

struct Nppoll {
	pthread_mutex_t	lock;
	int		shutdown;
	int		pipe[2];
	pthread_t	thread;
	int		flags;
	int		fdnum;
	int		fdsize;
	Npollfd**	npfds;
	struct pollfd*	fds;
	Npollfd*	pending_npfds;
};

struct Npollfd {
	int		fd;
	int		events;
	void*		aux;
	int		(*notify)(int, int, void *);

	pthread_mutex_t	lock;	
	Nppoll*		np;
	int		connected;
	struct pollfd*	pfd;
	Npollfd*	next;	/* list of the fds pending addition */
};


static void* np_poll_proc(void *a);
static void np_poll_notify(Nppoll *p);

Nppoll *
np_poll_create()
{
	Nppoll *np;

	np = malloc(sizeof(*np));
	if (!np)
		return NULL;

	pthread_mutex_init(&np->lock, NULL);
	np->shutdown = 0;
	np->fdnum = 1;
	np->fdsize = ChunkSize;
	np->npfds = calloc(np->fdsize, sizeof(Npollfd *));
	np->fds = calloc(np->fdsize, sizeof(struct pollfd));
	pipe(np->pipe);
	fcntl(np->pipe[0], F_SETFL, O_NONBLOCK);

	np->flags = 0;
	np->npfds[0] = NULL;
	np->fds[0].fd = np->pipe[0];
	np->fds[0].events = POLLIN | POLLOUT;
	np->fds[0].revents = 0;
	np->pending_npfds = NULL;
	pthread_create(&np->thread, NULL, np_poll_proc, np);

	return np;
}

void
np_poll_destroy(Nppoll *np)
{
	pthread_mutex_lock(&np->lock);
	np->shutdown = 1;
	pthread_mutex_unlock(&np->lock);
}

Npollfd *
np_poll_add(Nppoll *np, int fd, int events, int (*notify)(int, int, void *), void *aux)
{
	int flags;
	Npollfd *npfd;

	npfd = malloc(sizeof(*npfd));
	if (!npfd)
		return NULL;

	//fprintf(stderr, "nppoll_add %p fd %d events %x\n", npfd, fd, events);
	pthread_mutex_init(&npfd->lock, NULL);
	npfd->np = np;
	npfd->fd = fd;
	npfd->events = events;
	npfd->connected = 1;
	npfd->aux = aux;
	npfd->notify = notify;
	npfd->pfd = NULL;
	npfd->next = NULL;

	pthread_mutex_lock(&np->lock);
	npfd->next = np->pending_npfds;
	np->pending_npfds = npfd;

	flags = np->flags & Notified;
	np->flags = TblModified | Notified;
	pthread_mutex_unlock(&np->lock);
	if (!flags)
		np_poll_notify(np);

	return npfd;
}

void
np_poll_remove(Nppoll *np, Npollfd *npfd)
{
	int flags;

	pthread_mutex_lock(&np->lock);
	flags = np->flags & Notified;
	npfd->connected = 0;
	np->flags = TblModified | Notified;
	pthread_mutex_unlock(&np->lock);

	if (!flags)
		np_poll_notify(np);
}

void
np_poll_setevents(Npollfd *npfd, int events) 
{
	int ev, flags;
	Nppoll *np;

	np = npfd->np;
	ev = (events & 0xFF) | (npfd->events&~(0xFF & (events>>8)));

	fprintf(stderr, "setevents: fd %d flags %x/%x old %d new %d\n", npfd->fd, events & 0xFF, (events >> 8) & 0xFF, npfd->events, ev);
	pthread_mutex_lock(&npfd->lock);
	if (npfd->events == ev) {
		pthread_mutex_unlock(&npfd->lock);
		return;
	}

	npfd->events = ev;
	flags = np->flags & Notified;
	pthread_mutex_unlock(&npfd->lock);

	if (!flags)
		np_poll_notify(np);
}

static void
np_poll_update_table(Nppoll *p)
{
	int i, n, m;
	struct pollfd *tfds;
	struct Npollfd *npfd, **tnpfd, *pnpfd;

	/* get rid of the disconnected fds */
	for(i = 1; i < p->fdnum; i++) {
		npfd = p->npfds[i];
		if (npfd && !npfd->connected) {
			free(npfd);
			npfd = p->npfds[i] = NULL;
		}
	}

	/* try to fill the holes with pending fds */
	for(i = 1, pnpfd = p->pending_npfds; pnpfd && i < p->fdsize; i++) {
		npfd = p->npfds[i];
		if (!npfd) {
			p->npfds[i] = pnpfd;
			p->npfds[i]->pfd = &p->fds[i];
			p->fds[i].fd = pnpfd->fd;
			p->fds[i].events = pnpfd->events;
			pnpfd = pnpfd->next;
		}
	}

	/* if there are still holes, move some elements to fill them */
	for(n = i; i < p->fdnum; i++) {
		npfd = p->npfds[i];
		if (!npfd)
			continue;

		if (i != n) {
			p->npfds[n] = p->npfds[i];
			p->npfds[n]->pfd = &p->fds[n];
			p->fds[n] = p->fds[i];
			p->npfds[i] = NULL;
		}
		n++;
	}

	/* find out the number of still pending fds */
	for(i = 0; pnpfd != NULL; pnpfd = pnpfd->next, i++)
		;

	/* increase the array if we have to */
	m = n + i + ChunkSize - ((n+i)%ChunkSize);
	if (p->fdsize < m) {
		tfds = realloc(p->fds, sizeof(struct pollfd) * m);
		if (tfds)
			p->fds = tfds;

		tnpfd = realloc(p->npfds, sizeof(Npollfd *) * m);
		if (tnpfd) {
			for(i = 1; i < n; i++)
				tnpfd[i]->pfd = &p->fds[i];

			for(i = p->fdsize; i < m; i++)
				tnpfd[i] = NULL;
			p->npfds = tnpfd;
		}

		if (tfds && tnpfd)
			p->fdsize = m;
			
	}

	/* put the remaining pending fds in place */
	for(i = n; i < p->fdsize && pnpfd != NULL; i++, pnpfd = pnpfd->next) {
		p->npfds[i] = pnpfd;
		p->npfds[i]->pfd = &p->fds[i];
		p->fds[i].fd = pnpfd->fd;
		p->fds[i].events = pnpfd->events;
	}

	p->pending_npfds = pnpfd;
	p->fdnum = i;
}

static void*
np_poll_proc(void *a)
{
	int i, n, shutdown, errors, events;
	Nppoll *p;
	struct pollfd *pfd;
	struct Npollfd *npfd;
	char buf[10];

	p = a;
	shutdown = 0;
	while (!shutdown) {
		n = poll(p->fds, p->fdnum, 10000);

//		pthread_mutex_lock(&p->lock);
		errors = 0;
		for(i = 1; i < p->fdnum; i++) {
			npfd = p->npfds[i];
			pfd = &p->fds[i];

			if (!npfd || !npfd->connected) {
				errors++;
				continue;
			}

			if ((pfd->revents&(npfd->events | POLLERR|POLLHUP|POLLNVAL))) {
				pthread_mutex_lock(&npfd->lock);
				events = (*npfd->notify)(npfd->fd, pfd->revents, npfd->aux);
//				ev = npfd->events;
				fprintf(stderr, "poll_proc fd %d %x/%x events %d\n", npfd->fd, events & 0xFF, (events>>8) & 0xFF, npfd->events);
				npfd->events = (events & 0xFF) | (npfd->events&~(0xFF & (events>>8)));
				pthread_mutex_unlock(&npfd->lock);
				if (!npfd->connected) 
					errors++;
			}

			fprintf(stderr, "poll_proc fd %d old events %d new %d\n", npfd->fd, pfd->events, npfd->events);
			pfd->events = npfd->events;
		}

		if (errors || p->fds[0].revents & POLLIN) {
			shutdown = p->shutdown;
			n = read(p->pipe[0], buf, sizeof(buf));

			if (errors || p->flags&TblModified)
				np_poll_update_table(p);

			p->flags = 0;
		}
//		pthread_mutex_unlock(&p->lock);
	}

	free(p);
	return NULL;
}

static void
np_poll_notify(Nppoll *p)
{
	int n;
	char *buf = "";

	n = write(p->pipe[1], buf, 1);
}
