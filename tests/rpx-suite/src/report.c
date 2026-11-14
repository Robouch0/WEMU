#include "report.h"

#include <coreinit/debug.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned sErrors;
static char sLastDetail[96];

static void emit(const char *prefix, const char *format, va_list args)
{
    char line[512];
    int used = snprintf(line, sizeof(line), "[WCS] %s", prefix);
    vsnprintf(line + used, sizeof(line) - (size_t)used, format, args);
    // The workload a scene describes ("detail ...") is also shown on the summary screen.
    if (strncmp(line, "[WCS] detail ", 13) == 0)
        snprintf(sLastDetail, sizeof(sLastDetail), "%.95s", line + 13);
    // OSReport treats its argument as a format string.
    for (char *c = line; *c; ++c)
        if (*c == '%')
            *c = '#';
    OSReport(line);
}

void report(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    emit("", format, args);
    va_end(args);
}

bool report_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    emit("error ", format, args);
    va_end(args);
    ++sErrors;
    return false;
}

unsigned report_error_count(void)
{
    return sErrors;
}

const char *report_last_detail(void)
{
    return sLastDetail;
}

void report_clear_detail(void)
{
    sLastDetail[0] = '\0';
}
