#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <getopt.h>
#include <stdarg.h>

#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>

#define DEFAULT_TIMEOUT 500
#define DNS_TIMEOUT 1000 /* micro sec */
#define DEFAULT_PING_DATA_SIZE 56
#define SIZE_ICMP_HDR ICMP_MINLEN /* FROM ip_icmp.h */
#define DEFAULT_INTERVAL 25


extern char *optarg;
extern int optind, opterr;
extern int h_errno;

char *icmp_type_str[19] = {
    "ICMP Echo Reply",          /* 0 */
    "",
    "",
    "ICMP Unreachable",         /* 3 */
    "ICMP Source Quench",       /* 4 */
    "ICMP Redirect",            /* 5 */
    "",
    "",
    "ICMP Echo",                /* 8 */
    "",
    "",
    "ICMP Time Exceeded",       /* 11 */
    "ICMP Parameter Problem",   /* 12 */
    "ICMP Timestamp Request",   /* 13 */
    "ICMP Timestamp Reply",     /* 14 */
    "ICMP Information Request", /* 15 */
    "ICMP Information Reply",   /* 16 */
    "ICMP Mask Request",        /* 17 */
    "ICMP Mask Reply"           /* 18 */
};

char *icmp_unreach_str[16] =
{
    "ICMP Network Unreachable",                                     /* 0 */
    "ICMP Host Unreachable",                                        /* 1 */
    "ICMP Protocol Unreachable",                                    /* 2 */
    "ICMP Port Unreachable",                                        /* 3 */
    "ICMP Unreachable (Fragmentation Needed)",                      /* 4 */
    "ICMP Unreachable (Source Route Failed)",                       /* 5 */
    "ICMP Unreachable (Destination Network Unknown)",               /* 6 */
    "ICMP Unreachable (Destination Host Unknown)",                  /* 7 */
    "ICMP Unreachable (Source Host Isolated)",                      /* 8 */
    "ICMP Unreachable (Communication with Network Prohibited)",     /* 9 */
    "ICMP Unreachable (Communication with Host Prohibited)",        /* 10 */
    "ICMP Unreachable (Network Unreachable For Type Of Service)",   /* 11 */
    "ICMP Unreachable (Host Unreachable For Type Of Service)",      /* 12 */
    "ICMP Unreachable (Communication Administratively Prohibited)", /* 13 */
    "ICMP Unreachable (Host Precedence Violation)",                 /* 14 */
    "ICMP Unreachable (Precedence cutoff in effect)"                /* 15 */
};

#define EV_TYPE_PING 1
#define EV_TYPE_TIMEOUT 2

typedef struct host_entry {
	char *name; /* name as given by user*/
	char *host; /* text description of host*/
	int	timeout; /* time to wait for response */
	unsigned char running; /* unset when through sending*/
	unsigned char waiting; /* waiting for response*/
	int min_reply; /* shortest response time */
	struct host_entry *ev_prev;
	struct host_entry *ev_next;
	struct timeval ev_time;
	int ev_type; /* event type */

	int i; /* index into table*/
}HOST_ENTRY; 

HOST_ENTRY *rrlist = NULL;
HOST_ENTRY **table = NULL;

HOST_ENTRY *ev_first;
HOST_ENTRY *ev_last;

char *prog;
int ident; /* our pid */
int s; /* socket */

/* times get *100 because all times are calculated in 10 usec units, not ms */
unsigned int timeout = DEFAULT_TIMEOUT * 100;
unsigned int ping_pkt_size;
unsigned int ping_data_size = DEFAULT_PING_DATA_SIZE;
unsigned int interval = DEFAULT_INTERVAL * 100;

/* global stas */
int max_hostname_len = 0;
long min_reply = 10000000;
int num_hosts; /* total number of hosts */
int num_noaddress = 0; /* total number of addresses not found */
int num_jobs = 0; /* number of hosts still to do */
struct timeval current_time
struct timeval start_time;
struct timeval end_time;
struct timezone tz;
struct timeval last_send_time; /* time last ping was sent*/

/* switches */
int generate_flag = 0; 

