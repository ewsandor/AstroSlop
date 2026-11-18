#include <assert.h>

static unsigned log_count;
static unsigned argument_count;

void test_log(int level, const char *format, ...) {
  (void)level;
  (void)format;
  ++log_count;
}

#define APP_LOG(...) test_log(__VA_ARGS__)
#include "../../src/c/logging.h"

int main(void) {
  ASTRO_LOG(1, "test %u", ++argument_count);
#ifdef ASTRO_DEBUG
  assert(log_count == 1);
  assert(argument_count == 1);
#else
  assert(log_count == 0);
  assert(argument_count == 0);
#endif
  return 0;
}
