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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "npfs.h"
#include "strutil.h"

int
tokenize(char *s, char ***ss)
{
	int i, n;
	char *e, *p, **toks;


	*ss = NULL;	
	for(i=0, n=1, p=s; *p != '\0'; p++, i++)
		if (*p==' ' || *p=='\t' || *p=='\n')
			n++;

	toks = malloc((n+1)*sizeof(char *) + i + 1);
	if (!toks) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	p = (char *) toks + (n+1)*sizeof(char *);
	memmove(p, s, i + 1);
	
	for(i = 0; *p != '\0'; i++) {
		if (i >= n) {
			np_werror("internal error", EIO);
			free(toks);
			return -1;
		}
				
		toks[i] = unquotestr(p, &e);
		if (!toks[i]) {
			np_werror("invalid format", EIO);
			free(toks);
			return -1;
		}

		p = e;
		while (*p==' ' || *p=='\t' || *p=='\n')
			p++;
	}

	toks[i] = NULL;
	*ss = toks;
	return i;
}
