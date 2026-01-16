#pragma once

/**
 * @file Logger.hpp
 * @brief Simple logging system for Mini-Engine
 * 
 * Provides logging macros with different severity levels.
 * In Release builds, DEBUG level logs are compiled out.
 * 
 * Usage:
 *   LOG_DEBUG("Camera") << "Position: " << x << ", " << y;
 *   LOG_INFO("Renderer") << "Pipeline created successfully";
 *   LOG_WARN("Vulkan") << "Deprecated feature used";
 *   LOG_ERROR("BuildingManager") << "Failed to create building";
 */

#include <iostream>
#include <sstream>
#include <string>

namespace mini_engine {

/**
 * @brief Log severity levels
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4  // Disable all logging
};

/**
 * @brief Global log level configuration
 * Set this at runtime to filter log messages
 */
inline LogLevel g_logLevel = LogLevel::Debug;

/**
 * @brief RAII-style log message builder
 * Automatically outputs message on destruction
 */
class LogMessage {
public:
    LogMessage(LogLevel level, const char* tag, std::ostream& stream)
        : m_level(level), m_stream(stream), m_enabled(level >= g_logLevel) {
        if (m_enabled) {
            m_buffer << "[" << levelToString(level) << "][" << tag << "] ";
        }
    }

    ~LogMessage() {
        if (m_enabled) {
            m_stream << m_buffer.str() << std::endl;
        }
    }

    // Disable copy
    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;

    // Enable move
    LogMessage(LogMessage&&) = default;

    template<typename T>
    LogMessage& operator<<(const T& value) {
        if (m_enabled) {
            m_buffer << value;
        }
        return *this;
    }

private:
    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            default:              return "?";
        }
    }

    LogLevel m_level;
    std::ostream& m_stream;
    std::ostringstream m_buffer;
    bool m_enabled;
};

/**
 * @brief Helper to create a LogMessage instance
 */
inline LogMessage createLogMessage(LogLevel level, const char* tag) {
    std::ostream& stream = (level >= LogLevel::Warn) ? std::cerr : std::cout;
    return LogMessage(level, tag, stream);
}

} // namespace mini_engine

// ============================================================================
// Logging Macros
// ============================================================================

/**
 * @brief Debug-level logging (verbose, development info)
 * May be compiled out in Release builds
 */
#ifdef NDEBUG
    #define LOG_DEBUG(tag) if (false) mini_engine::createLogMessage(mini_engine::LogLevel::Debug, tag)
#else
    #define LOG_DEBUG(tag) mini_engine::createLogMessage(mini_engine::LogLevel::Debug, tag)
#endif

/**
 * @brief Info-level logging (general information)
 */
#define LOG_INFO(tag) mini_engine::createLogMessage(mini_engine::LogLevel::Info, tag)

/**
 * @brief Warning-level logging (potential issues)
 */
#define LOG_WARN(tag) mini_engine::createLogMessage(mini_engine::LogLevel::Warn, tag)

/**
 * @brief Error-level logging (errors that need attention)
 */
#define LOG_ERROR(tag) mini_engine::createLogMessage(mini_engine::LogLevel::Error, tag)

/**
 * @brief Set the global log level at runtime
 */
#define LOG_SET_LEVEL(level) mini_engine::g_logLevel = (level)
