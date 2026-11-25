// SPDX-FileCopyrightText: 2026 Riverhorse contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "parallel_port.h"
#include "host.h"
#include "settings.h"
#include "system.h"

#include "util/imgui_manager.h"
#include "util/state_wrapper.h"

#include "common/bitutils.h"
#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"

#include "IconsFontAwesome5.h"
#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

Log_SetChannel(ParallelPort);

namespace ParallelPort {

namespace {

class AT29C040A
{
public:
  static constexpr u32 SIZE = 512 * 1024;
  static constexpr u32 STATE_SIZE = SIZE + 20;

  bool LoadImage(const std::string& path, Error* error);
  void SaveImageIfModified(const std::string& path);

  void Reset();
  void DoState(StateWrapper& sw);

  u8 Read(u32 offset);
  u8 Peek(u32 offset) const;
  void Write(u32 offset, u8 value);

private:
  static constexpr u32 FLASH_PAGE_SIZE = 256;
  static constexpr u32 BYTE_LOAD_CYCLES = 5080;
  static constexpr std::array<u8, 2> ID = {0x1F, 0xA4};

  enum class UnlockStep : u8
  {
    Idle,
    FirstAA,
    Second55,
    Erase80,
    EraseAA,
    Erase55,
  };

  bool IsLoadTimedOut() const;
  u8 GetLoadStatus(u32 offset) const;
  void LoadByte(u32 offset, u8 value);
  void EndLoad();
  void ExecuteCommand(u8 command);
  void ExecuteEraseCommand(u8 command);

  std::vector<u8> m_data;
  u32 m_used_size = 0;
  bool m_modified = false;

  UnlockStep m_unlock_step = UnlockStep::Idle;
  bool m_id_mode = false;
  bool m_loading = false;
  bool m_load_timed = false;
  u8 m_toggle = 0;
  u16 m_load_count = 0;
  u32 m_load_page = 0;
  u32 m_last_load_tick = 0;
};

class Device
{
public:
  virtual ~Device() = default;

  virtual size_t GetStateSize() const = 0;
  virtual void Reset() = 0;
  virtual void DoState(StateWrapper& sw) = 0;
  virtual void UpdateSettings() = 0;

  virtual u8 Read(u32 offset) = 0;
  virtual u8 Peek(u32 offset) const = 0;
  virtual void Write(u32 offset, u8 value) = 0;
};

class XplorerCartridge final : public Device
{
public:
  static constexpr u32 SRAM_SIZE = 128 * 1024;
  static constexpr size_t STATE_SIZE = AT29C040A::STATE_SIZE + SRAM_SIZE + 1;

  ~XplorerCartridge() override;

  bool Initialize(Error* error);

  size_t GetStateSize() const override { return STATE_SIZE; }
  void Reset() override;
  void DoState(StateWrapper& sw) override;
  void UpdateSettings() override;

  u8 Read(u32 offset) override;
  u8 Peek(u32 offset) const override;
  void Write(u32 offset, u8 value) override;

private:
  static constexpr u32 FIXED_FLASH_END = 0x40000;
  static constexpr u32 WINDOW_END = 0x60000;
  static constexpr u32 IO_END = 0x70000;

  static constexpr u8 LATCH_SRAM_SELECT = 0x10;
  static constexpr u8 LATCH_FLASH_A17 = 0x20;

  bool IsSRAMMapped() const { return (m_latch & LATCH_SRAM_SELECT) != 0; }
  u32 GetWindowFlashOffset(u32 window_offset) const
  {
    return FIXED_FLASH_END + ((m_latch & LATCH_FLASH_A17) ? 0x20000u : 0u) + window_offset;
  }
  u8 ReadIO(u32 offset) const;

  AT29C040A m_flash;
  std::vector<u8> m_sram = std::vector<u8>(SRAM_SIZE);
  std::string m_image_path;
  bool m_image_writes = false;
  bool m_switch_on = true;
  u8 m_latch = 0;
};

} // namespace

static std::unique_ptr<Device> CreateDevice(ParallelPortDevice type, Error* error);

static constexpr u8 OPEN_BUS = 0xFF;

static std::unique_ptr<Device> s_device;

} // namespace ParallelPort

