#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>

#define MAX_SOCKLEN 1024
#define MSG_LEN 1024

struct packet_data {
	uint8_t num;
	struct timespec ts;
};

int create_output_sock(struct sockaddr *, char *);
int create_input_sock(struct sockaddr *, char *);

struct packet_data *find_place_for_data(struct packet_data *);		// Return address with empty place
struct packet_data *find_last_data(struct packet_data *);		// Return address where struct timeval begin
struct packet_data *find_next_data(struct packet_data *);		// Return address of next data in package
int fill_next_ts(struct packet_data *);



int main(int argc, char *argv[]) { 
	int so, si, so_accepted = 0;
	int retval;
	int so_insize = 0;
	char *si_name = NULL, *so_name = NULL;
	struct sockaddr addr_out, addr_in;
	fd_set rfds;

	int opt;
	while ((opt = getopt(argc, argv, "i:o:")) != -1) {
		size_t len = 0;
		switch (opt) {
		case 'i':
			len = strlen(optarg);
			si_name = (char *)malloc(strlen(optarg));
			si_name = strcpy(si_name, optarg);
			printf("si_name: %s\n", si_name);
			break;
		case 'o':
			len = strlen(optarg);
			so_name = (char *)malloc(strlen(optarg));
			so_name = strcpy(so_name, optarg);
			printf("so_name: %s\n", so_name);
			break;
		}
	}

	if (so_name == NULL && si_name == NULL) {
		printf("no input or output socket. goodbye.\n");
		printf("Usage: %s [-i input_socket] [-o output_socket]\n", argv[0]);
		return -1;
	}

	struct packet_data *buf = NULL;	// buffer for package
	if (so_name != NULL) {
		so = create_output_sock(&addr_out, so_name);
		if (so == -1) {
			return -1;
		}
		printf("prepare to accept\n");
		so_accepted = accept(so, &addr_out, &so_insize);
		printf("accepted\n");
		if (so_accepted == -1) {
			perror("accept()");
			close(so);
			return -1;
		}

	}
	if (si_name != NULL) {
		si = create_input_sock(&addr_in, si_name);
		if (si == -1) {
			return -1;
		}
	}

	// Receive package and send new ts
	if (si_name != NULL) {
		if (buf == NULL) {
			buf = (struct packet_data *)malloc(MSG_LEN);
			bzero(buf, MSG_LEN);
		}
		if (recv(si, buf, MSG_LEN, 0) != MSG_LEN) {
			perror("recv()");
			free(buf);
			close(si);
			return -1;
		}
	}
	if (so_name != NULL) {
		if (buf == NULL) {
			buf = (struct packet_data *) malloc(MSG_LEN);
			bzero(buf, MSG_LEN);
		}
		if (fill_next_ts(buf) == -1) {
			perror("fill_new_ts()");
			close(so);
			close(so_accepted);
			free(buf);
		}

		send(so_accepted, buf, MSG_LEN, 0);
	} else  if (si_name != NULL){
		struct timespec ts_start, ts_end;
		memcpy(&ts_start, &(find_last_data(buf)->ts), sizeof(struct timespec));


		printf("t: %ld\n", ts_start.tv_sec);
		printf("time: %s", ctime(&ts_start.tv_sec));
		printf("nanosecs: %ld\n", ts_start.tv_nsec);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		printf("et: %ld\n", ts_end.tv_sec);
		printf("etime: %s\n", ctime(&ts_end.tv_sec));
		printf("enanosecs: %ld\n", ts_end.tv_nsec);
		printf("diff: %lf\n", difftime(ts_end.tv_sec, ts_start.tv_sec));
		printf("diff_nanos: %ld\n", ts_end.tv_nsec - ts_start.tv_nsec);
	}

	if (si_name) free(si_name);
	if (so_name) free(so_name);
	if (buf) free(buf);

	close(si);
	close(so_accepted);
	close(so);
	return 0;
}


int fill_next_ts(struct packet_data *packet) {
	struct packet_data *n_data = find_place_for_data(packet);
	if (n_data == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &(n_data->ts)) == -1) {
		return -1;
	}
	n_data->num = 1;

	return 0;
}

inline struct packet_data *find_next_data(struct packet_data *packet) {
	if (packet == NULL) {
		return NULL;
	}
	return packet++;
}

inline struct packet_data *find_last_data(struct packet_data *packet) {
	struct packet_data *ret = NULL;
	for (ret = packet; ret != NULL && (ret+sizeof(struct packet_data))->num != 0; ret++) {}
	return ret++;
}

inline struct packet_data *find_place_for_data(struct packet_data *packet) {
	struct packet_data *ret = NULL;
	for (ret = packet; ret != NULL && (ret+sizeof(struct packet_data))->num != 0; ret ++) {}
	return ret;
}

int create_output_sock(struct sockaddr *ao, char *path) {
	int ret = 0;

	ret = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ret == -1) {
		perror("socket()");
		return ret;
	}

	memset(ao, '\0', sizeof(struct sockaddr));
	ao->sa_family = AF_UNIX;
	strncpy(ao->sa_data, path, sizeof(ao->sa_data) - 1);

	if (bind(ret, ao, sizeof(struct sockaddr)) == -1) {
		perror("bind()");
		return -1;
	}

	if (listen(ret, MAX_SOCKLEN) == -1) {
		perror("listen()");
		close(ret);
		return -1;
	}


	return ret;
}

int create_input_sock(struct sockaddr *ao, char *path) {
	int ret = 0;

	ret = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ret == -1) {
		perror("socket()");
		return ret;
	}

	memset(ao, '\0', sizeof(struct sockaddr));
	ao->sa_family = AF_UNIX;
	strncpy(ao->sa_data, path, sizeof(ao->sa_data) - 1);

	if (connect(ret, ao, sizeof(struct sockaddr)) == -1) {
		perror("connect()");
		return -1;
	}

	return ret;
}
