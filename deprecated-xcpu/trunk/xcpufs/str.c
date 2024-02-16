#include <stdlib.h>
#include <string.h>

char*
quotestrdup(char *str)
{
	int n, doquote, nquot;
	char *s, *t, *ret;

	doquote = 0;
	nquot = 0;
	s = str;
	while (*s != '\0') {
		if (*s == ' ' || *s == '\t' || *s == '\'') {
			doquote = 1;
			if (*s == '\'')
				nquot++;
		}

		s++;
	}

	if (!doquote)
		return strdup(str);

	n = (s - str) + 2 + nquot;
	ret = malloc(n + 1);
	if (!ret)
		return ret;

	t = ret;
	*(t++) = '\'';
	s = str;
	while (*s != '\0') {
		if (*s == '\'')
			*(t++) = '\'';
		*(t++) = *(s++);
	}
	*(t++) = '\'';
	*t = '\0';

	return ret;
}

char *
unquotestr(char *str, char **eptr)
{
	char *s, *t;

	s = str;
	if (*s != '\'') {
		while (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\0')
			s++;

		if (*s != '\0')
			*(s++) = '\0';

		if (eptr)
			*eptr = s;
		
		return str;
	}

	t = s + 1;
	while (*t != '\0') {
		if (*t == '\'') {
			if (*(t+1) != '\'') {
				break;
			} else {
				t++;
			}
		}
		
		*s = *t;
		s++; t++;
	}

	*s = '\0';
	if (*t != '\'')
		return NULL;
	else if (*t != '\0')
		t++;

	if (eptr)
		*eptr = t;

	return str;
}

char**
tokenize(char *s)
{
	int i, n;
	char *e, **t, **toks;

	n = 64;
	toks = malloc(sizeof(*toks) * (n + 1));
	if (!toks)
		return NULL;

	for(i = 0; *s != '\0'; i++) {
		if (i >= n) {
			n += 32;
			t = realloc(toks, sizeof(*toks) * (n + 1));
			if (!t) {
				free(toks);
				return NULL;
			}

			toks = t;
		}
				
		toks[i] = unquotestr(s, &e);
		if (!toks[i]) {
			free(toks);
			return NULL;
		}

		s = e;
		while (*s==' ' || *s=='\t' || *s=='\n')
			s++;
	}

	toks[i] = NULL;
	return toks;
}

int
cutstr(unsigned char *target, int toffset, int tcount, char *src, int soffset)
{
	int b, e;
	int slen;

	if (!src)
		return 0;

	slen = strlen(src);
	if (toffset > soffset+slen)
		return 0;

	if (soffset > toffset+tcount)
		return 0;

	b = soffset;
	if (b < toffset)
		b = toffset;

	e = soffset+slen;
	if (e > (toffset+tcount))
		e = toffset+tcount;

	memmove(target+(b-toffset), src+(b-soffset), e-b);
	return e-b;
}
