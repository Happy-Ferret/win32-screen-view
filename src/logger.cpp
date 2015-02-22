#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>

#include "logger.hpp"
#include "util.hpp"

namespace {
    // trim from start
    inline std::string ltrim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    // trim from end
    inline std::string rtrim(std::string s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    // trim from both ends
    inline std::string trim(std::string s) {
        return ltrim(rtrim(s));
    }

    static std::mutex      g_mutex;
    static SV_LogHandler_t g_handler  = nullptr;
    static void           *g_userdata = nullptr;

    class MyLogger : public std::stringbuf {
    public:
        virtual int sync()
        {
            std::lock_guard<std::mutex> guard(g_mutex);

            if (g_handler) {
                std::string msg = trim(this->str());

                g_handler(msg.c_str(), g_userdata);
            }

            this->str(std::string());

            return 0;
        }
    };
}

std::ostream* get_logger()
{
    static MyLogger     *my_buf = nullptr;
    static std::ostream *my_stream = nullptr;

    if (!my_buf)
        my_buf = new MyLogger;
    if (!my_stream)
        my_stream = new std::ostream(my_buf);

    return my_stream;
}

void SV_SetLogHandler(SV_LogHandler_t handler, void* userdata)
{
    std::lock_guard<std::mutex> guard(g_mutex);

    g_handler  = handler;
    g_userdata = userdata;
}

