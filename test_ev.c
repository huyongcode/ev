#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "ev.h"

int ncall = 0;
void time_cb(struct ev_loop *loop, struct ev_timer *self, void *data) {
	ncall++;
	printf("time event wait %lds\n", (long)data);
	if (ncall > 10) {
		if (self->type == EV_PERSIST)
			ev_del_timer(loop, self);
	}
}

void test_time() {
	struct ev_loop *loop;

	loop = ev_create_loop(10000);
	if (loop == NULL) {
		LOG(L_ERROR, "ev_create_loop failed\n");
		return;
	}
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	ev_add_timer(loop, (void *)3L, &tv, EV_NORMAL, time_cb);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	ev_add_timer(loop, (void *)1L, &tv, EV_PERSIST, time_cb);
	ev_process_loop(loop, NULL, 0);
	ev_destroy_loop(loop);
}

int pair[2];

int ecall = 0;
void write_cb(struct ev_loop *loop, int fd, void *data, int mask) {
	int n = write(fd, "abc", 4);
	if (n == -1)
		printf("write error %d: %s\n", errno, strerror(errno));
	else if (n == 0)
		printf("peer closed\n");
	else
		printf("write %d of 'abc' in %d \n", n, fd);

	ecall++;
	if (ecall > 2)
		ev_del_event(loop, fd, mask);
}

void read_cb(struct ev_loop *loop, int fd, void *data, int mask) {
	char buf[512];
	int n = read(fd, buf, 4);
	printf("read  %d of %s in %d\n", n, buf, fd);
}

void test_event() {
	struct ev_loop *loop;
	int n;
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) /* ignore the pipe singal */
		return;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		return;

	n = pair[0] > pair[1] ? pair[0] : pair[1];
	if (n < 10000)
		n = 10000;

	loop = ev_create_loop(n);
	if (loop == NULL) {
		LOG(L_ERROR, "ev_create_loop failed\n");
		return;
	}

	ev_set_event(loop, pair[0], 0, (void *)(long)pair[0], NULL, write_cb);
	ev_add_event(loop, pair[0], EV_WRITABLE);
	ev_set_event(loop, pair[1], 0, NULL, read_cb, NULL);
	ev_add_event(loop, pair[1], EV_READABLE);
	ev_process_loop(loop, NULL, 0);
	ev_destroy_loop(loop);
}


#define LISTEN_NUM 1
#define THREAD_NUM 2
#define CONNECT_NUM 70
#define START_PORT 8081
struct conn {
	int fd;
	struct ev *ev;
	struct sockaddr_in addr;
};

int fds[LISTEN_NUM];


int sock_write(int fd, void *buf, size_t size) {
	int ret;

	for (;;) {
		ret = write(fd, buf, size);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN) {
				return ret;
			} else {
				LOG(L_ERROR, "write socket %d %s. errno: %d\n",
						fd, strerror(errno), errno);
				return ret;
			}
		} else if (ret == 0) {
			LOG(L_ERROR, "socket %d's peer closed\n", fd);
			return ret;
		}
		return ret;
	}
	return ret;
}

int sock_blocking_write(int fd, void *buf, size_t size) {
	int ret;
	int nwrite = 0;
	while (nwrite < size) {
		ret = sock_write(fd, buf, size);
		if (ret <= 0) {
			if (errno == EAGAIN) {
				usleep(100);
				continue;
			}
			return ret;
		}
		nwrite += ret;
	}
	return nwrite;
}

int sock_read(int fd, void *buf, size_t size) {
	int ret;

	for (;;) {
		ret = read(fd, buf, size);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN)
				return ret;
			else {
				LOG(L_ERROR, "read socket %d %s. errno: %d\n",
						fd, strerror(errno), errno);
				return ret;
			}
		} else if (ret == 0) {
			LOG(L_ERROR, "soecket %d's peer closed\n", fd);
			return ret;
		} else {
			return ret;
		}
	}
	return -1;
}

int sock_blocking_read(int fd, void *buf, size_t size) {
	int ret;
	int nread = 0;
	while (nread < size) {
		ret = sock_read(fd, buf, size);
		if (ret <= 0) {
			if (errno == EAGAIN) {
				usleep(100);
				continue;
			}
			return ret;
		}
		nread += ret;
	}
	return nread;
}


int sock_svr_init() {
	unsigned short port = START_PORT;
	int i;
	struct sockaddr_in addr;

	for (i = 0; i < LISTEN_NUM; i++) {
		fds[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (fds[i] < 0) {
			LOG(L_ERROR, "socket failed\n");
			return 1;
		}
		set_nonblocking(fds[i]);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port + i);
		addr.sin_addr.s_addr = INADDR_ANY;
		if (bind(fds[i],
				(struct sockaddr *)&addr, sizeof(struct sockaddr)) == -1) {
			LOG(L_ERROR, "bind %d failed: %d %s\n", port + i,
					errno, strerror(errno));
			return 1;
		}
		if (listen(fds[i], 512) == -1) {
			LOG(L_ERROR, "listen %d failed: %d %s\n", port + i,
					errno, strerror(errno));
			return 1;
		}
		LOG(L_INFO, "listen socket %d port %d\n", fds[i], port + i);
	}
	return 0;
}

