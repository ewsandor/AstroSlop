CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
LDLIBS ?= -lm

ROOT := ../..
SOURCE := $(ROOT)/src/c/astro
OUT := $(ROOT)/build/host-tests
EPHEMERIS := $(ROOT)/resources/generated/ephemeris.bin
FULL_EPHEMERIS := $(OUT)/ephemeris-full-moon.bin
EPHEMERIS_INPUTS := $(ROOT)/ephemeris/minor_planets.csv \
                    $(wildcard $(ROOT)/ephemeris/meeus/*.csv) \
                    $(wildcard $(ROOT)/ephemeris/vsop87d/VSOP87D.*.gz)

.PHONY: all test references
all: test

references: $(OUT)/test_core $(OUT)/test_horizons $(OUT)/pointing_reference $(EPHEMERIS)

test: $(OUT)/test_core $(OUT)/test_ephemeris $(OUT)/test_horizons \
      $(OUT)/test_moon_truncation $(OUT)/test_log_release $(OUT)/test_log_debug \
      $(EPHEMERIS) $(FULL_EPHEMERIS)
	$(OUT)/test_core
	$(OUT)/test_ephemeris $(EPHEMERIS) $(FULL_EPHEMERIS)
	$(OUT)/test_horizons $(EPHEMERIS)
	$(OUT)/test_moon_truncation $(EPHEMERIS) $(FULL_EPHEMERIS)
	$(OUT)/test_log_release
	$(OUT)/test_log_debug

$(OUT):
	mkdir -p $@

$(OUT)/test_core: test_core.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/astro.h $(SOURCE)/events.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_ephemeris: test_ephemeris.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/events.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_horizons: test_horizons.c $(SOURCE)/astro.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_moon_truncation: test_moon_truncation.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/events.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/pointing_reference: pointing_reference.c $(SOURCE)/astro.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_log_release: test_log.c $(ROOT)/src/c/logging.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_log_debug: test_log.c $(ROOT)/src/c/logging.h | $(OUT)
	$(CC) $(CFLAGS) -DASTRO_DEBUG=1 $(filter %.c,$^) -o $@

$(EPHEMERIS): $(ROOT)/tools/build_ephemeris.py $(EPHEMERIS_INPUTS)
	python3 $< --output $@

$(FULL_EPHEMERIS): $(ROOT)/tools/build_ephemeris.py $(EPHEMERIS_INPUTS) | $(OUT)
	python3 $< --output $@ --full-moon-series
