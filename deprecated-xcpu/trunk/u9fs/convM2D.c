#include	<plan9.h>
#include	<fcall.h>

int
statcheck(uchar *buf, uint nbuf)
{
	uchar *ebuf;
	int i;
	int nelem = 4;

	ebuf = buf + nbuf;

	buf += STATFIXLEN - 4 * BIT16SZ;

	if(extended) {
		buf +=  (BIT32SZ * 3); 	/* n_uid, n_gid, n_muid */
		nelem++;
	}

	for(i = 0; i < nelem; i++){
		if(buf + BIT16SZ > ebuf)
			return -1;
		buf += BIT16SZ + GBIT16(buf);
	}

	if(buf != ebuf)
		return -1;

	return 0;
}

static char nullstring[] = "";

uint
convM2D(uchar *buf, uint nbuf, Dir *d, char *strs)
{
	uchar *p, *ebuf;
	char *sv[5];
	int i, ns;
	int nelem = 4;

	p = buf;
	ebuf = buf + nbuf;

	p += BIT16SZ;	/* ignore size */
	d->type = GBIT16(p);
	p += BIT16SZ;
	d->dev = GBIT32(p);
	p += BIT32SZ;
	d->qid.type = GBIT8(p);
	p += BIT8SZ;
	d->qid.vers = GBIT32(p);
	p += BIT32SZ;
	d->qid.path = GBIT64(p);
	p += BIT64SZ;
	d->mode = GBIT32(p);
	p += BIT32SZ;
	d->atime = GBIT32(p);
	p += BIT32SZ;
	d->mtime = GBIT32(p);
	p += BIT32SZ;
	d->length = GBIT64(p);
	p += BIT64SZ;

	d->n_uid = -1;
	d->n_gid = -1;
	d->n_muid = -1;
	d->name = nil;
	d->uid = nil;
	d->gid = nil;
	d->muid = nil;
	d->extension = nil;

	if(extended) 
		nelem++;

	for(i = 0; i < nelem; i++){
		if(p + BIT16SZ > ebuf)
			return 0;
		ns = GBIT16(p);
		p += BIT16SZ;
		if(p + ns > ebuf)
			return 0;
		if(strs){
			sv[i] = strs;
			memmove(strs, p, ns);
			strs += ns;
			*strs++ = '\0';
		}
		p += ns;
	}

	if(extended) {
		d->n_uid = GBIT32(p);
		p+= BIT32SZ;
		d->n_gid = GBIT32(p);
		p+= BIT32SZ;
		d->n_muid = GBIT32(p);
		p+= BIT32SZ;
	}

	if(strs){
		d->name = sv[0];
		d->uid = sv[1];
		d->gid = sv[2];
		d->muid = sv[3];
		if(extended) {
			d->extension = sv[4];
		} else {
			d->extension = "";
		}
	}else{
		d->name = nullstring;
		d->uid = nullstring;
		d->gid = nullstring;
		d->muid = nullstring;
		d->extension = nullstring;
	}
	
	return p - buf;
}
