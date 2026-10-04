#pragma once

#include "../valid_flow.hpp"
#include <expected>
#include <stdexcept>

namespace internal {

class Valve {
public:
    enum class ErrorType {
        /// Requested adjustment is non-finite or would put flow outside [0, maxFlux].
        InvalidFlow
    };

    struct Error {
        ErrorType type;
    };

    static constexpr units::liters_min maxFlux = 90;

    explicit Valve(units::liters_min startupFlux = 0)
        : flux_(startupFlux), requestedFlux_(startupFlux) {
        if (!internal::validFlow(startupFlux, maxFlux)) {
            throw std::invalid_argument("Valve startup flow must be finite and within [0, 90] L/min");
        }
    }

    /// Closing always stops the simulated water supply.
    void close() noexcept { setFlow(0); }
    void openFully() noexcept { setFlow(maxFlux); }

    [[nodiscard]] std::expected<void, Error> openBy(units::liters_min amount) noexcept {
        if (!internal::validFlow(amount, maxFlux) || amount > maxFlux - requestedFlux_) {
            return std::unexpected(Error{ErrorType::InvalidFlow});
        }
        setFlow(requestedFlux_ + amount);
        return {};
    }

    [[nodiscard]] std::expected<void, Error> closeBy(units::liters_min amount) noexcept {
        if (!internal::validFlow(amount, maxFlux) || amount > requestedFlux_) {
            return std::unexpected(Error{ErrorType::InvalidFlow});
        }
        setFlow(requestedFlux_ - amount);
        return {};
    }

    [[nodiscard]] units::liters_min actualFlow() const noexcept { return flux_; }

private:
    void setFlow(units::liters_min target) noexcept {
        flux_ = requestedFlux_ = target;
    }

    units::liters_min flux_;
    units::liters_min requestedFlux_;

    friend class FlowMeter;
};

} // namespace internal
