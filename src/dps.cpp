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

// Helper function to determine the number of bytes in a UTF-8 character based on the leading byte.
size_t utf8_char_length(unsigned char ch) {
  if ((ch & 0x80) == 0x00) {
    return 1; // 1-byte character (ASCII)
  } else if ((ch & 0xE0) == 0xC0) {
    return 2; // 2-byte character
  } else if ((ch & 0xF0) == 0xE0) {
    return 3; // 3-byte character
  } else if ((ch & 0xF8) == 0xF0) {
    return 4; // 4-byte character
  } else {
    return 0; // Invalid UTF-8 encoding
  }
}

// Function to extract the first `n` characters from a UTF-8 string.
std::string utf8_substring(const std::string &str, size_t n) {
  size_t byte_count = 0; // Total number of bytes to include
  size_t char_count = 0; // Number of characters processed

  for (size_t i = 0; i < str.size() && char_count < n;) {
    size_t char_len = utf8_char_length(static_cast<unsigned char>(str[i]));

    if (char_len == 0 || i + char_len > str.size()) {
      return "NULL"; // Return empty string on invalid UTF-8 encoding or incomplete character
    }

    i += char_len;          // Move to the next character
    byte_count += char_len; // Accumulate byte count
    ++char_count;           // Increment character count
  }

  return str.substr(0, byte_count); // Return the substring containing the first `n` characters
}

std::string utf8_substr(const std::string &str, size_t n) {
  if (n == 0)
    return "";

  size_t i = 0;
  size_t count = 0;
  while (i < str.size() && count < n) {
    unsigned char ch = static_cast<unsigned char>(str[i]);
    if (ch < 0x80 || ch >= 0xC0) {
      ++count;
    }
    ++i;
  }

  return str.substr(0, i);
}

std::string read_memory_string(std::uintptr_t base_address, const std::vector<std::uintptr_t> &offsets, uint64_t len) {
  auto current_address = base_address;

  for (size_t i = 0; i <= offsets.size(); ++i) {
    if (i >= 1) {
      current_address += offsets[i - 1];
    }
    if (i < offsets.size()) {
      current_address = *reinterpret_cast<std::uintptr_t *>(current_address);
    }
    if (current_address < 0xff) {
      return "NULL";
    }
  }

  return std::string(reinterpret_cast<char *>(current_address), len);
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
  auto current_address = base_address;

  for (size_t i = 0; i <= offsets.size(); ++i) {
    if (i >= 1) {
      current_address += offsets[i - 1];
    }
    if (i < offsets.size()) {
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
      auto name_offsets = NAME_OFFSETS;
      damage_offsets[4] = damage_offsets[4] + (0x2a0 * i);
      name_offsets[0] = name_offsets[0] + (0x58 * i);
      auto d = read_memory<int32_t>(base + DAMAGE_BASE, damage_offsets, 0);
      if (d > 0) {
        members[i].name = read_memory_string(base + PARTY_MEMBER_BASE, name_offsets, 32);
        members[i].start_damage = d;
        members[i].start_time = get_time_now();
        members[i].state = 1;
      }
    }
  }
}

void DPSMeter::update_damage() {
  std::unique_lock l(mtx);
  for (int i = 0; i < 4; i++) {
    auto hr_offsets = HR_OFFSETS;
    auto mr_offsets = MR_OFFSETS;
    auto damage_offsets = DAMAGE_OFFSETS;
    hr_offsets[0] = hr_offsets[0] + (0x58 * i);
    mr_offsets[0] = mr_offsets[0] + (0x58 * i);
    damage_offsets[4] = damage_offsets[4] + (0x2a0 * i);
    if (members[i].state == 1) {
      members[i].master_rank = read_memory<int16_t>(base + PARTY_MEMBER_BASE, mr_offsets, 0);
      members[i].damage = read_memory<int32_t>(base + DAMAGE_BASE, damage_offsets, 0);
    }
  }
}

std::vector<std::string> DPSMeter::get_dps_text() {
  update_damage();
  std::string name_info;
  std::string dps_info;
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
      if (i == 2) {
        dps_info.append("\n");
        name_info.append("\n");
      }
      auto dps = (m.damage - m.start_damage) / (now - m.start_time);
      float percent = (float)m.damage * 100 / total_damage;
      // ret.append(std::format("{}, {}dps, {}d, {:.1f}%\n", m.master_rank, dps, m.damage, percent));
      dps_info.append(std::format("<STYL MOJI_RED_DEFAULT>{}dps</STYL>"
                                  "{}d<STYL MOJI_ORANGE_DEFAULT>{:.1f}%</STYL>",
                                  dps, m.damage, percent));
      name_info.append(std::format("{}<STYL MOJI_BLUE_DEFAULT>MR{}</STYL>", utf8_substring(m.name, 2), m.master_rank));
      i++;
    }
  }
  // if (!ret.empty() && ret.back() == '\n') {
  //   ret.pop_back();
  // }
  return std::vector{name_info, dps_info};
}
