#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>

#define MAX_SOCKLEN 5

#ifndef MSGLEN
#define MSGLEN 1024
#endif

#ifndef NOBLOCK
#define NOBLOCK 0
#endif

struct packet_data {
	uint8_t num;
	struct timespec ts;
};

int create_output_sock(struct sockaddr_in *, uint16_t);
int create_input_sock(struct sockaddr_in *, uint16_t);

int fill_next_packet(struct packet_data *);

void print_usage(char *argv[]);


int main(int argc, char *argv[]) { 
	int so = 0, si = 0, so_accepted = 0;
	int retval;
	int so_insize = 0;
	char *respath = NULL;
	FILE *resfile = NULL;
	uint32_t frequency = 100;
	uint32_t count = 1;
	uint16_t out_port = 0;
	uint16_t in_port = 0;
	struct sockaddr_in tcp_addr_out = {0};
	struct sockaddr_in tcp_addr_in = {0};

	int opt;
	while ((opt = getopt(argc, argv, "i:o:f:c:r:")) != -1) {
		size_t len = 0;
		switch (opt) {
		case 'i':
			in_port = (uint16_t)strtoul(optarg, NULL, 10);
			break;
		case 'o':
			out_port = (uint16_t)strtoul(optarg, NULL, 10);
			break;
		case 'f':
			frequency = (uint32_t)strtoul(optarg, NULL, 10);
			break;
		case 'c':
			count = (uint32_t)strtoul(optarg, NULL, 10);
			break;
		case 'r':
			respath = strdup(optarg);
			break;
		case '?':
			if (optopt == 'i' || optopt == 'o' || optopt == 'f' || optopt == 'c') {
				fprintf(stderr, "Option -%c requires an argument\n", optopt);
			} else if (isprint(optopt)) {
				fprintf(stderr, "Unknown option '-%c'\n", optopt);
			} else {
				fprintf(stderr, "Unknown option character '\\x%x'\n", optopt);
			}
			print_usage(argv);
			return -1;
		default:
			abort();
		}
	}

	if (!out_port && !in_port) {
		fprintf(stderr, "no input or output socket. goodbye.\n");
		print_usage(argv);
		return -1;
	}

#ifdef SCHEDCPU
	{
		char *cpus = strdup(SCHEDCPU);
		uint8_t cpuid;
		cpu_set_t mask;
		CPU_ZERO(&mask);
		char *c_cpu = strtok(cpus, ",");
		if (c_cpu == NULL) { goto endsched; }
		do {
			cpuid = (uint8_t)strtoul(c_cpu, NULL, 10);
			CPU_SET(cpuid, &mask);
		} while (c_cpu = strtok(NULL, " "));
		retval = sched_setaffinity(0, sizeof(mask), &mask);
		if (retval == -1) {
			perror("sched_setaffinity()");
		}
endsched:
		free(cpus);
	}
#endif

	// Prepare output socket
	struct packet_data *buf = NULL;	// buffer for package
	if (out_port) {
		so = create_output_sock(&tcp_addr_out, out_port);
		if (so == -1) {
			return -1;
		}
		so_accepted = accept(so, (struct sockaddr *)&tcp_addr_out, &so_insize);
		if (so_accepted == -1) {
			perror("accept()");
			close(so);
			return -1;
		}

	}
	// Prepare input socket
	if (in_port) {
		si = create_input_sock(&tcp_addr_in, in_port);
		if (si == -1) {
			return -1;
		}
		recv(si, &count, sizeof(count), 0);
	}
	// Send count of packages before translation begins
	// And wait till other sockets become ready
	if (out_port) {
		retval = send(so_accepted, &count, sizeof(count), 0);
		if (retval == -1) {
			perror("send(..., count, ...)");
			close(so_accepted);
			close(so);
			return -1;
		}

		uint8_t ready_flag = 0;
		retval = recv(so_accepted, &ready_flag, sizeof(uint8_t), 0);
		if (retval == -1) {
			perror("recv(..., ready_flag, ...)");
			close(so_accepted);
			close(so);
			return retval;
		} else if (retval == 0) {
			fprintf(stderr, "socket closed\n");
			close(so_accepted);
			close(so);
			return retval;
		}
#if NOBLOCK == 1
		retval = fcntl(so_accepted, F_GETFL);
		if (retval == -1) {
			perror("fcntl(so_accepted, F_GETFL)");
			close(so);
			return -1;
		}
		retval = fcntl(so_accepted, F_SETFL, retval|O_NONBLOCK);
		if (retval == -1) {
			perror("fcnlt(so_accepted, F_GETFL)");
			close(so);
			return -1;
		}
#endif
		if (si) {
			retval = send(si, &ready_flag, sizeof(uint8_t), 0);
			if (retval == -1) {
				perror("send(..., ready_flag, ...)");
				close(so_accepted);
				close(so);
				close(si);
				return -1;
			}
		}
	}
	else {
		resfile = fopen(respath, "w");
		if (resfile == NULL) {
			perror("fopen()");
			if (respath) free(respath);
			close(si);
			return -1;
		}

		retval = send(si, &(uint8_t){1}, sizeof(uint8_t), 0);
		if (retval == -1) {
			perror("send(..., ready_flag(1), ...)");
			close(si);
			return retval;
		}
#if NOBLOCK == 1
		retval = fcntl(si, F_GETFL);
		if (retval == -1) {
			perror("fcntl(si, F_GETFL)");
			close(si);
			return -1;
		}
		retval = fcntl(si, F_SETFL, retval|O_NONBLOCK);
		if (retval == -1) {
			perror("fcnlt(si, F_GETFL)");
			close(si);
			return -1;
		}
#endif
	}

	struct timespec freq_ts;
	freq_ts.tv_nsec = 1000000000.0/frequency;
	freq_ts.tv_sec = 0;
	int ps_ret = 0;
	int recval = 0;
	fd_set fds;
	for (uint32_t cur_count = 0; cur_count < count; cur_count++) {
		// Receive package and send new ts
		if (in_port) {
			if (!buf) {
				buf = (struct packet_data *)malloc(MSGLEN);
				bzero(buf, MSGLEN);
			}

			FD_ZERO(&fds);
			FD_SET(si, &fds);
			for (int cur_len = 0; cur_len < MSGLEN; cur_len += recval>0?recval:0)
			{
				ps_ret = pselect(si+1, &fds, NULL, NULL, NULL, NULL);
				if (ps_ret > 0) {
					if (FD_ISSET(si, &fds)) {
						recval = recv(si, (void*)buf+cur_len, MSGLEN, 0);
						if (recval == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
							perror("recv()");
							break;
						} else if (recval == 0) {
							perror("recv()[closed]:");
						}
					}
				} else break;
			}
			if (recval == 0) {
				fprintf(stderr, "si closed\n");
				break;
			}
		}
		if (out_port) {
			if (!buf) {
				buf = (struct packet_data *) malloc(MSGLEN);
				bzero(buf, MSGLEN);
			}

			if (fill_next_packet(buf) == -1) {
				perror("fill_new_ts()");
				break;
			}

			FD_ZERO(&fds);
			FD_SET(so_accepted, &fds);
			ps_ret = pselect(so_accepted+1, NULL, &fds, NULL, NULL, NULL);
			if (ps_ret > 0) {
				if (FD_ISSET(so_accepted, &fds)) {
					retval = send(so_accepted, buf, MSGLEN, 0);
					if (retval == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
						fprintf(stderr, "send(..., buf, ...) out_port:%" PRIu16" \n", out_port);
						perror("send(..., buf, ...)");
						break;
					}
// 					fprintf(stderr, "send[%" PRIu16 "]\n", out_port);
				}
			}
			// First in chain should wait to create needed frequency
			if (!in_port) {
				nanosleep(&freq_ts, NULL);
			}
		} else {
			// Last in chain has only input
			// And should write 
			struct packet_data *pd = buf;
			uint8_t pos = 0;
			struct timespec ts = {0};
			clock_gettime(CLOCK_MONOTONIC, &ts);
			fprintf(resfile, "%ld,%ld,%ld,%ld,%ld\n", pd[0].ts.tv_sec, pd[0].ts.tv_nsec, ts.tv_sec, ts.tv_nsec,
					(ts.tv_sec - pd[0].ts.tv_sec) * 1000000000 + (ts.tv_nsec - pd[0].ts.tv_nsec));

		}
		bzero(buf, MSGLEN);
	}

	if (buf) free(buf);
	if (respath) free(respath);

	if (resfile) fclose(resfile);
	if (si) close(si);
	if (so_accepted) close(so_accepted);
	if (so) close(so);
	return 0;
}


