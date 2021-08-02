#include "basic.hpp"
#include "queued_stream.hpp"

#include <iostream>

template<typename Server>
class connection : public mux::queued_stream<boost::asio::ip::tcp::socket>
{
    using this_t = connection<Server>;
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    mux::channel_id_t channel_;
    Server & server_;
    bool is_closed = false;

public:
    connection(boost::asio::io_context& io, mux::channel_id_t id, Server & s):
        io_context_{io},
        socket_{io},
        channel_{id},
        server_{s},
        queued_stream{io, socket_}
    {
        queued_stream::lock();
    }

    ~connection() { server_.remove_channel(channel_); }

    void connect(boost::asio::ip::tcp::resolver::results_type endpoints_)
    {
        BOOST_LOG_TRIVIAL(trace) << "[remote] connect: " << channel_;
        boost::asio::async_connect(
            socket_,
            endpoints_,
            [self=this->cast_shared_from_this<this_t>()](boost::system::error_code const & error, boost::asio::ip::tcp::endpoint) {
                if (error)
                {
                    BOOST_LOG_TRIVIAL(error) << "[remote] connect error: " << error.message();
                    self->close();
                }
                else
                {
                    self->start_read_socket();
                    self->queued_stream::unlock();
                }
            });
    }

    void start_read_socket()
    {
        BOOST_LOG_TRIVIAL(trace) << "[remote] start_read_socket: " << channel_;
        mux::chunk_ptr buf = std::make_shared<mux::chunk>(mux::bodysize);
        socket_.async_read_some(
            boost::asio::buffer(buf->data(), buf->size()),
            [buf, self=cast_shared_from_this<this_t>()] (boost::system::error_code const & error,
                                                        std::size_t bytes_transferred) {
                if (error)
                {
                    if (not mux::is_common_error(error))
                        BOOST_LOG_TRIVIAL(error) << "[remote] start_read_socket error: " << error.message();
                    self->close();
                }
                else
                {
                    buf->resize(bytes_transferred);
                    mux::encode_header(buf, self->channel_, bytes_transferred);
                    self->server_.post(buf);
                    self->start_read_socket();
                }
            });
    }

    void close() override
    {
        if (!is_closed)
            boost::asio::post(io_context_,
                              [self=cast_shared_from_this<this_t>()] {
                                  mux::chunk_ptr buf = std::make_shared<mux::chunk>();
                                  mux::encode_header(buf, self->channel_, 0);
                                  self->server_.post(buf);
                                  self->socket_.close();
                                  self->queued_stream::close();
                                  self->is_closed = true;
                              });
    }

    auto socket() -> boost::asio::ip::tcp::socket& { return socket_; }
    auto channel() -> mux::channel_id_t { return channel_; }
};

class server
{
    boost::asio::io_context &io_context_;
    boost::asio::ip::tcp::resolver::results_type endpoints_;
    boost::asio::posix::stream_descriptor stdin_, stdout_;
    std::unordered_map<mux::channel_id_t, std::shared_ptr<connection<server>>> channel_used_;
    mux::queued_stream_ptr<decltype(stdout_)> managed_stream_;

public:
    server(boost::asio::io_context & io, boost::asio::ip::tcp::resolver::results_type endpoints):
        io_context_{io},
        endpoints_{endpoints},
        stdin_{io, ::dup(STDIN_FILENO)},
        stdout_{io, ::dup(STDOUT_FILENO)},
        managed_stream_{std::make_shared<mux::queued_stream<decltype(stdout_)>>(io, stdout_)}
    { start_read_stdin(); }

    void start_read_stdin()
    {
        BOOST_LOG_TRIVIAL(trace) << "[remote] start_read_stdin";
        mux::header_buf_ptr buf = std::make_shared<mux::header_buf>();
        boost::asio::async_read(
            stdin_,
            boost::asio::buffer(buf->data(), buf->size()),
            [buf, this] (boost::system::error_code const & error,
                         std::size_t bytes_transferred) {
                if (error)
                {
                    if (not mux::is_common_error(error))
                        BOOST_LOG_TRIVIAL(error) << "[remote] stdin read error: " << error.message();
                    close();
                }
                else
                {
                    auto && [chan, size] = mux::decode_header(buf);
                    if (size == 0)
                    {
                        auto it = channel_used_.find(chan);
                        if (it != channel_used_.end())
                            it->second->close();
                        start_read_stdin();
                    }
                    else
                        start_read_stdin_body(chan, size);
                }
            });

    }

    void start_read_stdin_body(mux::channel_id_t id, std::size_t bytes_transferred)
    {
        BOOST_LOG_TRIVIAL(trace) << "[remote] start_read_stdin_body: " << id << ", " << bytes_transferred;
        mux::chunk_ptr buf = std::make_shared<mux::chunk>(bytes_transferred);
        std::shared_ptr<connection<server>> conn;
        {
            auto it = channel_used_.find(id);
            if (it == channel_used_.end())
            {
                conn = std::make_shared<connection<server>>(io_context_, id, *this);
                conn->connect(endpoints_);
                channel_used_.insert({id, conn});
            }
            else
                conn = it->second;
        }

        boost::asio::async_read(
            stdin_,
            boost::asio::buffer(buf->data(), buf->size()),
            [buf, conn, this] (boost::system::error_code const & error,
                               std::size_t bytes_transferred) {
                if (error)
                {
                    if (not mux::is_common_error(error))
                        BOOST_LOG_TRIVIAL(error) << "[remote] start_read_stdin_body error: " << error.message() ;
                    close();
                }
                else
                {
                    buf->resize(bytes_transferred);
                    conn->post(buf);
                    start_read_stdin();
                }
            });
    }

    void post(mux::chunk_ptr chk) { managed_stream_->post(chk); }

    void remove_channel(mux::channel_id_t id)
    {
        channel_used_.erase(id);
    }

    void close()
    {
        BOOST_LOG_TRIVIAL(info) << "[remote] closed";
        boost::asio::post(io_context_, [this] { stdin_.close(); });
    }
};

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    namespace fs = boost::filesystem;
    mux::init_log();
    try
    {
        std::string target;
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("to", po::value<std::string>(&target)->required(), "connect to this host:port")
            ;

        po::positional_options_description p;
        po::variables_map vm;
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(desc).positional(p).run();
        po::store(parsed, vm);

        if (vm.count("help"))
        {
            BOOST_LOG_TRIVIAL(info) << desc;
            std::exit(EXIT_SUCCESS);
        }

        po::notify(vm);

        std::vector<std::string> result;
        boost::split(result, target, boost::is_any_of(":"));
        boost::asio::io_context io;

        boost::asio::ip::tcp::resolver resolver{io};
        auto endpoints = resolver.resolve(result.at(0), result.at(1));

        server s {io, endpoints};

        io.run();

//        boost::thread_group tg;
//        for (int i = 0; i < std::thread::hardware_concurrency(); i++)
//            tg.create_thread([&io]{ io.run(); });
//
//        tg.join_all();
    }
    catch(std::exception const& e)
    {
        BOOST_LOG_TRIVIAL(error) << "[remote] Error: " << e.what();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
