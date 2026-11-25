// SPDX-FileCopyrightText: 2016 iCatButler, 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: GPL-2.0+

#include "cpu_pgxp.h"
#include "bus.h"
#include "cpu_core.h"
#include "cpu_disasm.h"
#include "settings.h"

#include "util/gpu_device.h"

#include "common/assert.h"
#include "common/log.h"

#include <algorithm>
#include <climits>
#include <cmath>

Log_SetChannel(CPU::PGXP);

// #define LOG_VALUES 1
// #define LOG_LOOKUPS 1

// TODO: Get rid of all the rs/rt subscripting.
// TODO: Don't update flags on Validate(), instead return it.

namespace CPU::PGXP {
namespace {

enum : u32
{
  VERTEX_CACHE_WIDTH = 2048,
  VERTEX_CACHE_HEIGHT = 2048,
  VERTEX_CACHE_SIZE = VERTEX_CACHE_WIDTH * VERTEX_CACHE_HEIGHT,
  PGXP_MEM_SIZE = (static_cast<u32>(Bus::RAM_8MB_SIZE) + static_cast<u32>(CPU::SCRATCHPAD_SIZE)) / 4,
  PGXP_MEM_SCRATCH_OFFSET = Bus::RAM_8MB_SIZE / 4,
};

enum : u32
{
  COMP_X,
  COMP_Y,
  COMP_Z,
};

enum : u32
{
  VALID_X = (1u << 0),
  VALID_Y = (1u << 1),
  VALID_Z = (1u << 2),
  BITFIELD = (1u << 3),
  VALID_LOWZ = (1u << 16),      // Valid Z from the low part of a 32-bit value.
  VALID_HIGHZ = (1u << 17),     // Valid Z from the high part of a 32-bit value.
  VALID_TAINTED_Z = (1u << 31), // X/Y has been changed, Z may not be accurate.

  VALID_XY = (VALID_X | VALID_Y),
  VALID_XYZ = (VALID_X | VALID_Y | VALID_Z),
  VALID_ALL = (VALID_X | VALID_Y | VALID_Z),
};

union psx_value
{
  u32 d;
  s32 sd;
  struct
  {
    u16 l, h;
  } w;
  struct
  {
    s16 l, h;
  } sw;
};
} // namespace

static void CacheVertex(u32 value, const PGXP_value& vertex);
static PGXP_value* GetCachedVertex(u32 value);

static float TruncateVertexPosition(float p);
static bool IsWithinTolerance(float precise_x, float precise_y, int int_x, int int_y);


static void Validate(PGXP_value* pV, u32 psxV);
static void SetFromNativeValue(PGXP_value& value);
static void MaskValidate(PGXP_value* pV, u32 psxV, u32 mask, u32 validMask);

static double f16Sign(double in);
static double f16Unsign(double in);
static double f16Overflow(double in);

static PGXP_value* GetPtr(u32 addr);

static void ValidateAndCopyMem(PGXP_value* dest, u32 addr, u32 value);
static void ValidateAndCopyMem16(PGXP_value* dest, u32 addr, u32 value, bool sign);

static void CPU_MTC2_int(const PGXP_value& value, u32 reg, u32 native);
static void CPU_BITWISE(u32 instr, u32 rdVal, u32 rsVal, u32 rtVal);

static void WriteMem(const PGXP_value* value, u32 addr);
static void WriteMem16(const PGXP_value* src, u32 addr);

static void CopyZIfMissing(PGXP_value& dst, const PGXP_value& src);
static void SelectZ(PGXP_value& dst, const PGXP_value& src1, const PGXP_value& src2);
static bool IsExactZero(const PGXP_value& value, u32 psx_value);
static constexpr u32 AND_FUNCT = 0x24;
static constexpr u32 GTE_V0 = 0;
static constexpr u32 GTE_IR1 = 9;
static constexpr u32 GTE_MAC1 = 25;
static constexpr u32 GTE_LLM = 40;
static constexpr u32 GTE_BK = 45;
static constexpr u32 GTE_LCM = 48;
static constexpr u32 NOR_FUNCT = 0x27;
static const PGXP_value* KeepExactHalvesThroughPackedMask(PGXP_value& result, u32 rsVal, u32 rtVal,
                                                          const PGXP_value& prsVal, const PGXP_value& prtVal,
                                                          u32 rdVal);
static void ShiftLeft(u32 instr, u32 rtVal, u32 sh);
static void ShiftRight(u32 instr, u32 rtVal, u32 sh, bool arithmetic, bool variable);

#ifdef LOG_VALUES
static void LogInstruction(u32 pc, u32 instr);
static void LogValue(const char* name, u32 rval, const PGXP_value* val);
static void LogValueStr(SmallStringBase& str, const char* name, u32 rval, const PGXP_value* val);

// clang-format off
#define LOG_VALUES_NV() do { LogInstruction(CPU::g_state.current_instruction_pc, instr); } while (0)
#define LOG_VALUES_1(name, rval, val) do { LogInstruction(CPU::g_state.current_instruction_pc, instr); LogValue(name, rval, val); } while (0)
#define LOG_VALUES_C1(rnum, rval) do { LogInstruction(CPU::g_state.current_instruction_pc,instr); LogValue(CPU::GetRegName(static_cast<CPU::Reg>(rnum)), rval, &g_state.pgxp_gpr[static_cast<u32>(rnum)]); } while(0)
#define LOG_VALUES_C2(r1num, r1val, r2num, r2val) do { LogInstruction(CPU::g_state.current_instruction_pc,instr); LogValue(CPU::GetRegName(static_cast<CPU::Reg>(r1num)), r1val, &g_state.pgxp_gpr[static_cast<u32>(r1num)]); LogValue(CPU::GetRegName(static_cast<CPU::Reg>(r2num)), r2val, &g_state.pgxp_gpr[static_cast<u32>(r2num)]); } while(0)
#define LOG_VALUES_LOAD(addr, val) do { LogInstruction(CPU::g_state.current_instruction_pc,instr); LogValue(TinyString::from_format("MEM[{:08X}]", addr).c_str(), val, GetPtr(addr)); } while(0)
#define LOG_VALUES_STORE(rnum, rval, addr) do { LOG_VALUES_C1(rnum, rval); std::fprintf(s_log, " addr=%08X", addr); } while(0)
#else
#define LOG_VALUES_NV() (void)0
#define LOG_VALUES_1(name, rval, val) (void)0
#define LOG_VALUES_C1(rnum, rval) (void)0
#define LOG_VALUES_C2(r1num, r1val, r2num, r2val) (void)0
#define LOG_VALUES_LOAD(addr, val) (void)0
#define LOG_VALUES_STORE(rnum, rval, addr) (void)0
#endif
// clang-format on

static constexpr PGXP_value PGXP_value_invalid = {0.f, 0.f, 0.f, 0, 0};
static constexpr PGXP_value PGXP_value_zero = {0.f, 0.f, 0.f, 0, VALID_XY};
static PGXP_value s_discarded_gpr;

static PGXP_value& DestinationGPR(u32 reg)
{
  if (reg != static_cast<u32>(Reg::zero))
    return g_state.pgxp_gpr[reg];

  s_discarded_gpr = PGXP_value_zero;
  return s_discarded_gpr;
}

static PGXP_value* s_mem = nullptr;
static PGXP_value* s_vertex_cache = nullptr;

#ifdef LOG_VALUES
static std::FILE* s_log;
#endif
} // namespace CPU::PGXP

void CPU::PGXP::Initialize()
{
  std::memset(g_state.pgxp_gpr, 0, sizeof(g_state.pgxp_gpr));
  std::memset(g_state.pgxp_cop0, 0, sizeof(g_state.pgxp_cop0));
  std::memset(g_state.pgxp_gte, 0, sizeof(g_state.pgxp_gte));
  g_state.pgxp_gpr[static_cast<u8>(Reg::zero)].flags = VALID_XY;

  if (!s_mem)
  {
    s_mem = static_cast<PGXP_value*>(std::calloc(PGXP_MEM_SIZE, sizeof(PGXP_value)));
    if (!s_mem)
      Panic("Failed to allocate PGXP memory");
  }

  if (g_settings.gpu_pgxp_vertex_cache && !s_vertex_cache)
  {
    s_vertex_cache = static_cast<PGXP_value*>(std::calloc(VERTEX_CACHE_SIZE, sizeof(PGXP_value)));
    if (!s_vertex_cache)
    {
      ERROR_LOG("Failed to allocate memory for vertex cache, disabling.");
      g_settings.gpu_pgxp_vertex_cache = false;
    }
  }

  if (s_vertex_cache)
    std::memset(s_vertex_cache, 0, sizeof(PGXP_value) * VERTEX_CACHE_SIZE);
}

void CPU::PGXP::Reset()
{
  std::memset(g_state.pgxp_gpr, 0, sizeof(g_state.pgxp_gpr));
  std::memset(g_state.pgxp_cop0, 0, sizeof(g_state.pgxp_cop0));
  std::memset(g_state.pgxp_gte, 0, sizeof(g_state.pgxp_gte));
  g_state.pgxp_gpr[static_cast<u8>(Reg::zero)].flags = VALID_XY;

  if (s_mem)
  {
    std::memset(s_mem, 0, sizeof(PGXP_value) * (Bus::g_ram_size / 4));
    std::memset(&s_mem[PGXP_MEM_SCRATCH_OFFSET], 0, sizeof(PGXP_value) * (CPU::SCRATCHPAD_SIZE / 4));
  }

  if (g_settings.gpu_pgxp_vertex_cache && s_vertex_cache)
    std::memset(s_vertex_cache, 0, sizeof(PGXP_value) * VERTEX_CACHE_SIZE);
}