void svr_read_cb(struct ev_loop *loop, int fd, void *data, int mask) {
	char buf[512];
	int n = sock_read(fd, buf, 4);
	if (n == 0) {
		close(fd);
	}
	//printf("read  %d of %s in %d\n", n, buf, fd);
	//sock_write(fd, buf, 4);
}

void accept_cb(struct ev_loop *loop, int fd, void *data, int mask) {
	int client_fd;
	struct conn *conn = (struct conn *)malloc(sizeof(struct conn));
	if (conn == NULL) {
		LOG(L_ERROR, "malloc conn failed\n");
		return ;
	}
	socklen_t len = sizeof(struct sockaddr);
loop:
	client_fd = accept(fd, (struct sockaddr *)&conn->addr, &len 
			/*, SOCK_NONBLOCK | SOCK_CLOEXEC*/);
	if (client_fd < 0) {
		if (errno == EINTR) {
			goto loop;
		} else if (errno == EAGAIN) {
			LOG(L_ERROR, "thread %ld socket %d accept EAGAIN\n", 
					(long)pthread_self(), fd);
			return;
		}
		LOG(L_ERROR, "accept failed: %s errno: %d\n", strerror(errno), errno);
		return;
	}
	set_nonblocking(client_fd);
	LOG(L_INFO, "thread %ld new client %d\n", (long)pthread_self(), client_fd);

	conn->fd = client_fd;
	conn->ev = get_ev(loop, client_fd);

	loop->connections++;
	int total = loop->nev / THREAD_NUM;
	loop->disable_accept = total / 8 - (total - loop->connections); 

	ev_set_event(loop, client_fd, 0, conn, svr_read_cb, NULL);
	ev_add_event(loop, client_fd, EV_READABLE);
}

void *svr_loop(void *arg) {
	struct ev_loop *loop;
	loop = ev_create_loop(80);
	if (loop == NULL) {
		LOG(L_ERROR, "ev_create_loop failed\n");
		return NULL;
	}
	int i;
	for (i = 0; i < LISTEN_NUM; i++) {
		// listen conn be copies per thread
		struct conn *conn = (struct conn *)malloc(sizeof(struct conn));
		if (conn == NULL) {
			LOG(L_ERROR, "malloc conn failed\n");
			return NULL;
		}
		conn->fd = fds[i];
		conn->ev = get_ev(loop, conn->fd);
		ev_set_event(loop, conn->fd, 1, conn, accept_cb, NULL);
	}

	ev_process_loop(loop, fds, LISTEN_NUM);
	ev_destroy_loop(loop);
	return NULL;
}

void test_sock(int argc, char *argv[]) {
	int ret;

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) /* ignore the pipe singal */
		return;

	if (argv[1][0] == 's') {
		int i;
		if (sock_svr_init() != 0)
			return;

		pthread_t tid[THREAD_NUM];
		for (i = 0; i < THREAD_NUM; i++) {
			pthread_create(&tid[i], NULL, svr_loop, NULL);
			printf("create thread %ld\n", tid[i]);
		}
		for (i = 0; i < THREAD_NUM; i++) {
			pthread_join(tid[i], NULL);
		}
	} else {

		int i;
		int cfds[CONNECT_NUM];
		struct sockaddr_in addr;
		char buf[512];
		for (i = 0; i < CONNECT_NUM; i++) {
			cfds[i] = socket(AF_INET, SOCK_STREAM, 0);
			if (cfds[i] < 0) {
				LOG(L_ERROR, "socket failed\n");
				return;
			}
			addr.sin_family = AF_INET;
			addr.sin_port = htons(START_PORT + i % LISTEN_NUM);
			addr.sin_addr.s_addr = inet_addr("127.0.0.1");
			ret = connect(cfds[i], (struct sockaddr *)&addr,
					sizeof(struct sockaddr));
			if (ret == -1) {
				LOG(L_ERROR, "connect %s, errno: %d\n", strerror(errno), errno);
				return;
			}
			LOG(L_INFO, "socket %d connected port %d\n", cfds[i],
					START_PORT + i % LISTEN_NUM);
			
			sprintf(buf, "%d", cfds[i]);
			/*		
			ret = sock_blocking_write(cfds[i], &i, sizeof(int));
			if (ret <= 0) {
				close(cfds[i]);
				continue;
			}
			LOG(L_INFO, "socket %d, write %d, %d bytes\n", cfds[i], i, ret);
			ret = sock_blocking_read(cfds[i], buf, sizeof(int));
			if (ret <= 0) {
				close(cfds[i]);
				continue;
			}
			LOG(L_INFO, "socket %d, read %d, %d bytes\n", cfds[i], (int)buf, ret);
			*/
			sleep(1);
		}
	}

}
int main(int argc, char *argv[]) {
	//test_time();
	//test_event();
	test_sock(argc, argv);
	return 0;
}
