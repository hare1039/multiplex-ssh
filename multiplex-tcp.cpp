#include "basic.hpp"
#include "queued_stream.hpp"

#include <iostream>
#include <random>

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
        queued_stream{io, socket_} {}

    ~connection() { server_.remove_channel(channel_); }

    void start_read_socket()
    {
        BOOST_LOG_TRIVIAL(trace) << "[multiplex] start_read_socket: " << channel_;
        mux::chunk_ptr buf = std::make_shared<mux::chunk>(mux::bodysize);
        socket_.async_read_some(
            boost::asio::buffer(buf->data(), buf->size()),
            [buf, this, self=this->cast_shared_from_this<this_t>()] (boost::system::error_code const & error,
                                                                     std::size_t bytes_transferred) {
                if (error)
                {
                    if (error != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "[multiplex] start_read_socket error: " << error.message();
                    close();
                }
                else
                {
                    buf->resize(bytes_transferred);
                    mux::encode_header(buf, channel_, bytes_transferred);
                    server_.post(buf);
                    start_read_socket();
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
    std::random_device rd{};
    std::mt19937 gen{rd()};
    std::uniform_int_distribution<mux::channel_id_t> distrib{0, std::numeric_limits<mux::channel_id_t>::max()};
    std::unordered_map<mux::channel_id_t, std::shared_ptr<connection<server>>> channel_used_;

    auto rand() -> mux::channel_id_t { return distrib(gen); }
    auto get_unused_channel() -> std::shared_ptr<connection<server>>
    {
        mux::channel_id_t id = 0;
        bool found = false;
        do {
            id = rand();

            found = channel_used_.find(id) != channel_used_.end();
        } while (found);

        auto conn = std::make_shared<connection<server>>(io_context_, id, *this);
        channel_used_.insert({id, conn->cast_shared_from_this<connection<server>>()});
        return conn;
    }

private:
    boost::asio::io_context &io_context_;
    boost::asio::io_context::strand io_strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::process::async_pipe process_output_, process_input_;
    boost::process::child process_;
    mux::queued_stream_ptr<decltype(process_input_)> managed_stream_;

public:
    server(boost::asio::io_context & io, int port, std::string const & cmd):
        io_context_{io},
        io_strand_{io},
        acceptor_{io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)},
        process_output_{io},
        process_input_{io},
        process_(cmd,
                 boost::process::std_out > process_output_,
                 boost::process::std_in < process_input_,
                 io),
        managed_stream_{std::make_shared<mux::queued_stream<decltype(process_input_)>>(io, process_input_)}
    {
        start_accept();
        start_read_process();
    }

    void start_accept()
    {
        std::shared_ptr<connection<server>> conn = get_unused_channel();

        BOOST_LOG_TRIVIAL(trace) << "[multiplex] start_accept: " << conn->channel();
        acceptor_.async_accept(
            conn->socket(),
            [this, conn] (boost::system::error_code const& error) {
                if (error)
                    BOOST_LOG_TRIVIAL(error) << error.message();
                else
                    conn->start_read_socket();
                start_accept();
            });
    }

    void remove_channel(mux::channel_id_t id)
    {
        boost::asio::post(
            io_context_,
            [this, id] {
                BOOST_LOG_TRIVIAL(trace) << "[multiplex] removed_channel: " << id;
                channel_used_.erase(id);
            });
    }

    void post(mux::chunk_ptr chk) { managed_stream_->post(chk); }

    void start_read_process()
    {
        BOOST_LOG_TRIVIAL(trace) << "[multiplex] start_read_process";
        mux::header_buf_ptr buf = std::make_shared<mux::header_buf>();
        boost::asio::async_read(
            process_output_,
            boost::asio::buffer(buf->data(), buf->size()),
            [buf, this] (boost::system::error_code const & error,
                         std::size_t bytes_transferred) {
                if (error)
                {
                    if (error != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "[multiplex] process header read error: " << error.message();
                    process_output_close();
                }
                else
                {
                    auto && [chan, size] = mux::decode_header(buf);
                    if (size == 0)
                    {
                        auto it = channel_used_.find(chan);
                        if (it != channel_used_.end())
                            it->second->close();
                        start_read_process();
                    }
                    else
                    {
                        std::shared_ptr<connection<server>> conn;
                        auto it = channel_used_.find(chan);
                        if (it != channel_used_.end())
                            conn = it->second;

                        start_read_process_body(conn, size);
                    }
                }
            });
    }

    void start_read_process_body(std::shared_ptr<connection<server>> conn, std::size_t bytes_transferred)
    {
        BOOST_LOG_TRIVIAL(trace) << "[multiplex] start_read_process_body, id: " << conn->channel();
        mux::chunk_ptr buf = std::make_shared<mux::chunk>(bytes_transferred);

        boost::asio::async_read(
            process_output_,
            boost::asio::buffer(buf->data(), bytes_transferred),
            [buf, conn, this] (boost::system::error_code const & error,
                               std::size_t bytes_transferred) {
                if (error)
                {
                    if (error != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "[multiplex] process body read error: " << error.message();
                    process_output_close();
                }
                else
                {
                    if (conn)
                        conn->post(buf);
                    start_read_process();
                }
            });
    }

    void process_output_close()
    {
        boost::asio::post(io_context_, [this] { process_output_.close(); });
    }
};

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    namespace fs = boost::filesystem;
    mux::init_log();
    try
    {
        std::string run_argv;
        int listen_port;
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("run", po::value<std::string>(&run_argv)->required(), "For every connection, run this command and attach stdin/stdout")
            ("listen", po::value<int>(&listen_port)->required(), "listen on this port")
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
        boost::asio::io_context io;

        server s {io, listen_port, run_argv};

        io.run();

//        boost::thread_group tg;
//        for (int i = 0; i < std::thread::hardware_concurrency(); i++)
//            tg.create_thread([&io]{ io.run(); });
//
//        tg.join_all();
    }
    catch(std::exception const& e)
    {
        BOOST_LOG_TRIVIAL(error) << "[multiplex] Error: " << e.what();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
