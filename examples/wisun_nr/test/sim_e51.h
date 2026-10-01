// test/sim_e51.h — simulated EBYTE E51 (NR CLI firmware) plus a border router.
//
// Behaviour follows the verified sessions in the spec: command echo, "> "
// prompt with no newline, async "[...]" lines that can land inside another
// command's output, the join sequence, sockets, and the two-connection TCP
// pattern with the BR. The BR side speaks the section 7 frame protocol.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  bool br_firmware;          // module answers like BR firmware (wisun.state =)
  bool factory_settings;     // start with settings that differ from section 4
  bool br_powered;
  bool start_operational;    // module already joined (the MCU-reset case)
} sim_opts_t;

typedef struct {
  uint32_t commands;
  uint32_t join_fan11;
  uint32_t saves;
  uint32_t reboot_cmds;
  uint32_t max_cmd_len;
  uint32_t bad_socket_writes;       // spaces in the payload / wrong arg count
  uint32_t single_flight_violations;
  uint32_t socket_closes;
  uint32_t pings;
  uint32_t tcp_clients;
  uint32_t get_wisun;
  // border router
  uint32_t br_msgs;
  uint32_t br_acks_sent;
  uint32_t br_frames_rx;
  uint32_t br_hb_rx;
  uint32_t br_crc_errors;
  uint32_t br_send_ok;
  uint32_t br_send_failed;
  uint16_t br_last_len;
  uint8_t br_last_msg[1100];
  bool br_connected_both;
} sim_stats_t;

void sim_init(const sim_opts_t* opts);
void sim_tick(uint32_t now_ms);
void sim_from_mcu(const uint8_t* bytes, size_t len);

// Test controls.
void sim_br_power(bool on);
void sim_module_reboot(void);
void sim_br_drop_acks(int count);            // BR silently withholds the next ACKs
void sim_br_drop_connections(void);          // BR app restarts: both TCP links close
void sim_br_app_down(bool down);             // BR app closed: the module accepts, nothing connects back
void sim_br_send(const uint8_t* msg, size_t len, bool lose_first_ack);
void sim_br_send_corrupt_frame(void);

const sim_stats_t* sim_stats(void);
int sim_join_state(void);
const char* sim_br_ip(void);
const char* sim_nr_ip(void);
size_t sim_socket_count(void);
