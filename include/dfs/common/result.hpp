#pragma once

#include <string>
#include <utility>
#include <variant>

namespace dfs {

template <typename T>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(std::string error) { return Result(Error{std::move(error)}); }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const { return std::get<T>(storage_); }
    [[nodiscard]] T& value() { return std::get<T>(storage_); }
    [[nodiscard]] const std::string& error() const { return std::get<Error>(storage_).message; }

private:
    struct Error { std::string message; };
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(Error error) : storage_(std::move(error)) {}

    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    static Result success() { return Result(true, {}); }
    static Result failure(std::string error) { return Result(false, std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    Result(bool ok, std::string error) : ok_(ok), error_(std::move(error)) {}
    bool ok_;
    std::string error_;
};

} // namespace dfs
