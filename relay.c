#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAX_SOCKLEN 5
#define MSG_LEN 1024

struct packet_data {
	uint8_t num;
	struct timespec ts;
};

int create_output_sock(struct sockaddr_in *, uint16_t);
int create_input_sock(struct sockaddr_in *, uint16_t);

int fill_next_packet(struct packet_data *);


int main(int argc, char *argv[]) { 
	int so, si, so_accepted = 0;
	int retval;
	int so_insize = 0;
	uint8_t frequency = 100;
	uint16_t count = 1;
	uint16_t out_port = 0;
	uint16_t in_port = 0;
	struct sockaddr_in tcp_addr_out = {0};
	struct sockaddr_in tcp_addr_in = {0};

	int opt;
	while ((opt = getopt(argc, argv, "i:o:f:c:")) != -1) {
		size_t len = 0;
		switch (opt) {
		case 'i':
			in_port = (uint16_t)strtoul(optarg, NULL, 10);
			break;
		case 'o':
			out_port = (uint16_t)strtoul(optarg, NULL, 10);
			break;
		case 'f':
			frequency = (uint8_t)strtoul(optarg, NULL, 10);
			break;
		case 'c':
			count = (uint16_t)strtoul(optarg, NULL, 10);
			break;
		case '?':
			if (optopt == 'i' || optopt == 'o' || optopt == 'f' || optopt == 'c') {
				fprintf(stderr, "Option -%c requires an argument\n", optopt);
			} else if (isprint(optopt)) {
				fprintf(stderr, "Unknown option '-%c'\n", optopt);
			} else {
				fprintf(stderr, "Unknown option character '\\x%x'\n", optopt);
			}
			return -1;
		default:
			abort();
		}
	}

	if (!out_port && !in_port) {
		printf("no input or output socket. goodbye.\n");
		printf("Usage: %s [-c count] [-f freq] [-i input_port] [-o output_port]\n", argv[0]);
		printf("count and freq works only for copy of program which only has output socket, and doesn't have input socket\n");
		return -1;
	}

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
	if (in_port) {
		si = create_input_sock(&tcp_addr_in, in_port);
		if (si == -1) {
			return -1;
		}
		recv(si, &count, sizeof(count), 0);
	}
	if (out_port) send(so_accepted, &count, sizeof(count), 0);

	struct timespec freq_ts;
	for (uint16_t cur_count = 0; cur_count < count; cur_count++) {
		// Receive package and send new ts
		if (in_port) {
			if (!buf) {
				buf = (struct packet_data *)malloc(MSG_LEN);
				bzero(buf, MSG_LEN);
			}
			int retval = recv(si, buf, MSG_LEN, 0);
			if (retval == -1) {
				perror("recv()");
			} else if (retval == 0) {
				break;
			}
		}
		if (out_port) {
			if (!buf) {
				buf = (struct packet_data *) malloc(MSG_LEN);
				bzero(buf, MSG_LEN);
			}

			// First in chain should wait to create needed frequency
			if (!in_port) {
				freq_ts.tv_nsec = 1000000000.0/frequency;
				freq_ts.tv_sec = 0;
				nanosleep(&freq_ts, NULL);
			}

			if (fill_next_packet(buf) == -1) {
				perror("fill_new_ts()");
				break;
			}

			send(so_accepted, buf, MSG_LEN, 0);
		} else if (in_port) {
			struct packet_data *pd = buf;
			uint8_t pos = 0;
			struct timespec ts = {0};
			clock_gettime(CLOCK_MONOTONIC, &ts);
			for (pos = 0; pd[pos].num != 0; pos++) {
				printf("%" PRIu16 ",%" PRIu8 ",%ld,%ld,%ld\n",
						cur_count, pos, pd[pos].ts.tv_sec, pd[pos].ts.tv_nsec,
						(pos!=0) ? (pd[pos].ts.tv_sec - pd[pos-1].ts.tv_sec) * 1000000000 + (pd[pos].ts.tv_nsec - pd[pos-1].ts.tv_nsec) : 0);
			}
			printf("%" PRIu16 ",%" PRIu8 ",%ld,%ld,%ld\n", cur_count, pos, ts.tv_sec, ts.tv_nsec,
					(ts.tv_sec - pd[pos-1].ts.tv_sec) * 1000000000 + (ts.tv_nsec - pd[pos-1].ts.tv_nsec));

		}
		bzero(buf, MSG_LEN);
	}

	if (buf) free(buf);

	close(si);
	close(so_accepted);
	close(so);
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

	if (connect(ret, (struct sockaddr *)ao, sizeof(struct sockaddr_in)) == -1) {
		perror("connect()");
		printf("ao->sin_port: %" PRIu16 "\n", port);
		close(ret);
		return -1;
	}

	return ret;
}
