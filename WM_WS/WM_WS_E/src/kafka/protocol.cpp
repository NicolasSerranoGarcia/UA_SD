#include "kafka/protocol.hpp"

#include <boost/json/basic_parser.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <algorithm>
#include <stdexcept>

namespace kafka {
namespace {

std::string decimalCode(std::string_view value) {
    constexpr std::string_view maximum = "340282366920938463463374607431768211455";
    if (value.empty() || (value.size() > 1 && value.front() == '0') ||
        !std::all_of(value.begin(), value.end(), [](char digit) {
            return digit >= '0' && digit <= '9';
        }) || value.size() > maximum.size() || (value.size() == maximum.size() && value > maximum))
        throw std::invalid_argument("operation_code must be a nonnegative JSON integer within the UUID range");
    return std::string{value};
}

// Read the original number token before the DOM converts an oversized integer to double.
struct OperationReader {
    static constexpr std::size_t max_array_size = std::size_t(-1);
    static constexpr std::size_t max_object_size = std::size_t(-1);
    static constexpr std::size_t max_string_size = std::size_t(-1);
    static constexpr std::size_t max_key_size = std::size_t(-1);
    using Error = boost::system::error_code;
    std::size_t depth = 0;
    std::string key;
    std::string keyPart;
    std::string numberPart;
    std::string number;

    bool on_document_begin(Error &) { return true; }
    bool on_document_end(Error &) { return true; }
    bool on_array_begin(Error &) { ++depth; return true; }
    bool on_object_begin(Error &) { ++depth; return true; }
    bool on_array_end(std::size_t, Error &) { --depth; return true; }
    bool on_object_end(std::size_t, Error &) { --depth; return true; }
    bool on_string_part(boost::json::string_view, std::size_t, Error &) { return true; }
    bool on_string(boost::json::string_view, std::size_t, Error &) { return true; }
    bool on_bool(bool, Error &) { return true; }
    bool on_null(Error &) { return true; }
    bool on_comment_part(boost::json::string_view, Error &) { return true; }
    bool on_comment(boost::json::string_view, Error &) { return true; }

    bool on_key_part(boost::json::string_view part, std::size_t, Error &) {
        if (depth == 1) keyPart.append(part.data(), part.size());
        return true;
    }
    bool on_key(boost::json::string_view part, std::size_t, Error &) {
        if (depth == 1) {
            key = std::move(keyPart);
            key.append(part.data(), part.size());
            keyPart.clear();
            numberPart.clear();
        }
        return true;
    }
    bool on_number_part(boost::json::string_view part, Error &) {
        if (depth == 1 && key == "operation_code") numberPart.append(part.data(), part.size());
        return true;
    }
    bool complete(boost::json::string_view part) {
        if (depth == 1 && key == "operation_code") {
            number = std::move(numberPart);
            number.append(part.data(), part.size());
            numberPart.clear();
        }
        return true;
    }
    bool on_int64(std::int64_t, boost::json::string_view part, Error &) { return complete(part); }
    bool on_uint64(std::uint64_t, boost::json::string_view part, Error &) { return complete(part); }
    bool on_double(double, boost::json::string_view part, Error &) { return complete(part); }
};

} // namespace

std::string operationKey(const boost::json::value &operation) {
    if (operation.is_string())
        return decimalCode(operation.as_string());
    if (operation.is_uint64())
        return std::to_string(operation.as_uint64());
    if (operation.is_int64() && operation.as_int64() >= 0)
        return std::to_string(operation.as_int64());
    throw std::invalid_argument("operation_code must be a nonnegative JSON integer");
}

boost::json::object parseCommand(std::string_view payload) {
    auto command = boost::json::parse(payload).as_object();
    auto &operation = command.at("operation_code");
    if (operation.is_double()) {
        boost::json::basic_parser<OperationReader> reader{boost::json::parse_options{}};
        boost::system::error_code error;
        reader.write_some(false, payload.data(), payload.size(), error);
        if (error)
            throw std::invalid_argument("invalid JSON operation code: " + error.message());
        operation = decimalCode(reader.handler().number);
    } else if (operation.is_string()) {
        throw std::invalid_argument("operation_code must be a JSON integer, not a string");
    }
    (void)operationKey(operation);
    return command;
}

std::string serializeEvent(const boost::json::object &value) {
    std::string result = "{";
    for (const auto &field : value) {
        if (result.size() != 1) result += ',';
        result += boost::json::serialize(boost::json::string(field.key()));
        result += ':';
        if (field.key() == "operation_code" && field.value().is_string())
            result += operationKey(field.value());
        else
            result += boost::json::serialize(field.value());
    }
    result += '}';
    return result;
}

} // namespace kafka
