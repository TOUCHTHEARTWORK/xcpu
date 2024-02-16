#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>        /* for AF_INET */
#include <string.h>

int
host2ip(char *host, char **ip){
	struct hostent *h;

	h = gethostbyname(host);
	if (! h) {
		*ip = strdup("0.0.0.0");
		return -1;
	} else {
		*ip = strdup(inet_ntoa(*(struct in_addr *)h->h_addr));
		return 0;
	}
}
