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
#include <sys/socket.h>

#define MAX_SOCKLEN 5
#define MSG_LEN 1024*1024

#ifndef BLOCK_INPUT
#define BLOCK_INPUT 0
#endif

#ifndef BLOCK_OUTPUT
#define BLOCK_OUTPUT 0
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

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(2, &mask);
	CPU_SET(3, &mask);
	retval = sched_setaffinity(0, sizeof(mask), &mask);
	if (retval == -1) {
		perror("sched_setaffinity()");
	}

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
#if BLOCK_OUTPUT == 0
		retval = fcntl(so, F_GETFL);
		if (retval == -1) {
			perror("fcntl(si, F_GETFL)");
			close(so);
			return -1;
		}
		retval = fcntl(so, F_SETFL, retval|O_NONBLOCK);
		if (retval == -1) {
			perror("fcnlt(si, F_GETFL)");
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
#if BLOCK_INPUT == 0
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
	for (uint32_t cur_count = 0; cur_count < count; cur_count++) {
		// Receive package and send new ts
		if (in_port) {
			if (!buf) {
				buf = (struct packet_data *)malloc(MSG_LEN);
				bzero(buf, MSG_LEN);
			}
			while (1) {
				retval = recv(si, buf, MSG_LEN, MSG_WAITALL);
				if (retval == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
					perror("recv()");
				} else if (retval >= 0) {
					break;
				}
			}
			if (retval == 0) break;
		}
		if (out_port) {
			if (!buf) {
				buf = (struct packet_data *) malloc(MSG_LEN);
				bzero(buf, MSG_LEN);
			}

			if (fill_next_packet(buf) == -1) {
				perror("fill_new_ts()");
				break;
			}

			while (1) {
				retval = send(so_accepted, buf, MSG_LEN, 0);
				if (retval == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
					perror("send(..., buf, ...)");
					break;
				} else if (retval >= 0) {
					break;
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
		bzero(buf, MSG_LEN);
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
	clock_gettime(CLOCK_MONOTONIC, &(packet[num].ts));

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

	if (setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , TCP_NODELAY, ...)");
		close(ret);
		return -1;
	}

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

	if (setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) == -1) {
		perror("setsockopt(... , TCP_NODELAY, ...)");
		close(ret);
		return -1;
	}

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
