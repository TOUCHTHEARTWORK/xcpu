#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include "npfs.h"
#include "xcpufs.h"

static void *
pip_read_proc(void *a)
{
	int i, n, m;
	u8 buf[256];
	Npfcall *rc;
	Xfilepipe *p;
	Xpipereq *pr, *pr1;

	p = a;
	while (!p->err && (n = read(p->lfd, buf, sizeof(buf))) > 0) {
		//fprintf(stderr, "pip_read_proc pip %p fd %d count %d\n", p, p->lfd, n);
		pthread_mutex_lock(&p->lock);
		i = 0;
		while (i < n) {
			if (!p->reqs) 
				pthread_cond_wait(&p->cond, &p->lock);

			if (p->err || !p->reqs)
				break;

			pr = p->reqs;
			p->reqs = pr->next;
			if (pr == p->reqlast)
				p->reqlast = NULL;

			m = n - i;
			if (m > pr->req->tcall->count)
				m = pr->req->tcall->count;

			rc = np_create_rread(m, buf + i);
			i += m;
			np_respond(pr->req, rc);
			free(pr);
		}

		pthread_mutex_unlock(&p->lock);
	}

	//fprintf(stderr, "pip_read_proc pip %p err %d n %d\n", p, p->err, n);
	pthread_mutex_lock(&p->lock);
	if (!p->err)
		p->err = EPIPE;

	pr = p->reqs;
	p->reqs = NULL;
	pthread_mutex_unlock(&p->lock);

	while (pr != NULL) {
		rc = np_create_rread(0, buf);
		np_respond(pr->req, rc);
		pr1 = pr->next;
		free(pr);
		pr = pr1;
	}

	return NULL;
}

static void*
pip_write_proc(void *a)
{
	int i, n, m;
	u8 *data;
	Npfcall *rc;
	Xfilepipe *p;
	Xpipereq *pr, *pr1;

	p = a;
	pthread_mutex_lock(&p->lock);
	while (!p->err) {
		if (!p->reqs)
			pthread_cond_wait(&p->cond, &p->lock);

		if (!p->reqs)
			continue;

		pr = p->reqs;
		p->reqs = pr->next;
		if (p->reqlast == pr)
			p->reqlast = NULL;
		pthread_mutex_unlock(&p->lock);

		n = pr->req->tcall->count;
		data = pr->req->tcall->data;
		i = 0;
		while ((m = write(p->lfd, data + i, n - i)) > 0)
			i += m;

		if (i < n) {
			pthread_mutex_lock(&p->lock);
			if (m < 0)
				p->err = errno;
			else
				p->err = EPIPE;
			pthread_mutex_unlock(&p->lock);
		} else {
			rc = np_create_rwrite(n);
			np_respond(pr->req, rc);
			free(pr);
		}
		pthread_mutex_lock(&p->lock);
	}

	pr = p->reqs;
	p->reqs = NULL;
	pthread_mutex_unlock(&p->lock);

	while (pr != NULL) {
		rc = np_create_rwrite(0);
		np_respond(pr->req, rc);
		pr1 = pr->next;
		free(pr);
		pr = pr1;
	}

	return NULL;
}

	

Xfilepipe *
pip_create(int direction)
{
	int pip[2];
	Xfilepipe *p;
	void *(*proc)(void *);

	p = malloc(sizeof(*p));
	if (!p)
		return NULL;

	pthread_mutex_init(&p->lock, NULL);
	pthread_cond_init(&p->cond, NULL);
	p->err = 0;
	p->direction = direction;
	pipe(pip);
	if (direction == Read) {
		p->lfd = pip[0];
		p->rfd = pip[1];
		proc = &pip_read_proc;
	} else {
		p->lfd = pip[1];
		p->rfd = pip[0];
		proc = &pip_write_proc;
	}

	//fprintf(stderr, "pip_create pip %p lfd %d\n", p, p->lfd);
	fcntl(p->lfd, F_SETFD, FD_CLOEXEC);
	p->reqs = NULL;
	p->reqlast = NULL;
	pthread_create(&p->thread, NULL, proc, p);

	return p;
}

void
pip_destroy(Xfilepipe *p)
{
	void *ret;

	//fprintf(stderr, "pip_destroy pip %p\n", p);
	pthread_mutex_lock(&p->lock);
	if (p->lfd >= 0)
		close(p->lfd);
	if (p->rfd >= 0)
		close(p->rfd);
	p->err = EPIPE;
	pthread_mutex_unlock(&p->lock);
	pthread_cond_broadcast(&p->cond);
	pthread_join(p->thread, &ret);
	free(p);
}

int
pip_addreq(Xfilepipe* p, Npreq *req)
{
	Xpipereq *preq;

	//fprintf(stderr, "pip_addreq pip %p fid %d req %p\n", p, req->tcall->fid, req);
	preq = malloc(sizeof(*preq));
	if (!preq) {
		np_werror(Enomem, ENOMEM);
		return 0;
	}

	preq->pip = p;
	preq->req = req;
	preq->next = NULL;

	pthread_mutex_lock(&p->lock);
	if (p->err) {
//		np_werror("pipe closed", p->err);
		free(preq);
		pthread_mutex_unlock(&p->lock);
		return 0;
	}

	if (!p->reqlast)
		p->reqs = preq;
	else
		p->reqlast->next = preq;

	p->reqlast = preq;
	pthread_mutex_unlock(&p->lock);
	pthread_cond_broadcast(&p->cond);

	return 1;
}

void
pip_flushreq(Xfilepipe *p, Npreq *req)
{
	Xpipereq *r, *r1, *pr;

	//fprintf(stderr, "pip_flushreq pip %p req %p\n", p, req);
	pthread_mutex_lock(&p->lock);
	pr = NULL;
	r = p->reqs;
	while (r != NULL) {
		r1 = r->next;
		if (r->req == req) {
			if (pr)
				pr->next = r->next;
			else
				p->reqs = r->next;

			if (r == p->reqlast)
				p->reqlast = pr;

			free(r);
			np_respond(req, NULL);
		}
		r = r1;
	}
		
	pthread_mutex_unlock(&p->lock);
}

void
pip_close_remote(Xfilepipe *p)
{
	pthread_mutex_lock(&p->lock);
	if (p->rfd >= 0)
		close(p->rfd);
	p->rfd = -1;
	pthread_mutex_unlock(&p->lock);
	pthread_cond_broadcast(&p->cond);
}

void
pip_close_local(Xfilepipe *p)
{
	pthread_mutex_lock(&p->lock);
	if (p->lfd >= 0)
		close(p->lfd);
	p->lfd = -1;
	pthread_mutex_unlock(&p->lock);
	pthread_cond_broadcast(&p->cond);
}