void CPU::PGXP::Shutdown()
{
  if (s_vertex_cache)
  {
    std::free(s_vertex_cache);
    s_vertex_cache = nullptr;
  }
  if (s_mem)
  {
    std::free(s_mem);
    s_mem = nullptr;
  }

  std::memset(g_state.pgxp_gte, 0, sizeof(g_state.pgxp_gte));
  std::memset(g_state.pgxp_gpr, 0, sizeof(g_state.pgxp_gpr));
  std::memset(g_state.pgxp_cop0, 0, sizeof(g_state.pgxp_cop0));
}

// Instruction register decoding
#define op(_instr) (_instr >> 26)          // The op part of the instruction register
#define func(_instr) ((_instr) & 0x3F)     // The funct part of the instruction register
#define sa(_instr) ((_instr >> 6) & 0x1F)  // The sa part of the instruction register
#define rd(_instr) ((_instr >> 11) & 0x1F) // The rd part of the instruction register
#define rt(_instr) ((_instr >> 16) & 0x1F) // The rt part of the instruction register
#define rs(_instr) ((_instr >> 21) & 0x1F) // The rs part of the instruction register
#define imm(_instr) (_instr & 0xFFFF)      // The immediate part of the instruction register
#define cop2idx(_instr) (((_instr >> 11) & 0x1F) | ((_instr >> 17) & 0x20))

#define SX0 (g_state.pgxp_gte[12].x)
#define SY0 (g_state.pgxp_gte[12].y)
#define SX1 (g_state.pgxp_gte[13].x)
#define SY1 (g_state.pgxp_gte[13].y)
#define SX2 (g_state.pgxp_gte[14].x)
#define SY2 (g_state.pgxp_gte[14].y)

#define SXY0 (g_state.pgxp_gte[12])
#define SXY1 (g_state.pgxp_gte[13])
#define SXY2 (g_state.pgxp_gte[14])
#define SXYP (g_state.pgxp_gte[15])

