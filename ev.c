#include <stdlib.h>
#include <error.h>
#include <string.h>
#include <pthread.h>

#include "ev.h"
#include "ev_epoll.h"
#include "ev_select.h"
#include "min_heap.h"

#define START_FD_NUN  128

#define evutil_timeradd(tvp, uvp, vvp)							\
	do {														\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;			\
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;       \
		if ((vvp)->tv_usec >= 1000000) {						\
			(vvp)->tv_sec++;									\
			(vvp)->tv_usec -= 1000000;							\
		}														\
	} while (0)

#define	evutil_timersub(tvp, uvp, vvp)						\
	do {													\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {							\
			(vvp)->tv_sec--;								\
			(vvp)->tv_usec += 1000000;						\
		}		\
	} while (0)
	
void get_time(struct timeval *tvp) {
#ifdef CLOCK
	struct timespec ts;
	int rc;
	
	rc = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (rc != 0) {
		perror("get_cur_time");
		exit(rc);
	}

	tvp->tv_sec = ts.tv_sec;
	tvp->tv_usec = ts.tv_nsec / 1000;
#else
	int rc;
	rc = gettimeofday(tvp, NULL);
	if (rc != 0) {
		perror("get_cur_time");
		exit(rc);
	}
#endif
}

void update_current_time(struct ev_loop *loop) {
	get_time(&loop->current_time);
}


void ev_init_timer(
		struct ev_timer *ev,
		void *data,
		struct timeval *tvp,
		enum time_type type,
		void (*timeout_cb)(struct ev_loop *loop,
				struct ev_timer *self, void *data)) {
	min_heap_elem_init(ev);

	ev->data = data;
	ev->timeout_cb = timeout_cb;
	ev->type = type;
	ev->time.tv_sec = tvp->tv_sec;
	ev->time.tv_usec = tvp->tv_usec;
	return ev;
}

int ev_add_timer(struct ev_loop *loop, struct ev_timer *ev) {
	LOG(L_DEBUG, "add timer %p\n", ev);
	evutil_timeradd(&loop->current_time, tvp, &ev->ev_timeout);
	if (min_heap_push(&loop->timeheap, ev) == -1) {
		/*error == ENOMEM*/
		LOG(L_ERROR, "%s(): no memory\n", __func__);
		return -1;
	}
	return 0;
}

void ev_del_timer(struct ev_loop *loop, struct ev_timer *ev) {
	LOG(L_DEBUG, "del timer %p\n", ev);
	min_heap_erase(&loop->timeheap, ev);
}

int ev_init_event(
		struct ev,
		int fd,
		int priority,
		void *data,
		void (*event_cb)(struct ev_loop *loop, struct ev *ev, int mask)) {
	ev->fd = fd;
	ev->priority = priority;
	ev->data = data;
	ev->event_cb = event_cb;
	return 0;
}

struct ev *ev_add_event(struct ev_loop *loop, struct ev *ev,  int mask) {
	if (state_add(loop, ev, mask) == -1) {
		return NULL;
	}

	ev->mask |= (unsigned char)mask;

	/*
	if (mask & EV_READABLE)
		ev->read_cb = ev_cb;
	if (mask & EV_WRITABLE)
		ev->write_cb = ev_cb;
	ev->data = data;
	*/
#ifdef EV_SELECT
	int fd = ev->fd;
	if (fd > loop->max_fd) {
		loop->state->ev_map = realloc(loop->state->ev_map,
			fd + 1);	
		loop->max_fd = fd;
	}
	loop->state->ev_map[fd] = ev;
	if (ev->mask & EV_ET) {
		ev->mask &= ~EV_ET;
	}	
#endif
	return ev;
}

void ev_del_event(struct ev_loop *loop, struct ev *ev, int mask) {
	if (ev->mask == EV_NONE)
		return;
	state_del(loop, ev, mask);
	ev->mask = ev->mask & (~mask);

#ifdef EV_SELECT
	int fd = ev->fd;
	if (ev->mask & EV_ET) {
		ev->mask &= ~EV_ET;
	}	
	if (ev->mask == EV_NONE) {
		loop->state->ev_map[fd] = NULL;
	}
	if (fd == loop->max_fd && ev->mask == EV_NONE) {
		/* Update the max fd */
		int i;

		for (i = loop->max_fd - 1; i >= 0; i--) {
			if (loop->state->ev_map[i]->mask != EV_NONE)
				break;
		}
		loop->max_fd = i;
	}
#endif
}


