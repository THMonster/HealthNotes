#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#include "dps.h"
#include "loader.h"

const std::uintptr_t PARTY_SIZE_BASE = 0x051C46B8;
const std::uintptr_t PARTY_MEMBER_BASE = 0x05013530;
const std::uintptr_t DAMAGE_BASE = 0x051C46B8;

const std::vector<std::uintptr_t> PARTY_SIZE_OFFSETS = {0x258, 0x10, 0x6574};
const std::vector<std::uintptr_t> NAME_OFFSETS = {0x1ab0, 0x49};
const std::vector<std::uintptr_t> HR_OFFSETS = {0x1ab0, 0x70};
const std::vector<std::uintptr_t> MR_OFFSETS = {0x1ab0, 0x72};
const std::vector<std::uintptr_t> DAMAGE_OFFSETS = {0x258, 0x38, 0x450, 0x8, 0x48};

int64_t get_time_now() {
  auto now = std::chrono::system_clock::now();
  auto unix_timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  return unix_timestamp;
}

template <typename T>
inline T read_memory(std::uintptr_t base_address, const std::vector<std::uintptr_t> &offsets, T default_value) {
  auto current_address = base_address;

  for (size_t i = 0; i <= offsets.size(); ++i) {
    if (i >= 1) {
      current_address += offsets[i - 1];
    }
    if (i < offsets.size()) {
      current_address = *reinterpret_cast<std::uintptr_t *>(current_address);
    }
    if (current_address < 0xff) {
      return default_value;
    }
  }

  return *reinterpret_cast<T *>(current_address);
}

template <typename T> inline T read_memory(std::uintptr_t base_address, const std::vector<std::uintptr_t> &offsets) {
  auto current_address = *reinterpret_cast<std::uintptr_t *>(base_address);

  for (size_t i = 0; i < offsets.size(); ++i) {
    current_address += offsets[i];

    if (i < offsets.size() - 1) {
      current_address = *reinterpret_cast<std::uintptr_t *>(current_address);
    }
  }

  return *reinterpret_cast<T *>(current_address);
}

DPSMeter::DPSMeter() {
  PartyMember m = {0, "", 0, 0, 0, 0, 0};
  members.push_back(m);
  members.push_back(m);
  members.push_back(m);
  members.push_back(m);
}

DPSMeter::~DPSMeter() {}

void DPSMeter::init_base() {
  // std::unique_lock l(mtx);
  HMODULE hModule = GetModuleHandle(NULL);
  base = reinterpret_cast<std::uintptr_t>(hModule);
  loader::LOG(loader::INFO) << std::format("base addr: 0x{:x}", base);
}

void DPSMeter::reset() {
  std::unique_lock l(mtx);
  current_max_members = 1;
  for (int i = 0; i < 4; i++) {
    members[i] = {0, "", 0, 0, 0, 0, 0};
  }
}

void DPSMeter::check_members() {
  std::unique_lock l(mtx);
  auto party_len = read_memory<int32_t>(base + PARTY_SIZE_BASE, PARTY_SIZE_OFFSETS, 0);
  if (party_len > 4 || party_len <= 0) {
    return;
  }
  if (party_len > current_max_members) {
    current_max_members = party_len;
  }
  for (int i = 0; i < current_max_members; i++) {
    if (members[i].state == 0) {
      auto damage_offsets = DAMAGE_OFFSETS;
      damage_offsets[4] = damage_offsets[4] + (0x2a0 * i);
      auto d = read_memory<int32_t>(base + DAMAGE_BASE, damage_offsets, 0);
      if (d > 0) {
        members[i].start_damage = d;
        members[i].start_time = get_time_now();
        members[i].state = 1;
      }
    }
  }
}

void DPSMeter::update_damage() {
  std::unique_lock l(mtx);
  auto party_len = read_memory<int32_t>(base + PARTY_SIZE_BASE, PARTY_SIZE_OFFSETS, 0);
  if (party_len > 4 || party_len <= 0) {
    return;
  }
  for (int i = 0; i < 4; i++) {
    auto hr_offsets = HR_OFFSETS;
    auto mr_offsets = MR_OFFSETS;
    auto damage_offsets = DAMAGE_OFFSETS;
    hr_offsets[0] = hr_offsets[0] + (0x58 * i);
    mr_offsets[0] = mr_offsets[0] + (0x58 * i);
    damage_offsets[4] = damage_offsets[4] + (0x2a0 * i);
    if (members[i].state == 1) {
      members[i].hunter_rank = read_memory<int16_t>(base + PARTY_MEMBER_BASE, hr_offsets);
      members[i].master_rank = read_memory<int16_t>(base + PARTY_MEMBER_BASE, mr_offsets);
      members[i].damage = read_memory<int32_t>(base + DAMAGE_BASE, damage_offsets);
    }
  }
}

std::string DPSMeter::get_dps_text() {
  update_damage();
  std::string ret;
  auto now = get_time_now();
  auto total_damage = 0;
  for (auto m : members) {
    if (m.state == 1) {
      total_damage = total_damage + m.damage;
    }
  }
  if (total_damage <= 0) {
    total_damage = 1;
  }
  int i = 0;
  for (auto m : members) {
    if (m.state == 1) {
      if (i == 3) {
        ret.pop_back();
      }
      auto dps = (m.damage - m.start_damage) / (now - m.start_time);
      float percent = (float)m.damage * 100 / total_damage;
      // ret.append(std::format("{}, {}dps, {}d, {:.1f}%\n", m.master_rank, dps, m.damage, percent));
      ret.append(std::format("MR{}<STYL MOJI_RED_DEFAULT>{}dps</STYL>"
                             "{}d<STYL MOJI_ORANGE_DEFAULT>{:.1f}%</STYL>\n",
                             m.master_rank, dps, m.damage, percent));
      i++;
    }
  }
  if (!ret.empty() && ret.back() == '\n') {
    ret.pop_back();
  }
  return ret;
}
