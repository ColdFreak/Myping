#include <stdio.h>
#include <stdlib.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <strings.h> /* bzero() */
#include <string.h>
#include <arpa/inet.h> /* inet_ntop()*/
#include <netinet/ip_icmp.h> /* struct icmp */
#include <sys/time.h>
#include <netinet/in.h> /* struct sockaddr_in */

#define BUFSIZE	1500

char *host;
pid_t pid;
int datalen = 56;
char sendbuf[BUFSIZE];
int nsent = 0;

uint16_t in_cksum(uint16_t *addr, int len);
int main(int argc, char **argv) {
	struct addrinfo hints, *res;
	char h[128];
	int n;
	struct sockaddr_in *sin;
	char *buffer;
	struct icmp *icmp;
	int len;
	int sockfd;

	if(argc != 2) {
		perror("usage: ./ping <hostname>");
		exit (1);
	}

	host = argv[1];
	pid = getpid() & 0xffff;
//	signal(SIGALRM, sig_alrm); 

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_flags = AI_CANONNAME;
	if( ( n = getaddrinfo(host, NULL, &hints, &res)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n",gai_strerror(n));
		exit (1);
	}
	
	sin = (struct sockaddr_in *)(res->ai_addr);	
	switch (sin->sin_family) {
		case AF_INET: 
			if(inet_ntop(AF_INET,&(sin->sin_addr), h, 128) == NULL) {
				perror("inet_ntop");
				exit (1);
			}
			break;
		
		default:
			perror("Not IPv4 address");
			exit (1);
	}
	printf("PING %s (%s): %d data bytes\n",res->ai_canonname ? res->ai_canonname:h, h, datalen);
	// create socket from here
	if(res->ai_family == AF_INET) {
		if((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
			perror("socket");
			exit(1);
		}
	}
	else {
		fprintf(stderr,"unknown address family %d", res->ai_family);
		exit (1);
	}
	setuid(getuid());



	icmp = (struct icmp *)sendbuf;
	icmp->icmp_type = ICMP_ECHO;
	icmp->icmp_code = 0;
	icmp->icmp_seq = nsent++;
	icmp->icmp_id = pid;
	memset(icmp->icmp_data,0, datalen);
	if(gettimeofday((struct timeval *)icmp->icmp_data, NULL) < 0) {
		perror("gettimeofday");
		exit (1);
	}
	len = 8 + datalen;
	icmp->icmp_cksum = 0;
	icmp->icmp_cksum = in_cksum((u_short *)icmp,len); 
	return 0;
}

uint16_t in_cksum(uint16_t *addr, int len) {
	int nleft = len;
	uint32_t sum = 0;
	uint16_t *w = addr;
	uint16_t answer = 0;

	while(nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}
	if (nleft == 1) {
		*(unsigned char *)(&answer) = *(unsigned char *)w;
		sum += answer;
	}
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}
