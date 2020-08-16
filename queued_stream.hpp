#ifndef QUEUED_STREAM_HPP__
#define QUEUED_STREAM_HPP__

#include "basic.hpp"

#include <iostream>

namespace mux
{

namespace detail
{

template<typename ContainerPointer, typename AsyncWriteStream>
class queued_stream : public std::enable_shared_from_this<queued_stream<ContainerPointer, AsyncWriteStream>>
{
    boost::asio::io_context& io_context_;
    std::deque<ContainerPointer> send_queue_;
    AsyncWriteStream& stream_;
    bool locked = false;

    void start_writing_stream()
    {
        BOOST_LOG_TRIVIAL(trace) << "start_writing_stream: " << send_queue_.front()->size();
        boost::asio::async_write(
            stream_,
            boost::asio::buffer(send_queue_.front()->data(), send_queue_.front()->size()),
            [this, self=this->shared_from_this()] (boost::system::error_code const & error,
                                                   std::size_t bytes_transferred) {
                if (error)
                {
                    if (error != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "write stream error: " << error.message();
                    close();
                }
                else
                {
                    send_queue_.pop_front();
                    if (not send_queue_.empty())
                    {
                        start_writing_stream();
                    }
                }
            });
    }

public:
    queued_stream(boost::asio::io_context& io, AsyncWriteStream& aws):
        io_context_{io}, stream_{aws} {}

    virtual
    ~queued_stream() {}

    void post(ContainerPointer chk)
    {
        bool is_writing = not send_queue_.empty();
        send_queue_.push_back(chk);
        if (not locked and not is_writing)
            start_writing_stream();
    }

    virtual
    void close()
    {
        boost::asio::post(io_context_, [this] { stream_.close(); });
    }

    void lock() { locked = true; }
    void unlock()
    {
        locked = false;
        if (not send_queue_.empty())
            start_writing_stream();
    }

    template<typename ChildType>
    auto cast_shared_from_this() -> std::shared_ptr<ChildType>
    {
        return std::dynamic_pointer_cast<ChildType>(this->shared_from_this());
    }
};

} // namespace detail

template<typename AsyncWriteStream>
using queued_stream = detail::queued_stream<chunk_ptr, AsyncWriteStream>;

template<typename AsyncWriteStream>
using queued_stream_ptr = std::shared_ptr<queued_stream<AsyncWriteStream>>;

} // namespace mux

#endif // QUEUED_STREAM_HPP__
