//#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include "npfs.h"
#include "strutil.h"
#include "xcpu.h"
#include "xcpufs.h"

typedef struct Xauth Xauth;
struct Xauth {
	int	authdone;
	char	challenge[20];
	char	response[80];
};

static void
xauth_check_response(Xauth *a)
{
	// TODO
}

Npfcall*
xauth_auth(Npfid *afid, Npstr *uname, Npstr *aname)
{
	return np_create_rerror("authentication not required", EIO, afid->conn->dotu);
/*
	Npqid aqid;
	Xauth *a;

	a = malloc(sizeof(*a));
	if (!a) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	a->authdone = 0;
	snprintf(a->challenge, sizeof(a->challenge), "%04x%04x", (unsigned int) random(), (unsigned int) random());
	memset(a->response, 0, sizeof(a->response));
	afid->aux = a;

	aqid.type = 0;
	aqid.version = 0;
	aqid.path = 0;
	return np_create_rauth(&aqid);
*/
}

Npfcall*
xauth_attach(Npfid *afid, Npstr *uname, Npstr *aname)
{
	Xauth *a;

	a = afid->aux;
	if (a->authdone)
		return NULL;

	return np_create_rerror("authentication failed", EIO, afid->conn->dotu);
}

Npfcall*
xauth_read(Npfid *fid, u64 offset, u32 count)
{
	int n;
	u8 *s;
	Npfcall *ret;
	Xauth *a;

	a = fid->aux;
	s = malloc(count);
	if (!s) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	n = cutstr(s, offset, count, a->challenge, 0);
	ret = np_create_rread(n, s);
	free(s);
	return ret;
}

Npfcall*
xauth_write(Npfid *fid, u64 offset, u32 count, u8 *data)
{
	Xauth *a;

	a = fid->aux;
	if (offset+count > sizeof(a->response))
		count = sizeof(a->response) - offset;

	if (count <= 0)
		return np_create_rwrite(0);

	memmove(a->response + offset, data, count);
	if (offset + count == sizeof(a->response))
		xauth_check_response(a);

	return np_create_rwrite(count);
}


Npfcall*
xauth_clunk(Npfid *fid)
{
	free(fid->aux);
	return np_create_rclunk();
}