void crash_and_burn(char *message);
void errno_crash_and_burn(char *message);
void usage(int is_error); 
void add_cidr(char * addr);
void add_range(char *, char *);
void add_name (char *name );
void u_sleep(int u_sec);
void ev_enqueue(HOST_ENTRY *h);
HOST_ENTRY *ev_dequeue();
HOST_ENTRY *ev_remove(HOST_ENTRY *h);
void print_warning(char *format, ...);
void finish();

int main ( int argc, char **argv) {
	
	prog = argv[0];
	
	struct protoent *proto;

	HOST_ENTRY * cursor;
	
	if( ( proto = getprotobyname("icmp") ) == NULL)
		
		crash_and_burn("icmp: unknown protocol");
		
	s = socket(AF_INET, SOCK_RAW, proto->p_proto);

	if(s < 0)
		errno_crash_and_burn("can't create raw socket");

	if( ( uid = getuid())) 
		
		seteuid(getuid());

	ident = getpid() & 0xFFFF;

	while( ( c = getopt(argc, argv, "g")) != EOF) {

		switch( c ) {

			case 'g':
				generate_flag = 1;
				break;

			default:
				usage(1);
				break;
		}
	}
	argv = argv[optind];
	argc -= optind;

	if(*argv && generate_flag) {
		if(argc == 1)
			add_cidr(argv[0]);
		else if(argc == 2)
			add_range(argv[0], argv[1]);
		else 
			usage(1);
	}

	table = (HOST_ENTRY **)malloc(sizeof(HOST_ENTRY *) * num_hosts);
	if(!table )
		crash_and_burn("Can't malloc array of hosts");

	cursor = ev_first;

	for(num_jobs = 0; num_jobs < num_hosts; num_jobs++) {
		table[num_jobs] = cursor;
		cursor->i =num_jobs;
		cursor = cursor->ev_next;
	}
	
	ping_pkt_size = ping_data_size + SIZE_ICMP_HDR;

	signal(SIGINT, finish);

	gettimeofday(&start_time, &tz);
	current_time = start_time;

	last_send_time.tv_sec = current_time.tv_sec - 10000;

	main_loop();
	finish();
	return 0;
} /* main() */

void crash_and_burn(char *message) {
	
	fprintf(stderr, "%s: %s\n",prog, message);
	
	exit(4);
}

void errno_crash_and_burn(char *message) {
	fprintf(stderr, "%s: %s : %s\n", prog, message, strerror(errno));
	exit(4);
}

void usage(int is_error) {
	FILE *out = is_error ? stderr: stdout;
	fprintf(out, "\n");
	fprintf(out, "-g	generate target list\n");
	fprintf(out, "		(specify the start and end IP in the targe list, or supply a IP netmask)\n");
	exit(is_error);
}

void add_cidr(char * addr) {
	char *addr_end;
	char *mask_str;
	unsigned long mask;
	unsigned long bitmask;
	int ret;
	struct addrinfo addr_hints;
	struct addrinfo *addr_res;
	unsigned long net_addr;
	unsigned long net_last;

	addr_end = strchr(addr, "/");
	if(addr_end == NULL)
		usage(1);
	*addr_end = '\0';
	mask_str = addr_end + 1;
	mask = atoi(mask_str);
	if(mask < 1 || mask > 30 ) { 
		fprintf(stderr, "Error: netmask must be between 1 and 30(is: %s)\n",mask_str);
		exit(2);
	}

	memset(&addr_hints, 0, sizeof(struct addrinfo));
	addr_hints.ai_family = AF_UNSPEC;
	addr_hints.ai_flags = AI_NUMERICHOST;
	ret = getaddrinfo(addr, NULL, &addr_hints, &addr_res);
	if(ret) {
		fprintf(stderr, "Error: can't parse address %s: %s\n",addr, gai_strerror(ret));
		exit(2);
	}

	if(addr_res->ai_family != AF_INET) {
		fprintf(stderr, "Error: -g works only with IPv4 addresses\n");
		exit(2);
	}

	net_addr = ntohl(((struct sockaddr_in *)addr_res->ai_addr)->sin_addr.s_addr);

	bitmask = ((unsigned long)0xFFFFFFFF) << ( 32 - mask);

	/* net_addr and net_last are something like
	 * 192.168.1.0 and 192.168.1.255 in binary format*/
	net_addr &= bitmask;

	net_last = net_addr + ((unsigned long)0x1 << (32 - mask)) -1; /* now net_last is 255 */

	while(++net_addr < net_last) {

		struct in_addr in_addr_temp;
		char buffer[20];
		in_adr_temp.s_addr = htonl(net_addr);
		inet_ntop(AF_INET, &in_addr_temp, buffer, sizeof(buffer));
		add_name(buffer); /* now buffer should be a dotted decimal IP address */
	}
	freeaddrinfo(addr_res);
}

