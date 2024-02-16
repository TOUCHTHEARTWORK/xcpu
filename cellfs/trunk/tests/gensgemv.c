#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int
main(int argc, char *argv[])
{
	int i, j, n, m, fd, np;
	float f;

	np = 4096;
	unlink("/tmp/sgemv/x");
	fd = open("/tmp/sgemv/x", O_WRONLY | O_CREAT | O_TRUNC, 0666);
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
	unlink("/tmp/sgemv/y");
	fd = open("/tmp/sgemv/y", O_WRONLY | O_CREAT | O_TRUNC, 0666);
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

	unlink("/tmp/sgemv/a");
	fd = open("/tmp/sgemv/a", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		goto error;
	for(i = 0; i < np; i++) 
		for(j = 0; j < np; j++) {
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
