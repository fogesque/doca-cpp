/**
 * @file error.hpp
 * @brief DOCA error conversion utilities
 *
 * This file provides utilities for converting DOCA C error codes (doca_error_t)
 * to C++ error objects using the errors package.
 */

#pragma once

#include <doca_error.h>

#include <errors/errors.hpp>
#include <string>

namespace doca
{

/**
 * @class DocaError
 * @brief Error class that wraps DOCA error codes
 *
 * This class extends the errors::Error interface to provide DOCA-specific
 * error information including error code, name, and description.
 */
class DocaError : public errors::Error
{
public:
    /**
     * @brief Construct a DocaError from a doca_error_t code
     * @param code The DOCA error code
     */
    explicit DocaError(doca_error_t code) : errorCode(code) {}

    /**
     * @brief Get the error message
     * @return Formatted error message with code, name, and description
     */
    [[nodiscard]] std::string What() const override
    {
        const char * name = doca_error_get_name(errorCode);
        const char * descr = doca_error_get_descr(errorCode);
        return std::string("DOCA Error [") + std::string(name) + "]: " + descr;
    }

    /**
     * @brief Get the underlying DOCA error code
     * @return The doca_error_t code
     */
    [[nodiscard]] doca_error_t GetNative() const
    {
        return errorCode;
    }

private:
    doca_error_t errorCode;
};

/**
 * @brief Convert doca_error_t to error object
 *
 * @param docaError The DOCA error code to convert
 * @return error object (nullptr if DOCA_SUCCESS)
 */
inline error FromDocaError(doca_error_t docaError)
{
    if (docaError == DOCA_SUCCESS) {
        return nullptr;
    }
    return std::make_shared<DocaError>(docaError);
}

/**
 * @brief Check if an error is a specific DOCA error code
 *
 * @param err The error to check
 * @param docaError The DOCA error code to compare against
 * @return true if err wraps the specified DOCA error code
 */
inline bool IsDocaError(error err, doca_error_t docaError)
{
    if (!err) {
        return docaError == DOCA_SUCCESS;
    }

    std::shared_ptr<DocaError> docaErr;
    if (errors::As(err, &docaErr)) {
        return docaErr->GetNative() == docaError;
    }

    return false;
}

}  // namespace doca