int fill_next_packet(struct packet_data *packet) {
	uint8_t num;
	for (num = 0; packet[num].num != 0; num++) {}
	packet[num].num = num+1;
	if (clock_gettime(CLOCK_MONOTONIC, &(packet[num].ts)) == -1) {
		return -1;
	}

	return 0;
}

int create_output_sock(struct sockaddr_in *ao, uint16_t port) {
	int ret = 0;

	memset(ao, '\0', sizeof(struct sockaddr));
	ao->sin_family = AF_INET;
	ao->sin_port = htons(port);
	ao->sin_addr.s_addr = INADDR_ANY;

	ret = socket(ao->sin_family, SOCK_STREAM, IPPROTO_TCP);
	if (ret == -1) {
		perror("socket()");
		return ret;
	}

	if (setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , SO_REUSEADDR, ...)");
		close(ret);
		return -1;
	}

#ifdef NODELAY
	if (setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , TCP_NODELAY, ...)");
		close(ret);
		return -1;
	}
#endif

//	if (setsockopt(ret, SOL_SOCKET, SO_INCOMING_CPU, &(int){1}, sizeof(int)) == -1) {
//		perror("setsockopt(... , SO_INCOMING_CPU, ...)");
//		close(ret);
//		return -1;
//	}

	if (bind(ret, (struct sockaddr *)ao, sizeof(struct sockaddr_in)) == -1) {
		perror("bind()");
		printf("ao->sin_port: %" PRIu16 "\n", port);
		close(ret);
		return -1;
	}

	if (listen(ret, MAX_SOCKLEN) == -1) {
		perror("listen()");
		close(ret);
		return -1;
	}


	return ret;
}

