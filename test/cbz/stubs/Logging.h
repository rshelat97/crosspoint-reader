#pragma once
inline void cbzTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) cbzTestLog(__VA_ARGS__)
#define LOG_INF(...) cbzTestLog(__VA_ARGS__)
#define LOG_DBG(...) cbzTestLog(__VA_ARGS__)
