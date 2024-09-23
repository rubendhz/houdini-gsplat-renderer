/***************************************************************************************/
/*  Filename: GSplatLogger.h                                                           */
/*  Description: A basic Logger class for the GSplat Plugin                            */
/*                                                                                     */
/*  Copyright (C) 2024 Ruben Diaz                                                      */
/*                                                                                     */
/*  License: AGPL-3.0-or-later                                                         */
/*           https://github.com/rubendhz/houdini-gsplat-renderer/blob/develop/LICENSE  */
/***************************************************************************************/

#ifndef __GSPLAT_LOGGER__
#define __GSPLAT_LOGGER__

#include <string>
#include <unordered_set>
#include <cstdarg>


class GSplatLogger 
{
public:
    enum class LogLevel {
        INFOX,
        WARNINGX,
        ERRORX
    };

    static GSplatLogger& getInstance() {
        static GSplatLogger instance;
        return instance;
    }

    static void log(const LogLevel level, const char* format, ...);  // Updated to const char* for variadic handling
    static std::string formatInteger(const int number, const char separator = ',');

protected:
    GSplatLogger() {}
    static std::string formatString(const char* format, va_list args);  // Updated to const char* for consistency
    static void _log(const LogLevel level, const char * message);

private:
    GSplatLogger(const GSplatLogger&) = delete;
    GSplatLogger& operator=(const GSplatLogger&) = delete;

    static std::string logLevelToString(const LogLevel level);
    //static std::string addColorCode(LogLevel level);
};


class GSplatOneTimeLogger : public GSplatLogger 
{
public:
    static GSplatOneTimeLogger& getInstance() {
        static GSplatOneTimeLogger instance;
        return instance;
    }

    static void log(const GSplatLogger::LogLevel level, const char* format, ...);

protected:
    GSplatOneTimeLogger() {}

private:
    GSplatOneTimeLogger(const GSplatOneTimeLogger&) = delete;
    GSplatOneTimeLogger& operator=(const GSplatOneTimeLogger&) = delete;

    static std::unordered_set<std::size_t> loggedMessages;
};


#endif // __GSPLAT_LOGGER__
