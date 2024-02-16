#include "xsrv.h"

Queue*
qalloc(void)
{
    Queue *q;

    q = mallocz(sizeof(Queue), 1);
    if(q == nil)
        return nil;
	q->r.l = &q->lk;
    return q;
}

/* not really sure this is the right way to do it */
void
qfree(Queue *q)
{
	Qel *e, *ne;

	qlock(&q->lk);
	e = q->head;
	while(e != nil) {
		ne = e->next;
		respond(e->p, "freeing request");
		free(e);
		e = ne;
	}
	free(q);
}

int
sendq(Queue *q, void *p)
{
    Qel *e;

    e = malloc(sizeof(Qel));
	if(e == nil)
		return -1;
    qlock(&q->lk);
    e->p = p;
    e->next = nil;
    if(q->head == nil)
        q->head = e;
    else
        q->tail->next = e;
    q->tail = e;
	rwakeup(&q->r);
    qunlock(&q->lk);
    return 0;
}

void *
recvq(Queue *q)
{   
    void *p;
    Qel *e;

    qlock(&q->lk);
	while(q->head == nil)
		rsleep(&q->r);
    e = q->head;
    q->head = e->next;
    qunlock(&q->lk);
    p = e->p;
    free(e);
    return p;
}
