#pragma once

#include "../../../shared/hcp.hpp"

#include <array>
#include <charconv>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ninttp/endpoints.hpp>
#include <ninttp/socket/socket.hpp>

namespace nap {

    // Deadline-bounded connection: consume each response before sending another request.
    template<class EndpointT>
    class Connection {
        static_assert(std::is_same_v<EndpointT, ninttp::IPv4Endpoint> ||
                      std::is_same_v<EndpointT, ninttp::IPv6Endpoint>,
                      "EndpointT must either be IPv4 or IPv6");

    public:
        struct Error {
            enum class Type {
                Io, Closed, NoPendingRequest, InvalidResponse, Checksum,
                Nack, InvalidField, PendingRequest, Timeout
            } type;
            std::optional<ninttp::SocketError> socketErr = std::nullopt;
            std::optional<unsigned int> nackCode = std::nullopt;

            std::string message() const {
                switch (type) {
                    case Type::Timeout: return "NAP operation timed out";
                    case Type::Io: return socketErr ? socketErr->what() : "Socket I/O failed";
                    case Type::Closed: return "Connection closed before a complete response";
                    case Type::NoPendingRequest: return "No request is awaiting a response";
                    case Type::InvalidResponse: return "Invalid NAP response or mismatched ACK code";
                    case Type::Checksum: return "Invalid NAP LRC";
                    case Type::Nack: return "NAP NACK: " + std::to_string(nackCode.value_or(0));
                    case Type::InvalidField: return "NAP fields must be nonempty and contain no #";
                    case Type::PendingRequest: return "Read the pending response before sending another request";
                }
                return "Unknown NAP error";
            }
        };

        explicit Connection(std::chrono::milliseconds timeout = std::chrono::milliseconds{2000})
            : timeout_(timeout), sock_(ninttp::Protocol::Tcp, false) {}

        std::expected<void, Error> open(EndpointT endpoint) noexcept {
            auto connected = hcp::connectWithin(sock_, endpoint, deadline());
            if (!connected)
                return std::unexpected{fromIoError(connected.error())};
            return {};
        }

        std::expected<void, Error> sendStatus(bool statusOk) {
            return sendRequest("2", statusOk ? "OK" : "KO");
        }

        std::expected<void, Error> sendRegistration(std::string_view wsId,
                                                   std::string_view location) {
            if (!validField(wsId) || !validField(location))
                return std::unexpected{Error{Error::Type::InvalidField}};
            std::string fields{wsId};
            fields += '#';
            fields += location;
            return sendRequest("0", fields);
        }

        std::expected<void, Error> sendActivation(std::string_view wsId) {
            if (!validField(wsId))
                return std::unexpected{Error{Error::Type::InvalidField}};
            return sendRequest("1", wsId);
        }

