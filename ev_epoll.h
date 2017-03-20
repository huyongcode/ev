#ifndef EV_EPOLL_H_
#define EV_EPOLL_H_

#ifdef EV_EPOLL

#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include "ev.h"

struct ev_state {
	int epfd;
	struct epoll_event *ees;
};

static int state_create(struct ev_loop *loop) {
	struct ev_state *state = (struct ev_state *)malloc(sizeof(*state));
	if (state == NULL)
		return -1;
	bzero(state, sizeof(*state));

	state->ees = (struct epoll_event *)malloc(
			loop->nev * sizeof(struct epoll_event));
	if (state->ees == NULL) {
		free(state);
		return -1;
	}
	bzero(state->ees, loop->nev * sizeof(struct epoll_event));

	state->epfd = epoll_create(1024);
	if (state->epfd == -1) {
		free(state->ees);
		free(state);
		return -1;
	}

	loop->state = state;
	loop->mode = M_EPOLL;
	return 0;
}

static void state_destroy(struct ev_loop *loop) {
	struct ev_state *state = loop->state;
	close(state->epfd);
	free(state->ees);
	free(state);
	loop->state = NULL;
}

static int state_add(struct ev_loop *loop, struct ev *ev, int mask) {
	struct ev_state *state = loop->state;
	struct epoll_event ee;
	int old_mask = ev->mask;

	ee.events = 0;
	mask |= (int)(ev->mask);
    if (mask & EV_READABLE)
    	ee.events |= EPOLLIN;
    if (mask & EV_WRITABLE)
    	ee.events |= EPOLLOUT;
	if (mask & EV_ET)
		ee.events |= EPOLLET;

    ee.data.ptr = (void *)ev;

    old_mask &= (EV_READABLE | EV_WRITABLE);
    int op = (old_mask == EV_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD);
    return  epoll_ctl(state->epfd, op, fd, &ee);
}

static void state_del(struct ev_loop *loop, struct ev *ev, int del_mask) {
	struct ev_state *state = loop->state;
	struct epoll_event ee;
	int mask = (int)(ev->mask) & (~del_mask);

	ee.events = 0;
	if (mask & EV_READABLE)
		ee.events |= EPOLLIN;
	if (mask & EV_WRITABLE)
		ee.events |= EPOLLOUT;
	if (mask & EV_ET)
		ee.events |= EPOLLET;

	ee.data.ptr = (void *)ev;

	/* Note, Kernel < 2.6.9 requires a non null event pointer even for
	 * EPOLL_CTL_DEL. */
	mask &= (EV_READABLE | EV_WRITABLE);
	int op = (mask == EV_NONE ? EPOLL_CTL_DEL : EPOLL_CTL_MOD);
	epoll_ctl(state->epfd, op, fd, &ee);
}


static int state_poll(struct ev_loop *loop, struct timeval *tvp) {
	struct ev_state *state = loop->state;
	int ret;
	int i;

	for (i = 0; i < MAX_PRIORITY; i++) {
		loop->ready_evs[i].count = 0;
	}

	long wait_time;
	if (tvp == NULL) {
		wait_time = -1;
	} else {
		wait_time = tvp->tv_sec * 1000 + tvp->tv_usec / 1000;
		if (wait_time == 0 && tvp->tv_usec > 0)
			wait_time = 1;
	}

	ret = epoll_wait(state->epfd, state->ees, loop->nev, wait_time);

	if (ret < 0) {
		if (errno != EINTR) {
			LOG(L_ERROR, "%s():errno %d, %s\n", __func__,
					errno, strerror(errno));
			return ret;
		}
		return 0;
	} else if (ret == 0) {
		LOG(L_DEBUG_HUGE, "thread %ld epoll_wait return 0\n", (long)pthread_self());
		return 0;
	}

	LOG(L_DEBUG_HUGE, "thread %ld poll return %d events\n", (long)pthread_self(), ret);

	for (i = 0; i < ret; i++) {
		int mask = EV_NONE;
		struct epoll_event *ee = &(state->ees[i]);
		struct ev *ev = (struct ev *)(ee->data.ptr);

		if (ee->events & EPOLLIN)
			mask |= EV_READABLE;
		if (ee->events & EPOLLOUT)
			mask |= EV_WRITABLE;
		if (ee->events & EPOLLERR)
			mask |= EV_WRITABLE;
		if (ee->events & EPOLLHUP)
			mask |= EV_WRITABLE;

		struct ev_ready_ctrl *evr_ctrl = &loop->ready_evs[ev->priority];
		evr_ctrl->evr[evr_ctrl->count++] = ev;
	}

	// if (ret == loop->nev) {
	// 	int new_nev = loop->nev * 2;
	// 	loop->state->ees = (struct epoll_event *)realloc(loop->state->ees,
	// 			new_nev * sizeof(struct epoll_event));
	// 	if (loop->state->ees != NULL) {
	// 		memset(loop->state->ees + loop->nev, 0, sizeof(struct epoll_event) * loop->nev);
	// 		loop->expand = 1;
	// 	} else {
	// 		LOG(L_ERROR, "no memory\n\n");
	// 	}
	// }
	return ret;
}

#endif

#endif /* EV_EPOLL_H_ */
