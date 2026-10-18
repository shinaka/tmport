#pragma once
// Exception types used across the engine.

#include <stdexcept>
#include <string>

namespace tmport {

// Base class for all engine errors.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Malformed or truncated data in one of the user's game files.
class FormatError : public Error {
public:
    using Error::Error;
};

// A required game file is missing. what() names the file.
class MissingFileError : public Error {
public:
    MissingFileError(std::string file, const std::string& message)
        : Error(message), file_(std::move(file)) {}
    const std::string& file() const { return file_; }

private:
    std::string file_;
};

}  // namespace tmport
