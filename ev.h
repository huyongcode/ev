#ifndef EV_H_
#define EV_H_

#include <time.h>
#include <sys/time.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "log.h"
//#include "min_heap.h"

#define EV_NONE     0
#define EV_READABLE 1
#define EV_WRITABLE 2
#define EV_ET       4

#define EV_OK 0
#define EV_ERROR -1
#define EV_AGAIN -2
#define EV_CLOSE -3
#define MAX_PRIORITY 3

enum time_type {
	EV_NORMAL = 0,
	EV_PERSIST,
	EV_FIXED
};

enum ev_mode {
	M_EPOLL = 0,
	M_SELECT,
}

struct ev_loop;

typedef struct min_heap
{
    struct ev_timer** p;
    unsigned n, a;
} min_heap_t;

struct ev {
	int fd;  
	unsigned short mask; /* EV_READABLE | EV_WRITABLE */
	unsigned short priority;
	void (*event_cb)(struct ev_loop *loop, struct ev *ev, int mask);
	void *data;
};

struct ev_ready {
	struct ev *ev;
	int mask;
};

struct ev_ready_ctrl {
	struct ev_ready *evr;
	int count;
}

struct ev_timer {
	struct timeval ev_timeout;
	void (*timeout_cb)(struct ev_loop *loop, struct ev_timer *self, void *data);
	void *data;
	struct timeval time;
	unsigned int min_heap_idx;	
	enum time_type type;
};

struct ev_loop {
	int nev;
#ifdef EV_SELECT
	int max_fd;
#endif

	struct ev_ready_ctrl ready_evs[MAX_PRIORITY];

	struct timeval current_time;

	struct ev_state *state;
	min_heap_t timeheap;

	enum ev_mode mode;
};

struct ev_loop *ev_create_loop(int n);
void ev_destroy_loop(struct ev_loop *loop);
int ev_process_loop(struct ev_loop *loop, int *fast_fds, int fast_num);

int ev_init_event(
		stuct ev *ev, 
		int fd,
		int priority,
		void *data,
		void (*event_cb)(struct ev_loop *loop, struct ev *ev, int mask));
struct ev *ev_add_event(struct ev_loop *loop, int fd, int mask);
void ev_del_event(struct ev_loop *loop, int fd, int mask);
static inline void ev_set_event_priority(struct ev *ev, unsigned short priority) {
	ev->priority = priority;
}
static inline void ev_set_event_data(struct ev *ev, void *data) {
	ev->data = data;
}

void ev_init_timer(
		struct ev_timer *ev,
		void *data,
		struct timeval *tvp,
		enum time_type type,
		void (*timeout_cb)(struct ev_loop *loop, struct ev_timer *self, void *data));
int ev_add_timer(struct ev_loop *loop, struct ev_timer *ev);
void ev_del_timer(struct ev_loop *loop, struct ev_timer *ev);

static inline void ev_set_timer_data(struct ev_timer *ev, void *data) {
	ev->data = data;
}

static inline void set_nonblocking(int fd) {
	int flag = fcntl(fd, F_GETFL, 0);
	if (-1 == flag ) {
		return;
	}

	fcntl(fd, F_SETFL, flag | O_NONBLOCK);
}

#ifdef __cplusplus
}
#endif

#endif /* EV_H_ */
