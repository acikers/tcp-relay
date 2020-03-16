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
int prepare_member(int, uint8_t *, const uint32_t *);
int prepare_last_member(int);

int send_packages(FILE *, int, int, uint32_t, uint32_t, uint16_t, uint16_t);

int fill_next_packet(struct packet_data *);

int set_cpu_affinity(void);

// Just because usage should be first
void print_usage(char *argv[]) {
	fprintf(stderr, "Usage: %s [-c count] [-f freq] [-i input_port] [-o output_port] [-r resfile.csv]\n", argv[0]);
	fprintf(stderr, "count and freq works only for copy of program which only has output socket, and doesn't have input socket\n");
	fprintf(stderr, "resfile - only valid for last app in chain\n");
}


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
	retval = set_cpu_affinity()
	if (retval == -1)
	{
		return retval;
	}
#endif

	// Prepare output socket
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
		uint8_t ready_flag = 0;
		retval = prepare_member(so_accepted, &ready_flag, &count);
		if (retval == -1) {
			perror("prepare_member()");
			close(so_accepted);
			close(so);
			return -1;
		}
		// Send ready_flag to previous member
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

		retval = prepare_last_member(si);
	}
	
	retval = send_packages(resfile, si, so_accepted, frequency, count, in_port, out_port);
	if (retval == -1)
	{
		perror("send_packages()");
	}

	if (respath) free(respath);

	if (resfile) fclose(resfile);
	if (si) close(si);
	if (so_accepted) close(so_accepted);
	if (so) close(so);
	return 0;
}

int send_packages(FILE* resfile, int in_fd, int out_fd, uint32_t freq, uint32_t count, uint16_t in_port, uint16_t out_port) {
	struct packet_data *buf = NULL;	// buffer for package
	struct timespec freq_ts;
	freq_ts.tv_nsec = 1000000000.0/freq;
	freq_ts.tv_sec = 0;
	int ps_ret = 0, recval = 0, retval =0;
	fd_set fds;
	
	for (uint32_t cur_count = 0; cur_count < count; cur_count++) {
		// Receive package and send new ts
		if (in_port) {
			if (!buf) {
				buf = (struct packet_data *)malloc(MSGLEN);
				bzero(buf, MSGLEN);
			}


			FD_ZERO(&fds);
			FD_SET(in_fd, &fds);
			for (int cur_len = 0; cur_len < MSGLEN; cur_len += recval>0?recval:0)
			{
#if NOBLOCK == 1
				ps_ret = pselect(in_fd+1, &fds, NULL, NULL, NULL, NULL);
				if (ps_ret > 0) {
					if (FD_ISSET(in_fd, &fds)) {
#endif
						recval = recv(in_fd, (void*)buf+cur_len, MSGLEN, 0);
						if (recval == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
							perror("recv()");
							fprintf(stderr, "[%d] cur_len: %d\n", in_port, cur_len);
							break;
						} else if (recval == 0) {
							break;
						}
#if NOBLOCK == 1
					}
				} else break;
#endif
			}
			if (recval == 0) {
				fprintf(stderr, "in_fd closed\n");
				break;
			} else if (recval == -1) {
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
			FD_SET(out_fd, &fds);
			ps_ret = pselect(out_fd+1, NULL, &fds, NULL, NULL, NULL);
			if (ps_ret > 0) {
				if (FD_ISSET(out_fd, &fds)) {
					retval = send(out_fd, buf, MSGLEN, 0);
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
			// And should write to result file
			struct packet_data *pd = buf; // some sugar
			struct timespec ts = {0};
			clock_gettime(CLOCK_MONOTONIC, &ts);
			fprintf(resfile, "%ld,%ld,%ld,%ld,%ld\n", pd[0].ts.tv_sec, pd[0].ts.tv_nsec, ts.tv_sec, ts.tv_nsec,
					(ts.tv_sec - pd[0].ts.tv_sec) * 1000000000 + (ts.tv_nsec - pd[0].ts.tv_nsec));

		}
		bzero(buf, MSGLEN);
	}

	if (buf) free(buf);
	return 0;
}

int set_cpu_affinity(void) {
	int retval = 0;
#ifdef SCHEDCPU
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
#endif
	return retval;
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

inline int set_sock_opts(int sockfd) {
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , SO_REUSEADDR, ...)");
		return -1;
	}

#ifdef NODELAY
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , TCP_NODELAY, ...)");
		return -1;
	}
#endif
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

	if (set_sock_opts(ret) == -1) {
		close(ret);
		return -1;
	}

// #ifdef SCHEDCPU
// 	if (setsockopt(ret, SOL_SOCKET, SO_INCOMING_CPU, &(int){1}, sizeof(int)) == -1) {
// 		perror("setsockopt(... , SO_INCOMING_CPU, ...)");
// 		close(ret);
// 		return -1;
// 	}
// #endif

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

	if (set_sock_opts(ret) == -1) {
		close(ret);
		return -1;
	}


// #ifdef SCHEDCPU
// 	if (setsockopt(ret, SOL_SOCKET, SO_INCOMING_CPU, &(int){1}, sizeof(int)) == -1) {
// 		perror("setsockopt(... , SO_INCOMING_CPU, ...)");
// 		close(ret);
// 		return -1;
// 	}
// #endif

	if (connect(ret, (struct sockaddr *)ao, sizeof(struct sockaddr_in)) == -1) {
		perror("connect()");
		printf("ao->sin_port: %" PRIu16 "\n", port);
		close(ret);
		return -1;
	}

	return ret;
}

inline int set_nonblock(int fd) {
	int retval = 0;
#if NOBLOCK == 1
	retval = fcntl(so_fd, F_GETFL);
	if (retval == -1) {
		perror("fcntl(so_accepted, F_GETFL)");
		return -1;
	}
	retval = fcntl(so_fd, F_SETFL, retval|O_NONBLOCK);
	if (retval == -1) {
		perror("fcnlt(so_accepted, F_GETFL)");
		return -1;
	}
#endif
	return retval;
}

int prepare_member(int so_fd, uint8_t *ready_flag, const uint32_t *count) {
	int retval = send(so_fd, &count, sizeof(count), 0);
	if (retval == -1) {
		perror("send(..., count, ...)");
		return -1;
	}

	retval = recv(so_fd, ready_flag, sizeof(uint8_t), 0);
	if (retval == -1) {
		perror("recv(..., ready_flag, ...)");
		return retval;
	} else if (retval == 0) {
		fprintf(stderr, "socket closed\n");
		return retval;
	}
	if (set_nonblock(so_fd) == -1) {
		return -1;
	}
	return 0;
}

int prepare_last_member(int so_fd) {
	int retval = send(so_fd, &(uint8_t){1}, sizeof(uint8_t), 0);
	if (retval == -1) {
		perror("send(..., ready_flag(1), ...)");
		return -1;
	}
	if (set_nonblock(so_fd) == -1) {
		return -1;
	}
	return 0;
}