void add_addr(char *name, char *host, struct in_addr ipaddr) {

	HOST_ENTRY *p;

	int n, *i;

	p = (HOST_ENTRY *)malloc(sizeof(HOST_ENTRY));

	if(!p)
		crash_and_burn("can't allocate HOST_ENTRY");

	memset((char *)p, 0, sizeof(HOST_ENTRY));

	p->name = strdup(name); /* name is given by user*/
	p->host = strdup(host); /* text description of host*/

	p->saddr.sin_family = AF_INET;
	p->saddr.sin_addr = ipaddr;

	p->timeout = timeout;
	p->running = 1;
	p->min_reply = 10000000;

	if(strlen(p->host) > max_hostname_len)
		max_hostname_len = strlen(p->host);

	p->ev_type = EV_TYPE_PING;
	p->ev_time.tv_sec = 0;
	p->ev_time.tv_usec = 0;
	ev_enqueue(p);

	num_hosts++;
}

void add_name (char *name ) {
	struct hostent *host_ent;
	unsigned int ipaddress;
	struct in_addr *ipa = (struct in_addr *)&ipaddress;
	struct in_addr *host_add;
	char *nm;
	int i = 0;

	if( (ipaddress = inet_addr(name)) != -1) {

		add_addr(name, name, *ipa);

		return;
	}

	host_ent = gethostbyname( name );
	if( host_ent == NULL) {
		/* The variable h_errno can have the TRY_AGAIN value
		 * means a temporary error occurred on an 
		 * authoritative name server. Try again. */
		if(h_error == TRY_AGAIN) {
			u_sleep(DNS_TIMEOUT);
			host_ent = gethostbyname(name);
		}
		if( host_ent == NULL) {
			print_warning("%s address not found\n",name);
			num_noaddress++;
			return;
		}
	}

	if(host_ent->h_addrtype != AF_INET) {
		print_warning("%s: IPv6 address returned by gethostbyname(options inet6 in resolv.conf?)\n",name);
		num_noaddress++;
		return ;
	}

	host_add = (struct in_addr *)*(host_ent->h_addr_list);
	if(host_addr == NULL) {
		print_warning("%s has no address data\n",name);
		num_noaddress++;
		return;
	}
	else {
		while(host_add) {
			add_addr(name,name, *host_addr);
			host_add = (struct in_addr*)(host_ent->h_addr_list[++i]);
		}
	}

}

