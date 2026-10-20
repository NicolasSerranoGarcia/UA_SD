#pragma once

#include "engine/valid_flow.hpp"
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
        : flux_(startupFlux) {
        if (!internal::validFlow(startupFlux, maxFlux)) {
            throw std::invalid_argument("Valve startup flow must be finite and within [0, 90] L/min");
        }
    }

    /// Closing always stops the simulated water supply.
    void close() noexcept { setFlow(0); }
    void openFully() noexcept { setFlow(maxFlux); }

    [[nodiscard]] std::expected<void, Error> openBy(units::liters_min amount) noexcept {
        if (!internal::validFlow(amount, maxFlux) || amount > maxFlux - flux_) {
            return std::unexpected(Error{ErrorType::InvalidFlow});
        }
        setFlow(flux_ + amount);
        return {};
    }

    [[nodiscard]] std::expected<void, Error> closeBy(units::liters_min amount) noexcept {
        if (!internal::validFlow(amount, maxFlux) || amount > flux_) {
            return std::unexpected(Error{ErrorType::InvalidFlow});
        }
        setFlow(flux_ - amount);
        return {};
    }

private:
    void setFlow(units::liters_min target) noexcept {
        flux_ = target;
    }

    units::liters_min flux_;

    friend class FlowMeter;
};

} // namespace internal
