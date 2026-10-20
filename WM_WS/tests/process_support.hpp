#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace testSupport {

using Tcp = boost::asio::ip::tcp;
using Deadline = std::chrono::steady_clock::time_point;

// Owns a child and its pipes. Cleanup terminates/reaps it, including on assertion failures.
class Process {
  public:
    // Capture only for commands followed by waitFor(), which drains their output.
    Process(const std::filesystem::path &executable, const std::vector<std::string> &arguments,
            bool captureOutput = false);
    ~Process();
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;

    void sendInput(std::string_view text);
    void closeInput();
    bool running();
    void stop();
    std::optional<int> waitFor(std::chrono::milliseconds timeout);
    const std::string &output() const { return output_; }
    const std::string &errors() const { return errors_; }

  private:
    boost::asio::io_context context_;
    boost::asio::writable_pipe input_{context_};
    boost::asio::readable_pipe outputPipe_{context_};
    boost::asio::readable_pipe errorPipe_{context_};
    std::string output_;
    std::string errors_;
    boost::process::v2::process child_;
};

class Server {
  public:
    Server();
    std::uint16_t port() const;
    Tcp::socket accept(std::chrono::milliseconds timeout = std::chrono::seconds{3});

  private:
    boost::asio::io_context context_;
    Tcp::acceptor listener_;
};

class Engine {
  public:
    Engine();
    Tcp::socket reconnect();
    std::uint16_t port() const { return port_; }
    Process &process() { return process_; }
    Tcp::socket &socket() { return socket_; }

  private:
    boost::asio::io_context context_;
    std::uint16_t port_;
    Process process_;
    Tcp::socket socket_;
};

void send(Tcp::socket &socket, std::string_view text);
std::string receive(Tcp::socket &socket, std::size_t count,
                    Deadline deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3});
std::string receiveFrame(Tcp::socket &socket);
std::vector<std::string> monitorArguments(std::uint16_t centralPort,
                                          std::uint16_t enginePort = 8080);

struct CommandResult {
    int code;
    std::string output;
    std::string errors;
};
CommandResult command(const std::filesystem::path &executable,
                      const std::vector<std::string> &arguments);

} // namespace testSupport