bool ParallelPort::AT29C040A::LoadImage(const std::string& path, Error* error)
{
  if (path.empty())
  {
    Error::SetStringView(error, TRANSLATE_SV("ParallelPort", "No cartridge image is selected."));
    return false;
  }

  std::optional<std::vector<u8>> image = FileSystem::ReadBinaryFile(path.c_str(), error);
  if (!image.has_value())
  {
    Error::AddPrefixFmt(error, TRANSLATE_FS("ParallelPort", "Failed to read cartridge image '{}': "),
                        Path::GetFileName(path));
    return false;
  }

  if (image->size() > SIZE)
    WARNING_LOG("Cartridge image is {} bytes, only the first {} are used.", image->size(), SIZE);

  m_used_size = static_cast<u32>(std::min<size_t>(image->size(), SIZE));
  m_data = std::move(image.value());
  m_data.resize(SIZE, 0xFF);
  m_modified = false;
  return true;
}

void ParallelPort::AT29C040A::SaveImageIfModified(const std::string& path)
{
  if (!m_modified)
    return;

  INFO_LOG("Writing cartridge image '{}'", Path::GetFileName(path));

  Error error;
  if (!FileSystem::WriteBinaryFile(path.c_str(), m_data.data(), m_used_size, &error))
    ERROR_LOG("Failed to write cartridge image '{}': {}", Path::GetFileName(path), error.GetDescription());
}

void ParallelPort::AT29C040A::Reset()
{
  m_unlock_step = UnlockStep::Idle;
  m_id_mode = false;
  m_loading = false;
  m_load_timed = false;
  m_toggle = 0;
  m_load_count = 0;
  m_load_page = 0;
  m_last_load_tick = 0;
}

void ParallelPort::AT29C040A::DoState(StateWrapper& sw)
{
  sw.DoBytes(m_data.data(), SIZE);
  sw.Do(&m_used_size);
  sw.Do(&m_modified);
  sw.Do(&m_unlock_step);
  sw.Do(&m_id_mode);
  sw.Do(&m_loading);
  sw.Do(&m_load_timed);
  sw.Do(&m_toggle);
  sw.Do(&m_load_count);
  sw.Do(&m_load_page);
  sw.Do(&m_last_load_tick);
}

bool ParallelPort::AT29C040A::IsLoadTimedOut() const
{
  return (m_load_timed && (System::GetGlobalTickCounter() - m_last_load_tick) >= BYTE_LOAD_CYCLES);
}

u8 ParallelPort::AT29C040A::GetLoadStatus(u32 offset) const
{
  return static_cast<u8>((m_toggle ^ 0x40) | (~m_data[offset] & 0x80));
}

u8 ParallelPort::AT29C040A::Read(u32 offset)
{
  if (m_loading)
  {
    if (!IsLoadTimedOut())
    {
      const u8 status = GetLoadStatus(offset);
      m_toggle ^= 0x40;
      EndLoad();
      return status;
    }

    EndLoad();
  }

  return m_id_mode ? ID[offset & 1] : m_data[offset];
}

u8 ParallelPort::AT29C040A::Peek(u32 offset) const
{
  if (m_loading && !IsLoadTimedOut())
    return GetLoadStatus(offset);

  return m_id_mode ? ID[offset & 1] : m_data[offset];
}

void ParallelPort::AT29C040A::Write(u32 offset, u8 value)
{
  if (m_loading)
  {
    if (!IsLoadTimedOut())
    {
      LoadByte(offset, value);
      return;
    }

    EndLoad();
  }

  const u32 command_address = offset & 0x7FFF;
  const bool aa_to_5555 = (command_address == 0x5555 && value == 0xAA);
  switch (m_unlock_step)
  {
    case UnlockStep::Idle:
      m_unlock_step = aa_to_5555 ? UnlockStep::FirstAA : UnlockStep::Idle;
      break;

    case UnlockStep::FirstAA:
      m_unlock_step = (command_address == 0x2AAA && value == 0x55) ? UnlockStep::Second55 :
                                                                    (aa_to_5555 ? UnlockStep::FirstAA : UnlockStep::Idle);
      break;

    case UnlockStep::Second55:
      m_unlock_step = UnlockStep::Idle;
      if (command_address == 0x5555)
        ExecuteCommand(value);
      break;

    case UnlockStep::Erase80:
      m_unlock_step = aa_to_5555 ? UnlockStep::EraseAA : UnlockStep::Idle;
      break;

    case UnlockStep::EraseAA:
      m_unlock_step = (command_address == 0x2AAA && value == 0x55) ? UnlockStep::Erase55 :
                                                                    (aa_to_5555 ? UnlockStep::FirstAA : UnlockStep::Idle);
      break;

    case UnlockStep::Erase55:
      m_unlock_step = UnlockStep::Idle;
      if (command_address == 0x5555)
        ExecuteEraseCommand(value);
      break;
  }
}