void print_warning(char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void u_sleep(int u_sec) {
	int nfound;
	struct timeval to;
	fd_set readset, writeset;

select_agian:
	to.tv_sec = u_sec / 1000000;
	to.tv_usec = u_sec - (to.tv_sec * 1000000);

	FD_ZERO(&readset);
	FD_ZERO(&writeset);

	nfound = select(0, &readset, &writeset, NULL, &to);

	if(nfound < 0) {
		if(errno == EINTR) 
			goto select_again;
		else 
			errno_crash_and_burn("select");
	}
	return;
}

void ev_enqueue(HOST_ENTRY *h) {

	HOST_ENTRY *i;
	HOST_ENTRY *i_prev;

	if(ev_last == NULL) {
		h->ev_next = NULL;
		h->ev_prev = NULL;
		ev_first = h;
		ev_last = h;
		return;
	}

	if(h->ev_time.tv_sec > ev_last.tv_sec || (h->ev_time.tv_sec == ev_last->ev_time.tv_sec && h->ev_time.tv_usec >= ev_last->ev_time.tv_usec)) {
		h->ev_next = NULL;
		h->ev_prev = ev_last;
		ev_last->ev_next = h;
		ev_last = h;
		return;
	}

	i = ev_last;
	while(1) {

		i_prev = i->ev_prev;

		if(i_prev == NULL || h->ev_time.tv_sec > i_prev->ev_time.tv_sec || (h->ev_time.tv_sec == i_prev->ev_time.tv_sec && h->ev_time.tv_usec >= i_prev->ev_time.tv_usec)) {

			h->ev_prev = i_prev;

			h->ev_next = i;

			i->ev_prev = h;

			if(i_prev != NULL) {

				i_prev->ev_next = h;

			}
			else 

				ev_first = h;

			return;
		}

		i = i_prev;
	}
}

void finish() {
	int i;
	HOST_ENTRY *h;

	gettimeofday(&end_time, &tz);

	if(num_noaddress) 
		exit(2);
	exit(0)
}

void add_range( char *start, char *end) {

	struct addrinfo addr_hints;
	
	struct addrinfo *addr_res;
	
	unsigned long start_long;

	unsigned long end_long;

	int ret;

	memset(&addr_hints, 0, sizeof(struct addrinfo));

	addr_hints.ai_family = AF_UNSPEC;

	addr_hints.ai_flags = AI_NUMERICHOST;

	/* int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);*/
	ret = getaddrinfo(start, NULL, &addr_hints, &addr_res);

	if(ret) {

		fprintf(stderr, "Error: can't parse address %s: %s\n", start, gai_strerror(ret));

		exit(2);
	}

	if(addr_res->ai_family != AF_INET) {

		fprintf(stderr, "Error, -g works only with IPv4 addresses\n");

		exit(2);
	}
	
	/* struct sockaddr *ai_addr */
	start_long = ntohl(((struct sockaddr_in *)addr_res->ai_addr)->sin_addr.s_addr);

	memset(&addr_hints, 0, sizeof(struct addrinfo));

	addr_hints.ai_family = AF_UNSPEC;

	addr_hints.ai_flags = AI_NUMERICHOST;

	ret = getaddrinfo(end, NULL, &addr_hints, &addr_res);

	if(ret) {

		fprintf(stderr, "Error: can't parse address %s: %s\n", end, gai_strerror(ret));

		exit(2);
	}

	if(addr_res->ai_family != AF_INET) {

		fprintf(stderr, "Error, -g works only with IPv4 addresses\n");

		exit(2);
	}

	end_long = ntohl(((struct sockaddr_in)addr_res->ai_addr)->sin_addr.s_addr);

	/* generate */

	while(start_long <= end_long) {

		struct in_addr in_addr_tmp;

		char buffer[20];

		in_addr_tmp.s_addr = htonl(start_long);

		/*inet_ntop - convert IPv4 and IPv6 addresses from binary to text form */
		inet_ntop(AF_INET, &in_addr_tmp, buffer, sizeof(buffer));

		add_name(buffer);

		start_long++;
	}

}

void main_loop() {

	long lt;

	long wait_time;
	
	HOST_ENTRY *h;

	while(ev_first) {

		/* current_time is a global variable 
		 * right before main_loop() in line 202*/
		if(ev_first->ev_time.tv_sec < current_time.tv_sec || (ev_first->ev_time.tv_sec == current_time.tv_sec && ev_first->ev_time.tv_usec < current_time.tv_usec)) {
			
			if(ev_first->ev_type == EV_TYPE_PING) {

				lt = timeval_diff(&current_time, &last_send_time);

				/**/
				if(lt < interval ) goto wait_for_reply;

				h = ev_dequeue();

				/* Send the ping
				 * printf("Sending ping after %d ms\n",lt/100);*/
				if(!send_ping(s,h)) goto wait_for_reply;
			}
			else if(ev_first->ev_type == EV_TYPE_TIMEOUT) {

				num_timeout++;

				remove_job(ev_first);
			}
		}

wait_for_reply:

		/* when can we expect the next event */
		if(ev_first) {

			if(ev_first->ev_time.tv_sec == 0) {

				wait_time = 0;
			}
			else {
				wait_time = timeval_diff(&ev_first->ev_time, &current_time);
				if(wait_time < 0) 

					wait_time = 0;
			}

			if(ev_first->ev_type == EV_TYPE_PING) {

				if(wait_time < interval) {

					lt = timeval_diff(&current_time, &last_send_time);

					if(lt < interval)

						wait_time = interval-lt;
					else
						wait_time = 0;
				}
			}
		}

		else 
			wait_time = interval;

		if(wait_for_reply(wait_time)) {

			while(wait_for_reply(0))
				;
		}
		gettimeofday(&current_time, &tz);
	}
}



