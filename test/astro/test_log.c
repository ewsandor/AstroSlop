#include <assert.h>

static unsigned log_count;
static unsigned argument_count;

void test_log(int level, const char *format, ...) {
  (void)level;
  (void)format;
  ++log_count;
}

#define APP_LOG(...) test_log(__VA_ARGS__)
#define APP_LOG_LEVEL_ERROR 1
#include "../../src/c/logging.h"

int main(void) {
  ASTRO_LOG(2, "test %u", ++argument_count);
#ifdef ASTRO_DEBUG
  assert(log_count == 1);
  assert(argument_count == 1);
#else
  assert(log_count == 0);
  assert(argument_count == 0);
#endif
  ASTRO_LOG(APP_LOG_LEVEL_ERROR, "error %u", ++argument_count);
#ifdef ASTRO_DEBUG
  assert(log_count == 2);
  assert(argument_count == 2);
#else
  assert(log_count == 1);
  assert(argument_count == 1);
#endif
  return 0;
}
