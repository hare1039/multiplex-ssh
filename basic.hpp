#ifndef BASIC_HPP__
#define BASIC_HPP__

#include <string>
#include <algorithm>
#include <vector>
#include <array>
#include <cstring>
#include <memory>

#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>
#include <boost/program_options.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/regex.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/attributes/named_scope.hpp>

namespace mux
{

using channel_id_t = std::uint16_t;
using length_t = std::uint16_t;
int constexpr headersize = sizeof (channel_id_t) + sizeof (length_t);
int constexpr bufsize = 4096;
int constexpr bodysize = bufsize - headersize;

using header_buf = std::array<std::uint8_t, headersize>;
using header_buf_ptr = std::shared_ptr<header_buf>;
using chunk = std::vector<std::uint8_t>;
using chunk_ptr = std::shared_ptr<chunk>;

auto decode_header(header_buf_ptr buf) -> std::pair<channel_id_t, length_t>
{
    channel_id_t chan = 0;
    length_t size = 0;
    std::memcpy(&chan, buf->data(), sizeof chan);
    std::memcpy(&size, buf->data() + sizeof chan, sizeof size);
    boost::endian::big_to_native_inplace(chan);
    boost::endian::big_to_native_inplace(size);

    return {chan, size};
}

void encode_header(chunk_ptr buf, channel_id_t channel, length_t size)
{
    boost::endian::native_to_big_inplace(channel);
    boost::endian::native_to_big_inplace(size);

    buf->resize(buf->size() + headersize);
    std::rotate(buf->rbegin(), buf->rbegin() + headersize, buf->rend());
    std::memcpy(buf->data(), &channel, sizeof channel);
    std::memcpy(buf->data() + sizeof channel, &size, sizeof size);
}

void init_log()
{
    boost::log::add_common_attributes();
    boost::log::core::get()->add_global_attribute("Scope",
                                                  boost::log::attributes::named_scope());
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info
    );

    /* log formatter: https://gist.github.com/xiongjia/e23b9572d3fc3d677e3d
     * [TimeStamp] [Severity Level] [Scope] Log message
     */
    auto timestamp = boost::log::expressions::
        format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S.%f");
    auto severity = boost::log::expressions::
        attr<boost::log::trivial::severity_level>("Severity");
    auto scope = boost::log::expressions::format_named_scope("Scope",
        boost::log::keywords::format = "%n(%f:%l)",
        boost::log::keywords::iteration = boost::log::expressions::reverse,
        boost::log::keywords::depth = 2);
    boost::log::formatter final_format =
        boost::log::expressions::format("[%1%] (%2%): %3%")
        % timestamp % severity
        % boost::log::expressions::smessage;

    /* console sink */
    auto console = boost::log::add_console_log(std::clog);
    console->set_formatter(final_format);
}

} // namespace mux
#endif // BASIC_HPP__
