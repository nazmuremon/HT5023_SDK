# Host tests for examples/wisun_nr: the portable firmware sources compiled for
# the PC, driven against a simulated E51 and border router in virtual time.
#
#   make -C examples/wisun_nr/test          # build and run
#   make -C examples/wisun_nr/test clean

CC      ?= cc
CFLAGS  := -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
FW      := ../wisun_cli.c ../wisun_events.c ../wisun_frame.c ../wisun_nr.c ../wisun_util.c
SRCS    := $(FW) hal_host.c sim_e51.c test_main.c
BIN     := wisun_nr_test

.PHONY: run clean
run: $(BIN) $(BIN)_locked $(BIN)_payload
	./$(BIN)
	./$(BIN)_locked
	./$(BIN)_payload

# The scenarios read the firmware's diagnostic log, so it is switched on here.
$(BIN): $(SRCS) $(wildcard ../*.h) sim_e51.h
	$(CC) $(CFLAGS) -DWISUN_LOG_PAYLOAD_ONLY=0 $(SRCS) -o $@

# The same sources with WISUN_APPLY_CONFIG=0: settings are reported, never changed.
$(BIN)_locked: $(SRCS) $(wildcard ../*.h) sim_e51.h
	$(CC) $(CFLAGS) -DWISUN_LOG_PAYLOAD_ONLY=0 -DWISUN_APPLY_CONFIG=0 $(SRCS) -o $@

# The firmware default, WISUN_LOG_PAYLOAD_ONLY=1: nothing but payloads and console replies.
$(BIN)_payload: $(SRCS) $(wildcard ../*.h) sim_e51.h
	$(CC) $(CFLAGS) $(SRCS) -o $@

clean:
	rm -f $(BIN) $(BIN)_locked $(BIN)_payload
