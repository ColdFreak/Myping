#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>


char *host;
pid_t pid;

int main(int argc, char **argv) {
	struct addrinfo hints, *res;
	char h[128];
	struct sockaddr_in *sin;

	if(argc != 2) {
		perror("usage: ./ping <hostname>");
		exit (1);
	}

	host = argv[1];
	pid = getpid() & 0xffff;
	signal(SIGALRM, sig_alrm); 

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_ai_flags = AI_CANONNAME;
	if( ( n = getaddrinfo(host, NULL, &hints, &res)) != 0) {
		fprint(stderr, "getaddrinfo: %s\n",gai_strerror(n));
		exit (1);
	}
	
	sin = (struct sockaddr_in *)(res->ai_addr);	
	switch (sin->sa_family) {
		case AF_INET: 
			if(inet_ntop(AF_INET,&sin->sin_addr, h, sizeof(str)) == NULL) {
				perror("inet_ntop");
				exit (1);
			}
			break;
		
		default:
			perror("Not IPv4 address");
			exit (1);
	}




