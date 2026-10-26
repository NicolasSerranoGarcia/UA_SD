#include "process_support.hpp"

#include "paths.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/process/v2/stdio.hpp>
#include <stdexcept>
#include <thread>
#include <utility>

namespace testSupport {
namespace {

boost::process::v2::process_stdio processStdio(boost::asio::writable_pipe &input,
                                             boost::asio::readable_pipe &output,
                                             boost::asio::readable_pipe &errors, bool capture) {
    if (capture)
        return boost::process::v2::process_stdio{input, output, errors};
    return boost::process::v2::process_stdio{input, nullptr, nullptr};
}

bool wouldBlock(const boost::system::error_code &error) {
    return error == boost::asio::error::would_block || error == boost::asio::error::try_again ||
           error == boost::asio::error::interrupted;
}

void pauseUntil(Deadline deadline) {
    if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("Timed out waiting for a test peer");
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
}

std::uint16_t unusedPort(boost::asio::io_context &context) {
    Tcp::acceptor reservation(context, {boost::asio::ip::address_v4::loopback(), 0});
    return reservation.local_endpoint().port();
}

Tcp::socket connect(boost::asio::io_context &context, std::uint16_t port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
        Tcp::socket socket(context);
        bool completed = false;
        boost::system::error_code error;
        socket.async_connect({boost::asio::ip::address_v4::loopback(), port}, [&](auto result) {
            completed = true;
            error = result;
        });
        context.restart();
        context.run_for(std::chrono::milliseconds{50});
        if (!completed) {
            socket.cancel();
            context.restart();
            context.run(); // Finish the callback before its references leave scope.
            continue;
        }
        if (!error) {
            socket.non_blocking(true);
            return socket;
        }
        pauseUntil(deadline);
    }
    throw std::runtime_error("Engine did not start its HCP listener");
}

} // namespace

Process::Process(const std::filesystem::path &executable, const std::vector<std::string> &arguments,
                 bool captureOutput)
    : child_(context_, executable.native(), arguments,
             processStdio(input_, outputPipe_, errorPipe_, captureOutput)) {
    if (captureOutput) {
        boost::asio::async_read(outputPipe_, boost::asio::dynamic_buffer(output_), [](auto, auto) {});
        boost::asio::async_read(errorPipe_, boost::asio::dynamic_buffer(errors_), [](auto, auto) {});
    }
}

Process::~Process() {
    boost::system::error_code error;
    if (child_.running(error))
        child_.terminate(error);
    (void)child_.wait(error);
    input_.close(error);
    outputPipe_.close(error);
    errorPipe_.close(error);
    context_.restart();
    context_.poll();
}

void Process::sendInput(std::string_view text) {
    boost::asio::write(input_, boost::asio::buffer(text.data(), text.size()));
}

void Process::closeInput() {
    if (input_.is_open())
        input_.close();
}

bool Process::running() { return child_.running(); }

void Process::stop() {
    if (child_.running())
        child_.terminate();
    (void)child_.wait();
}

std::optional<int> Process::waitFor(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (child_.running()) {
        context_.restart();
        context_.poll(); // Drain output while waiting, so a full pipe cannot block the child.
        if (std::chrono::steady_clock::now() >= deadline)
            return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    context_.restart();
    context_.run_for(std::chrono::milliseconds{100});
    return child_.exit_code();
}

Server::Server() : listener_(context_, {boost::asio::ip::address_v4::loopback(), 0}) {
    listener_.non_blocking(true);
}

std::uint16_t Server::port() const { return listener_.local_endpoint().port(); }

Tcp::socket Server::accept(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        Tcp::socket socket(context_);
        boost::system::error_code error;
        listener_.accept(socket, error);
        if (!error) {
            socket.non_blocking(true);
            return socket;
        }
        if (!wouldBlock(error))
            throw boost::system::system_error(error);
        pauseUntil(deadline);
    }
}

Engine::Engine()
    : port_(unusedPort(context_)),
      process_(enginePath, {"--queue", "127.0.0.1:9092", "--hcp-port", std::to_string(port_),
                            "--hcp-timeout-ms", "2000"}),
      socket_(connect(context_, port_)) {}

Tcp::socket Engine::reconnect() { return connect(context_, port_); }

void send(Tcp::socket &socket, std::string_view text) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!text.empty()) {
        boost::system::error_code error;
        const auto count = socket.write_some(boost::asio::buffer(text.data(), text.size()), error);
        if (error && !wouldBlock(error))
            throw boost::system::system_error(error);
        text.remove_prefix(count);
        if (!text.empty())
            pauseUntil(deadline);
    }
}

std::string receive(Tcp::socket &socket, std::size_t count, Deadline deadline) {
    std::string text(count, '\0');
    std::size_t received = 0;
    while (received < count) {
        boost::system::error_code error;
        received +=
            socket.read_some(boost::asio::buffer(text.data() + received, count - received), error);
        if (error == boost::asio::error::eof)
            break;
        if (error && !wouldBlock(error))
            throw boost::system::system_error(error);
        if (received < count)
            pauseUntil(deadline);
    }
    text.resize(received);
    return text;
}

std::string receiveFrame(Tcp::socket &socket) {
    std::string frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!frame.ends_with("#FIN")) {
        const auto next = receive(socket, 1, deadline);
        if (next.empty() || frame.size() >= 256)
            throw std::runtime_error("Incomplete or oversized NAP frame from Monitor");
        frame += next;
    }
    return frame;
}

std::vector<std::string> monitorArguments(std::uint16_t centralPort, std::uint16_t enginePort) {
    return {"--ws-id",
            "station-1",
            "--engine-ip",
            "127.0.0.1",
            "--engine-port",
            std::to_string(enginePort),
            "--central-hostname",
            "127.0.0.1",
            "--central-port",
            std::to_string(centralPort),
            "--nap-interval-ms",
            "100"};
}

CommandResult command(const std::filesystem::path &executable,
                      const std::vector<std::string> &arguments) {
    Process process(executable, arguments, true);
    const auto code = process.waitFor(std::chrono::seconds{3});
    if (!code)
        throw std::runtime_error("Command timed out: " + executable.string());
    return {*code, process.output(), process.errors()};
}

} // namespace testSupport
