/* this is from Plan 9, and Lucent Public License applies */
#include "xcpusrv.h"
#include <ndb.h>

char Edecode[] = "error decoding input packet";
char Eencode[] = "out of space encoding output packet (BUG)";
char Ehangup[] = "hungup connection";
char Ememory[] = "out of memory";

int debuglevel;
int doabort;

void
error(char *fmt, ...)
{
	va_list arg;
	char buf[2048];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	fprint(2, "%s: %s\n", argv0, buf);
	if(doabort)
		abort();
	threadexits(buf);
}

void
debug(int level, char *fmt, ...)
{
	va_list arg;
	if((level&debuglevel) == 0)
		return;
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
}


int
readstrnl(int fd, char *buf, int nbuf)
{
	int i;

	for(i=0; i<nbuf; i++){
		switch(read(fd, buf+i, 1)){
		case -1:
			return -1;
		case 0:
			werrstr("unexpected EOF");
			return -1;
		default:
			if(buf[i]=='\n'){
				buf[i] = '\0';
				return 0;
			}
			break;
		}
	}
	werrstr("line too long");
	return -1;
}

/*
 * this is far too smart.
 */
static int
pstrcmp(const void *a, const void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

char*
trim(char *s)
{
	char *t;
	int i, last, n, nf;
	char **f;
	char *p;

	t = emalloc9p(strlen(s)+1);
	t[0] = '\0';
	n = 1;
	for(p=s; *p; p++)
		if(*p == ' ')
			n++;
	f = emalloc9p((n+1)*sizeof(f[0]));
	nf = tokenize(s, f, n+1);
	qsort(f, nf, sizeof(f[0]), pstrcmp);
	last=-1;
	for(i=0; i<nf; i++){
		if(last==-1 || strcmp(f[last], f[i])!=0){
			if(last >= 0)
				strcat(t, ",");
			strcat(t, f[i]);
			last = i;
		}
	}
	return t;	
}