        // Returns the validated ACK frame, or an error (including the server's NACK code).
        // No automatic retries: retry policy belongs to the caller.
        std::expected<std::string, Error> getResponse() {
            if (!pendingAck_)
                return std::unexpected{Error{Error::Type::NoPendingRequest}};

            // Read through exactly one terminator without consuming the next TCP frame.
            // ACK/NACK responses only contain a version, numeric codes, and an LRC.
            std::string response;
            constexpr std::size_t maxResponseSize = 256;
            const auto responseDeadline = deadline();
            while (!response.ends_with("#FIN")) {
                if (response.size() == maxResponseSize)
                    return failResponse(Error{Error::Type::InvalidResponse});
                std::array<char, 1> byte{};
                auto got = hcp::receiveExact(sock_, byte, responseDeadline);
                if (!got)
                    return failResponse(fromIoError(got.error()));
                response += byte[0];
            }

            const auto expectedAck = *pendingAck_;
            pendingAck_.reset();
            std::vector<std::string_view> fields;
            const std::string_view view{response};
            std::size_t start = 0;
            for (std::size_t end; (end = view.find('#', start)) != view.npos; start = end + 1)
                fields.push_back(view.substr(start, end - start));
            fields.push_back(view.substr(start));

            if ((fields.size() != 6 && fields.size() != 7) ||
                fields[0] != "NAP" || fields[1] != "1.0" || fields.back() != "FIN")
                return std::unexpected{Error{Error::Type::InvalidResponse}};

            unsigned int ack = 0;
            if (!parseNumber(fields[2], ack) || ack != expectedAck)
                return std::unexpected{Error{Error::Type::InvalidResponse}};

            const bool isAck = fields[3] == "3" && fields.size() == 6;
            const bool isNack = fields[3] == "4" && fields.size() == 7;
            unsigned int nack = 0;
            if ((!isAck && !isNack) ||
                (isNack && (!parseNumber(fields[4], nack) || nack > 3)))
                return std::unexpected{Error{Error::Type::InvalidResponse}};

            const auto lrc = fields[fields.size() - 2];
            const auto lrcSeparator = static_cast<std::size_t>(lrc.data() - view.data()) - 1;
            unsigned int receivedLrc = 0;
            if (lrc.size() != 2 || !parseNumber(lrc, receivedLrc, 16) ||
                receivedLrc != calculateLrc(view.substr(0, lrcSeparator)))
                return std::unexpected{Error{Error::Type::Checksum}};

            if (isNack)
                return std::unexpected{Error{Error::Type::Nack, std::nullopt, nack}};
            return response;
        }

        // Preserve ninttp's close error and socket-ownership information.
        auto close() noexcept {
            pendingAck_.reset();
            return sock_.close();
        }

    private:
        hcp::Deadline deadline() const {
            return std::chrono::steady_clock::now() + timeout_;
        }

        static Error fromIoError(const hcp::IoError& error) {
            if (error.kind == hcp::IoFailure::Timeout) return Error{Error::Type::Timeout};
            if (error.kind == hcp::IoFailure::Closed) return Error{Error::Type::Closed};
            return Error{Error::Type::Io, error.cause};
        }

        static bool validField(std::string_view field) {
            return !field.empty() && field.find('#') == field.npos;
        }

        static bool parseNumber(std::string_view field, unsigned int& value, int base = 10) {
            if (field.empty()) return false;
            const auto result = std::from_chars(field.data(), field.data() + field.size(), value, base);
            return result.ec == std::errc{} && result.ptr == field.data() + field.size();
        }

        static unsigned char calculateLrc(std::string_view body) {
            unsigned char lrc = 0;
            for (unsigned char byte : body) lrc ^= byte;
            return lrc;
        }

        static std::string finishMessage(std::string body) {
            const auto lrc = calculateLrc(body);
            // Wire encoding assumption: two uppercase hexadecimal digits.
            // XOR covers the body only, excluding the # preceding the LRC.
            constexpr char hex[] = "0123456789ABCDEF";
            body += '#';
            body += hex[lrc >> 4];
            body += hex[lrc & 0x0F];
            body += "#FIN";
            return body;
        }

        std::expected<void, Error> sendRequest(std::string_view code, std::string_view fields) {
            if (pendingAck_)
                return std::unexpected{Error{Error::Type::PendingRequest}};
            std::string body = "NAP#1.0#" + std::to_string(nextAck_) + '#';
            body += code;
            body += '#';
            body += fields;
            auto sent = hcp::sendExact(sock_, finishMessage(body), deadline());
            if (!sent) {
                (void)sock_.close(); // A partial send cannot safely be retried on this stream.
                return std::unexpected{fromIoError(sent.error())};
            }
            pendingAck_ = nextAck_++;
            return {};
        }

        std::expected<std::string, Error> failResponse(Error error) {
            pendingAck_.reset();
            (void)sock_.close(); // Discard a partial or oversized frame before another request.
            return std::unexpected{error};
        }

        std::chrono::milliseconds timeout_;
        unsigned int nextAck_ = 0;
        std::optional<unsigned int> pendingAck_;
        ninttp::StreamSocket<EndpointT> sock_;
    };
}
