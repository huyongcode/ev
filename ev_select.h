#ifndef EV_SELECT_H_
#define EV_SELECT_H_

#ifdef EV_SELECT

#include <errno.h>
#include "ev.h"

struct ev_state {
	fd_set rfds, wfds;
	/* We need to have a copy of the fd sets as it's not safe to reuse
	 * FD sets after select(). */
	fd_set _rfds, _wfds;
	struct ev **ev_map;
};

static int state_create(struct ev_loop *loop) {
	struct ev_state *state;
	state = (struct ev_state *) malloc(sizeof(*state));
	if (state == NULL)
		return -1;
	FD_ZERO(&state->rfds);
	FD_ZERO(&state->wfds);
	loop->state = state;
	loop->mode = M_SELECT;

	return 0;
}

static void state_destroy(struct ev_loop *loop) {
	free(loop->state);
	loop->state = NULL;
}

static int state_add(struct ev_loop *loop, struct ev *ev, int mask) {
	int fd = ev->fd;
	struct ev_state *state = loop->state;
	if (mask & EV_READABLE)
		FD_SET(fd, &state->rfds);
	if (mask & EV_WRITABLE)
		FD_SET(fd, &state->wfds);
    return 0;
}

static void state_del(struct ev_loop *loop, struct ev *ev, int del_mask) {
	int fd = ev->fd;
	struct ev_state *state = loop->state;
	if (del_mask & EV_READABLE)
		FD_CLR(fd, &state->rfds);
	if (del_mask & EV_WRITABLE)
		FD_CLR(fd, &state->wfds);
}

static int state_poll(struct ev_loop *loop, struct timeval *tvp) {
	struct ev_state *state = loop->state;
	int numevents = 0;
	int ret;
	int i;

	memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
	memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

	for (i = 0; i < MAX_PRIORITY; i++) {
		loop->ready_evs[i].count = 0;
	}

	ret = select(loop->max_fd + 1, &state->_rfds, &state->_wfds, NULL, tvp);
	if (ret < 0) {
		if (errno != EINTR) {
			LOG(L_ERROR, "%s():errno %d, %s\n", __func__,
					errno, strerror(errno));
			return ret;
		}
		return 0;
	} else if (ret == 0) {
		LOG(L_DEBUG_HUGE, "select return 0\n");
		return 0;
	}

	for (i = 0; i <= loop->max_fd; i++) {
		int mask = 0;
		struct ev *ev = loop->state->ev_map[i];

		if (ev->mask == EV_NONE)
			continue;
		if (ev->mask & EV_READABLE && FD_ISSET(i, &state->_rfds))
			mask |= EV_READABLE;
		if (ev->mask & EV_WRITABLE && FD_ISSET(i, &state->_wfds))
			mask |= EV_WRITABLE;

		struct ev_ready_ctrl *evr_ctrl = &loop->ready_evs[ev->priority];
		evr_ctrl->evr[evr_ctrl->count++] = ev;
		numevents++;
	}
	return numevents;
}

#endif

#endif /* EV_SELECT_H_ */