void ParallelPort::AT29C040A::ExecuteCommand(u8 command)
{
  switch (command)
  {
    case 0x90:
      DEV_LOG("Flash enters ID mode");
      m_id_mode = true;
      break;

    case 0xF0:
      DEV_LOG("Flash leaves ID mode");
      m_id_mode = false;
      break;

    case 0xA0:
      m_loading = true;
      m_load_timed = true;
      m_load_count = 0;
      m_last_load_tick = System::GetGlobalTickCounter();
      break;

    case 0x80:
      m_unlock_step = UnlockStep::Erase80;
      break;

    default:
      WARNING_LOG("Unknown flash command 0x{:02X}", command);
      break;
  }
}

void ParallelPort::AT29C040A::ExecuteEraseCommand(u8 command)
{
  switch (command)
  {
    case 0x10:
      DEV_LOG("Flash chip erase");
      std::fill(m_data.begin(), m_data.end(), static_cast<u8>(0xFF));
      m_used_size = SIZE;
      m_modified = true;
      break;

    case 0x20:
      m_loading = true;
      m_load_timed = false;
      m_load_count = 0;
      break;

    case 0x60:
      DEV_LOG("Flash enters ID mode");
      m_id_mode = true;
      break;

    default:
      WARNING_LOG("Unknown flash command 0x80 0x{:02X}", command);
      break;
  }
}

void ParallelPort::AT29C040A::LoadByte(u32 offset, u8 value)
{
  const u32 page = offset / FLASH_PAGE_SIZE;
  if (m_load_count == 0)
  {
    m_load_page = page;
    std::memset(&m_data[page * FLASH_PAGE_SIZE], 0xFF, FLASH_PAGE_SIZE);
    m_used_size = std::max(m_used_size, (page + 1) * FLASH_PAGE_SIZE);
    m_modified = true;
  }

  m_last_load_tick = System::GetGlobalTickCounter();

  if (page == m_load_page)
    m_data[offset] = value;
  else
    WARNING_LOG("Flash page load to 0x{:05X} is outside page 0x{:05X}", offset, m_load_page * FLASH_PAGE_SIZE);

  if (++m_load_count == FLASH_PAGE_SIZE)
    EndLoad();
}

void ParallelPort::AT29C040A::EndLoad()
{
  m_loading = false;
  m_load_count = 0;
}

ParallelPort::XplorerCartridge::~XplorerCartridge()
{
  if (m_image_writes)
    m_flash.SaveImageIfModified(m_image_path);
}

bool ParallelPort::XplorerCartridge::Initialize(Error* error)
{
  m_image_path = g_settings.parallel_port_image_path;
  m_image_writes = g_settings.parallel_port_image_writes;
  m_switch_on = g_settings.parallel_port_switch_on;
  return m_flash.LoadImage(m_image_path, error);
}

void ParallelPort::XplorerCartridge::UpdateSettings()
{
  m_switch_on = g_settings.parallel_port_switch_on;
  m_image_writes = g_settings.parallel_port_image_writes;
}

void ParallelPort::XplorerCartridge::Reset()
{
  m_flash.Reset();
  std::fill(m_sram.begin(), m_sram.end(), static_cast<u8>(0));
  m_latch = 0;
}

void ParallelPort::XplorerCartridge::DoState(StateWrapper& sw)
{
  m_flash.DoState(sw);
  sw.DoBytes(m_sram.data(), SRAM_SIZE);
  sw.Do(&m_latch);
}

u8 ParallelPort::XplorerCartridge::ReadIO(u32 offset) const
{
  switch (offset & 7)
  {
    case 0:
      return m_switch_on ? 0xFF : 0xFE;

    case 1:
      return 0x00;

    case 2:
      return 0xFE;

    default:
      return OPEN_BUS;
  }
}

u8 ParallelPort::XplorerCartridge::Read(u32 offset)
{
  if (offset < FIXED_FLASH_END)
    return m_flash.Read(offset);
  if (offset < WINDOW_END)
  {
    const u32 window_offset = offset - FIXED_FLASH_END;
    return IsSRAMMapped() ? m_sram[window_offset] : m_flash.Read(GetWindowFlashOffset(window_offset));
  }
  if (offset < IO_END)
    return ReadIO(offset);

  return OPEN_BUS;
}

u8 ParallelPort::XplorerCartridge::Peek(u32 offset) const
{
  if (offset < FIXED_FLASH_END)
    return m_flash.Peek(offset);
  if (offset < WINDOW_END)
  {
    const u32 window_offset = offset - FIXED_FLASH_END;
    return IsSRAMMapped() ? m_sram[window_offset] : m_flash.Peek(GetWindowFlashOffset(window_offset));
  }
  if (offset < IO_END)
    return ReadIO(offset);

  return OPEN_BUS;
}

