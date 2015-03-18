#include "session.hpp"
#include "threadpool.hpp"
extern "C" {
#include <stdio.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include "server.h"
#include "boardd.h"
}

namespace {

class LineProcessJob
{
public:
    LineProcessJob(Session *session, void *ctx, char *line)
	: session_(session), ctx_(ctx), line_(line)
    {
	session_->add_ref();
    }

    ~LineProcessJob()
    {
	session_->dec_ref();
    }

    void Run()
    {
	session_->process_line(ctx_, line_);
    }

private:
    // Ref-counted
    Session *session_;

    // Not owned
    void *ctx_;
    char *line_;
};

static ThreadPool<LineProcessJob> *g_threadpool;

static void
read_cb(struct bufferevent *bev, void *ctx)
{
    reinterpret_cast<Session *>(ctx)->on_read();
}

static void
event_cb(struct bufferevent *bev, short events, void *ctx)
{
    reinterpret_cast<Session *>(ctx)->on_error(events);
}

}  // namespace

void
session_init()
{
#ifdef BOARDD_MT
    fprintf(stderr, "boardd: built with mulit-threading, %d threads.\n",
	    NUM_THREADS);
    g_threadpool = new ThreadPool<LineProcessJob>(NUM_THREADS);
#endif
}

void
session_shutdown()
{
    delete g_threadpool;
}

void session_create(struct event_base *base, evutil_socket_t fd,
		    struct sockaddr *address, int socklen)
{
    new Session(process_line, base, fd, address, socklen);
}

Session::Session(LineFunc process_line,
		 struct event_base *base, evutil_socket_t fd,
		 struct sockaddr *address, int socklen)
    : process_line_(process_line)
{
    bev_ = bufferevent_socket_new(
	base, fd,
	BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS | BEV_OPT_THREADSAFE);
    bufferevent_setcb(bev_, read_cb, NULL, event_cb, this);
    bufferevent_set_timeouts(bev_, common_timeout, common_timeout);
    bufferevent_enable(bev_, EV_READ|EV_WRITE);
    add_ref();
}

Session::~Session()
{}

void Session::on_error(short events)
{
    if (events & BEV_EVENT_ERROR)
	perror("Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_TIMEOUT | BEV_EVENT_ERROR)) {
	shutdown();
    }
}

void Session::on_read()
{
    LineProcessJob *job;

    {
	std::lock_guard<std::mutex> _(mutex_);

	if (!bev_)
	    return;

	size_t len;
	struct evbuffer *input = bufferevent_get_input(bev_);
	char *line = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF);

	if (!line)
	    return;

	job = new LineProcessJob(this, NULL, line);
#ifdef BOARDD_MT
	// One request at a time.
	bufferevent_disable(bev_, EV_READ);
#endif
    }

#ifdef BOARDD_MT
    g_threadpool->Do(job);
#else
    // This must be put out of lock to prevent recursive locking.
    job->Run();
    delete job;
#endif
}

// Call from a worker thread
void Session::process_line(void *ctx, char *line)
{
    evbuffer *output = evbuffer_new();
    if (process_line_(output, ctx, line) == 0) {
	send_and_resume(output);
    } else {
	shutdown();
    }
    evbuffer_free(output);
}

void Session::shutdown()
{
    std::lock_guard<std::mutex> _(mutex_);
    if (bev_) {
	bufferevent_free(bev_);
	bev_ = nullptr;
	dec_ref();
    }
}

void Session::send_and_resume(evbuffer *buf)
{
    std::lock_guard<std::mutex> _(mutex_);
    if (bev_) {
	evbuffer_add_buffer(bufferevent_get_output(bev_), buf);
	bufferevent_enable(bev_, EV_READ);
    }
}
