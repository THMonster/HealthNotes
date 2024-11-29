#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "dps.h"

const std::uintptr_t PARTY_SIZE_BASE = 0x051C46B8;
const std::uintptr_t PARTY_MEMBER_BASE = 0x05013530;
const std::uintptr_t DAMAGE_BASE = 0x051C46B8;

const std::vector<std::uintptr_t> PARTY_SIZE_OFFSETS = {0x258, 0x10, 0x6574};
const std::vector<std::uintptr_t> NAME_OFFSETS = {0x1ab0, 0x49};
const std::vector<std::uintptr_t> HR_OFFSETS = {0x1ab0, 0x70};
const std::vector<std::uintptr_t> MR_OFFSETS = {0x1ab0, 0x70};
const std::vector<std::uintptr_t> DAMAGE_OFFSETS = {0x258, 0x38, 0x450, 0x8, 0x48};

int64_t get_time_now() {
  auto now = std::chrono::system_clock::now();
  auto unix_timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  return unix_timestamp;
}

// 函数：通过基址和多级偏移读取内存的值
template <typename T> T read_memory(std::uintptr_t base_address, const std::vector<std::uintptr_t> &offsets) {
  // 当前地址
  auto current_address = base_address;

  // 遍历偏移链
  for (size_t i = 0; i < offsets.size(); ++i) {
    current_address += offsets[i];

    // 如果不是最后一级偏移，将当前地址解释为指针继续解析
    if (i < offsets.size() - 1) {
      current_address = *reinterpret_cast<std::uintptr_t *>(current_address);
    }
  }

  // 最终地址读取数据
  return *reinterpret_cast<T *>(current_address);
}

DPSMeter::DPSMeter() {
  PartyMember m = {0, "", 0, 0, 0};
  // members = std::vector<PartyMember>();
  members.push_back(m);
  members.push_back(m);
  members.push_back(m);
  members.push_back(m);
}

void DPSMeter::reset() {
  std::unique_lock l(mtx);
  for (int i = 0; i < 4; i++) {
    members[i].state = 0;
    // members[i].name = "";
    // members[i].damage = 0;
    // members[i].start_time = 0;
  }
}

void DPSMeter::check_members() {
  std::unique_lock l(mtx);
  auto party_len = read_memory<int32_t>(PARTY_SIZE_BASE, PARTY_SIZE_OFFSETS);
  if (party_len > 4 && party_len <= 0) {
    return;
  }
  for (int i = 0; i < party_len; i++) {
    if (members[i].state == 0) {
      members[i].start_time = get_time_now();
      members[i].state = 1;
    }
  }
}

void DPSMeter::update_damage() {
  std::unique_lock l(mtx);
  auto party_len = read_memory<int32_t>(PARTY_SIZE_BASE, PARTY_SIZE_OFFSETS);
  if (party_len > 4 && party_len <= 0) {
    return;
  }
  for (int i = 0; i < party_len; i++) {
    auto mr_offsets = MR_OFFSETS;
    auto damage_offsets = DAMAGE_OFFSETS;
    mr_offsets[0] = mr_offsets[0] + (0x58 * i);
    damage_offsets[4] = damage_offsets[4] + (0x2a0 * i);
    if (members[i].state == 1) {
      members[i].master_rank = read_memory<int16_t>(PARTY_MEMBER_BASE, mr_offsets);
      members[i].damage = read_memory<int32_t>(DAMAGE_BASE, damage_offsets);
    }
  }
}

std::string DPSMeter::get_dps_text() {
  update_damage();
  std::string ret;
  auto now = get_time_now();
  for (auto m : members) {
    if (m.state == 1) {
      auto dps = m.damage / (now - m.start_time);
      ret.append(std::format("{}, {}dps, {}d\n", m.master_rank, dps, m.damage));
    }
  }
  return ret;
}