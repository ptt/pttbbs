// Copyright (c) 2015, Robert Wang <robert@arctic.tw>
// The MIT License.
#include <iostream>
#include <memory>
#include <functional>
#include <future>
#include <mutex>
#include <vector>
#include <boost/asio.hpp>
extern "C" {
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "boardd.h"
}

namespace asio = boost::asio;
using boost::system::error_code;
using boost::asio::ip::tcp;

class Conn : public std::enable_shared_from_this<Conn> {
  public:
    using Ptr = std::shared_ptr<Conn>;

    static Ptr Create(asio::io_service &io_service) {
	return Ptr(new Conn(io_service));
    }

    virtual ~Conn() {
	evbuffer_free(pending_output_);
    }

    tcp::socket& Socket() { return socket_; }

    void Start() {
	std::lock_guard<std::mutex> lk(mutex_);
	ReadLine();
	ResetTimer(true);
    }

  private:
    Conn(asio::io_service &io_service)
      : socket_(io_service)
      , timer_(io_service)
      , pending_output_(evbuffer_new()) { }

    void ReadLine() {
	asio::async_read_until(socket_, buffer_, '\n',
			       std::bind(&Conn::OnReadLines,
					 shared_from_this(),
					 std::placeholders::_1,
					 std::placeholders::_2));
    }

    void OnReadLines(const error_code &ec, size_t bytes) {
	std::lock_guard<std::mutex> lk(mutex_);
	if (ec) {
	    Close();
	    return;
	}
	ResetTimer();
	std::string line;
	size_t i = 0, consumed = 0;
	auto data = buffer_.data();
	for (auto it = asio::buffers_begin(data),
	     end = asio::buffers_end(data);
	     it != end;
	     ++it) {
	    const char c = *it;
	    ++i;
	    line.push_back(c);
	    if (c == '\n') {
		consumed = i;
		process_line(pending_output_, nullptr, strdup(line.c_str()));
		line.clear();
	    }
	}
	buffer_.consume(consumed);

	size_t total = evbuffer_get_length(pending_output_);
	asio::const_buffers_1 output(
	    evbuffer_pullup(pending_output_, total), total);
	async_write(socket_, output,
		    std::bind(&Conn::OnWriteCompleted, shared_from_this(),
			      std::placeholders::_1, std::placeholders::_2));
	ResetTimer();
    }

    void OnWriteCompleted(const error_code &ec, size_t bytes) {
	std::lock_guard<std::mutex> lk(mutex_);
	if (ec) {
	    Close();
	    return;
	}
	evbuffer_drain(pending_output_, evbuffer_get_length(pending_output_));
	ReadLine();
	ResetTimer();
    }

    void ResetTimer(bool is_first_time = false) {
	using boost::posix_time::seconds;
	if (timer_.expires_from_now(seconds(kTimeoutSeconds)) > 0 ||
	    is_first_time) {
	    timer_.async_wait(std::bind(&Conn::OnTimeout,
					shared_from_this(),
					std::placeholders::_1));
	}
    }

    void OnTimeout(const error_code &ec) {
	std::lock_guard<std::mutex> lk(mutex_);
	if (ec != asio::error::operation_aborted) {
	    Close();
	}
    }

    void Close() {
	socket_.close();
    }

    std::mutex mutex_;
    tcp::socket socket_;
    asio::deadline_timer timer_;
    asio::streambuf buffer_;
    evbuffer *pending_output_;

    static const int kTimeoutSeconds = 60;
};

class Server {
  public:
    Server(asio::io_service &io_service,
	   const std::string &bind, unsigned short port)
      : acceptor_(io_service,
		  tcp::endpoint(asio::ip::address::from_string(bind), port))
    { StartAccept(); }

  private:
    void StartAccept() {
	auto conn = Conn::Create(acceptor_.get_io_service());
	acceptor_.async_accept(conn->Socket(),
			       std::bind(&Server::HandleAccept,
					 this, conn, std::placeholders::_1));
    }

    void HandleAccept(Conn::Ptr conn, const error_code &ec) {
	if (!ec) {
	    conn->Start();
	}
	StartAccept();
    }

    tcp::acceptor acceptor_;
};

void ServiceThread(asio::io_service &io_service) {
    try {
	io_service.run();
    } catch (std::exception &ex) {
	std::cerr << ex.what() << std::endl;
	exit(1);
    }
}

void start_server(const char *host, unsigned short port) {
    std::cerr << "boardd: built with mulit-threading, "
	<< NUM_THREADS << " threads." << std::endl;

    asio::io_service io_service;
    Server server(io_service, host, port);
    std::vector<std::future> futures;
    futures.reserve(NUM_THREADS);
    for (std::size_t i = 0; i < NUM_THREADS; ++i) {
	futures.emplace_back(
		std::async(std::launch::async, ServiceThread, std::ref(io_service)));
    }
    for (auto &fut : futures) {	//TODO remove the for loop after C++14 (it is redundant)
	fut.get();
    }
}
