#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int
main(int argc, char *argv[])
{
	int i, n, m, fd, np;
	float f;

	np = strtol(argv[1], NULL, 10);
	fd = open("/tmp/euler/pos", O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0)
		goto error;

	for(i = 0; i < np*4; i++) {
		n = rand();
		m = rand();
		if (!m)
			m = 1;

		f = (float) n / m;
		write(fd, &f, sizeof(f));
	}

	close(fd);
	fd = open("/tmp/euler/vel", O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0)
		goto error;

	for(i = 0; i < np*4; i++) {
		n = rand();
		m = rand();
		if (!m)
			m = 1;

		f = (float) n / m;
		write(fd, &f, sizeof(f));
	}
	close(fd);

	fd = open("/tmp/euler/invmass", O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0)
		goto error;
	for(i = 0; i < np; i++) {
		n = rand();
		m = rand();
		if (!m)
			m = 1;

		f = (float) n / m;
		write(fd, &f, sizeof(f));
	}
	close(fd);

	fd = open("/tmp/euler/force", O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0)
		goto error;
	for(i = 0; i < 4; i++) {
		n = rand();
		m = rand();
		if (!m)
			m = 1;

		f = (float) n / m;
		write(fd, &f, sizeof(f));
	}
	close(fd);

	return 0;

error:
	perror("Error");
	return -1;
}
