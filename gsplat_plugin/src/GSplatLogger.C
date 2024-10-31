/***************************************************************************************/
/*  Filename: GSplatLogger.C                                                           */
/*  Description: A basic Logger class for the GSplat Plugin                            */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/


#include "GSplatLogger.h"
#include "GSplatPluginVersion.h"
#include <iostream>
#include <cstdarg>

#include <unordered_set>
#include <string>
#include <sstream>
#include <functional>


void GSplatLogger::_log(const LogLevel level, const char * message)
{
    std::cout << "GSplat Plugin" << logLevelToString(level) 
              << message  << std::endl; 
}

void GSplatLogger::log(const LogLevel level, const char* format, ...) 
{
    va_list args;
    va_start(args, format);
    std::string formattedMessage = formatString(format, args);
    va_end(args);
    _log(level, formattedMessage.c_str());
}


std::string GSplatLogger::logLevelToString(const LogLevel level) 
{
#if !defined(WIN32) // Playing it safe with colouring on Windows
    switch (level) {
        case LogLevel::_INFO_:
            return " [\033[34mINFO\033[0m] ";    //Blue
        case LogLevel::_WARNING_:
            return " [\033[33mWARNING\033[0m] "; //Yellow
        case LogLevel::_ERROR_:
            return " [\033[31mERROR\033[0m] ";   //Red
        default:
            return " [UNKNOWN] ";
    }
#else
    switch (level) {
        case LogLevel::_INFO_:
            return " [INFO] ";
        case LogLevel::_WARNING_:
            return " [WARNING] ";
        case LogLevel::_ERROR_:
            return " [ERROR] ";
        default:
            return " [UNKNOWN] ";
    }
#endif
}

std::string GSplatLogger::formatInteger(const int number, const char separator) {
    std::string numStr = std::to_string(number);
    std::string result;
    int count = 0;

    // Iterate over the number string in reverse
    for (auto it = numStr.rbegin(); it != numStr.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            result += separator;  // Insert the separator
        }
        result += *it;
        ++count;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::string GSplatLogger::formatString(const char* format, va_list args) 
{
    va_list args_copy;
    va_copy(args_copy, args);
    int size = std::vsnprintf(nullptr, 0, format, args_copy) + 1;
    va_end(args_copy);

    if (size <= 0) {
        return "Formatting error";
    }

    std::vector<char> buf(size);
    std::vsnprintf(buf.data(), size, format, args);
    return std::string(buf.data(), buf.size() - 1);
}

std::unordered_set<std::size_t> GSplatOneTimeLogger::loggedMessages;

void GSplatOneTimeLogger::log(const GSplatLogger::LogLevel level, const char* format, ...) 
{
    va_list args;
    va_start(args, format);
    std::string formattedMessage = formatString(format, args);
    va_end(args);

    std::size_t msgHash = generateHash(level, formattedMessage);

    if (loggedMessages.insert(msgHash).second) {
        _log(level, formattedMessage.c_str());
    }
}

void GSplatOneTimeLogger::resetLoggedMessageHistory(const GSplatLogger::LogLevel level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string formattedMessage = formatString(format, args);
    va_end(args);

    std::size_t msgHash = generateHash(level, formattedMessage);
    loggedMessages.erase(msgHash);
}

std::size_t GSplatOneTimeLogger::generateHash(const GSplatLogger::LogLevel level, const std::string& formattedMessage)
{
    std::size_t hash = std::hash<int>{}(static_cast<int>(level));
    // Mix hash of formatted message and log level.
    // This adds a constant (0x9e3779b9, which is the golden ratio) and incorporates the current hash in a way
    // that helps spread out the hash values. The shifts (<< 6) and (>> 2) help to further 
    // mix the bits, reducing collisions in the hash table.
    hash ^= std::hash<std::string>{}(formattedMessage) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    return hash;
}