#pragma once

#include <stdbool.h>

// Every machine-readable line starts with "[WCS] " and goes through OSReport,
// so the runner can parse it from the emulator log.
void report(const char *format, ...) __attribute__((format(printf, 1, 2)));
// Logs "[WCS] error ..." and returns false so callers can `return report_error(...)`.
bool report_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
unsigned report_error_count(void);
// Text of the latest report("detail ...") line since report_clear_detail(); "" when there was none.
const char *report_last_detail(void);
void report_clear_detail(void);
