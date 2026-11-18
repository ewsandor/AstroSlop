CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
LDLIBS ?= -lm

ROOT := ../..
SOURCE := $(ROOT)/src/c/astro
OUT := $(ROOT)/build/host-tests
EPHEMERIS := $(ROOT)/resources/generated/ephemeris.bin
EPHEMERIS_INPUTS := $(ROOT)/ephemeris/minor_planets.csv \
                    $(wildcard $(ROOT)/ephemeris/meeus/*.csv) \
                    $(wildcard $(ROOT)/ephemeris/vsop87d/VSOP87D.*.gz)

.PHONY: all test
all: test

test: $(OUT)/test_core $(OUT)/test_ephemeris $(OUT)/test_horizons \
      $(OUT)/test_accuracy_full $(OUT)/test_accuracy_low $(EPHEMERIS)
	$(OUT)/test_core
	$(OUT)/test_ephemeris $(EPHEMERIS)
	$(OUT)/test_horizons $(EPHEMERIS)
	python3 test_accuracy.py $(OUT)/test_accuracy_full $(OUT)/test_accuracy_low $(EPHEMERIS)

$(OUT):
	mkdir -p $@

$(OUT)/test_core: test_core.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/astro.h $(SOURCE)/events.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_ephemeris: test_ephemeris.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/events.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_horizons: test_horizons.c $(SOURCE)/astro.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_accuracy_full: test_accuracy.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/events.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

$(OUT)/test_accuracy_low: test_accuracy.c $(SOURCE)/astro.c $(SOURCE)/events.c $(SOURCE)/ephemeris.c $(SOURCE)/astro.h $(SOURCE)/events.h $(SOURCE)/ephemeris.h | $(OUT)
	$(CC) $(CFLAGS) -DASTRO_LOW_MEMORY $(filter %.c,$^) $(LDLIBS) -o $@

$(EPHEMERIS): $(ROOT)/tools/build_ephemeris.py $(EPHEMERIS_INPUTS)
	python3 $< --output $@