int create_input_sock(struct sockaddr_in *ao, uint16_t port) {
	int ret = 0;

	memset(ao, '\0', sizeof(struct sockaddr));
	ao->sin_family = AF_INET;
	ao->sin_port = htons(port);
	ao->sin_addr.s_addr = INADDR_ANY;

	ret = socket(ao->sin_family, SOCK_STREAM, IPPROTO_TCP);
	if (ret == -1) {
		perror("socket()");
		return ret;
	}

	if (setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , SO_REUSEADDR, ...)");
		close(ret);
		return -1;
	}

#ifdef NODELAY
	if (setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , TCP_NODELAY, ...)");
		close(ret);
		return -1;
	}
#endif

//	if (setsockopt(ret, SOL_SOCKET, SO_INCOMING_CPU, &(int){1}, sizeof(int)) == -1) {
//		perror("setsockopt(... , SO_INCOMING_CPU, ...)");
//		close(ret);
//		return -1;
//	}

	if (connect(ret, (struct sockaddr *)ao, sizeof(struct sockaddr_in)) == -1) {
		perror("connect()");
		printf("ao->sin_port: %" PRIu16 "\n", port);
		close(ret);
		return -1;
	}

	return ret;
}

void print_usage(char *argv[]) {
	fprintf(stderr, "Usage: %s [-c count] [-f freq] [-i input_port] [-o output_port] [-r resfile.csv]\n", argv[0]);
	fprintf(stderr, "count and freq works only for copy of program which only has output socket, and doesn't have input socket\n");
	fprintf(stderr, "resfile - only valid for last app in chain\n");
}