struct ev_loop *ev_create_loop(int n) {
	struct ev_loop *loop = (struct ev_loop *)malloc(sizeof(struct ev_loop));
	if (loop == NULL)
		return NULL;
	bzero(loop, sizeof(struct ev_loop));
	loop->nev = n;
	for (int i = 0; i < MAX_PRIORITY; i++) {
		loop->ready_evs[i].evr = (struct ev_ready *)malloc(
			n * sizeof(struct ev_ready));
		if (loop->read_evs[i].evr == NULL) {
			goto fail;
		}	
	}
	if (state_create(loop) != 0) {
		goto fail;
	}
	update_current_time(loop);
	min_heap_ctor(&loop->timeheap);

#ifdef EV_SELECT
	loop->max_fd = -1;
#endif
	return loop;
fail:
	if (loop->ready_ev != NULL)
		free(loop->ready_ev);
	if (loop->fast_ready_ev != NULL)
		free(loop->fast_ready_ev);
	if (loop->evs != NULL)
		free(loop->evs);
	if (loop != NULL)
		free(loop);
	return NULL;
}

void ev_destroy_loop(struct ev_loop *loop) {
	free(loop->ready_ev);
	free(loop->evs);
	state_destroy(loop);
	struct ev_timer *ev;
	while ((ev = min_heap_top(&loop->timeheap)) != NULL) {
		ev_del_timer(loop, ev);
	}
	min_heap_dtor(&loop->timeheap);
	free(loop);
}


int process_timeout(struct ev_loop *loop)
{
	struct ev_timer *ev;
	int num = 0;

	if (min_heap_empty(&loop->timeheap))
		return 0;

	while ((ev = min_heap_top(&loop->timeheap))) {
		if (evutil_timercmp(&ev->ev_timeout, &loop->current_time, >))
			break;

		/* delete this event from the I/O queues */		
		ev_del_timer(loop, ev);
		if (ev->type == EV_PERSIST)
			ev_add_timer(loop, ev);			
		}

		LOG(L_DEBUG, "timeout_process: %p", ev->data);
		if (ev->timeout_cb != NULL)
			ev->timeout_cb(loop, ev, ev->data);
		num++;
	}
	return num;
}

void proccess_event(struct ev_loop *loop, struct ev_ready *ready_ev, int num) {
	int i;
	for (i = 0; i < num; i++) {
		int mask = ready_ev[i].mask;
		struct ev *ev = ready_ev[i].ev;
		int read = 0;

		/* note the fe->mask & mask & ... code: maybe an already processed
         * event removed an element that fired and we still didn't
         * processed, so we check if the event is still valid. */
		if (ev->mask & mask & (EV_READABLE | EV_WRITABLE) {
			if (ev->evnet_cb != NULL)
				ev->event_cb(loop, ev, mask);
			read = 1;
		}
	}
}

struct timeval *calc_waiting_time(struct ev_loop *loop, struct ev_timer *tv_wait) {
	struct timeval *tvp;
	struct ev_timer *timer;

	timer = min_heap_top(&loop->timeheap);
	if (timer == NULL) {
		return NULL;
	} 

	evutil_timersub(&timer->ev_timeout, &loop->current_time, tv_wait);
	if (tv_wait.tv_sec < 0)
		tv_wait.tv_sec = 0;
	if (tv_wait.tv_usec < 0)
		tv_wait.tv_usec = 0;
	tvp = &tv_wait;

	return tvp;
}


// void loop_realloc(struct ev_loop *loop) {
// 	if (loop->nev == 0 || loop->expand == 0)
// 		return;
// 	loop->expand = 1;

// 	int new_nev = loop->nev * 2 ;

// 	for (int i = 0; i < MAX_PRIORITY; i++) {
// 		if (loop->ready_ev) {
// 			free (loop->ready_ev);
// 			loop->ready_evs[i].evr = (struct ev_ready *)malloc(
// 				new_nev * sizeof(struct ev_ready));
// 			if (loop->read_evs[i].evr == NULL) {
// 				loop->state->
// 				return 0;
// 			}
// 		}	
// 	}

// 	loop->nev = new_nev;

// }


int ev_process_loop(struct ev_loop *loop) {
	int num;

	update_current_time(loop);
	for (;;) {
		struct timeval *tvp;
		struct timeval tv_wait;

		tvp = calc_waiting_time(loop, &tv_wait);

		num = state_poll(loop, tvp);
		if (num < 0) {
			LOG(L_ERROR, "state_poll failed");
			return num;
		}

		update_current_time(loop);

		proccess_event(loop, loop->ready_ev, loop->ready_num);
		process_timeout(loop);

		// if (loop->nev == num && loop->mode == M_EPOLL) {
		// 	loop_realloc(loop);
		// }
	}
	return 0;
}



