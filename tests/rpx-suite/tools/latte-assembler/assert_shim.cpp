#include <cstdio>
#include <cstdlib>
#include <string>
void assertFailed(const char *file, unsigned line, const char *expr, const std::string &msg)
{
   std::fprintf(stderr, "assert %s:%u %s %s\n", file, line, expr, msg.c_str());
   std::abort();
}
