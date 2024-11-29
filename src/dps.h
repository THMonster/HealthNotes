#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

struct PartyMember {
  int8_t state;
  std::string name;
  int16_t master_rank;
  int32_t damage;
  int64_t start_time;
};

class DPSMeter {
  std::vector<PartyMember> members;
  std::mutex mtx;
  std::uintptr_t base;

public:
  DPSMeter();
  ~DPSMeter();

  void init_base();
  void reset();
  void check_members();
  void update_damage();
  std::string get_dps_text();
};