void ParallelPort::XplorerCartridge::Write(u32 offset, u8 value)
{
  if (offset < FIXED_FLASH_END)
  {
    m_flash.Write(offset, value);
  }
  else if (offset < WINDOW_END)
  {
    const u32 window_offset = offset - FIXED_FLASH_END;
    if (IsSRAMMapped())
      m_sram[window_offset] = value;
    else
      m_flash.Write(GetWindowFlashOffset(window_offset), value);
  }
  else if (offset < IO_END && (offset & 7) == 1)
  {
    DEV_LOG("Xplorer latch <- 0x{:02X}", value);
    m_latch = value;
  }
}

std::unique_ptr<ParallelPort::Device> ParallelPort::CreateDevice(ParallelPortDevice type, Error* error)
{
  switch (type)
  {
    case ParallelPortDevice::XplorerCartridge:
    {
      std::unique_ptr<XplorerCartridge> cart = std::make_unique<XplorerCartridge>();
      if (!cart->Initialize(error))
        return nullptr;

      cart->Reset();
      return cart;
    }

    case ParallelPortDevice::None:
    default:
      return nullptr;
  }
}

bool ParallelPort::Initialize(Error* error)
{
  s_device.reset();
  if (g_settings.parallel_port_device == ParallelPortDevice::None)
    return true;

  s_device = CreateDevice(g_settings.parallel_port_device, error);
  return static_cast<bool>(s_device);
}

void ParallelPort::Shutdown()
{
  s_device.reset();
}

void ParallelPort::Reset()
{
  if (s_device)
    s_device->Reset();
}

void ParallelPort::UpdateSettings(const Settings& old_settings)
{
  if (g_settings.parallel_port_device == old_settings.parallel_port_device &&
      g_settings.parallel_port_image_path == old_settings.parallel_port_image_path)
  {
    if (s_device)
      s_device->UpdateSettings();
    return;
  }

  s_device.reset();

  Error error;
  if (g_settings.parallel_port_device != ParallelPortDevice::None &&
      !(s_device = CreateDevice(g_settings.parallel_port_device, &error)))
  {
    Host::AddIconOSDMessage("ParallelPortDevice", ICON_FA_EXCLAMATION_TRIANGLE,
                            fmt::format(TRANSLATE_FS("ParallelPort", "Parallel port device removed: {}"),
                                        error.GetDescription()),
                            Host::OSD_ERROR_DURATION);
  }
}

size_t ParallelPort::GetMaxStateSize()
{
  return sizeof(u8) + sizeof(u32) +
         ((g_settings.parallel_port_device == ParallelPortDevice::XplorerCartridge) ? XplorerCartridge::STATE_SIZE : 0);
}

bool ParallelPort::DoState(StateWrapper& sw)
{
  const ParallelPortDevice current_type = s_device ? g_settings.parallel_port_device : ParallelPortDevice::None;
  ParallelPortDevice state_type = current_type;
  u32 device_state_size = s_device ? static_cast<u32>(s_device->GetStateSize()) : 0;
  sw.Do(&state_type);
  sw.Do(&device_state_size);

  if (state_type != current_type)
  {
    WARNING_LOG("Save state has parallel port device {}, but {} is in use.",
                Settings::GetParallelPortDeviceName(state_type), Settings::GetParallelPortDeviceName(current_type));
    sw.SkipBytes(device_state_size);
    Reset();
    return !sw.HasError();
  }

  if (!s_device)
    return !sw.HasError();

  const size_t start = sw.GetPosition();
  s_device->DoState(sw);
  if (!sw.HasError() && (sw.GetPosition() - start) != device_state_size)
  {
    ERROR_LOG("Parallel port device state is {} bytes, expected {}.", sw.GetPosition() - start, device_state_size);
    return false;
  }

  return !sw.HasError();
}

u8 ParallelPort::ReadByte(u32 offset)
{
  return s_device ? s_device->Read(offset) : OPEN_BUS;
}

void ParallelPort::WriteByte(u32 offset, u8 value)
{
  if (s_device)
    s_device->Write(offset, value);
}

void ParallelPort::ReadCode(u32 offset, u32* words, u32 word_count)
{
  if (!s_device)
  {
    std::fill_n(words, word_count, 0xFFFFFFFFu);
    return;
  }

  for (u32 i = 0; i < word_count; i++, offset += sizeof(u32))
  {
    words[i] = ZeroExtend32(s_device->Peek(offset)) | (ZeroExtend32(s_device->Peek(offset + 1)) << 8) |
               (ZeroExtend32(s_device->Peek(offset + 2)) << 16) | (ZeroExtend32(s_device->Peek(offset + 3)) << 24);
  }
}
