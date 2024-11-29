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

public:
  DPSMeter();
  ~DPSMeter();

  void reset();
  void check_members();
  void update_damage();
  std::string get_dps_text();
};
