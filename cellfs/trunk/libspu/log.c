/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "libspu.h"
#include "spuimpl.h"

static int logfd = -1;

int
spc_log_init()
{
	if (logfd < 0)
		logfd = spc_open("#l/stderr", Owrite);

	return logfd<0?-1:0;
}

static
char *
putdat(char *bp, char *be, void *val, int vsz)
{
	if (!bp)
		return NULL;

	if (be-bp < vsz)
		return NULL;

	memmove(bp, val, vsz);
	return bp + vsz;
}

int
spc_log(char *fmt, ...)
{
	int i;
	long long int l;
	double f;
	char *s, *t;
	char buf[128], *bp, *be;
	va_list ap;

	if (logfd < 0)
		return 0;

	bp = buf;
	be = buf + sizeof(buf);
	bp = putdat(bp, be, fmt, strlen(fmt) + 1);

	va_start(ap, fmt);
	s = fmt;
	while ((s = strchr(s, '%')) != NULL) {
		s++;
		switch (*s) {
		case 'l':
			l = va_arg(ap, long long int);
			bp = putdat(bp, be, &l, sizeof(l));
			break;

		case 'p':
		case 'd':
		case 'x':
			i = va_arg(ap, int);
			bp = putdat(bp, be, &i, sizeof(i));
			break;

		case 'f':
			f = va_arg(ap, double);
			bp = putdat(bp, be, &f, sizeof(f));
			break;

		case 's':
			t = va_arg(ap, char *);
			bp = putdat(bp, be, t, strlen(t) + 1);
			break;
		}
	}
	va_end(ap);

	spc_write(logfd, (u8 *) buf, bp-buf);

	return 0;
}