ALWAYS_INLINE_RELEASE bool CPU::PGXP::IsExactZero(const PGXP_value& value, u32 psx_value)
{
  return (psx_value == 0 && (value.flags & (VALID_XY | VALID_Z)) == VALID_XY && value.x == 0.0f && value.y == 0.0f);
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::SetFromNativeValue(PGXP_value& value)
{
  value.x = static_cast<float>(static_cast<s16>(value.value));
  value.y = static_cast<float>(static_cast<s16>(value.value >> 16));
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::Validate(PGXP_value* pV, u32 psxV)
{
  pV->flags = (pV->value == psxV) ? pV->flags : 0;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::MaskValidate(PGXP_value* pV, u32 psxV, u32 mask, u32 validMask)
{
  pV->flags = ((pV->value & mask) == (psxV & mask)) ? pV->flags : (pV->flags & ~validMask);
}

ALWAYS_INLINE_RELEASE double CPU::PGXP::f16Sign(double in)
{
  const s32 s = static_cast<s32>(static_cast<s64>(in * (USHRT_MAX + 1)));
  return static_cast<double>(s) / static_cast<double>(USHRT_MAX + 1);
}
ALWAYS_INLINE_RELEASE double CPU::PGXP::f16Unsign(double in)
{
  return (in >= 0) ? in : (in + (USHRT_MAX + 1));
}
ALWAYS_INLINE_RELEASE double CPU::PGXP::f16Overflow(double in)
{
  return std::floor(in / static_cast<double>(USHRT_MAX + 1));
}

ALWAYS_INLINE_RELEASE CPU::PGXP_value* CPU::PGXP::GetPtr(u32 addr)
{
#if 0
  if ((addr & CPU::KSEG_MASK) >= 0x0017A2B4 &&
      (addr & CPU::KSEG_MASK) <= 0x0017A2B4)
    __debugbreak();
#endif

  if ((addr & SCRATCHPAD_ADDR_MASK) == SCRATCHPAD_ADDR)
    return &s_mem[PGXP_MEM_SCRATCH_OFFSET + ((addr & SCRATCHPAD_OFFSET_MASK) >> 2)];

  const u32 paddr = (addr & KSEG_MASK);
  if (paddr < Bus::RAM_MIRROR_END)
    return &s_mem[(paddr & Bus::g_ram_mask) >> 2];
  else
    return nullptr;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::ValidateAndCopyMem(PGXP_value* dest, u32 addr, u32 value)
{
  PGXP_value* pMem = GetPtr(addr);
  if (!pMem)
  {
    *dest = PGXP_value_invalid;
    return;
  }

  Validate(pMem, value);
  *dest = *pMem;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::ValidateAndCopyMem16(PGXP_value* dest, u32 addr, u32 value, bool sign)
{
  PGXP_value* pMem = GetPtr(addr);
  if (!pMem)
  {
    *dest = PGXP_value_invalid;
    return;
  }

  psx_value val{0}, mask{0};
  u32 valid_mask = 0;

  // determine if high or low word
  const bool hiword = ((addr & 2) != 0);
  if (hiword)
  {
    val.w.h = static_cast<u16>(value);
    mask.w.h = 0xFFFF;
    valid_mask = VALID_Y;
  }
  else
  {
    val.w.l = static_cast<u16>(value);
    mask.w.l = 0xFFFF;
    valid_mask = VALID_X;
  }

  // validate and copy whole value
  MaskValidate(pMem, val.d, mask.d, valid_mask);
  *dest = *pMem;

  // if high word then shift
  if (hiword)
  {
    dest->x = dest->y;
    dest->SetValid(COMP_X, dest->HasValid(COMP_Y));
  }

  // only set y as valid if x is also valid.. don't want to make fake values
  if (dest->HasValid(COMP_X))
  {
    dest->y = (dest->x < 0) ? -1.f * sign : 0.f;
    dest->SetValid(COMP_Y);
  }
  else
  {
    dest->y = 0.0f;
    dest->SetValid(COMP_Y, false);
  }

  dest->value = value;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::WriteMem(const PGXP_value* value, u32 addr)
{
  PGXP_value* pMem = GetPtr(addr);

  if (pMem)
  {
    *pMem = *value;
    pMem->flags = (value->flags & ~(VALID_LOWZ | VALID_HIGHZ)) |
                  ((value->flags & VALID_Z) ? (VALID_LOWZ | VALID_HIGHZ) : 0);
  }
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::WriteMem16(const PGXP_value* src, u32 addr)
{
  PGXP_value* dest = GetPtr(addr);
  if (!dest)
    return;

  // determine if high or low word
  const bool hiword = ((addr & 2) != 0);
  if (hiword)
  {
    dest->y = src->x;
    dest->SetValid(COMP_Y, src->HasValid(COMP_X));
    dest->value = (dest->value & UINT32_C(0x0000FFFF)) | (src->value << 16);
  }
  else
  {
    dest->x = src->x;
    dest->SetValid(COMP_X, src->HasValid(COMP_X));
    dest->value = (dest->value & UINT32_C(0xFFFF0000)) | (src->value & UINT32_C(0x0000FFFF));
  }

  // overwrite z/w if valid
  // TODO: Check modified
  if (src->HasValid(COMP_Z))
  {
    dest->z = src->z;
    dest->SetValid(COMP_Z);
    dest->flags |= hiword ? VALID_HIGHZ : VALID_LOWZ;
  }
  else
  {
    dest->flags &= hiword ? ~VALID_HIGHZ : ~VALID_LOWZ;
    if (dest->flags & VALID_Z && !(dest->flags & (VALID_HIGHZ | VALID_LOWZ)))
      dest->flags &= ~VALID_Z;
  }
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::CopyZIfMissing(PGXP_value& dst, const PGXP_value& src)
{
  dst.z = dst.HasValid(COMP_Z) ? dst.z : src.z;
  dst.flags |= (src.flags & VALID_Z);
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::SelectZ(PGXP_value& dst, const PGXP_value& src1, const PGXP_value& src2)
{
  // Prefer src2 if src1 is missing Z, or is potentially an imprecise value, when src2 is precise.
  dst.z = (!(src1.flags & VALID_Z) ||
           (src1.flags & VALID_TAINTED_Z && (src2.flags & (VALID_Z | VALID_TAINTED_Z)) == VALID_Z)) ?
            src2.z :
            src1.z;
  dst.flags |= ((src1.flags | src2.flags) & VALID_Z);
}

#ifdef LOG_VALUES
void CPU::PGXP::LogInstruction(u32 pc, u32 instr)
{
  if (!s_log) [[unlikely]]
  {
    s_log = std::fopen("pgxp.log", "wb");
  }
  else
  {
    std::fflush(s_log);
    std::fputc('\n', s_log);
  }

  SmallString str;
  DisassembleInstruction(&str, pc, instr);
  std::fprintf(s_log, "%08X %08X %-20s", pc, instr, str.c_str());
}

void CPU::PGXP::LogValue(const char* name, u32 rval, const PGXP_value* val)
{
  if (!s_log) [[unlikely]]
    return;

  SmallString str;
  LogValueStr(str, name, rval, val);
  std::fprintf(s_log, " %s", str.c_str());
}

void CPU::PGXP::LogValueStr(SmallStringBase& str, const char* name, u32 rval, const PGXP_value* val)
{
  str.append_format("{}=[{:08X}", name, rval);
  if (!val)
  {
    str.append(", NULL]");
  }
  else
  {
    if (val->value != rval)
      str.append_format(", PGXP{:08X}", val->value);

    str.append_format(", {{{},{},{}}}", val->x, val->y, val->z);

    if (val->flags & VALID_ALL)
    {
      str.append(", valid=");
      if (val->flags & VALID_X)
        str.append('X');
      if (val->flags & VALID_Y)
        str.append('Y');
      if (val->flags & VALID_Z)
        str.append('Z');
    }

    // if (val->flags & VALID_TAINTED_Z)
    // str.append(", tainted");

    str.append(']');
  }
}

#endif

void CPU::PGXP::GTE_RTPS(float x, float y, float z, u32 value)
{
  // push values down FIFO
  SXY0 = SXY1;
  SXY1 = SXY2;

  SXY2.x = x;
  SXY2.y = y;
  SXY2.z = z;
  SXY2.value = value;
  SXY2.flags = VALID_ALL;

  if (g_settings.gpu_pgxp_vertex_cache)
    CacheVertex(value, SXY2);
}

#define VX(n) (psxRegs.CP2D.p[n << 1].sw.l)
#define VY(n) (psxRegs.CP2D.p[n << 1].sw.h)
#define VZ(n) (psxRegs.CP2D.p[(n << 1) + 1].sw.l)

int CPU::PGXP::GTE_NCLIP_valid(u32 sxy0, u32 sxy1, u32 sxy2)
{
  Validate(&SXY0, sxy0);
  Validate(&SXY1, sxy1);
  Validate(&SXY2, sxy2);

  // Don't use accurate clipping for game-constructed values, which don't have a valid Z.
  return (((SXY0.flags & SXY1.flags & SXY2.flags & VALID_XYZ) == VALID_XYZ));
}

void CPU::PGXP::GTE_DropPreciseMACIR()
{
  for (u32 i = 0; i < 3; i++)
  {
    g_state.pgxp_gte[GTE_IR1 + i].flags = 0;
    g_state.pgxp_gte[GTE_MAC1 + i].flags = 0;
  }
}

ALWAYS_INLINE_RELEASE static void SplitIntoHalves(CPU::PGXP_value& dst, double value)
{
  const double high = std::floor(value / static_cast<double>(USHRT_MAX + 1));
  dst.x = static_cast<float>(CPU::PGXP::f16Sign(value - high * static_cast<double>(USHRT_MAX + 1)));
  dst.y = static_cast<float>(high);
}

ALWAYS_INLINE_RELEASE static double NearestToNativeHalf(double precise, double native)
{
  if (precise - native > 32768.0)
    return precise - static_cast<double>(USHRT_MAX + 1);
  else if (precise - native < -32768.0)
    return precise + static_cast<double>(USHRT_MAX + 1);
  else
    return precise;
}

void CPU::PGXP::GTE_GPF_GPL(const u32 old_ir[3], const u32* old_mac, u8 shift)
{
  const double ir0 = static_cast<double>(g_state.gte_regs.IR0);
  const double scale = static_cast<double>(1u << shift);
  for (u32 i = 0; i < 3; i++)
  {
    PGXP_value& ir = g_state.pgxp_gte[GTE_IR1 + i];
    PGXP_value& mac = g_state.pgxp_gte[GTE_MAC1 + i];
    const u32 new_ir = g_state.gte_regs.dr32[GTE_IR1 + i];
    const u32 new_mac = g_state.gte_regs.dr32[GTE_MAC1 + i];

    Validate(&ir, old_ir[i]);
    const double ir_x =
      NearestToNativeHalf(ir.GetValidX(old_ir[i]), static_cast<double>(static_cast<s16>(old_ir[i])));
    double value = ir_x * ir0;
    PGXP_value source = ir;
    bool precise = (ir.flags & VALID_X) != 0;
    if (old_mac)
    {
      Validate(&mac, old_mac[i]);
      const bool mac_precise = ((mac.flags & VALID_XY) == VALID_XY);
      value += (mac_precise ? (f16Unsign(mac.x) + static_cast<double>(mac.y) * (USHRT_MAX + 1)) :
                              static_cast<double>(static_cast<s32>(old_mac[i]))) *
               scale;
      CopyZIfMissing(source, mac);
      precise |= mac_precise;
    }
    value /= scale;

    if (!precise || value < static_cast<double>(INT_MIN) || value > static_cast<double>(INT_MAX))
    {
      mac.value = new_mac;
      mac.flags = 0;
      ir.value = new_ir;
      ir.flags = 0;
      continue;
    }

    SplitIntoHalves(mac, value);
    mac.z = source.z;
    mac.value = new_mac;
    mac.flags = (source.flags & VALID_Z) | VALID_XY | VALID_TAINTED_Z;

    ir = mac;
    ir.value = new_ir;
    if (new_ir != new_mac)
    {
      ir.x = static_cast<float>(static_cast<s16>(new_ir));
      ir.y = static_cast<float>(static_cast<s16>(new_ir >> 16));
    }
  }
}

void CPU::PGXP::GTE_MVMVA(u32 inst_bits, const u32 old_ir[3])
{
  const GTE::Instruction inst{inst_bits};
  const GTE::Regs& regs = g_state.gte_regs;
  const u32 matrix = (inst.mvmva_multiply_matrix == 1) ? GTE_LLM : GTE_LCM;
  const u32 vector = inst.mvmva_multiply_vector;
  const u32 translation = inst.mvmva_translation_vector;

  const auto read_half = [](u32 index, bool high, u32 native, bool* precise) -> double {
    PGXP_value& value = g_state.pgxp_gte[index];
    Validate(&value, native);
    const double native_half = high ? static_cast<s16>(native >> 16) : static_cast<s16>(native);
    if (!(value.flags & (high ? VALID_Y : VALID_X)))
      return native_half;

    *precise = true;
    return NearestToNativeHalf(high ? value.y : value.x, native_half);
  };

  double v[3];
  s16 native_v[3];
  bool v_precise[3] = {};
  for (u32 i = 0; i < 3; i++)
  {
    const u32 index = (vector == 3) ? (GTE_IR1 + i) : (GTE_V0 + vector * 2 + i / 2);
    const u32 native = (vector == 3) ? old_ir[i] : regs.r32[index];
    const bool high = (vector != 3 && i == 1);
    native_v[i] = high ? static_cast<s16>(native >> 16) : static_cast<s16>(native);
    v[i] = read_half(index, high, native, &v_precise[i]);
  }

  const double scale = static_cast<double>(1u << inst.GetShift());
  for (u32 row = 0; row < 3; row++)
  {
    PGXP_value& mac = g_state.pgxp_gte[GTE_MAC1 + row];
    PGXP_value& ir = g_state.pgxp_gte[GTE_IR1 + row];
    mac = {};
    ir = {};
    mac.value = regs.r32[GTE_MAC1 + row];
    ir.value = regs.r32[GTE_IR1 + row];

    if (regs.FLAG.bits & ((1u << (30 - row)) | (1u << (27 - row))))
      continue;

    double result = 0.0;
    s64 native_result = 0;
    bool precise = false;
    u8 matrix_word_halves[5] = {};
    if (translation < 2)
    {
      const s32 native = (translation == 0) ? regs.TR[row] : regs.BK[row];
      native_result = static_cast<s64>(native) * 4096;
      result = static_cast<double>(native);
      if (translation == 1)
      {
        PGXP_value& background = g_state.pgxp_gte[GTE_BK + row];
        Validate(&background, static_cast<u32>(native));
        if ((background.flags & VALID_XY) == VALID_XY)
        {
          result = f16Unsign(background.x) + static_cast<double>(background.y) * 65536.0;
          precise = true;
        }
      }
      result *= 4096.0;
    }

    for (u32 col = (translation == 2) ? 1 : 0; col < 3; col++)
    {
      const u32 element = row * 3 + col;
      const u32 index = matrix + element / 2;
      const bool high = (element & 1) != 0;
      const s16 native_m = high ? static_cast<s16>(regs.r32[index] >> 16) : static_cast<s16>(regs.r32[index]);
      bool m_precise = false;
      const double m = read_half(index, high, regs.r32[index], &m_precise);
      result += m * v[col];
      native_result += static_cast<s64>(native_m) * native_v[col];
      precise |= (m_precise && v[col] != 0.0) || (v_precise[col] && m != 0.0);
      if (m_precise && v[col] != 0.0)
        matrix_word_halves[element / 2] |= high ? 2 : 1;
    }
    result /= scale;
    native_result >>= inst.GetShift();

    if (!precise || native_result < INT_MIN || native_result > INT_MAX || result < static_cast<double>(INT_MIN) ||
        result > static_cast<double>(INT_MAX))
    {
      continue;
    }

    SplitIntoHalves(mac, result);
    mac.flags = VALID_XY | VALID_TAINTED_Z;

    float z_sum = 0.0f;
    u32 z_count = 0;
    bool z_valid = true;
    for (u32 word = 0; word < std::size(matrix_word_halves); word++)
    {
      if (matrix_word_halves[word] == 0)
        continue;

      const PGXP_value& matrix_word = g_state.pgxp_gte[matrix + word];
      z_valid &= (matrix_word_halves[word] == 3 && (matrix_word.flags & VALID_Z));
      z_sum += matrix_word.z;
      z_count++;
    }
    if (z_valid && z_count > 0)
    {
      mac.z = z_sum / static_cast<float>(z_count);
      mac.flags |= VALID_Z;
    }

    SplitIntoHalves(ir, std::clamp(result, inst.lm ? 0.0 : -32768.0, 32767.0));
    ir.z = mac.z;
    ir.flags = mac.flags;
  }
}

float CPU::PGXP::GTE_NCLIP()
{
  float nclip = ((SX0 * SY1) + (SX1 * SY2) + (SX2 * SY0) - (SX0 * SY2) - (SX1 * SY0) - (SX2 * SY1));

  // ensure fractional values are not incorrectly rounded to 0
  float nclipAbs = std::abs(nclip);
  if ((0.1f < nclipAbs) && (nclipAbs < 1.f))
    nclip += (nclip < 0.f ? -1 : 1);

  // float AX = SX1 - SX0;
  // float AY = SY1 - SY0;

  // float BX = SX2 - SX0;
  // float BY = SY2 - SY0;

  //// normalise A and B
  // float mA = sqrt((AX*AX) + (AY*AY));
  // float mB = sqrt((BX*BX) + (BY*BY));

  //// calculate AxB to get Z component of C
  // float CZ = ((AX * BY) - (AY * BX)) * (1 << 12);

  return nclip;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::CPU_MTC2_int(const PGXP_value& value, u32 reg, u32 native)
{
  PGXP_value& gte_value = g_state.pgxp_gte[reg];
  switch (reg)
  {
    case 15:
      SXY0 = SXY1;
      SXY1 = SXY2;
      SXY2 = value;
      SXY2.value = native;
      SXYP = SXY2;
      return;

    case 31:
      return;

    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 16:
    case 17:
    case 18:
    case 19:
    case 36:
    case 44:
    case 52:
    case 58:
    case 59:
    case 61:
    case 62:
    {
      const bool sign_extended = (reg != 7 && (reg < 16 || reg > 19));
      const float native_low = static_cast<float>(sign_extended ? static_cast<s32>(static_cast<s16>(native)) :
                                                                  static_cast<s32>(static_cast<u16>(native)));
      const float distance = value.x - native_low;
      const float precise_low =
        value.x + ((distance > 32768.0f) ? -65536.0f : (distance < -32768.0f) ? 65536.0f : 0.0f);
      gte_value = value;
      gte_value.y = std::floor(precise_low / static_cast<float>(USHRT_MAX + 1));
      gte_value.value = sign_extended ? SignExtend32(static_cast<u16>(native)) : ZeroExtend32(static_cast<u16>(native));
      gte_value.flags = (value.flags & ~VALID_Y) | ((value.flags & VALID_X) << 1);
      return;
    }

    default:
      gte_value = value;
      gte_value.value = native;
      return;
  }
}

////////////////////////////////////
// Data transfer tracking
////////////////////////////////////

void CPU::PGXP::CPU_MFC2(u32 instr, u32 rdVal)
{
  // CPU[Rt] = GTE_D[Rd]
  const u32 idx = cop2idx(instr);
  LOG_VALUES_1(CPU::GetGTERegisterName(idx), rdVal, &g_state.pgxp_gte[idx]);
  Validate(&g_state.pgxp_gte[idx], rdVal);
  PGXP_value& prt = DestinationGPR(rt(instr));
  prt = g_state.pgxp_gte[idx];
  prt.value = rdVal;
}

void CPU::PGXP::CPU_MTC2(u32 instr, u32 rtVal)
{
  // GTE_D[Rd] = CPU[Rt]
  const u32 idx = cop2idx(instr);
  LOG_VALUES_C1(rt(instr), rtVal);
  Validate(&g_state.pgxp_gpr[rt(instr)], rtVal);
  CPU_MTC2_int(g_state.pgxp_gpr[rt(instr)], idx, rtVal);
}

////////////////////////////////////
// Memory Access
////////////////////////////////////
void CPU::PGXP::CPU_LWC2(u32 instr, u32 addr, u32 rtVal)
{
  // GTE_D[Rt] = Mem[addr]
  LOG_VALUES_LOAD(addr, rtVal);
  PGXP_value val;
  ValidateAndCopyMem(&val, addr, rtVal);
  CPU_MTC2_int(val, rt(instr), rtVal);
}

void CPU::PGXP::CPU_SWC2(u32 instr, u32 addr, u32 rtVal)
{
  //  Mem[addr] = GTE_D[Rt]
  const u32 idx = rt(instr);
#ifdef LOG_VALUES
  LOG_VALUES_1(CPU::GetGTERegisterName(idx), rtVal, &g_state.pgxp_gte[idx]);
  std::fprintf(s_log, " addr=%08X", addr);
#endif
  Validate(&g_state.pgxp_gte[idx], rtVal);
  WriteMem(&g_state.pgxp_gte[idx], addr);
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::CacheVertex(u32 value, const PGXP_value& vertex)
{
  const s16 sx = static_cast<s16>(value & 0xFFFFu);
  const s16 sy = static_cast<s16>(value >> 16);
  DebugAssert(sx >= -1024 && sx <= 1023 && sy >= -1024 && sy <= 1023);
  s_vertex_cache[(sy + 1024) * VERTEX_CACHE_WIDTH + (sx + 1024)] = vertex;
}

ALWAYS_INLINE_RELEASE CPU::PGXP_value* CPU::PGXP::GetCachedVertex(u32 value)
{
  const s16 sx = static_cast<s16>(value & 0xFFFFu);
  const s16 sy = static_cast<s16>(value >> 16);
  return (sx >= -1024 && sx <= 1023 && sy >= -1024 && sy <= 1023) ?
           &s_vertex_cache[(sy + 1024) * VERTEX_CACHE_WIDTH + (sx + 1024)] :
           nullptr;
}

ALWAYS_INLINE_RELEASE float CPU::PGXP::TruncateVertexPosition(float p)
{
  const s32 int_part = static_cast<s32>(p);
  const float int_part_f = static_cast<float>(int_part);
  return static_cast<float>(static_cast<s16>(int_part << 5) >> 5) + (p - int_part_f);
}

ALWAYS_INLINE_RELEASE bool CPU::PGXP::IsWithinTolerance(float precise_x, float precise_y, int int_x, int int_y)
{
  const float tolerance = g_settings.gpu_pgxp_tolerance;
  if (tolerance < 0.0f)
    return true;

  return (std::abs(precise_x - static_cast<float>(int_x)) <= tolerance &&
          std::abs(precise_y - static_cast<float>(int_y)) <= tolerance);
}

bool CPU::PGXP::GetPreciseVertex(u32 addr, u32 value, int x, int y, int xOffs, int yOffs, float* out_x, float* out_y,
                                 float* out_w)
{
  const PGXP_value* vert = GetPtr(addr);
  if (vert && ((vert->flags & VALID_XY) == VALID_XY) && (vert->value == value))
  {
    // There is a value here with valid X and Y coordinates
    *out_x = TruncateVertexPosition(vert->x) + static_cast<float>(xOffs);
    *out_y = TruncateVertexPosition(vert->y) + static_cast<float>(yOffs);
    *out_w = vert->z / 32768.0f;

#ifdef LOG_LOOKUPS
    GL_INS_FMT("0x{:08X} {},{} => {},{} ({},{},{}) ({},{})", addr, x, y, *out_x, *out_y,
               TruncateVertexPosition(vert->x), TruncateVertexPosition(vert->y), vert->z, std::abs(*out_x - x),
               std::abs(*out_y - y));
#endif

    if (IsWithinTolerance(*out_x, *out_y, x, y))
    {
      // check validity of z component
      return ((vert->flags & VALID_Z) == VALID_Z);
    }
  }

  if (g_settings.gpu_pgxp_vertex_cache)
  {
    vert = GetCachedVertex(value);
    if (vert && (vert->flags & VALID_XY) == VALID_XY)
    {
      *out_x = TruncateVertexPosition(vert->x) + static_cast<float>(xOffs);
      *out_y = TruncateVertexPosition(vert->y) + static_cast<float>(yOffs);
      *out_w = vert->z / 32768.0f;

      if (IsWithinTolerance(*out_x, *out_y, x, y))
        return false;
    }
  }

  // no valid value can be found anywhere, use the native PSX data
  *out_x = static_cast<float>(x);
  *out_y = static_cast<float>(y);
  *out_w = 1.0f;
  return false;
}

// Instruction register decoding
#define op(_instr) (_instr >> 26)          // The op part of the instruction register
#define func(_instr) ((_instr) & 0x3F)     // The funct part of the instruction register
#define sa(_instr) ((_instr >> 6) & 0x1F)  // The sa part of the instruction register
#define rd(_instr) ((_instr >> 11) & 0x1F) // The rd part of the instruction register
#define rt(_instr) ((_instr >> 16) & 0x1F) // The rt part of the instruction register
#define rs(_instr) ((_instr >> 21) & 0x1F) // The rs part of the instruction register
#define imm(_instr) (_instr & 0xFFFF)      // The immediate part of the instruction register
#define imm_sext(_instr)                                                                                               \
  static_cast<s32>(static_cast<s16>(_instr & 0xFFFF)) // The immediate part of the instruction register

void CPU::PGXP::CPU_LW(u32 instr, u32 addr, u32 rtVal)
{
  // Rt = Mem[Rs + Im]
  LOG_VALUES_LOAD(addr, rtVal);
  ValidateAndCopyMem(&DestinationGPR(rt(instr)), addr, rtVal);
}

void CPU::PGXP::CPU_LBx(u32 instr, u32 addr, u32 rtVal)
{
  LOG_VALUES_LOAD(addr, rtVal);
  DestinationGPR(rt(instr)) = PGXP_value_invalid;
}

void CPU::PGXP::CPU_LH(u32 instr, u32 addr, u32 rtVal)
{
  // Rt = Mem[Rs + Im] (sign extended)
  LOG_VALUES_LOAD(addr, rtVal);
  ValidateAndCopyMem16(&DestinationGPR(rt(instr)), addr, rtVal, true);
}

void CPU::PGXP::CPU_LHU(u32 instr, u32 addr, u32 rtVal)
{
  // Rt = Mem[Rs + Im] (zero extended)
  LOG_VALUES_LOAD(addr, rtVal);
  ValidateAndCopyMem16(&DestinationGPR(rt(instr)), addr, rtVal, false);
}

void CPU::PGXP::CPU_SB(u32 instr, u32 addr, u32 rtVal)
{
  LOG_VALUES_STORE(rt(instr), rtVal, addr);
  WriteMem(&PGXP_value_invalid, addr);
}

void CPU::PGXP::CPU_SH(u32 instr, u32 addr, u32 rtVal)
{
  LOG_VALUES_STORE(rt(instr), rtVal, addr);
  PGXP_value* val = &g_state.pgxp_gpr[rt(instr)];
  Validate(val, rtVal);
  WriteMem16(val, addr);
}

void CPU::PGXP::CPU_SW(u32 instr, u32 addr, u32 rtVal)
{
  // Mem[Rs + Im] = Rt
  LOG_VALUES_STORE(rt(instr), rtVal, addr);
  PGXP_value* val = &g_state.pgxp_gpr[rt(instr)];
  Validate(val, rtVal);
  WriteMem(val, addr);
}

namespace CPU::PGXP {
namespace {
struct UnalignedWordSources
{
  std::array<u8, 4> byte_sources;
};

static constexpr u8 FROM_SECOND = 0x80;

UnalignedWordSources GetUnalignedWordSources(InstructionOp op, u32 shift)
{
  UnalignedWordSources sources;
  for (u32 i = 0; i < 4; i++)
  {
    switch (op)
    {
      case InstructionOp::lwl:
        sources.byte_sources[i] = (i >= 3 - shift) ? static_cast<u8>(FROM_SECOND | (i - (3 - shift))) : static_cast<u8>(i);
        break;
      case InstructionOp::lwr:
        sources.byte_sources[i] = (i + shift <= 3) ? static_cast<u8>(FROM_SECOND | (i + shift)) : static_cast<u8>(i);
        break;
      case InstructionOp::swl:
        sources.byte_sources[i] = (i <= shift) ? static_cast<u8>(FROM_SECOND | (i + (3 - shift))) : static_cast<u8>(i);
        break;
      case InstructionOp::swr:
      default:
        sources.byte_sources[i] = (i >= shift) ? static_cast<u8>(FROM_SECOND | (i - shift)) : static_cast<u8>(i);
        break;
    }
  }
  return sources;
}

u32 GetBytesFromFirstMask(const UnalignedWordSources& sources)
{
  u32 mask = 0;
  for (u32 i = 0; i < 4; i++)
  {
    if (!(sources.byte_sources[i] & FROM_SECOND))
      mask |= 0xFFu << (i * 8);
  }
  return mask;
}

u32 GetSecondSourceBytesAsWord(const UnalignedWordSources& sources, u32 merged)
{
  u32 value = 0;
  for (u32 i = 0; i < 4; i++)
  {
    if (sources.byte_sources[i] & FROM_SECOND)
      value |= ((merged >> (i * 8)) & 0xFFu) << ((sources.byte_sources[i] & 3u) * 8);
  }
  return value;
}

u32 GetSecondSourceMask(const UnalignedWordSources& sources)
{
  u32 mask = 0;
  for (u32 i = 0; i < 4; i++)
  {
    if (sources.byte_sources[i] & FROM_SECOND)
      mask |= 0xFFu << ((sources.byte_sources[i] & 3u) * 8);
  }
  return mask;
}

PGXP_value MergeUnalignedWord(const UnalignedWordSources& sources, const PGXP_value& first,
                              const std::array<u32, 2>& first_z_flags, const PGXP_value& second,
                              const std::array<u32, 2>& second_z_flags, u32 merged, std::array<u32, 2>* out_z_flags)
{
  PGXP_value result = first;
  result.value = merged;
  result.flags &= ~(VALID_X | VALID_Y | VALID_Z | VALID_LOWZ | VALID_HIGHZ);

  bool z_set = false;
  for (u32 half = 0; half < 2; half++)
  {
    const u8 lo = sources.byte_sources[half * 2];
    const u8 hi = sources.byte_sources[half * 2 + 1];
    const u32 coord_flag = half ? VALID_Y : VALID_X;
    float& coord = half ? result.y : result.x;
    (*out_z_flags)[half] = 0;

    const bool same_source = ((lo & FROM_SECOND) == (hi & FROM_SECOND));
    const u32 src_lo = lo & 3u;
    if (!same_source || (src_lo & 1u) != 0 || (hi & 3u) != src_lo + 1)
    {
      coord = static_cast<float>(static_cast<s16>(merged >> (half * 16)));
      continue;
    }

    const u32 src_half = src_lo >> 1;
    const PGXP_value& src = (lo & FROM_SECOND) ? second : first;
    const u32 src_z_flag = ((lo & FROM_SECOND) ? second_z_flags : first_z_flags)[src_half];
    coord = src_half ? src.y : src.x;
    if (src.flags & (src_half ? VALID_Y : VALID_X))
      result.flags |= coord_flag;
    if (src_z_flag)
    {
      result.z = src.z;
      (*out_z_flags)[half] = half ? VALID_HIGHZ : VALID_LOWZ;
      z_set = true;
    }
  }

  if (z_set)
    result.flags |= VALID_Z;
  return result;
}
} // namespace
} // namespace CPU::PGXP

void CPU::PGXP::CPU_LWx(u32 instr, u32 addr, u32 rtVal)
{
  LOG_VALUES_LOAD(addr, rtVal);

  const Instruction inst{instr};
  PGXP_value* pmem = GetPtr(addr & ~3u);
  PGXP_value& prt = DestinationGPR(rt(instr));
  if (!pmem)
  {
    prt = PGXP_value_invalid;
    return;
  }

  const UnalignedWordSources sources = GetUnalignedWordSources(inst.op, addr & 3u);
  MaskValidate(&prt, rtVal, GetBytesFromFirstMask(sources), VALID_X | VALID_Y | VALID_Z);
  const u32 second_mask = GetSecondSourceMask(sources);
  MaskValidate(pmem, GetSecondSourceBytesAsWord(sources, rtVal), second_mask,
               VALID_X | VALID_Y | VALID_Z | VALID_LOWZ | VALID_HIGHZ);

  const u32 reg_z = (prt.flags & VALID_Z) ? VALID_Z : 0;
  std::array<u32, 2> z_flags;
  prt = MergeUnalignedWord(sources, prt, {reg_z, reg_z}, *pmem,
                           {pmem->flags & VALID_LOWZ, pmem->flags & VALID_HIGHZ}, rtVal, &z_flags);
}

void CPU::PGXP::CPU_SWx(u32 instr, u32 addr, u32 memVal)
{
  LOG_VALUES_STORE(rt(instr), memVal, addr);

  const Instruction inst{instr};
  PGXP_value* pmem = GetPtr(addr & ~3u);
  if (!pmem)
    return;

  PGXP_value& prt = g_state.pgxp_gpr[rt(instr)];
  const UnalignedWordSources sources = GetUnalignedWordSources(inst.op, addr & 3u);
  MaskValidate(pmem, memVal, GetBytesFromFirstMask(sources), VALID_X | VALID_Y | VALID_Z | VALID_LOWZ | VALID_HIGHZ);
  const u32 second_mask = GetSecondSourceMask(sources);
  MaskValidate(&prt, GetSecondSourceBytesAsWord(sources, memVal), second_mask, VALID_X | VALID_Y | VALID_Z);

  const u32 reg_z = (prt.flags & VALID_Z) ? VALID_Z : 0;
  std::array<u32, 2> z_flags;
  *pmem = MergeUnalignedWord(sources, *pmem, {pmem->flags & VALID_LOWZ, pmem->flags & VALID_HIGHZ}, prt,
                             {reg_z, reg_z}, memVal, &z_flags);
  pmem->flags |= z_flags[0] | z_flags[1];
}

void CPU::PGXP::CPU_MOVE_Packed(u32 rd_and_rs, u32 rsVal)
{
  const u32 Rs = (rd_and_rs & 0xFFu);
  const u32 Rd = (rd_and_rs >> 8);
  CPU_MOVE(Rd, Rs, rsVal);
}

void CPU::PGXP::CPU_MOVE(u32 Rd, u32 Rs, u32 rsVal)
{
#ifdef LOG_VALUES
  const u32 instr = 0;
  LOG_VALUES_C1(Rs, rsVal);
#endif
  Validate(&g_state.pgxp_gpr[Rs], rsVal);
  DestinationGPR(Rd) = g_state.pgxp_gpr[Rs];
}

void CPU::PGXP::CPU_ADDI(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs + Imm (signed)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  Validate(&prsVal, rsVal);

  psx_value tempImm;
  tempImm.d = SignExtend32(static_cast<u16>(imm(instr)));

  PGXP_value& prtVal = DestinationGPR(rt(instr));
  prtVal = prsVal;

  if (tempImm.d == 0)
    return;

  if (rsVal == 0)
  {
    // x is low precision value
    prtVal.x = static_cast<float>(tempImm.sw.l);
    prtVal.y = static_cast<float>(tempImm.sw.h);
    prtVal.flags |= VALID_X | VALID_Y | VALID_TAINTED_Z;
    prtVal.value = tempImm.d;
    return;
  }

  const double x = f16Unsign(prsVal.GetValidX(rsVal)) + static_cast<double>(tempImm.w.l);
  const float of = (x >= (USHRT_MAX + 1)) ? 1.f : (x < 0) ? -1.f : 0.f;
  prtVal.x = static_cast<float>(f16Sign(x));
  prtVal.y = prsVal.GetValidY(rsVal) + tempImm.sw.h + of;
  prtVal.y += (prtVal.y >= (SHRT_MAX + 1)) ? -(USHRT_MAX + 1) : (prtVal.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

  prtVal.value = rsVal + tempImm.d;

  prtVal.flags |= VALID_TAINTED_Z;
}

void CPU::PGXP::CPU_ANDI(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs & Imm
  const u32 rtVal = rsVal & imm(instr);
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  Validate(&prsVal, rsVal);

  psx_value vRt;
  vRt.d = rtVal;

  PGXP_value& prtVal = DestinationGPR(rt(instr));
  prtVal = prsVal;

  prtVal.value = rtVal;
  prtVal.y = 0.f; // remove upper 16-bits
  prtVal.SetValid(COMP_Y);
  prtVal.flags |= VALID_TAINTED_Z;

  if (imm(instr) == 0)
  {
    prtVal.x = 0.0f;
    prtVal.SetValid(COMP_X);
  }
  else if (static_cast<u16>(rtVal) != static_cast<u16>(rsVal))
  {
    prtVal.x = vRt.sw.l;
    prtVal.SetValid(COMP_X);
  }
}

void CPU::PGXP::CPU_ORI(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs | Imm
  const u32 rtVal = rsVal | imm(instr);

  Validate(&g_state.pgxp_gpr[rs(instr)], rsVal);
  PGXP_value ret = g_state.pgxp_gpr[rs(instr)];

  psx_value vRt;
  vRt.d = rtVal;

  if (static_cast<u16>(rtVal) != static_cast<u16>(rsVal))
  {
    ret.x = vRt.sw.l;
    ret.SetValid(COMP_X);
    ret.flags |= VALID_TAINTED_Z;
  }

  ret.value = rtVal;
  DestinationGPR(rt(instr)) = ret;
}

void CPU::PGXP::CPU_XORI(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs ^ Imm
  const u32 rtVal = rsVal ^ imm(instr);

  Validate(&g_state.pgxp_gpr[rs(instr)], rsVal);
  PGXP_value ret = g_state.pgxp_gpr[rs(instr)];

  psx_value vRt;
  vRt.d = rtVal;

  switch (imm(instr))
  {
    case 0:
      // if 0 then x == x
      break;
    default:
      // otherwise x is low precision value
      ret.x = vRt.sw.l;
      ret.SetValid(COMP_X);
      ret.flags |= VALID_TAINTED_Z;
      break;
  }

  ret.value = rtVal;
  DestinationGPR(rt(instr)) = ret;
}

void CPU::PGXP::CPU_SLTI(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs < Imm (signed)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  Validate(&prsVal, rsVal);

  PGXP_value& prtVal = DestinationGPR(rt(instr));
  prtVal.value = BoolToUInt32(static_cast<s32>(rsVal) < imm_sext(instr));
  prtVal.x = static_cast<float>(prtVal.value);
  prtVal.y = 0.0f;
  prtVal.z = prsVal.z;
  prtVal.flags = prsVal.flags | VALID_X | VALID_Y | VALID_TAINTED_Z;
}

void CPU::PGXP::CPU_SLTIU(u32 instr, u32 rsVal)
{
  LOG_VALUES_C1(rs(instr), rsVal);

  // Rt = Rs < Imm (Unsigned)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  Validate(&g_state.pgxp_gpr[rs(instr)], rsVal);

  PGXP_value& prtVal = DestinationGPR(rt(instr));
  prtVal.value = BoolToUInt32(rsVal < static_cast<u32>(imm_sext(instr)));
  prtVal.x = static_cast<float>(prtVal.value);
  prtVal.y = 0.f;
  prtVal.z = prsVal.z;
  prtVal.flags = prsVal.flags | VALID_X | VALID_Y | VALID_TAINTED_Z;
}

////////////////////////////////////
// Load Upper
////////////////////////////////////
void CPU::PGXP::CPU_LUI(u32 instr)
{
  LOG_VALUES_NV();

  // Rt = Imm << 16
  PGXP_value& prt = DestinationGPR(rt(instr));
  prt = PGXP_value_zero;
  prt.y = (float)(s16)imm(instr);
  prt.value = static_cast<u32>(imm(instr)) << 16;
  prt.flags = VALID_XY;
}

////////////////////////////////////
// Register Arithmetic
////////////////////////////////////

void CPU::PGXP::CPU_ADD(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  PGXP_value prdVal;

  // Rd = Rs + Rt (signed)
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  if (rtVal == 0 && !IsExactZero(prsVal, rsVal))
  {
    prdVal = prsVal;
    CopyZIfMissing(prdVal, prtVal);
  }
  else if (rsVal == 0)
  {
    prdVal = prtVal;
    CopyZIfMissing(prdVal, prsVal);
  }
  else
  {
    const double x = f16Unsign(prsVal.GetValidX(rsVal)) + f16Unsign(prtVal.GetValidX(rtVal));

    // carry on over/underflow
    const float of = (x >= (USHRT_MAX + 1)) ? 1.f : (x < 0) ? -1.f : 0.f;
    prdVal.x = static_cast<float>(f16Sign(x));
    // prdVal.x -= of * (USHRT_MAX + 1);
    prdVal.y = prsVal.GetValidY(rsVal) + prtVal.GetValidY(rtVal) + of;

    // truncate on overflow/underflow
    prdVal.y += (prdVal.y >= (SHRT_MAX + 1)) ? -(USHRT_MAX + 1) : (prdVal.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

    prdVal.value = rsVal + rtVal;

    // valid x/y only if one side had a valid x/y
    prdVal.flags = prsVal.flags | (prtVal.flags & VALID_XY) | VALID_TAINTED_Z;

    SelectZ(prdVal, prsVal, prtVal);
    if (prsVal.flags & prtVal.flags & VALID_Z)
      prdVal.z = 0.5f * (prsVal.z + prtVal.z);
    if (prsVal.flags & prtVal.flags & VALID_Z)
      prdVal.z = 0.5f * (prsVal.z + prtVal.z);
  }

  DestinationGPR(rd(instr)) = prdVal;
}

void CPU::PGXP::CPU_SUB(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  PGXP_value prdVal;

  // Rd = Rs - Rt (signed)
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  if (rtVal == 0)
  {
    prdVal = prsVal;
    CopyZIfMissing(prdVal, prtVal);
    if (IsExactZero(prsVal, rsVal) && !IsExactZero(prtVal, rtVal))
      prdVal.flags &= ~VALID_XY;
  }
  else
  {
    const double x = f16Unsign(prsVal.GetValidX(rsVal)) - f16Unsign(prtVal.GetValidX(rtVal));

    // carry on over/underflow
    const float of = (x >= (USHRT_MAX + 1)) ? 1.f : (x < 0) ? -1.f : 0.f;
    prdVal.x = static_cast<float>(f16Sign(x));
    // prdVal.x -= of * (USHRT_MAX + 1);
    prdVal.y = prsVal.GetValidY(rsVal) - (prtVal.GetValidY(rtVal) - of);

    // truncate on overflow/underflow
    prdVal.y += (prdVal.y >= (SHRT_MAX + 1)) ? -(USHRT_MAX + 1) : (prdVal.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

    prdVal.value = rsVal - rtVal;

    // valid x/y only if one side had a valid x/y
    prdVal.flags = (IsExactZero(prsVal, rsVal) ? 0u : prsVal.flags) | (prtVal.flags & VALID_XY) | VALID_TAINTED_Z;

    SelectZ(prdVal, prsVal, prtVal);
  }

  DestinationGPR(rd(instr)) = prdVal;
}

ALWAYS_INLINE_RELEASE const CPU::PGXP_value*
CPU::PGXP::KeepExactHalvesThroughPackedMask(PGXP_value& result, u32 rsVal, u32 rtVal, const PGXP_value& prsVal,
                                            const PGXP_value& prtVal, u32 rdVal)
{
  const bool rt_is_mask = ((rtVal >> 16) == 0xFFFFu);
  const u32 mask_value = rt_is_mask ? rtVal : rsVal;
  const u32 source_value = rt_is_mask ? rsVal : rtVal;
  const PGXP_value& source = rt_is_mask ? prsVal : prtVal;
  const u16 low_mask = static_cast<u16>(mask_value);
  const s16 source_high = static_cast<s16>(source_value >> 16);
  if ((mask_value >> 16) != 0xFFFFu || low_mask < 0x0FFF || low_mask == 0xFFFF || (low_mask & (low_mask + 1)) != 0 ||
      source_high == 0 || source_high == -1 || (source.flags & VALID_ALL) != VALID_ALL)
  {
    return nullptr;
  }

  result.x = static_cast<float>(static_cast<s16>(rdVal)) + (source.x - static_cast<float>(static_cast<s16>(source_value)));
  result.y = source.y + static_cast<float>(static_cast<u16>(source_value) & ~low_mask) /
                          static_cast<float>(USHRT_MAX + 1);
  return &source;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::CPU_BITWISE(u32 instr, u32 rdVal, u32 rsVal, u32 rtVal)
{
  // Rd = Rs & Rt
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  psx_value vald, vals, valt;
  vald.d = rdVal;
  vals.d = rsVal;
  valt.d = rtVal;

  const bool rs_zero = IsExactZero(prsVal, rsVal);
  const bool rt_zero = IsExactZero(prtVal, rtVal);
  const u32 source_flags = (rs_zero ? 0u : prsVal.flags) | (rt_zero ? 0u : prtVal.flags);
  const bool exact_zero_result = (rdVal == 0 && (rs_zero || rt_zero) && (instr & 0x3F) != NOR_FUNCT);

  PGXP_value ret;
  ret.flags = ((source_flags & VALID_XY) || exact_zero_result) ? (VALID_XY | VALID_TAINTED_Z) : 0;

  const PGXP_value* x_source = nullptr;
  const PGXP_value* y_source = nullptr;
  if (vald.w.l == 0)
    ret.x = 0.f;
  else if (vald.w.l == vals.w.l && (prsVal.flags & VALID_X))
    ret.x = (x_source = &prsVal)->x;
  else if (vald.w.l == valt.w.l && (prtVal.flags & VALID_X))
    ret.x = (x_source = &prtVal)->x;
  else
    ret.x = static_cast<float>(vald.sw.l);

  if (vald.w.h == 0)
    ret.y = 0.f;
  else if (vald.w.h == vals.w.h && (prsVal.flags & VALID_Y))
    ret.y = (y_source = &prsVal)->y;
  else if (vald.w.h == valt.w.h && (prtVal.flags & VALID_Y))
    ret.y = (y_source = &prtVal)->y;
  else
    ret.y = static_cast<float>(vald.sw.h);

  if ((instr & 0x3F) == AND_FUNCT)
  {
    if (const PGXP_value* mask_source = KeepExactHalvesThroughPackedMask(ret, rsVal, rtVal, prsVal, prtVal, rdVal))
      x_source = y_source = mask_source;
  }

  SelectZ(ret, prsVal, prtVal);
  if (x_source && y_source && x_source != y_source && (x_source->flags & y_source->flags & VALID_Z))
    ret.z = 0.5f * (x_source->z + y_source->z);

  ret.value = rdVal;
  DestinationGPR(rd(instr)) = ret;
}

void CPU::PGXP::CPU_AND_(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs & Rt
  const u32 rdVal = rsVal & rtVal;
  CPU_BITWISE(instr, rdVal, rsVal, rtVal);
}

void CPU::PGXP::CPU_OR_(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs | Rt
  if (rtVal == 0 && !IsExactZero(g_state.pgxp_gpr[rs(instr)], rsVal))
    CPU_MOVE(rd(instr), rs(instr), rsVal);
  else if (rsVal == 0)
    CPU_MOVE(rd(instr), rt(instr), rtVal);
  else
    CPU_BITWISE(instr, rsVal | rtVal, rsVal, rtVal);
}

void CPU::PGXP::CPU_XOR_(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs ^ Rt
  if (rs(instr) == rt(instr))
    CPU_BITWISE(instr, 0, rsVal, rtVal);
  else if (rtVal == 0 && !IsExactZero(g_state.pgxp_gpr[rs(instr)], rsVal))
    CPU_MOVE(rd(instr), rs(instr), rsVal);
  else if (rsVal == 0)
    CPU_MOVE(rd(instr), rt(instr), rtVal);
  else
    CPU_BITWISE(instr, rsVal ^ rtVal, rsVal, rtVal);
}

void CPU::PGXP::CPU_NOR(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs NOR Rt
  const u32 rdVal = ~(rsVal | rtVal);
  CPU_BITWISE(instr, rdVal, rsVal, rtVal);
}

void CPU::PGXP::CPU_SLT(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs < Rt (signed)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value ret = prsVal;
  ret.value = BoolToUInt32(static_cast<s32>(rsVal) < static_cast<s32>(rtVal));
  ret.x = static_cast<float>(ret.value);
  ret.y = 0.f;
  ret.flags |= VALID_TAINTED_Z | VALID_X | VALID_Y;
  DestinationGPR(rd(instr)) = ret;
}

void CPU::PGXP::CPU_SLTU(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Rd = Rs < Rt (unsigned)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value ret = prsVal;
  ret.value = BoolToUInt32(rsVal < rtVal);
  ret.x = static_cast<float>(ret.value);
  ret.y = 0.f;
  ret.flags |= VALID_TAINTED_Z | VALID_X | VALID_Y;
  DestinationGPR(rd(instr)) = ret;
}

////////////////////////////////////
// Register mult/div
////////////////////////////////////

void CPU::PGXP::CPU_MULT(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Hi/Lo = Rs * Rt (signed)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value& ploVal = g_state.pgxp_gpr[static_cast<u8>(Reg::lo)];
  PGXP_value& phiVal = g_state.pgxp_gpr[static_cast<u8>(Reg::hi)];
  ploVal = prsVal;
  CopyZIfMissing(ploVal, prtVal);

  // Z/valid is the same
  phiVal = ploVal;

  double xx, xy, yx, yy;
  double lx = 0, ly = 0, hx = 0, hy = 0;

  const float rsx = prsVal.GetValidX(rsVal);
  const float rsy = prsVal.GetValidY(rsVal);
  const float rtx = prtVal.GetValidX(rtVal);
  const float rty = prtVal.GetValidY(rtVal);

  // Multiply out components
  xx = f16Unsign(rsx) * f16Unsign(rtx);
  xy = f16Unsign(rsx) * (rty);
  yx = (rsy)*f16Unsign(rtx);
  yy = (rsy) * (rty);

  // Split values into outputs
  lx = xx;

  ly = f16Overflow(xx);
  ly += xy + yx;

  hx = f16Overflow(ly);
  hx += yy;

  hy = f16Overflow(hx);

  ploVal.x = (float)f16Sign(lx);
  ploVal.y = (float)f16Sign(ly);
  ploVal.flags |= VALID_TAINTED_Z | (prtVal.flags & VALID_XY);
  phiVal.x = (float)f16Sign(hx);
  phiVal.y = (float)f16Sign(hy);
  phiVal.flags |= VALID_TAINTED_Z | (prtVal.flags & VALID_XY);

  // compute PSX value
  const u64 result = static_cast<u64>(static_cast<s64>(SignExtend64(rsVal)) * static_cast<s64>(SignExtend64(rtVal)));
  phiVal.value = Truncate32(result >> 32);
  ploVal.value = Truncate32(result);
}

void CPU::PGXP::CPU_MULTU(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Hi/Lo = Rs * Rt (unsigned)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value& ploVal = g_state.pgxp_gpr[static_cast<u8>(Reg::lo)];
  PGXP_value& phiVal = g_state.pgxp_gpr[static_cast<u8>(Reg::hi)];
  ploVal = prsVal;
  CopyZIfMissing(ploVal, prtVal);

  // Z/valid is the same
  phiVal = ploVal;

  double xx, xy, yx, yy;
  double lx = 0, ly = 0, hx = 0, hy = 0;

  const float rsx = prsVal.GetValidX(rsVal);
  const float rsy = prsVal.GetValidY(rsVal);
  const float rtx = prtVal.GetValidX(rtVal);
  const float rty = prtVal.GetValidY(rtVal);

  // Multiply out components
  xx = f16Unsign(rsx) * f16Unsign(rtx);
  xy = f16Unsign(rsx) * f16Unsign(rty);
  yx = f16Unsign(rsy) * f16Unsign(rtx);
  yy = f16Unsign(rsy) * f16Unsign(rty);

  // Split values into outputs
  lx = xx;

  ly = f16Overflow(xx);
  ly += xy + yx;

  hx = f16Overflow(ly);
  hx += yy;

  hy = f16Overflow(hx);

  ploVal.x = (float)f16Sign(lx);
  ploVal.y = (float)f16Sign(ly);
  ploVal.flags |= VALID_TAINTED_Z | (prtVal.flags & VALID_XY);
  phiVal.x = (float)f16Sign(hx);
  phiVal.y = (float)f16Sign(hy);
  phiVal.flags |= VALID_TAINTED_Z | (prtVal.flags & VALID_XY);

  // compute PSX value
  const u64 result = ZeroExtend64(rsVal) * ZeroExtend64(rtVal);
  phiVal.value = Truncate32(result >> 32);
  ploVal.value = Truncate32(result);
}

void CPU::PGXP::CPU_DIV(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Lo = Rs / Rt (signed)
  // Hi = Rs % Rt (signed)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value& ploVal = g_state.pgxp_gpr[static_cast<u8>(Reg::lo)];
  PGXP_value& phiVal = g_state.pgxp_gpr[static_cast<u8>(Reg::hi)];
  ploVal = prsVal;
  CopyZIfMissing(ploVal, prtVal);

  // Z/valid is the same
  phiVal = ploVal;

  double vs = f16Unsign(prsVal.GetValidX(rsVal)) + prsVal.GetValidY(rsVal) * (double)(1 << 16);
  double vt = f16Unsign(prtVal.GetValidX(rtVal)) + prtVal.GetValidY(rtVal) * (double)(1 << 16);

  const bool zero_divisor = (rtVal == 0 || vt == 0.0);
  double lo = zero_divisor ? 0.0 : (vs / vt);
  ploVal.y = (float)f16Sign(f16Overflow(lo));
  ploVal.x = (float)f16Sign(lo);
  ploVal.flags |= VALID_TAINTED_Z | (zero_divisor ? 0u : (prtVal.flags & VALID_XY));

  double hi = zero_divisor ? 0.0 : fmod(vs, vt);
  phiVal.y = (float)f16Sign(f16Overflow(hi));
  phiVal.x = (float)f16Sign(hi);
  phiVal.flags |= VALID_TAINTED_Z | (zero_divisor ? 0u : (prtVal.flags & VALID_XY));

  // compute PSX value
  if (static_cast<s32>(rtVal) == 0)
  {
    // divide by zero
    ploVal.value = (static_cast<s32>(rsVal) >= 0) ? UINT32_C(0xFFFFFFFF) : UINT32_C(1);
    phiVal.value = static_cast<u32>(static_cast<s32>(rsVal));
  }
  else if (rsVal == UINT32_C(0x80000000) && static_cast<s32>(rtVal) == -1)
  {
    // unrepresentable
    ploVal.value = UINT32_C(0x80000000);
    phiVal.value = 0;
  }
  else
  {
    ploVal.value = static_cast<u32>(static_cast<s32>(rsVal) / static_cast<s32>(rtVal));
    phiVal.value = static_cast<u32>(static_cast<s32>(rsVal) % static_cast<s32>(rtVal));
  }

  if (zero_divisor)
  {
    SetFromNativeValue(ploVal);
    SetFromNativeValue(phiVal);
  }
}

void CPU::PGXP::CPU_DIVU(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rs(instr), rsVal, rt(instr), rtVal);

  // Lo = Rs / Rt (unsigned)
  // Hi = Rs % Rt (unsigned)
  PGXP_value& prsVal = g_state.pgxp_gpr[rs(instr)];
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prsVal, rsVal);
  Validate(&prtVal, rtVal);

  PGXP_value& ploVal = g_state.pgxp_gpr[static_cast<u8>(Reg::lo)];
  PGXP_value& phiVal = g_state.pgxp_gpr[static_cast<u8>(Reg::hi)];
  ploVal = prsVal;
  CopyZIfMissing(ploVal, prtVal);

  // Z/valid is the same
  phiVal = ploVal;

  double vs = f16Unsign(prsVal.GetValidX(rsVal)) + f16Unsign(prsVal.GetValidY(rsVal)) * (double)(1 << 16);
  double vt = f16Unsign(prtVal.GetValidX(rtVal)) + f16Unsign(prtVal.GetValidY(rtVal)) * (double)(1 << 16);

  const bool zero_divisor = (rtVal == 0 || vt == 0.0);
  double lo = zero_divisor ? 0.0 : (vs / vt);
  ploVal.y = (float)f16Sign(f16Overflow(lo));
  ploVal.x = (float)f16Sign(lo);
  ploVal.flags |= VALID_TAINTED_Z | (zero_divisor ? 0u : (prtVal.flags & VALID_XY));

  double hi = zero_divisor ? 0.0 : fmod(vs, vt);
  phiVal.y = (float)f16Sign(f16Overflow(hi));
  phiVal.x = (float)f16Sign(hi);
  phiVal.flags |= VALID_TAINTED_Z | (zero_divisor ? 0u : (prtVal.flags & VALID_XY));

  if (rtVal == 0)
  {
    // divide by zero
    ploVal.value = UINT32_C(0xFFFFFFFF);
    phiVal.value = rsVal;
  }
  else
  {
    ploVal.value = rsVal / rtVal;
    phiVal.value = rsVal % rtVal;
  }

  if (zero_divisor)
  {
    SetFromNativeValue(ploVal);
    SetFromNativeValue(phiVal);
  }
}

////////////////////////////////////
// Shift operations (sa)
////////////////////////////////////
ALWAYS_INLINE_RELEASE void CPU::PGXP::ShiftLeft(u32 instr, u32 rtVal, u32 sh)
{
  const u32 rdVal = rtVal << sh;
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prtVal, rtVal);

  PGXP_value result;
  result.z = prtVal.z;
  result.value = rdVal;

  if (sh >= 32)
  {
    result.x = 0.0f;
    result.y = 0.0f;
    result.flags = prtVal.flags | VALID_XY | VALID_TAINTED_Z;
  }
  else if (sh >= 16)
  {
    const double y = prtVal.GetValidX(rtVal) * static_cast<double>(1u << (sh - 16));
    result.y = static_cast<float>((sh == 16) ? y : f16Sign(f16Unsign(y)));
    result.x = 0.0f;
    result.flags = prtVal.flags | VALID_TAINTED_Z | ((prtVal.flags & VALID_Y) >> 1);
  }
  else
  {
    const bool bits_lost = ((static_cast<s32>(rdVal) >> sh) != static_cast<s32>(rtVal));
    const double x = f16Unsign(prtVal.GetValidX(rtVal)) * static_cast<double>(1u << sh);
    const double y_in = bits_lost ? static_cast<double>(static_cast<u16>(rtVal >> 16)) :
                                    f16Unsign(prtVal.GetValidY(rtVal));
    const double y = y_in * static_cast<double>(1u << sh) + f16Overflow(x);
    result.x = static_cast<float>(f16Sign(x));
    result.y = static_cast<float>(f16Sign(y));
    result.flags = (prtVal.flags & ~BITFIELD) | VALID_TAINTED_Z | (bits_lost ? BITFIELD : 0u);
  }

  DestinationGPR(rd(instr)) = result;
}

ALWAYS_INLINE_RELEASE void CPU::PGXP::ShiftRight(u32 instr, u32 rtVal, u32 sh, bool arithmetic, bool variable)
{
  PGXP_value& prtVal = g_state.pgxp_gpr[rt(instr)];
  Validate(&prtVal, rtVal);
  if (sh == 0)
  {
    DestinationGPR(rd(instr)) = prtVal;
    return;
  }

  const u32 rdVal = arithmetic ? static_cast<u32>(static_cast<s32>(rtVal) >> sh) : (rtVal >> sh);
  const s16 native_high = static_cast<s16>(rtVal >> 16);
  const s16 native_low = static_cast<s16>(rtVal);
  const double high_offset = f16Sign(prtVal.GetValidY(rtVal) - native_high);

  PGXP_value result;
  result.z = prtVal.z;
  result.value = rdVal;

  if (sh >= 16)
  {
    const double base = (prtVal.flags & BITFIELD) ?
                          static_cast<double>(static_cast<s32>(rdVal)) * static_cast<double>(1u << (sh - 16)) :
                          static_cast<double>(arithmetic ? native_high : static_cast<u16>(native_high));
    const double value = std::trunc((base + high_offset) / static_cast<double>(1u << (sh - 16)) * (USHRT_MAX + 1)) /
                         static_cast<double>(USHRT_MAX + 1);
    result.x = static_cast<float>(f16Sign(value));
    result.y = static_cast<float>(std::floor(value / static_cast<double>(USHRT_MAX + 1)));
    result.flags = (prtVal.flags & ~(VALID_X | BITFIELD)) | ((prtVal.flags & VALID_Y) >> 1) | VALID_TAINTED_Z;
    DestinationGPR(rd(instr)) = result;
    return;
  }

  if (arithmetic && !variable && !(prtVal.flags & VALID_Z))
  {
    result.x = static_cast<float>(static_cast<s16>(rdVal));
    result.y = static_cast<float>(static_cast<s16>(rdVal >> 16));
    result.z = 0.0f;
    result.flags = VALID_XY | VALID_TAINTED_Z;
    DestinationGPR(rd(instr)) = result;
    return;
  }

  double x = prtVal.GetValidX(rtVal);
  double y = arithmetic ? prtVal.GetValidY(rtVal) : f16Unsign(prtVal.GetValidY(rtVal));
  if (x - native_low > 32768.0)
    x -= static_cast<double>(USHRT_MAX + 1);
  else if (x - native_low < -32768.0)
    x += static_cast<double>(USHRT_MAX + 1);
  const bool x_negative = (x < 0.0);

  const s32 low_extended = native_low;
  const u32 high_with_low_sign = (rtVal & 0xFFFF0000u) | (static_cast<u32>(low_extended >> 16) & 0xFFFFu);
  const s32 shifted_low = low_extended >> sh;
  const u32 shifted_high = arithmetic ? static_cast<u32>(static_cast<s32>(high_with_low_sign) >> sh) :
                                        (high_with_low_sign >> sh);
  const s16 low_sign = static_cast<s16>(low_extended >> 16);
  const s16 shifted_high_upper = static_cast<s16>(shifted_high >> 16);

  if (static_cast<s16>(shifted_low) != low_sign)
    x /= static_cast<double>(1u << sh);
  else
    x = static_cast<s16>(shifted_low);

  const bool high_only_sign = (shifted_high_upper == 0 || shifted_high_upper == -1);
  const bool high_moved_into_low = (static_cast<s16>(shifted_high) != low_sign);
  if (high_moved_into_low)
  {
    x += (high_only_sign ? y : static_cast<double>(static_cast<u16>(native_high))) *
         static_cast<double>(1u << (16 - sh));
    if (x_negative)
      x += static_cast<double>(1u << (16 - sh));
  }

  if (high_only_sign)
  {
    if (high_moved_into_low)
    {
      x = std::trunc(x * (USHRT_MAX + 1)) / static_cast<double>(USHRT_MAX + 1);
      y = std::floor(x / static_cast<double>(USHRT_MAX + 1));
    }
    else
    {
      y = shifted_high_upper;
    }
  }
  else
  {
    y = shifted_high_upper + high_offset / static_cast<double>(1u << sh);
  }

  result.x = static_cast<float>(f16Sign(x));
  result.y = static_cast<float>(f16Sign(y));
  result.flags = prtVal.flags | VALID_TAINTED_Z;
  DestinationGPR(rd(instr)) = result;
}

void CPU::PGXP::CPU_SLL(u32 instr, u32 rtVal)
{
  LOG_VALUES_C1(rt(instr), rtVal);
  ShiftLeft(instr, rtVal, sa(instr));
}

void CPU::PGXP::CPU_SRL(u32 instr, u32 rtVal)
{
  LOG_VALUES_C1(rt(instr), rtVal);
  ShiftRight(instr, rtVal, sa(instr), false, false);
}

void CPU::PGXP::CPU_SRA(u32 instr, u32 rtVal)
{
  LOG_VALUES_C1(rt(instr), rtVal);
  ShiftRight(instr, rtVal, sa(instr), true, false);
}

void CPU::PGXP::CPU_SLLV(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rt(instr), rtVal, rs(instr), rsVal);
  ShiftLeft(instr, rtVal, rsVal & 0x1F);
}

void CPU::PGXP::CPU_SRLV(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rt(instr), rtVal, rs(instr), rsVal);
  ShiftRight(instr, rtVal, rsVal & 0x1F, false, true);
}

void CPU::PGXP::CPU_SRAV(u32 instr, u32 rsVal, u32 rtVal)
{
  LOG_VALUES_C2(rt(instr), rtVal, rs(instr), rsVal);
  ShiftRight(instr, rtVal, rsVal & 0x1F, true, true);
}

void CPU::PGXP::CPU_MFC0(u32 instr, u32 rdVal)
{
  LOG_VALUES_1(TinyString::from_format("cop0_{}", rd(instr)).c_str(), rdVal, &g_state.pgxp_cop0[rd(instr)]);

  // CPU[Rt] = CP0[Rd]
  Validate(&g_state.pgxp_cop0[rd(instr)], rdVal);
  PGXP_value& prt = DestinationGPR(rt(instr));
  prt = g_state.pgxp_cop0[rd(instr)];
  prt.value = rdVal;
}

void CPU::PGXP::CPU_MTC0(u32 instr, u32 rtVal)
{
  LOG_VALUES_C1(rt(instr), rtVal);

  // CP0[Rd] = CPU[Rt]
  Validate(&g_state.pgxp_gpr[rt(instr)], rtVal);
  g_state.pgxp_cop0[rd(instr)] = g_state.pgxp_gpr[rt(instr)];
  g_state.pgxp_cop0[rd(instr)].value = rtVal;
}
