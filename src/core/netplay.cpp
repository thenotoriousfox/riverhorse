// SPDX-FileCopyrightText: 2023 Connor McLaughlin <stenzek@gmail.com> and contributors.
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#define IMGUI_DEFINE_MATH_OPERATORS

#include "netplay.h"
#include "bios.h"
#include "cpu_core.h"
#include "digital_controller.h"
#include "game_list.h"
#include "gpu.h"
#include "host.h"
#include "imgui_overlays.h"
#include "netplay_packets.h"
#include "pad.h"
#include "save_state_version.h"
#include "settings.h"
#include "spu.h"
#include "system.h"

#include "util/cd_image.h"
#include "util/gpu_device.h"
#include "util/gpu_texture.h"
#include "util/imgui_manager.h"
#include "util/input_manager.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/memory_settings_interface.h"
#include "common/path.h"
#include "common/string_util.h"
#include "common/timer.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <bitset>
#include <cinttypes>
#include <deque>
#include <span>
#include <xxhash.h>
Log_SetChannel(Netplay);

#ifdef _WIN32
#include "common/windows_headers.h"
#endif

// TODO: windows.h getting picked up somewhere here...
#include "enet/enet.h"
#include "ggponet.h"

namespace Netplay {

using SaveStateBuffer = std::unique_ptr<System::MemorySaveState>;

static constexpr u32 MAX_INPUT_AXES = 8;

#pragma pack(push, 1)
struct Input
{
  u32 button_data;
  std::array<u8, MAX_INPUT_AXES> axes;
};
#pragma pack(pop)

static constexpr std::pair<const char*, const char*> s_synced_settings[] = {
  {"Console", "Region"},
  {"Console", "Enable8MBRAM"},
  {"CPU", "ExecutionMode"},
  {"CPU", "OverclockEnable"},
  {"CPU", "OverclockNumerator"},
  {"CPU", "OverclockDenominator"},
  {"CPU", "RecompilerMemoryExceptions"},
  {"CPU", "RecompilerICache"},
  {"GPU", "DisableInterlacing"},
  {"GPU", "ForceNTSCTimings"},
  {"GPU", "WidescreenHack"},
  {"CDROM", "ReadSpeedup"},
  {"CDROM", "SeekSpeedup"},
  {"CDROM", "MaxReadSpeedupCycles"},
  {"CDROM", "MaxSeekSpeedupCycles"},
  {"CDROM", "DisableSpeedupOnMDEC"},
  {"CDROM", "RegionCheck"},
  {"CDROM", "MechaconVersion"},
  {"CDROM", "ReadaheadSectors"},
  {"CDROM", "SubQSkew"},
  {"CDROM", "MuteCDAudio"},
  {"BIOS", "PatchFastBoot"},
  {"Hacks", "DMAMaxSliceTicks"},
  {"Hacks", "DMAHaltTicks"},
  {"Hacks", "GPUMaxRunAhead"},
  {"Hacks", "UseOldMDECRoutines"},
  {"Main", "DisableAllEnhancements"},
  {"Pad1", "Type"},
};

// TODO: Might be a bit generous... should we move this to config?
static constexpr float MAX_CONNECT_TIME = 30.0f;
static constexpr float MAX_CLOSE_TIME = 3.0f;
static constexpr u32 MAX_CONNECT_RETRIES = 4;

static bool NpAdvFrameCb(void* ctx, int flags);
static bool NpSaveFrameCb(void* ctx, unsigned char** buffer, int* len, int* checksum, int frame);
static bool NpLoadFrameCb(void* ctx, unsigned char* buffer, int len, int rb_frames, int frame_to_load);
static void NpFreeBuffCb(void* ctx, void* buffer, int frame);
static bool NpOnEventCb(void* ctx, GGPOEvent* ev);

static Input ReadLocalInput();
static GGPOErrorCode AddLocalInput(Netplay::Input input);
static GGPOErrorCode SyncInput(Input inputs[2], int* disconnect_flags);
static void SetInputs(Input inputs[2]);

static std::string GetHostSettingsText();
static bool ImageMatchesGame(const std::string& path, std::string_view serial, u64 hash);
static void SetSettings(std::string_view host_settings);

static void CloseSessionWithError(const std::string_view& message);
static void RequestCloseSession(CloseSessionMessage::Reason reason);

// ENet
static bool InitializeEnet();
static void ShutdownEnetHost();
static std::string PeerAddressString(const ENetPeer* peer);
static void HandleEnetEvent(const ENetEvent* event);
static void PollEnet(Common::Timer::Value until_time);

// Player management
static bool IsHost();
static bool IsValidPlayerId(s32 player_id);
static s32 GetFreePlayerId();
static s32 GetPlayerIdForPeer(const ENetPeer* peer);
std::string_view GetNicknameForPlayer(s32 player_id);
static void NotifyPlayerJoined(s32 player_id);
static void DropPlayer(s32 player_id, DropPlayerReason reason);
static void ShowChatMessage(s32 player_id, const std::string_view& message);
static void RequestReset(ResetRequestMessage::Reason reason, s32 causing_player_id = 0);
static void SendConnectRequest();

// Spectators
static bool IsSpectator(const ENetPeer* peer);
static s32 GetFreeSpectatorSlot();
static s32 GetSpectatorSlotForPeer(const ENetPeer* peer);
static void DropSpectator(s32 slot_id, DropPlayerReason reason);

// Controlpackets
static void HandleMessageFromNewPeer(ENetPeer* peer, const ENetPacket* pkt);
static void HandleControlMessage(s32 player_id, const ENetPacket* pkt);
static void HandleConnectResponseMessage(s32 player_id, const ENetPacket* pkt);
static void HandleJoinResponseMessage(s32 player_id, const ENetPacket* pkt);
static void HandlePreResetMessage(s32 player_id, const ENetPacket* pkt);
static void HandleResetMessage(s32 player_id, const ENetPacket* pkt);
static void HandleResetCompleteMessage(s32 player_id, const ENetPacket* pkt);
static void HandleResumeSessionMessage(s32 player_id, const ENetPacket* pkt);
static void HandleResetRequestMessage(s32 player_id, const ENetPacket* pkt);
static void HandlePlayerJoinedMessage(s32 player_id, const ENetPacket* pkt);
static void HandleDropPlayerMessage(s32 player_id, const ENetPacket* pkt);
static void HandleCloseSessionMessage(s32 player_id, const ENetPacket* pkt);
static void HandleChatMessage(s32 player_id, const ENetPacket* pkt);


// GGPO session.
static void CreateGGPOSession();
static void DestroyGGPOSession();
static bool Start(bool is_hosting, std::string nickname, const std::string& remote_addr, s32 port, s32 ldelay);
static void CloseSession();
static void SetSessionState(SessionState state);

// Host functions.
static void HandlePeerConnectionAsHost(ENetPeer* peer);
static void HandlePeerConnectionAsNonHost(ENetPeer* peer, s32 claimed_player_id);
static void HandlePeerDisconnectionAsHost(s32 player_id);
static void HandlePeerDisconnectionAsNonHost(s32 player_id);
static void PreReset();
static void Reset();
static void UpdateResetState();
static void UpdateConnectingState();

// static bool WaitUntilFrameAdvanced();
static void NetplayAdvanceFrame(Netplay::Input inputs[], int disconnect_flags);
static void AdvanceFrame();
static void RunFrame();

static s32 CurrentFrame();

/// Frame Pacing
static void InitializeFramePacing();
static void HandleTimeSyncEvent(float frame_delta, int update_interval);
static void Throttle();

// Desync Detection
static void GenerateChecksumForFrame(int* checksum, int frame, unsigned char* buffer, int buffer_size);

// OSD
static void OnNetplayMessage(std::string message);
static void ClearNetplayMessages();
static void DrawMessages();
static void DrawStats();
static void DrawChatDialog();

//////////////////////////////////////////////////////////////////////////
// Variables
//////////////////////////////////////////////////////////////////////////

static MemorySettingsInterface s_settings_overlay;
static SessionState s_state;
static bool send_desync_notifications = true;
static bool s_resync_pending = false;
static std::string s_join_game_path;
static bool s_keep_gpu_device_on_shutdown = false;
static bool s_cpu_executing = false;

/// Enet
struct Peer
{
  ENetPeer* peer;
  std::string nickname;
  GGPOPlayerHandle ggpo_handle;
};
static ENetHost* s_enet_host = nullptr;
static ENetAddress s_host_address;
static std::array<Peer, MAX_PLAYERS> s_peers;
static s32 s_host_player_id = 0;
static s32 s_player_id = 0;
static s32 s_num_players = 0;
static u32 s_reset_cookie = 0;
static std::bitset<MAX_PLAYERS> s_reset_players;
static Common::Timer s_reset_start_time;
static Common::Timer s_last_host_connection_attempt;

// Spectators
static std::array<Peer, MAX_SPECTATORS> s_spectators;
static std::bitset<MAX_SPECTATORS> s_reset_spectators;
static s32 s_num_spectators = 0;
static s32 s_spectating_failed_count = 0;
static bool s_local_spectating = false;


/// GGPO
static std::string s_local_nickname;
static std::string s_local_session_password;
static GGPOPlayerHandle s_local_handle = GGPO_INVALID_HANDLE;
static s32 s_local_delay = 0;
static GGPONetworkStats s_last_net_stats{};
static GGPOSession* s_ggpo = nullptr;

static std::deque<SaveStateBuffer> s_save_buffer_pool;

static std::array<std::array<float, 32>, NUM_CONTROLLER_AND_CARD_PORTS> s_net_input;

/// Frame timing. We manage our own frame pacing here, because we need to constantly adjust.
static float s_target_speed = 1.0f;
static Common::Timer::Value s_frame_period = 0;
static Common::Timer::Value s_next_frame_time = 0;
static s32 s_next_timesync_recovery_frame = -1;
static u32 s_frames_to_run = 0;

// OSD
static std::deque<std::pair<std::string, Common::Timer::Value>> s_netplay_messages;
static constexpr u32 MAX_NETPLAY_MESSAGES = 15;
static constexpr float NETPLAY_MESSAGE_DURATION = 15.0f;
static constexpr float NETPLAY_MESSAGE_FADE_TIME = 2.0f;
static bool s_netplay_chat_dialog_open = false;
static bool s_netplay_chat_dialog_opening = false;
static std::string s_netplay_chat_message;

//////////////////////////////////////////////////////////////////////////
// Packet helpers
//////////////////////////////////////////////////////////////////////////

static std::string_view DropPlayerReasonToString(DropPlayerReason reason)
{
  switch (reason)
  {
    case DropPlayerReason::ConnectTimeout:
      return TRANSLATE_SV("Netplay", "Connection timeout");
    case DropPlayerReason::DisconnectedFromHost:
      return TRANSLATE_SV("Netplay", "Disconnected from host");
    default:
      return "Unknown";
  }
}

template<typename T>
struct PacketWrapper
{
  ENetPacket* pkt;

  ALWAYS_INLINE const T* operator->() const { return reinterpret_cast<const T*>(pkt->data); }
  ALWAYS_INLINE T* operator->() { return reinterpret_cast<T*>(pkt->data); }
  ALWAYS_INLINE const T* operator&() const { return reinterpret_cast<const T*>(pkt->data); }
  ALWAYS_INLINE T* operator&() { return reinterpret_cast<T*>(pkt->data); }
};
template<typename T>
static PacketWrapper<T> NewWrappedPacket(u32 size = sizeof(T), u32 flags = 0)
{
  return PacketWrapper<T>{enet_packet_create(nullptr, size, flags)};
}
template<typename T>
static PacketWrapper<T> NewControlPacket(u32 size = sizeof(T), u32 flags = ENET_PACKET_FLAG_RELIABLE)
{
  PacketWrapper<T> ret = NewWrappedPacket<T>(size, flags);
  ControlMessageHeader* hdr = reinterpret_cast<ControlMessageHeader*>(ret.pkt->data);
  hdr->type = T::MessageType();
  hdr->size = size;
  return ret;
}
template<typename T>
static bool SendControlPacket(ENetPeer* peer, const PacketWrapper<T>& pkt)
{
  const int rc = enet_peer_send(peer, ENET_CHANNEL_CONTROL, pkt.pkt);
  if (rc != 0)
  {
    ERROR_LOG("enet_peer_send() failed: {}", rc);
    enet_packet_destroy(pkt.pkt);
    return false;
  }

  return true;
}
template<typename T>
static bool SendControlPacket(s32 player_id, const PacketWrapper<T>& pkt)
{
  DebugAssert(player_id >= 0 && player_id < MAX_PLAYERS && s_peers[player_id].peer);
  return SendControlPacket<T>(s_peers[player_id].peer, pkt);
}
template<typename T>
static void SendControlPacketToAll(const PacketWrapper<T>& pkt, bool send_to_spectators)
{
  const s32 total_peers = send_to_spectators ? MAX_PLAYERS + MAX_SPECTATORS : MAX_PLAYERS;
  for (s32 i = 0; i < total_peers; i++)
  {
    ENetPeer* peer_to_send = i < MAX_PLAYERS ? s_peers[i].peer : s_spectators[i - MAX_PLAYERS].peer;
    if (!peer_to_send)
      continue;

    ENetPacket* pkt_to_send = enet_packet_create(pkt.pkt->data, pkt.pkt->dataLength, pkt.pkt->flags);
    const int rc = enet_peer_send(peer_to_send, ENET_CHANNEL_CONTROL, pkt_to_send);
    if (rc != 0)
    {
      ERROR_LOG("enet_peer_send() to player {} failed: {}", i, rc);
      enet_packet_destroy(pkt.pkt);
    }
  }
}
template<typename T>
static const T* CheckReceivedPacket(s32 player_id, const ENetPacket* pkt)
{
  if (pkt->dataLength < sizeof(T))
  {
    ERROR_LOG("Received too-short control packet {} from player {}", static_cast<u32>(T::MessageType()),
                    player_id);
    return nullptr;
  }

  const ControlMessageHeader* hdr = reinterpret_cast<const ControlMessageHeader*>(pkt->data);
  if (hdr->size < sizeof(T))
  {
    ERROR_LOG("Received too-short control packet {} from player {} [inner field]",
                    static_cast<u32>(T::MessageType()), player_id);
    return nullptr;
  }

  return reinterpret_cast<const T*>(pkt->data);
}
} // namespace Netplay

// Netplay Impl

bool Netplay::Start(bool is_hosting, std::string nickname, const std::string& remote_addr, s32 port, s32 ldelay)
{
  if (IsActive())
  {
    ERROR_LOG("Netplay session already active");
    return false;
  }

  // Port should be valid regardless of hosting/joining.
  if (port < 0 || port >= 65535)
  {
    ERROR_LOG("Invalid port {}", port);
    return false;
  }

  // Need a system if we're hosting.
  if (is_hosting)
  {
    if (!System::IsValid())
    {
      ERROR_LOG("Can't host a netplay session without a valid VM");
      return false;
    }

    s_keep_gpu_device_on_shutdown = true;
  }
  else
  {
    // We shouldn't have a system, toss it if we do.
    if (System::IsValid())
      System::ShutdownSystem(false);

    // But we need the display to show the connecting screen.
    s_keep_gpu_device_on_shutdown = static_cast<bool>(g_gpu_device);
    Error error;
    if (!g_gpu_device && !Host::CreateGPUDevice(Settings::GetRenderAPIForRenderer(g_settings.gpu_renderer), &error))
    {
      ERROR_LOG("Failed to get host display for netplay: {}", error.GetDescription());
      return false;
    }
  }

  SetSessionState(SessionState::Initializing);

  if (!InitializeEnet())
  {
    ERROR_LOG("Failed to initialize Enet.");
    return false;
  }

  // Create our "host" (which is basically just our port).
  ENetAddress server_address;
  server_address.host = ENET_HOST_ANY;
  server_address.port = is_hosting ? static_cast<u16>(port) : ENET_PORT_ANY;
  s_enet_host = enet_host_create(&server_address, MAX_PLAYERS + MAX_SPECTATORS, NUM_ENET_CHANNELS, 0, 0);
  if (!s_enet_host)
  {
    ERROR_LOG("Failed to create enet host.");
    return false;
  }

  s_host_player_id = 0;
  s_local_nickname = std::move(nickname);
  s_local_delay = ldelay;
  s_reset_cookie = 0;
  s_reset_players.reset();
  s_reset_spectators.reset();

  // If we're the host, we can just continue on our merry way, the others will join later.
  if (is_hosting)
  {
    // Starting session with a single player.
    s_player_id = 0;
    s_num_players = 1;
    s_num_spectators = 0;
    s_reset_players = 1;
    s_reset_spectators = 0;
    s_peers[s_player_id].nickname = s_local_nickname;
    CreateGGPOSession();
    SetSessionState(SessionState::Running);
    SetSettings(GetHostSettingsText());

    INFO_LOG("Netplay session started as host on port {}.", port);

    // This is most likely called from within the host message pump.
    // So get out of the "normal" system execution.
    System::SetState(System::State::Paused);
    System::InterruptExecution();

    return true;
  }

  // for non-hosts, we don't know our player id yet until after we connect...
  s_player_id = -1;

  s_host_address.port = static_cast<u16>(port);
  if (enet_address_set_host(&s_host_address, remote_addr.c_str()) != 0)
  {
    ERROR_LOG("Failed to parse host: '{}'", remote_addr.c_str());
    return false;
  }

  s_peers[s_host_player_id].peer =
    enet_host_connect(s_enet_host, &s_host_address, NUM_ENET_CHANNELS, static_cast<u32>(s_player_id));
  if (!s_peers[s_host_player_id].peer)
  {
    ERROR_LOG("Failed to start connection to host.");
    return false;
  }

  // Wait until we're connected to the main host. They'll send us back state to load and a full player list.
  SetSessionState(SessionState::Connecting);
  s_reset_start_time.Reset();
  s_last_host_connection_attempt.Reset();
  return true;
}

void Netplay::SystemDestroyed()
{
  // something tried to shut us down..
  RequestCloseSession(CloseSessionMessage::Reason::HostShutdown);
  // TODO: CloseSession() ???
}

void Netplay::CloseSession()
{
  Assert(IsActive() || s_state == SessionState::ClosingSession);

  const bool was_host = IsHost();
  s_resync_pending = false;

  DestroyGGPOSession();
  ShutdownEnetHost();

  // Restore original settings.
  Host::Internal::SetNetplaySettingsLayer(nullptr);
  System::ApplySettings(false);

  SetSessionState(SessionState::Inactive);
  Host::OnNetplaySessionClosed();

  // Shut down the VM too, if we're not the host.
  if (!was_host)
    System::ShutdownSystem(false);

  if (!s_keep_gpu_device_on_shutdown)
  {
    Host::ReleaseGPUDevice();
    Host::ReleaseRenderWindow();
  }

  s_local_spectating = false;
}

void Netplay::SetSessionState(SessionState state)
{
  if (state == s_state)
    return;

  s_state = state;

  if (!System::IsValid())
    return;

  // Synchronize the netplay state with the system state, so the correct UI gets drawn.
  switch (state)
  {
    case SessionState::Running:
    case SessionState::Inactive:
      System::SetState(System::State::Running);
      break;

    case SessionState::Initializing:
    case SessionState::Connecting:
    case SessionState::Resetting:
      System::SetState(System::State::Paused);
      break;

    default:
      break;
  }
}

bool Netplay::IsActive()
{
  return (s_state >= SessionState::Initializing && s_state <= SessionState::Running);
}

void Netplay::CloseSessionWithError(const std::string_view& message)
{
  Host::ReportErrorAsync(TRANSLATE_SV("Netplay", "Netplay Error"), message);
  SetSessionState(SessionState::ClosingSession);

  if (s_peers[s_host_player_id].peer)
    enet_peer_disconnect_now(s_peers[s_host_player_id].peer, 0);
}

void Netplay::RequestCloseSession(CloseSessionMessage::Reason reason)
{
  if (IsHost())
  {
    // Notify everyone
    auto pkt = NewControlPacket<CloseSessionMessage>();
    pkt->reason = reason;
    SendControlPacketToAll(pkt, true);
    // close spectator connections
    for (s32 i = 0; i < MAX_SPECTATORS; i++)
    {
      if (s_spectators[i].peer)
        enet_peer_disconnect_now(s_spectators[i].peer, 0);
    }
  }

  // Close player connections
  DestroyGGPOSession();
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    if (s_peers[i].peer)
    {
      if (IsHost())
        enet_peer_disconnect_later(s_peers[i].peer, 0);
      else
        enet_peer_disconnect_now(s_peers[i].peer, 0);
    }
  }

  // but wait for them to actually drop
  SetSessionState(SessionState::ClosingSession);
  s_reset_start_time.Reset();

  // if we have a system, we can display the visual, otherwise just get out of here
  // that might happen if they click shutdown, then shutdown again and don't wait
  while (System::IsValid() && s_reset_start_time.GetTimeSeconds() < MAX_CLOSE_TIME)
  {
    // Just check that all players have disconnected.
    // We don't want to handle any requests here.
    ENetEvent event;
    if (enet_host_service(s_enet_host, &event, 1) >= 0)
    {
      switch (event.type)
      {
        case ENET_EVENT_TYPE_DISCONNECT:
        {
          const s32 player_id = GetPlayerIdForPeer(event.peer);
          if (player_id >= 0)
          {
            s_peers[player_id].peer = nullptr;
            return;
          }

          const s32 spectator_slot = GetSpectatorSlotForPeer(event.peer);
          if (spectator_slot >= 0)
          {
            s_spectators[spectator_slot].peer = nullptr;
            return;
          }
        }
        break;

        case ENET_EVENT_TYPE_RECEIVE:
        {
          // Discard all packets.
          enet_packet_destroy(event.packet);
        }
        break;

        default:
          break;
      }
    }

    if (std::none_of(s_peers.begin(), s_peers.end(), [](const Peer& p) { return p.peer != nullptr; }))
      return;

    Host::DisplayLoadingScreen("Closing session");
    Host::PumpMessagesOnCPUThread();
  }
}

bool Netplay::InitializeEnet()
{
  static bool enet_initialized = false;
  int rc;
  if (!enet_initialized && (rc = enet_initialize()) != 0)
  {
    ERROR_LOG("enet_initialize() returned {}", rc);
    return false;
  }

  std::atexit(enet_deinitialize);
  enet_initialized = true;
  return true;
}

void Netplay::ShutdownEnetHost()
{
  if (!s_enet_host)
    return;

  DEV_LOG("Shutting down Enet host");
  for (u32 i = 0; i < MAX_PLAYERS; i++)
  {
    if (s_peers[i].peer)
      enet_peer_reset(s_peers[i].peer);

    s_peers[i] = {};
  }

  for (u32 i = 0; i < MAX_SPECTATORS; i++)
  {
    if (s_spectators[i].peer)
      enet_peer_reset(s_spectators[i].peer);

    s_spectators[i] = {};
  }

  enet_host_destroy(s_enet_host);
  s_enet_host = nullptr;
}

std::string Netplay::PeerAddressString(const ENetPeer* peer)
{
  char buf[128];
  if (enet_address_get_host_ip(&peer->address, buf, std::size(buf)))
    buf[0] = 0;

  return fmt::format("{}:{}", buf, peer->address.port);
}

void Netplay::HandleEnetEvent(const ENetEvent* event)
{
  switch (event->type)
  {
    case ENET_EVENT_TYPE_CONNECT:
    {
      if (IsHost())
        HandlePeerConnectionAsHost(event->peer);
      else
        HandlePeerConnectionAsNonHost(event->peer, static_cast<s32>(event->data));

      return;
    }
    break;

    case ENET_EVENT_TYPE_DISCONNECT:
    {
      const s32 spectator_slot = GetSpectatorSlotForPeer(event->peer);
      const s32 player_id = GetPlayerIdForPeer(event->peer);

      if (s_state == SessionState::Connecting)
      {
        Assert(player_id == s_host_player_id);
        Panic("Failed to connect to host");
        return;
      }
      else if (s_state == SessionState::Resetting)
      {
        // let the timeout deal with it
        DEV_LOG("Ignoring disconnection from {} while synchronizing", player_id);
        return;
      }

      if (spectator_slot >= 0)
      {
        DropSpectator(spectator_slot, DropPlayerReason::DisconnectedFromHost);
        return;
      }

      WARNING_LOG("ENet player {} disconnected", player_id);
      if (IsValidPlayerId(player_id))
      {
        if (IsHost())
          HandlePeerDisconnectionAsHost(player_id);
        else
          HandlePeerDisconnectionAsNonHost(player_id);
      }
    }
    break;

    case ENET_EVENT_TYPE_RECEIVE:
    {
      s32 player_id = GetPlayerIdForPeer(event->peer);
      const s32 spectator_slot = GetSpectatorSlotForPeer(event->peer);

      if (player_id < 0 && spectator_slot < 0)
      {
        // If it's a new connection, we need to handle connection request messages.
        if (event->channelID == ENET_CHANNEL_CONTROL && IsHost())
          HandleMessageFromNewPeer(event->peer, event->packet);
        enet_packet_destroy(event->packet);
        return;
      }

      if (event->channelID == ENET_CHANNEL_CONTROL)
      {
        if (player_id < 0)
          player_id = spectator_slot + MAX_PLAYERS + 1;

        HandleControlMessage(player_id, event->packet);
      }
      else if (event->channelID == ENET_CHANNEL_GGPO)
      {
        TRACE_LOG("Received {} ggpo bytes from player {}", event->packet->dataLength, player_id);
        const int rc = ggpo_handle_packet(s_ggpo, event->peer, event->packet);
        if (rc != GGPO_OK)
          ERROR_LOG("Failed to process GGPO packet!");
      }
      else
      {
        ERROR_LOG("Unexpected packet channel {}", event->channelID);
      }

      enet_packet_destroy(event->packet);
    }
    break;

    default:
    {
      WARNING_LOG("Unhandled enet event {}", static_cast<int>(event->type));
    }
    break;
  }
}

void Netplay::PollEnet(Common::Timer::Value until_time)
{
  ENetEvent event;

  u64 current_time = Common::Timer::GetCurrentValue();

  while (IsActive())
  {
    const u32 enet_timeout = (current_time >= until_time) ?
                               0 :
                               static_cast<u32>(Common::Timer::ConvertValueToMilliseconds(until_time - current_time));

    // make sure s_enet_host exists
    Assert(s_enet_host);

    // might need resending
    if (s_ggpo)
      ggpo_network_idle(s_ggpo);

    const int res = enet_host_service(s_enet_host, &event, enet_timeout);
    if (res > 0)
    {
      HandleEnetEvent(&event);

      // receiving can trigger sending
      if (s_ggpo)
        ggpo_network_idle(s_ggpo);

      // make sure we get all events
      current_time = Common::Timer::GetCurrentValue();
      continue;
    }
    // exit once we're nonblocking
    current_time = Common::Timer::GetCurrentValue();
    if (enet_timeout == 0 || current_time >= until_time)
      break;
  }
}

bool Netplay::IsHost()
{
  return s_player_id == s_host_player_id;
}

#if 0
GGPOPlayerHandle Netplay::PlayerIdToGGPOHandle(s32 player_id)
{
  DebugAssert(player_id >= 0 && player_id < MAX_PLAYERS);
  return s_peers[player_id].ggpo_handle;
}

ENetPeer* Netplay::GetPeerForPlayer(s32 player_id)
{
  DebugAssert(player_id >= 0 && player_id < MAX_PLAYERS);
  return s_peers[player_id].peer;
}
#endif

s32 Netplay::GetPlayerIdForPeer(const ENetPeer* peer)
{
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    if (s_peers[i].peer == peer)
      return i;
  }

  return -1;
}

bool Netplay::IsValidPlayerId(s32 player_id)
{
  return s_player_id == player_id || (player_id >= 0 && player_id < MAX_PLAYERS && s_peers[player_id].peer);
}

s32 Netplay::GetFreePlayerId()
{
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    if (i != s_player_id && !s_peers[i].peer)
      return i;
  }

  return -1;
}

std::string_view Netplay::GetNicknameForPlayer(s32 player_id)
{
  return IsValidPlayerId(player_id) ?
           std::string_view(player_id == s_player_id ? s_local_nickname : s_peers[player_id].nickname) :
           std::string_view();
}

void Netplay::CreateGGPOSession()
{
  // TODO: since saving every frame during rollback loses us time to do actual gamestate iterations it might be better
  // to hijack the update / save / load cycle to only save every confirmed frame only saving when actually needed.

  GGPOSessionCallbacks cb = {};
  cb.advance_frame = NpAdvFrameCb;
  cb.save_game_state = NpSaveFrameCb;
  cb.load_game_state = NpLoadFrameCb;
  cb.free_buffer = NpFreeBuffCb;
  cb.on_event = NpOnEventCb;

  if (s_local_spectating)
  {
    ggpo_start_spectating(&s_ggpo, &cb, s_num_players, sizeof(Netplay::Input), s_peers[s_host_player_id].peer);
    return;
  }

  ggpo_start_session(&s_ggpo, &cb, s_num_players, sizeof(Netplay::Input), MAX_ROLLBACK_FRAMES);

  // if you are the host be sure to add the needed spectators to the session before the players
  // this way we prevent the session finishing to synchronize before adding the spectators.
  if (IsHost())
  {
    for (s32 i = 0; i < MAX_SPECTATORS; i++)
    {
      // slot filled?
      if (!s_spectators[i].peer)
        continue;

      GGPOErrorCode result;
      GGPOPlayer player = {sizeof(GGPOPlayer)};

      player.type = GGPO_PLAYERTYPE_SPECTATOR;
      player.u.remote.peer = s_spectators[i].peer;
      result = ggpo_add_player(s_ggpo, &player, &s_spectators[i].ggpo_handle);

      // It's a new session, this should always succeed...
      Assert(GGPO_SUCCEEDED(result));
    }
  }

  int player_num = 1;
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    // slot filled?
    if (!s_peers[i].peer && i != s_player_id)
      continue;

    GGPOPlayer player = {sizeof(GGPOPlayer)};
    GGPOErrorCode result;
    player.player_num = player_num++;
    if (i == s_player_id)
    {
      player.type = GGPO_PLAYERTYPE_LOCAL;
      result = ggpo_add_player(s_ggpo, &player, &s_peers[i].ggpo_handle);
      if (GGPO_SUCCEEDED(result))
        s_local_handle = s_peers[i].ggpo_handle;
    }
    else
    {
      player.type = GGPO_PLAYERTYPE_REMOTE;
      player.u.remote.peer = s_peers[i].peer;
      result = ggpo_add_player(s_ggpo, &player, &s_peers[i].ggpo_handle);
    }

    INFO_LOG("Adding player: {}", i);
    // It's a new session, this should always succeed...
    Assert(GGPO_SUCCEEDED(result));
  }

  ggpo_set_frame_delay(s_ggpo, s_local_handle, s_local_delay);
  InitializeFramePacing();
}

void Netplay::DestroyGGPOSession()
{
  if (!s_ggpo)
    return;

  DEV_LOG("Destroying GGPO session...");
  ggpo_close_session(s_ggpo);
  s_ggpo = nullptr;
  s_save_buffer_pool.clear();
  s_local_handle = GGPO_INVALID_HANDLE;
  s_spectating_failed_count = 0;

  for (Peer& p : s_peers)
    p.ggpo_handle = GGPO_INVALID_HANDLE;
}

void Netplay::HandleControlMessage(s32 player_id, const ENetPacket* pkt)
{
  if (pkt->dataLength < sizeof(ControlMessageHeader))
  {
    ERROR_LOG("Invalid control packet from player {} of size {}", player_id, pkt->dataLength);
    return;
  }

  const ControlMessageHeader* hdr = reinterpret_cast<const ControlMessageHeader*>(pkt->data);
  switch (hdr->type)
  {
    case ControlMessage::ConnectResponse:
      HandleConnectResponseMessage(player_id, pkt);
      break;

    case ControlMessage::JoinResponse:
      HandleJoinResponseMessage(player_id, pkt);
      break;

    case ControlMessage::PreReset:
      HandlePreResetMessage(player_id, pkt);
      break;

    case ControlMessage::Reset:
      HandleResetMessage(player_id, pkt);
      break;

    case ControlMessage::ResetComplete:
      HandleResetCompleteMessage(player_id, pkt);
      break;

    case ControlMessage::ResumeSession:
      HandleResumeSessionMessage(player_id, pkt);
      break;

    case ControlMessage::PlayerJoined:
      HandlePlayerJoinedMessage(player_id, pkt);
      break;

    case ControlMessage::DropPlayer:
      HandleDropPlayerMessage(player_id, pkt);
      break;

    case ControlMessage::ResetRequest:
      HandleResetRequestMessage(player_id, pkt);
      break;

    case ControlMessage::CloseSession:
      HandleCloseSessionMessage(player_id, pkt);
      break;

    case ControlMessage::ChatMessage:
      HandleChatMessage(player_id, pkt);
      break;

    default:
      ERROR_LOG("Unhandled control packet {} from player {} of size {}", static_cast<u32>(hdr->type), player_id, pkt->dataLength);
      break;
  }
}

void Netplay::HandlePeerConnectionAsHost(ENetPeer* peer)
{
  // TODO: we might want to put an idle timeout here...
  INFO_LOG(fmt::format("New peer connection from {}", PeerAddressString(peer)).c_str());

  // send them the session details
  const std::string& game_title = System::GetGameTitle();
  const std::string& game_serial = System::GetGameSerial();
  const std::string settings = GetHostSettingsText();
  auto pkt = NewControlPacket<ConnectResponseMessage>(
    sizeof(ConnectResponseMessage) + static_cast<u32>(game_serial.length()) + static_cast<u32>(game_title.length()) +
    static_cast<u32>(settings.length()));
  pkt->num_players = s_num_players;
  pkt->max_players = MAX_PLAYERS;
  pkt->game_title_length = static_cast<u32>(game_title.length());
  pkt->game_serial_length = static_cast<u32>(game_serial.length());
  pkt->settings_length = static_cast<u32>(settings.length());
  pkt->game_hash = System::GetGameHash();
  pkt->bios_hash = System::GetBIOSHash();

  u8* const extra = pkt.pkt->data + sizeof(ConnectResponseMessage);
  std::memcpy(extra, game_serial.data(), game_serial.length());
  std::memcpy(extra + game_serial.length(), game_title.data(), game_title.length());
  std::memcpy(extra + game_serial.length() + game_title.length(), settings.data(), settings.length());
  SendControlPacket(peer, pkt);
}

void Netplay::HandleConnectResponseMessage(s32 player_id, const ENetPacket* pkt)
{
  const ConnectResponseMessage* msg = CheckReceivedPacket<ConnectResponseMessage>(-1, pkt);
  if (!msg || player_id != s_host_player_id)
  {
    ERROR_LOG("Received unexpected connect response from player {}", player_id);
    return;
  }

  // ignore these messages when reconnecting
  if (s_state != SessionState::Connecting)
  {
    DEV_LOG("Ignoring connect response because we're not initially connecting");
    return;
  }

  if (!msg->Validate())
  {
    CloseSessionWithError(TRANSLATE_SV("Netplay", "Cannot join session: Invalid details received."));
    return;
  }

  INFO_LOG("Received session details from host: ");
  INFO_LOG("  BIOS Hash: {}", BIOS::ImageInfo::GetHashString(msg->bios_hash));
  INFO_LOG("  Game Serial: {}", msg->GetGameSerial());
  INFO_LOG("  Game Title: {}", msg->GetGameTitle());
  INFO_LOG("  Game Hash: {:016X}", msg->game_hash);

  const std::string bios_path = BIOS::FindBIOSFileWithHash(EmuFolders::Bios.c_str(), msg->bios_hash);
  if (bios_path.empty())
  {
    CloseSessionWithError(fmt::format(TRANSLATE_FS("Netplay", "Cannot join session: Unable to find BIOS with hash {}."),
                                      BIOS::ImageInfo::GetHashString(msg->bios_hash)));
    return;
  }

  std::string game_path;
  if (!s_join_game_path.empty() && ImageMatchesGame(s_join_game_path, msg->GetGameSerial(), msg->game_hash))
  {
    game_path = s_join_game_path;
  }
  else
  {
    auto lock = GameList::GetLock();
    const GameList::Entry* entry = GameList::GetEntryBySerialAndHash(msg->GetGameSerial(), msg->game_hash);
    if (entry)
      game_path = entry->path;
  }
  if (game_path.empty())
  {
    CloseSessionWithError(
      fmt::format(TRANSLATE_FS("Netplay", "Cannot join session: Unable to find game \"{}\".\nSerial: {}\nHash: {}"),
                  msg->GetGameTitle(), msg->GetGameSerial(), System::GetGameHashId(msg->game_hash)));
    return;
  }

  INFO_LOG("Found matching BIOS: {}", bios_path.c_str());
  INFO_LOG("Found matching game: {}", game_path.c_str());

  SetSettings(msg->GetSettings());
  s_settings_overlay.SetStringValue("BIOS", "PathNTSCU", bios_path.c_str());
  s_settings_overlay.SetStringValue("BIOS", "PathNTSCJ", bios_path.c_str());
  s_settings_overlay.SetStringValue("BIOS", "PathPAL", bios_path.c_str());

  // Create system with host details.
  Assert(!System::IsValid());
  SystemBootParameters params;
  params.filename = std::move(game_path);
  Error error;
  if (!System::BootSystem(std::move(params), &error))
  {
    CloseSessionWithError(TRANSLATE_SV("Netplay", "Cannot join session: Failed to boot system."));
    return;
  }

  // We're ready to go, send the connection request.
  SendConnectRequest();
}

void Netplay::SendConnectRequest()
{
  DebugAssert(!IsHost());

  std::string req = s_local_spectating ? "as a spectator" : fmt::format("with player id {}", s_player_id);
  DEV_LOG("Sending connect request to host {}", req.c_str());

  auto pkt = NewControlPacket<JoinRequestMessage>();
  pkt->mode = s_local_spectating ? JoinRequestMessage::Mode::Spectator : JoinRequestMessage::Mode::Player;
  pkt->requested_player_id = s_player_id;
  std::memset(pkt->nickname, 0, sizeof(pkt->nickname));
  std::memset(pkt->session_password, 0, sizeof(pkt->session_password));
  StringUtil::Strlcpy(pkt->nickname, s_local_nickname, std::size(pkt->nickname));
  StringUtil::Strlcpy(pkt->session_password, s_local_session_password, std::size(pkt->session_password));
  SendControlPacket(s_peers[s_host_player_id].peer, pkt);
}

bool Netplay::IsSpectator(const ENetPeer* peer)
{
  if (!peer)
    return false;

  for (s32 i = 0; i < MAX_SPECTATORS; i++)
  {
    if (s_spectators[i].peer == peer)
      return true;
  }
  return false;
}

s32 Netplay::GetFreeSpectatorSlot()
{
  for (s32 i = 0; i < MAX_SPECTATORS; i++)
  {
    if (!s_spectators[i].peer)
      return i;
  }
  return -1;
}

s32 Netplay::GetSpectatorSlotForPeer(const ENetPeer* peer)
{
  for (s32 i = 0; i < MAX_SPECTATORS; i++)
  {
    if (s_spectators[i].peer == peer)
      return i;
  }
  return -1;
}

void Netplay::DropSpectator(s32 slot_id, DropPlayerReason reason)
{
  Assert(IsHost());
  INFO_LOG("Dropping Spectator {}: {}", slot_id, s_spectators[slot_id].nickname.c_str());

  OnNetplayMessage(fmt::format(TRANSLATE_FS("Netplay", "Spectator {} left the session: {}"), slot_id,
                               s_spectators[slot_id].nickname, DropPlayerReasonToString(reason)));

  if (s_spectators[slot_id].peer)
    enet_peer_disconnect_now(s_spectators[slot_id].peer, 0);

  s_spectators[slot_id] = {};
  s_num_spectators--;

  if (s_num_spectators == 0 && s_num_players == 1)
    Reset();
}

void Netplay::UpdateConnectingState()
{
  if (s_reset_start_time.GetTimeSeconds() >= MAX_CONNECT_TIME)
  {
    CloseSessionWithError(TRANSLATE_SV("Netplay", "Timed out connecting to server."));
    return;
  }

  // MAX_CONNECT_RETRIES peer to host connection attempts
  // dividing by MAX_CONNECT_RETRIES + 1 because the last attempt will never happen.
  if (s_peers[s_host_player_id].peer &&
      s_last_host_connection_attempt.GetTimeSeconds() >= MAX_CONNECT_TIME / (MAX_CONNECT_RETRIES + 1) &&
      s_peers[s_host_player_id].peer->state != ENetPeerState::ENET_PEER_STATE_CONNECTED)
  {
    // we want to do this because the peer might have initiated a connection
    // too early while the host was still setting up. this gives the connection MAX_CONNECT_RETRIES tries within
    // MAX_CONNECT_TIME to establish the connection
    enet_peer_reset(s_peers[s_host_player_id].peer);
    enet_host_connect(s_enet_host, &s_host_address, NUM_ENET_CHANNELS, static_cast<u32>(s_player_id));
    s_last_host_connection_attempt.Reset();
  }

  // still waiting for connection to host..
  PollEnet(Common::Timer::GetCurrentValue() + Common::Timer::ConvertMillisecondsToValue(16));
  Host::DisplayLoadingScreen("Connecting to host...");
  Host::PumpMessagesOnCPUThread();
}

void Netplay::HandleMessageFromNewPeer(ENetPeer* peer, const ENetPacket* pkt)
{
  const JoinRequestMessage* msg = CheckReceivedPacket<JoinRequestMessage>(-1, pkt);
  if (!msg || msg->header.type != ControlMessage::JoinRequest)
  {
    WARNING_LOG("Received unknown packet from unknown player");
    enet_peer_reset(peer);
    return;
  }

  VERBOSE_LOG(fmt::format("New host peer connection from {} claiming player ID {}", PeerAddressString(peer),
                               msg->requested_player_id)
                     .c_str());

  PacketWrapper<JoinResponseMessage> response = NewControlPacket<JoinResponseMessage>();
  response->player_id = -1;

  // Gatekeep using the session password.
  if (msg->GetSessionPassword() != s_local_session_password)
  {
    response->result = JoinResponseMessage::Result::InvalidPassword;
    SendControlPacket(peer, response);
    return;
  }

  if (msg->mode == JoinRequestMessage::Mode::Spectator)
  {
    // something is really wrong if this isn't the host.
    Assert(IsHost());

    const s32 spectator_slot = GetFreeSpectatorSlot();
    // no free slots? notify the peer.
    if (spectator_slot < 0)
    {
      response->result = JoinResponseMessage::Result::ServerFull;
      SendControlPacket(peer, response);
      return;
    }

    Assert(s_num_spectators < MAX_SPECTATORS);
    response->result = JoinResponseMessage::Result::Success;
    response->player_id = MAX_PLAYERS + 1 + spectator_slot;
    SendControlPacket(peer, response);
    // continue and add peer to the list.
    s_spectators[spectator_slot].peer = peer;
    s_spectators[spectator_slot].nickname = msg->GetNickname();
    s_num_spectators++;
    // Force everyone to resync for now. sadly since ggpo currently only supports adding spectators during setup..
    Reset();
    // notify host that the spectator joined
    OnNetplayMessage(
      fmt::format(TRANSLATE_FS("Netplay", "{} is joining the session as a Spectator."), msg->GetNickname()));
    return;
  }

  // Player is attempting to reconnect.
  // Hopefully both sides have disconnected completely before they reconnect.
  // If not, too bad.
  if (msg->requested_player_id >= 0 && IsValidPlayerId(msg->requested_player_id))
  {
    ERROR_LOG("Player ID {} is already in use, rejecting connection.", msg->requested_player_id);
    response->result = JoinResponseMessage::Result::PlayerIDInUse;
    SendControlPacket(peer, response);
    return;
  }

  // Any free slots?
  const s32 new_player_id = (msg->requested_player_id >= 0) ? msg->requested_player_id : GetFreePlayerId();
  if (new_player_id < 0)
  {
    ERROR_LOG("Server full, rejecting connection.");
    response->result = JoinResponseMessage::Result::ServerFull;
    SendControlPacket(peer, response);
    return;
  }

  VERBOSE_LOG(
    fmt::format("New connection from {} assigned to player ID {}", PeerAddressString(peer), new_player_id).c_str());
  response->result = JoinResponseMessage::Result::Success;
  response->player_id = new_player_id;
  SendControlPacket(peer, response);

  // Set up their player slot.
  Assert(s_num_players < MAX_PLAYERS);
  s_peers[new_player_id].peer = peer;
  s_peers[new_player_id].nickname = msg->GetNickname();
  s_num_players++;

  // Force everyone to resynchronize with the new player.
  Reset();

  // Notify *after* reset so they have their nickname.
  NotifyPlayerJoined(new_player_id);
}

void Netplay::HandlePeerConnectionAsNonHost(ENetPeer* peer, s32 claimed_player_id)
{
  if (s_state == SessionState::Connecting)
  {
    if (peer != s_peers[s_host_player_id].peer)
    {
      ERROR_LOG(
        fmt::format("Unexpected connection from {} claiming player ID {}", PeerAddressString(peer), claimed_player_id)
          .c_str());
      enet_peer_disconnect_now(peer, 0);
    }

    // wait for session details
    return;
  }

  VERBOSE_LOG(
    fmt::format("New peer connection from {} claiming player ID {}", PeerAddressString(peer), claimed_player_id)
      .c_str());

  // shouldn't ever get a non-host connection without a valid ID
  if (claimed_player_id < 0 || claimed_player_id >= MAX_PLAYERS || claimed_player_id == s_player_id)
  {
    ERROR_LOG("Invalid claimed_player_id {}", claimed_player_id);
    enet_peer_disconnect_now(peer, 0);
    return;
  }

  // the peer should match up, unless we're the one receiving the connection
  Assert(s_peers[claimed_player_id].peer == peer || claimed_player_id > s_player_id);
  if (s_peers[claimed_player_id].peer == peer)
  {
    // WaitForPeerConnections handles this case.
    // If this was the host, we still need to wait for the synchronization.
    DEV_LOG("Connection complete with player {}{}", claimed_player_id,
                  (claimed_player_id == s_host_player_id) ? "[HOST]" : "");
    return;
  }

  DEV_LOG("Connection received from peer {}", claimed_player_id);
  s_peers[claimed_player_id].peer = peer;
}

void Netplay::HandleJoinResponseMessage(s32 player_id, const ENetPacket* pkt)
{
  if (s_state != SessionState::Connecting)
  {
    ERROR_LOG("Received unexpected join response from player {}", player_id);
    return;
  }

  const JoinResponseMessage* msg = CheckReceivedPacket<JoinResponseMessage>(player_id, pkt);
  if (msg->result != JoinResponseMessage::Result::Success)
  {
    CloseSessionWithError(
      fmt::format("Connection rejected by server with error code {}", static_cast<u32>(msg->result)));
    return;
  }

  // Still need to wait for synchronization
  INFO_LOG("Connected to host, we were assigned player ID {}", msg->player_id);
  s_player_id = msg->player_id;
  SetSessionState(SessionState::Resetting);
  s_reset_players.reset();
  s_reset_spectators.reset();
  s_reset_start_time.Reset();
}

void Netplay::HandlePreResetMessage(s32 player_id, const ENetPacket* pkt)
{
  if (player_id != s_host_player_id)
  {
    // This shouldn't ever happen, unless someone's being cheeky.
    ERROR_LOG("Dropping Pre-reset from non-host player {}", player_id);
    return;
  }

  if (s_state != SessionState::Resetting)
  {
    // Destroy session to stop sending ggpo packets
    DestroyGGPOSession();
    // Setup a fake resetting situation,
    // the real reset message will come and override this one.
    s_num_players = 0;
    SetSessionState(SessionState::Resetting);
    s_reset_players.reset();
    s_reset_spectators.reset();
    s_reset_start_time.Reset();
  }
}

void Netplay::HandlePeerDisconnectionAsHost(s32 player_id)
{
  INFO_LOG("Player {} disconnected from host, reclaiming their slot", player_id);
  DropPlayer(player_id, DropPlayerReason::DisconnectedFromHost);
}

void Netplay::HandlePeerDisconnectionAsNonHost(s32 player_id)
{
  INFO_LOG("Lost connection with player {}", player_id);
  if (player_id == s_host_player_id)
  {
    // TODO: Automatically try to reconnect to the host with our existing player ID.
    CloseSessionWithError(TRANSLATE_SV("Netplay", "Lost connection to host"));
    return;
  }

  // tell the host we dropped a connection, let them deal with it..
  RequestReset(ResetRequestMessage::Reason::ConnectionLost, player_id);
}

void Netplay::PreReset()
{
  Assert(IsHost());

  VERBOSE_LOG("Pre-Resetting...");

  SendControlPacketToAll(NewControlPacket<PreResetMessage>(), true);
}

void Netplay::Reset()
{
  // In high latency situations it smart to send a pre-reset message before sending the reset message.
  // To prepare them and not timeout.
  PreReset();

  Assert(IsHost());

  VERBOSE_LOG("Resetting...");

  DynamicHeapArray<u8> state(System::GetMaxSaveStateSize());
  size_t state_written = 0;
  Error error;
  if (!System::SaveStateToMemory(state.span(), &error, &state_written))
    Panic("Failed to save state...");

  const u32 state_data_size = static_cast<u32>(state_written);
  ResetMessage header = {};
  header.header.type = ControlMessage::Reset;
  header.header.size = sizeof(ResetMessage) + state_data_size;
  header.state_data_size = state_data_size;
  header.cookie = ++s_reset_cookie;

  // Fill in player info.
  header.num_players = s_num_players;
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    if (!IsValidPlayerId(i))
    {
      header.players[i].controller_port = -1;
      continue;
    }

    // TODO: This is wrong, so wrong....
    header.players[i].controller_port = static_cast<s16>(i);
    StringUtil::Strlcpy(header.players[i].nickname, s_peers[i].nickname, std::size(header.players[i].nickname));

    if (i == s_player_id)
    {
      // TODO: not valid... listening on any address.
      header.players[i].host = s_enet_host->address.host;
      header.players[i].port = s_enet_host->address.port;
    }
    else
    {
      header.players[i].host = s_peers[i].peer->address.host;
      header.players[i].port = s_peers[i].peer->address.port;
    }
  }

  // Distribute sync request to all peers, then wait for them to reload back.
  // Any GGPO packets will get dropped, since the session's gone temporarily.
  DestroyGGPOSession();

  for (s32 i = 0; i < MAX_PLAYERS + MAX_SPECTATORS; i++)
  {
    ENetPeer* peer_to_send = i < MAX_PLAYERS ? s_peers[i].peer : s_spectators[i - MAX_PLAYERS].peer;
    if (!peer_to_send)
      continue;

    ENetPacket* pkt = enet_packet_create(nullptr, sizeof(header) + state_data_size, ENET_PACKET_FLAG_RELIABLE);
    std::memcpy(pkt->data, &header, sizeof(header));
    std::memcpy(pkt->data + sizeof(header), state.data(), state_data_size);

    // This should never fail, we get errors back later..
    const int rc = enet_peer_send(peer_to_send, ENET_CHANNEL_CONTROL, pkt);
    if (rc != 0)
      ERROR_LOG("enet_peer_send() for synchronization request failed: {}", rc);
  }

  // Do a full state reload on the host as well, that way everything (including the GPU)
  // has a clean slate, reducing the chance of desyncs. Looking at you, mister rec, somehow
  // having a different number of cycles.
  // CPU::CodeCache::Flush();
  if (!System::LoadStateFromMemory(state.cspan().subspan(0, state_data_size), &error))
    Panic("Failed to reload host state");

  SetSessionState(SessionState::Resetting);
  s_reset_players.reset();
  s_reset_spectators.reset();
  s_reset_players.set(s_player_id);
  s_reset_start_time.Reset();
}

void Netplay::HandleResetMessage(s32 player_id, const ENetPacket* pkt)
{
  if (player_id != s_host_player_id)
  {
    // This shouldn't ever happen, unless someone's being cheeky.
    ERROR_LOG("Dropping reset from non-host player {}", player_id);
    return;
  }

  const ResetMessage* msg = reinterpret_cast<const ResetMessage*>(pkt->data);
  if (pkt->dataLength < sizeof(ResetMessage) || pkt->dataLength < (sizeof(ResetMessage) + msg->state_data_size))
  {
    CloseSessionWithError(fmt::format("Invalid synchronization request: expected {} bytes, got {} bytes",
                                      sizeof(ResetMessage) + msg->state_data_size, pkt->dataLength));
    return;
  }

  DestroyGGPOSession();

  // Make sure we're connected to all players.
  Assert(msg->num_players > 1 || s_local_spectating);
  DEV_LOG("Checking connections");
  s_num_players = msg->num_players;
  for (s32 i = 0; i < MAX_PLAYERS; i++)
  {
    // We are already connected to the host as a spectator we dont need any other connections
    if (s_local_spectating)
      continue;

    Peer& p = s_peers[i];
    if (msg->players[i].controller_port < 0)
    {
      // If we had a client here, it must've dropped or something..
      if (p.peer)
      {
        WARNING_LOG("Dropping connection to player {}", i);
        enet_peer_disconnect_now(p.peer, 0);
        p.peer = nullptr;
      }

      continue;
    }

    // Can't connect to ourselves!
    if (i == s_player_id)
      continue;

    // Update nickname.
    p.nickname = msg->players[i].GetNickname();

    // Or the host, which may not contain a correct address, since it's from itself.
    if (i == s_host_player_id)
      continue;

    // The host should fall into the category where we can reuse.
    if (p.peer && p.peer->address.host == msg->players[i].host && p.peer->address.port == msg->players[i].port)
    {
      DEV_LOG("Preserving connection to player {}", i);
      continue;
    }

    Assert(i != s_host_player_id);
    if (p.peer)
    {
      enet_peer_reset(p.peer);
      p.peer = nullptr;
    }

    // If this player has a higher ID than us, then they're responsible for connecting to us, not the other way around.
    // i.e. player 2 is responsible for connecting to players 0 and 1, player 1 is responsible for connecting to player
    // 0.
    if (i > s_player_id)
    {
      DEV_LOG("Not connecting to player {}, as they have a higher ID than us ({})", i, s_player_id);
      continue;
    }

    const ENetAddress address = {msg->players[i].host, msg->players[i].port};
    p.peer = enet_host_connect(s_enet_host, &address, NUM_ENET_CHANNELS, static_cast<u32>(s_player_id));
    if (!p.peer)
      Panic("Failed to connect to peer on resynchronize");
  }

  // Load state from packet.
  VERBOSE_LOG("Loading state from host");
  Error error;
  if (!System::LoadStateFromMemory(std::span<const u8>(pkt->data + sizeof(ResetMessage), msg->state_data_size), &error))
    Panic("Failed to load state from host");

  SetSessionState(SessionState::Resetting);
  s_reset_cookie = msg->cookie;
  s_reset_players.reset();
  s_reset_spectators.reset();

  if (s_local_spectating)
  {
    s_reset_spectators.set(s_player_id - (MAX_PLAYERS + 1));
    s_reset_start_time.Reset();
    return;
  }

  s_reset_players.set(s_player_id);
  s_reset_start_time.Reset();
}

void Netplay::HandleResetCompleteMessage(s32 player_id, const ENetPacket* pkt)
{
  const ResetCompleteMessage* msg = CheckReceivedPacket<ResetCompleteMessage>(player_id, pkt);
  if (!msg)
    return;

  if (s_state != SessionState::Resetting || player_id == s_host_player_id)
  {
    ERROR_LOG("Received unexpected reset complete from player {}", player_id);
    return;
  }
  else if (player_id > MAX_PLAYERS && player_id <= MAX_PLAYERS + MAX_SPECTATORS)
  {
    const s32 spectator_slot = player_id - (MAX_PLAYERS + 1);
    s_reset_spectators.set(spectator_slot);
    DEV_LOG("Spectator {} is now reset and ready", spectator_slot);
    return;
  }
  else if (s_reset_players.test(player_id))
  {
    ERROR_LOG("Received double reset from player {}", player_id);
    return;
  }
  else if (s_reset_cookie != msg->cookie)
  {
    ERROR_LOG("Incorrect reset cookie sent from player {}", player_id);
    return;
  }

  DEV_LOG("Player {} is now reset and ready", player_id);
  s_reset_players.set(player_id);
}

void Netplay::HandleResumeSessionMessage(s32 player_id, const ENetPacket* pkt)
{
  const ResumeSessionMessage* msg = CheckReceivedPacket<ResumeSessionMessage>(player_id, pkt);
  if (!msg)
    return;

  if (s_state != SessionState::Resetting || player_id != s_host_player_id)
  {
    ERROR_LOG("Received unexpected resume session from player {}", player_id);
    return;
  }

  DEV_LOG("Resuming session");
  CreateGGPOSession();
  SetSessionState(SessionState::Running);
}

void Netplay::UpdateResetState()
{
  const s32 num_players = (s_local_spectating && s_num_players > 1 ? 1 : s_num_players);
  if (IsHost())
  {
    if (static_cast<s32>(s_reset_players.count()) == num_players &&
        static_cast<s32>(s_reset_spectators.count()) == s_num_spectators)
    {
      VERBOSE_LOG("All players and spectators synchronized, resuming!");
      SendControlPacketToAll(NewControlPacket<ResumeSessionMessage>(), true);
      CreateGGPOSession();
      SetSessionState(SessionState::Running);
      return;
    }

    // connect timeout exceeded?
    if (s_reset_start_time.GetTimeSeconds() >= MAX_CONNECT_TIME)
    {
      // TODO: this should be tweaked, maybe only drop one at a time?
      INFO_LOG("Reset timeout, dropping any players who aren't connected");
      for (s32 i = 0; i < MAX_PLAYERS; i++)
      {
        if (!IsValidPlayerId(i) || s_reset_players.test(i))
          continue;

        // we'll check if we're done again next loop
        DEV_LOG("Dropping player {} because they didn't connect in time", i);
        DropPlayer(i, DropPlayerReason::ConnectTimeout);
      }

      for (s32 i = 0; i < MAX_SPECTATORS; i++)
      {
        if (!IsSpectator(s_spectators[i].peer) || s_reset_spectators.test(i))
          continue;

        // we'll check if we're done again next loop
        DEV_LOG("Dropping Spectator {} because they didn't connect in time", i);
        DropSpectator(i, DropPlayerReason::ConnectTimeout);
      }
    }
  }
  else
  {
    if (static_cast<s32>(s_reset_players.count()) != num_players)
    {
      for (s32 i = 0; i < MAX_PLAYERS; i++)
      {
        if (!IsValidPlayerId(i) || s_reset_players.test(i))
          continue;
        // be sure to first check whether the peer is still valid.
        if (s_peers[i].peer && s_peers[i].peer->state == ENET_PEER_STATE_CONNECTED)
          s_reset_players.set(i);
      }

      if (static_cast<s32>(s_reset_players.count()) == num_players)
      {
        // now connected to all!
        INFO_LOG("Connected to {} players, waiting for host...", s_num_players);
        auto pkt = NewControlPacket<ResetCompleteMessage>();
        pkt->cookie = s_reset_cookie;
        SendControlPacket(s_host_player_id, pkt);
      }
    }
    // cancel ourselves if we didn't get another synchronization request from the host
    if (s_reset_start_time.GetTimeSeconds() >= (MAX_CONNECT_TIME * 2.0f))
    {
      CloseSessionWithError(TRANSLATE_SV("Netplay", "Failed to connect within timeout"));
      return;
    }
  }

  const s32 min_progress = IsHost() ? static_cast<int>(s_reset_players.count() + s_reset_spectators.count()) :
                                      static_cast<int>(s_reset_players.count());
  const s32 max_progress = IsHost() ? s_num_players + s_num_spectators : num_players;

  PollEnet(Common::Timer::GetCurrentValue() + Common::Timer::ConvertMillisecondsToValue(16));
  Host::DisplayLoadingScreen("Netplay synchronizing", 0, max_progress, min_progress);
  Host::PumpMessagesOnCPUThread();
}

void Netplay::RequestReset(ResetRequestMessage::Reason reason, s32 causing_player_id /* = 0 */)
{
  Assert(!IsHost());

  auto pkt = NewControlPacket<ResetRequestMessage>();
  pkt->reason = reason;
  pkt->causing_player_id = causing_player_id;

  DEV_LOG("Requesting reset from host due to {}", pkt->ReasonToString().c_str());
  SendControlPacket(s_host_player_id, pkt);
}

void Netplay::HandleResetRequestMessage(s32 player_id, const ENetPacket* pkt)
{
  const ResetRequestMessage* msg = CheckReceivedPacket<ResetRequestMessage>(player_id, pkt);
  if (!msg)
    return;

  INFO_LOG("Received reset request from player {} due to {}", player_id, msg->ReasonToString().c_str());
  Reset();
}

void Netplay::NotifyPlayerJoined(s32 player_id)
{
  if (IsHost())
  {
    auto pkt = NewControlPacket<PlayerJoinedMessage>();
    pkt->player_id = player_id;
    SendControlPacketToAll(pkt, false);
  }

  OnNetplayMessage(fmt::format(TRANSLATE_FS("Netplay", "{} is joining the session as player {}."),
                               GetNicknameForPlayer(player_id), player_id));
}

void Netplay::HandlePlayerJoinedMessage(s32 player_id, const ENetPacket* pkt)
{
  const PlayerJoinedMessage* msg = CheckReceivedPacket<PlayerJoinedMessage>(player_id, pkt);
  if (!msg || player_id != s_host_player_id)
    return;

  NotifyPlayerJoined(msg->player_id);
}

void Netplay::DropPlayer(s32 player_id, DropPlayerReason reason)
{
  Assert(IsValidPlayerId(player_id) && s_host_player_id != player_id && s_player_id != player_id);
  DebugAssert(s_peers[player_id].peer);

  INFO_LOG("Dropping player {}", player_id);

  OnNetplayMessage(fmt::format(TRANSLATE_FS("Netplay", "{} left the session: {}"), GetNicknameForPlayer(player_id),
                               DropPlayerReasonToString(reason)));

  enet_peer_disconnect_now(s_peers[player_id].peer, 0);
  s_peers[player_id] = {};
  s_num_players--;

  if (!IsHost())
  {
    // if we're not the host, the host should send a resynchronize request shortly
    // but enter the state early, that way we don't keep sending ggpo stuff...
    DestroyGGPOSession();
    SetSessionState(SessionState::Resetting);
  }
  else
  {
    // tell who's left to also drop their side
    auto pkt = NewControlPacket<DropPlayerMessage>();
    pkt->reason = reason;
    pkt->player_id = player_id;
    SendControlPacketToAll(pkt, false);

    // resync with everyone who's left
    Reset();
  }
}

void Netplay::HandleDropPlayerMessage(s32 player_id, const ENetPacket* pkt)
{
  const DropPlayerMessage* msg = CheckReceivedPacket<DropPlayerMessage>(player_id, pkt);
  if (!msg)
    return;

  if (player_id != s_host_player_id)
  {
    ERROR_LOG("Received unexpected drop player from player {}", player_id);
    return;
  }

  DropPlayer(player_id, msg->reason);
}

void Netplay::HandleCloseSessionMessage(s32 player_id, const ENetPacket* pkt)
{
  const CloseSessionMessage* msg = CheckReceivedPacket<CloseSessionMessage>(player_id, pkt);
  if (!msg)
    return;

  Host::ReportErrorAsync(TRANSLATE_SV("Netplay", "Netplay Session Ended"), msg->ReasonToString());
  RequestCloseSession(msg->reason);
  // TODO: CloseSession() ???
}

void Netplay::ShowChatMessage(s32 player_id, const std::string_view& message)
{
  if (!message.empty())
    OnNetplayMessage(fmt::format("{}: {}", GetNicknameForPlayer(player_id), message));
}

void Netplay::HandleChatMessage(s32 player_id, const ENetPacket* pkt)
{
  const ChatMessage* msg = CheckReceivedPacket<ChatMessage>(player_id, pkt);
  if (!msg)
    return;

  ShowChatMessage(player_id, msg->GetMessage());
}

//////////////////////////////////////////////////////////////////////////
// Settings Overlay
//////////////////////////////////////////////////////////////////////////

void Netplay::SetRollbackSettings(SettingsInterface& si)
{
  si.SetBoolValue("Main", "LoadDevicesFromSaveStates", true);
  si.SetIntValue("Main", "RunaheadFrameCount", 0);
  si.SetBoolValue("Main", "RewindEnable", false);
  si.SetBoolValue("CPU", "RecompilerBlockLinking", false);
  si.SetBoolValue("CPU", "RecompilerInterpreterFallback", false);
  si.SetStringValue("CPU", "FastmemMode", Settings::GetCPUFastmemModeName(CPUFastmemMode::Disabled));
  si.SetBoolValue("GPU", "UseSoftwareRendererForReadbacks", true);
  si.SetBoolValue("GPU", "UseHardwareThread", false);
  si.SetBoolValue("GPU", "PGXPCulling", false);
  si.SetBoolValue("CDROM", "FastForwardLoading", false);
}

bool Netplay::ImageMatchesGame(const std::string& path, std::string_view serial, u64 hash)
{
  Error error;
  const std::unique_ptr<CDImage> image = CDImage::Open(path.c_str(), false, &error);
  std::string image_serial;
  System::GameHash image_hash = 0;
  return image && System::GetGameDetailsFromImage(image.get(), &image_serial, &image_hash) &&
         image_serial == serial && image_hash == hash;
}

std::string Netplay::GetHostSettingsText()
{
  MemorySettingsInterface effective;
  g_settings.Save(effective, false);

  std::string text;
  for (const auto& [section, key] : s_synced_settings)
  {
    if (const std::optional<std::string> value = effective.GetOptionalStringValue(section, key))
      text.append(fmt::format("{}/{}={}\n", section, key, value.value()));
  }
  for (const std::string& patch : Host::GetStringListSetting("Patches", "Enable"))
    text.append(fmt::format("Patches/Enable={}\n", patch));
  return text;
}

void Netplay::SetSettings(std::string_view host_settings)
{
  MemorySettingsInterface& si = s_settings_overlay;
  si.Clear();

  std::vector<std::string> patches;
  for (const std::string_view line : StringUtil::SplitString(host_settings, '\n'))
  {
    const std::string_view::size_type slash = line.find('/');
    const std::string_view::size_type equals = line.find('=');
    if (slash == std::string_view::npos || equals == std::string_view::npos || equals < slash)
      continue;

    const std::string section(line.substr(0, slash));
    const std::string key(line.substr(slash + 1, equals - slash - 1));
    const std::string value(line.substr(equals + 1));
    if (section == "Patches" && key == "Enable")
      patches.push_back(value);
    else
      si.SetStringValue(section.c_str(), key.c_str(), value.c_str());
  }
  si.SetStringList("Patches", "Enable", patches);

  const std::string pad_type = si.GetStringValue(Controller::GetSettingsSection(0).c_str(), "Type",
                                                 Controller::GetControllerInfo(ControllerType::DigitalController)->name);
  for (u32 i = 0; i < NUM_CONTROLLER_AND_CARD_PORTS; i++)
  {
    si.SetStringValue(Controller::GetSettingsSection(i).c_str(), "Type",
                      (i < MAX_PLAYERS) ? pad_type.c_str() : Controller::GetControllerInfo(ControllerType::None)->name);
    si.SetStringValue("MemoryCards", fmt::format("Card{}Type", i + 1).c_str(),
                      Settings::GetMemoryCardTypeName((i == 0) ? MemoryCardType::NonPersistent : MemoryCardType::None));
  }
  si.SetStringValue("ControllerPorts", "MultitapMode", Settings::GetMultitapModeName(MultitapMode::Disabled));
  si.SetBoolValue("CDROM", "LoadImagePatches", false);
  si.SetBoolValue("Console", "EnableCheats", false);
  si.SetBoolValue("PCDrv", "Enabled", false);
  si.SetStringValue("PIO", "DeviceType", Settings::GetParallelPortDeviceName(ParallelPortDevice::None));
  si.SetBoolValue("TextureReplacements", "EnableVRAMWriteReplacements", false);
  si.SetBoolValue("Cheevos", "Enabled", false);
  SetRollbackSettings(si);

  Host::Internal::SetNetplaySettingsLayer(&si);
  System::ApplySettings(false);
}

//////////////////////////////////////////////////////////////////////////
// Frame Pacing
//////////////////////////////////////////////////////////////////////////

void Netplay::InitializeFramePacing()
{
  // Start at 100% speed, adjust as soon as we get a timesync event.
  s_target_speed = 1.0f;
  UpdateThrottlePeriod();

  s_next_frame_time = Common::Timer::GetCurrentValue() + s_frame_period;
}

void Netplay::UpdateThrottlePeriod()
{
  s_frame_period =
    Common::Timer::ConvertSecondsToValue(1.0 / (static_cast<double>(System::GetThrottleFrequency()) * s_target_speed));
}

void Netplay::ToggleDesyncNotifications()
{
  bool was_enabled = send_desync_notifications;
  send_desync_notifications = send_desync_notifications ? false : true;
  if (was_enabled)
    ClearNetplayMessages();
}

void Netplay::HandleTimeSyncEvent(float frame_delta, int update_interval)
{
  // only activate timesync if its worth correcting.
  if (std::abs(frame_delta) < 1.75f)
    return;
  // Distribute the frame difference over the next N * 0.75 frames.
  // only part of the interval time is used since we want to come back to normal speed.
  // otherwise we will keep spiraling into unplayable gameplay.
  float total_time = (frame_delta * s_frame_period) / 4;
  float mun_timesync_frames = update_interval * 0.75f;
  float added_time_per_frame = -(total_time / mun_timesync_frames);
  float iterations_per_frame = 1.0f / s_frame_period;

  s_target_speed = (s_frame_period + added_time_per_frame) * iterations_per_frame;
  s_next_timesync_recovery_frame = CurrentFrame() + static_cast<s32>(std::ceil(mun_timesync_frames));

  UpdateThrottlePeriod();

  VERBOSE_LOG("TimeSync: {} frames {}, target speed {:.4f}%", std::abs(frame_delta),
                    (frame_delta >= 0.0f ? "ahead" : "behind"), s_target_speed * 100.0f);
}

void Netplay::Throttle()
{
  // if the s_next_timesync_recovery_frame has been reached revert back to the normal throttle speed
  s32 current_frame = CurrentFrame();
  if (s_target_speed != 1.0f && current_frame >= s_next_timesync_recovery_frame)
  {
    s_target_speed = 1.0f;
    UpdateThrottlePeriod();

    VERBOSE_LOG("TimeSync Recovery: frame {}, target speed {:.4f}%", current_frame, s_target_speed * 100.0f);
  }

  s_next_frame_time += s_frame_period;

  // If we're running too slow, advance the next frame time based on the time we lost. Effectively skips
  // running those frames at the intended time, because otherwise if we pause in the debugger, we'll run
  // hundreds of frames when we resume.
  Common::Timer::Value current_time = Common::Timer::GetCurrentValue();
  if (current_time > s_next_frame_time)
  {
    const Common::Timer::Value diff = static_cast<s64>(current_time) - static_cast<s64>(s_next_frame_time);
    s_next_frame_time += (diff / s_frame_period) * s_frame_period;
    PollEnet(0);
    return;
  }

  // Poll at 1ms throughout the sleep.
  // We need to send our ping requests through.
  const Common::Timer::Value sleep_period = Common::Timer::ConvertMillisecondsToValue(2);
  while (IsActive())
  {
    current_time = Common::Timer::GetCurrentValue();
    if (current_time >= s_next_frame_time)
      break;

    PollEnet(std::min(current_time + sleep_period, s_next_frame_time));
  }
}

void Netplay::GenerateChecksumForFrame(int* checksum, int frame, unsigned char* buffer, int buffer_size)
{
  const u32 sliding_window_size = 4096 * 4; // 4 pages.
  const u32 num_group_of_pages = buffer_size / sliding_window_size;
  const u32 start_position = (frame % num_group_of_pages) * sliding_window_size;
  *checksum = XXH32(buffer + start_position, sliding_window_size, frame);
  // VERBOSE_LOG("Netplay Checksum: f:{} wf:{} c:{}", frame, frame % num_group_of_pages, *checksum);
}

s32 Netplay::CurrentFrame()
{
  s32 current = -1;
  ggpo_get_current_frame(s_ggpo, current);
  return current;
}

void Netplay::CollectInput(u32 slot, u32 bind, float value)
{
  s_net_input[slot][bind] = value;
}

Netplay::Input Netplay::ReadLocalInput()
{
  Netplay::Input inp = {};
  const Controller::ControllerInfo* cinfo = Controller::GetControllerInfo(g_settings.controller_types[0]);
  if (!cinfo)
    return inp;

  u32 button = 0;
  u32 axis = 0;
  for (const Controller::ControllerBindingInfo& bi : cinfo->bindings)
  {
    const float value = (bi.bind_index < s_net_input[0].size()) ? s_net_input[0][bi.bind_index] : 0.0f;
    if (bi.type == InputBindingInfo::Type::Button && button < 32)
    {
      if (value >= 0.5f)
        inp.button_data |= 1u << button;
      button++;
    }
    else if ((bi.type == InputBindingInfo::Type::HalfAxis || bi.type == InputBindingInfo::Type::Axis) &&
             axis < MAX_INPUT_AXES)
    {
      const float normalized = (bi.type == InputBindingInfo::Type::Axis) ? ((value + 1.0f) * 0.5f) : value;
      inp.axes[axis++] = static_cast<u8>(std::clamp(normalized, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
  }

  return inp;
}

void Netplay::SendChatMessage(const std::string_view& msg)
{
  if (msg.empty())
    return;

  auto pkt = NewControlPacket<ChatMessage>(sizeof(ChatMessage) + static_cast<u32>(msg.length()));
  std::memcpy(pkt.pkt->data + sizeof(ChatMessage), msg.data(), msg.length());
  // TODO: turn chat on for spectators? it's kind of weird to handle. probably has to go through the host and be relayed
  // to the players.
  SendControlPacketToAll(pkt, false);

  // add own netplay message locally to netplay messages
  ShowChatMessage(s_player_id, msg);
}

GGPOErrorCode Netplay::SyncInput(Netplay::Input inputs[2], int* disconnect_flags)
{
  return ggpo_synchronize_input(s_ggpo, inputs, sizeof(Netplay::Input) * 2, disconnect_flags);
}

GGPOErrorCode Netplay::AddLocalInput(Netplay::Input input)
{
  return ggpo_add_local_input(s_ggpo, s_local_handle, &input, sizeof(Netplay::Input));
}

s32 Netplay::GetPing()
{
  const int handle = s_local_handle == 1 ? 2 : 1;
  ggpo_get_network_stats(s_ggpo, handle, &s_last_net_stats);
  return s_last_net_stats.network.ping;
}

u32 Netplay::GetMaxPrediction()
{
  return MAX_ROLLBACK_FRAMES;
}

void Netplay::SetInputs(Netplay::Input inputs[2])
{
  for (u32 i = 0; i < MAX_PLAYERS; i++)
  {
    Controller* const controller = Pad::GetController(i);
    const Controller::ControllerInfo* cinfo =
      controller ? Controller::GetControllerInfo(controller->GetType()) : nullptr;
    if (!cinfo)
      continue;

    u32 button = 0;
    u32 axis = 0;
    for (const Controller::ControllerBindingInfo& bi : cinfo->bindings)
    {
      if (bi.type == InputBindingInfo::Type::Button && button < 32)
      {
        controller->SetBindState(bi.bind_index, ((inputs[i].button_data >> button) & 1u) ? 1.0f : 0.0f);
        button++;
      }
      else if ((bi.type == InputBindingInfo::Type::HalfAxis || bi.type == InputBindingInfo::Type::Axis) &&
               axis < MAX_INPUT_AXES)
      {
        const float normalized = static_cast<float>(inputs[i].axes[axis++]) / 255.0f;
        controller->SetBindState(bi.bind_index,
                                 (bi.type == InputBindingInfo::Type::Axis) ? (normalized * 2.0f - 1.0f) : normalized);
      }
    }
  }
}

bool Netplay::CreateSession(std::string nickname, s32 port, s32 max_players, std::string password, int inputdelay)
{
  s_local_session_password = std::move(password);

  if (!Netplay::Start(true, std::move(nickname), std::string(), port, inputdelay))
  {
    CloseSession();
    return false;
  }

  Host::OnNetplaySessionOpened(true);
  return true;
}

bool Netplay::JoinSession(std::string nickname, const std::string& hostname, s32 port, std::string password,
                          bool spectating, int inputdelay, std::string game_path)
{
  s_join_game_path = std::move(game_path);
  s_local_session_password = std::move(password);
  s_local_spectating = spectating;

  if (!Netplay::Start(false, std::move(nickname), hostname, port, inputdelay))
  {
    CloseSession();
    return false;
  }

  Host::OnNetplaySessionOpened(false);
  return true;
}

void Netplay::ExecuteNetplay()
{
  while (s_state != SessionState::Inactive)
  {
    switch (s_state)
    {
      case SessionState::Connecting:
      {
        UpdateConnectingState();
        continue;
      }

      case SessionState::Resetting:
      {
        UpdateResetState();
        continue;
      }

      case SessionState::Running:
      {
        // TODO: Manage me better.
        System::SetState(System::State::Running);

#if 0
        WaitUntilFrameAdvanced(false);
        CPU::Execute();
#else
        RunFrame();

        // this can shut us down
        Host::PumpMessagesOnCPUThread();
        InputManager::PollSources();
        if (!System::IsValid())
          break;

        System::PresentDisplay(false, false);
        System::UpdatePerformanceCounters();

        Throttle();
#endif
        continue;
      }

      case SessionState::ClosingSession:
      {
        CloseSession();
        break;
      }

      default:
      case SessionState::Initializing:
      case SessionState::Inactive:
      {
        // shouldn't be here
        Panic("Invalid state");
        continue;
      }
    }
  }
}

#if 0
bool Netplay::WaitUntilFrameAdvanced(bool has_presented)
{
  for (;;)
  {
    if (s_state != SessionState::Running)
      return false;

    PollEnet(0);

    // housekeeping
    ggpo_idle(s_ggpo);

    // run game
    auto result = GGPO_OK;
    int disconnect_flags = 0;
    Netplay::Input inputs[2] = {};
    // add local input
    if (s_local_handle != GGPO_INVALID_HANDLE)
    {
      auto inp = ReadLocalInput();
      result = AddLocalInput(inp);
    }

    // advance game
    if (GGPO_SUCCEEDED(result))
    {
      result = SyncInput(inputs, &disconnect_flags);
      // check if you get stuck while spectating. if this is the case try to disconnect.
      if (s_local_spectating && result != GGPO_OK)
      {
        s_spectating_failed_count++;
        // after 15 seconds and still not spectating? you should close since you are stuck.
        if (s_spectating_failed_count > 900)
          CloseSessionWithError("Failed to sync spectator with host. Please try again.");
      }

      if (GGPO_SUCCEEDED(result))
      {
        // enable again when rolling back done
        SPU::SetAudioOutputMuted(false);
        SetInputs(inputs);
        s_frames_to_run++;
        s_spectating_failed_count = 0;
      }
    }

    if (!has_presented)
      System::PresentDisplay(false, false);
    else
      has_presented = false;

    Throttle();
    Host::PumpMessagesOnCPUThread();
    InputManager::PollSources();

    if (s_frames_to_run > 0)
      return true;
  }
}
#endif

void Netplay::AdvanceFrame()
{
  ggpo_advance_frame(s_ggpo, 0);
}

void Netplay::NetplayAdvanceFrame(Netplay::Input inputs[], int disconnect_flags)
{
  Netplay::SetInputs(inputs);

  // TODO: This sucks ass.
  DebugAssert(!s_cpu_executing);
  s_cpu_executing = true;
  g_gpu->RestoreDeviceContext();
  CPU::Execute();
  s_cpu_executing = false;

  Netplay::AdvanceFrame();
}

void Netplay::RunFrame()
{
  PollEnet(0);

  if (s_resync_pending)
  {
    s_resync_pending = false;
    OnNetplayMessage(TRANSLATE_STR("Netplay", "Resynchronizing players after a desync."));
    Reset();
    return;
  }

  if (!s_ggpo)
    return;
  // housekeeping
  ggpo_idle(s_ggpo);
  // run game
  auto result = GGPO_OK;
  int disconnect_flags = 0;
  Netplay::Input inputs[2] = {};
  // add local input
  if (s_local_handle != GGPO_INVALID_HANDLE)
  {
    auto inp = ReadLocalInput();
    result = AddLocalInput(inp);
  }
  // advance game
  if (GGPO_SUCCEEDED(result))
  {
    result = SyncInput(inputs, &disconnect_flags);
    // check if you get stuck while spectating. if this is the case try to disconnect.
    if (s_local_spectating && result != GGPO_OK)
    {
      s_spectating_failed_count++;
      // after 15 seconds and still not spectating? you should close since you are stuck.
      if (s_spectating_failed_count > 900)
        CloseSessionWithError("Failed to sync spectator with host. Please try again.");
    }

    if (GGPO_SUCCEEDED(result))
    {
      // enable again when rolling back done
      SPU::SetAudioOutputMuted(false);
      NetplayAdvanceFrame(inputs, disconnect_flags);
      s_spectating_failed_count = 0;
    }
  }
}

void Netplay::FrameDone()
{
#if 0
  DebugAssert(s_frames_to_run > 0);
  s_frames_to_run--;

  DEV_LOG("FrameDone(), remaining frames = {}", s_frames_to_run);

  System::PresentDisplay(false, false);
  Host::PumpMessagesOnCPUThread();
  
  ggpo_advance_frame(s_ggpo, 0);

  System::UpdatePerformanceCounters();

  if (!WaitUntilFrameAdvanced(true))
    CPU::ExitExecution();
#else
  CPU::ExitExecution();
#endif
}

bool Netplay::NpAdvFrameCb(void* ctx, int flags)
{
  Netplay::Input inputs[2] = {};
  int disconnect_flags;
  Netplay::SyncInput(inputs, &disconnect_flags);
#if 0
  s_frames_to_run++;
  DEV_LOG("frames_to_run = {}", s_frames_to_run);
#else
  NetplayAdvanceFrame(inputs, disconnect_flags);
#endif
  return true;
}

bool Netplay::NpSaveFrameCb(void* ctx, unsigned char** buffer, int* len, int* checksum, int frame)
{
  SaveStateBuffer our_buffer;
  if (s_save_buffer_pool.empty())
  {
    our_buffer = std::make_unique<System::MemorySaveState>();
  }
  else
  {
    our_buffer = std::move(s_save_buffer_pool.front());
    s_save_buffer_pool.pop_front();
  }

  if (!System::SaveMemoryState(our_buffer.get()))
  {
    s_save_buffer_pool.push_front(std::move(our_buffer));
    return false;
  }

  // desync detection
  const u32 state_size = static_cast<u32>(our_buffer->state_size);
  unsigned char* state = our_buffer->state_data.data();
  GenerateChecksumForFrame(checksum, frame, state, state_size);

#if 0
  if (frame > 100 && frame < 150 && s_num_players > 1)
  {
    std::string filename =
      Path::Combine(EmuFolders::Dumps, fmt::format("f{}_c{}_p{}.bin", frame, (u32)*checksum, s_local_handle));
    FileSystem::WriteBinaryFile(filename.c_str(), state, state_size);
  }

  VERBOSE_LOG("Saved real frame {} as GGPO frame {} CS {}", System::GetFrameNumber(), frame, *checksum);
#endif

  *len = sizeof(System::MemorySaveState);
  *buffer = reinterpret_cast<unsigned char*>(our_buffer.release());

  return true;
}

bool Netplay::NpLoadFrameCb(void* ctx, unsigned char* buffer, int len, int rb_frames, int frame_to_load)
{
  // Disable Audio For upcoming rollback
  SPU::SetAudioOutputMuted(true);

  const u32 prev_frame = System::GetFrameNumber();
  if (!System::LoadMemoryState(*reinterpret_cast<const System::MemorySaveState*>(buffer)))
    return false;

#if 0
  VERBOSE_LOG("Loaded real frame {} from GGPO frame {} [prev {}]", System::GetFrameNumber(), frame_to_load,
                    prev_frame);
#endif

  return true;
}

void Netplay::NpFreeBuffCb(void* ctx, void* buffer, int frame)
{
  // VERBOSE_LOG("Reuse Buffer: {}", frame);
  SaveStateBuffer our_buffer(reinterpret_cast<System::MemorySaveState*>(buffer));
  s_save_buffer_pool.push_back(std::move(our_buffer));
}

bool Netplay::NpOnEventCb(void* ctx, GGPOEvent* ev)
{
  switch (ev->code)
  {
    case GGPOEventCode::GGPO_EVENTCODE_CONNECTED_TO_PEER:
      INFO_LOG("GGPO connected to player: {}", ev->u.connected.player);
      break;
    case GGPOEventCode::GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
      INFO_LOG("GGPO synchronizing with player {}: {}/{}", ev->u.synchronizing.player, ev->u.synchronizing.count,
                     ev->u.synchronizing.total);
      break;
    case GGPOEventCode::GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER:
      INFO_LOG("GGPO synchronized with player: {}", ev->u.synchronized.player);
      break;
    case GGPOEventCode::GGPO_EVENTCODE_RUNNING:
      INFO_LOG("GGPO running");
      break;
    case GGPOEventCode::GGPO_EVENTCODE_TIMESYNC:
      HandleTimeSyncEvent(ev->u.timesync.frames_ahead, ev->u.timesync.timeSyncPeriodInFrames);
      break;
    case GGPOEventCode::GGPO_EVENTCODE_DESYNC:
      s_resync_pending = IsHost();
      if (!send_desync_notifications)
        break;

      OnNetplayMessage(fmt::format("Desync Detected: Current Frame: {}, Desync Frame: {}, Diff: {}, L:{}, R:{}",
                                   CurrentFrame(), ev->u.desync.nFrameOfDesync,
                                   CurrentFrame() - ev->u.desync.nFrameOfDesync, ev->u.desync.ourCheckSum,
                                   ev->u.desync.remoteChecksum));
      break;
    default:
      ERROR_LOG("Netplay Event Code: {}", static_cast<int>(ev->code));
      break;
  }

  return true;
}

void Netplay::OnNetplayMessage(std::string message)
{
  INFO_LOG("Netplay: {}", message.c_str());

  while (s_netplay_messages.size() >= MAX_NETPLAY_MESSAGES)
    s_netplay_messages.pop_front();

  s_netplay_messages.emplace_back(std::move(message), Common::Timer::GetCurrentValue() +
                                                        Common::Timer::ConvertSecondsToValue(NETPLAY_MESSAGE_DURATION));
}

void Netplay::ClearNetplayMessages()
{
  while (s_netplay_messages.size() > 0)
    s_netplay_messages.pop_front();
}

void Netplay::RenderOverlays()
{
  Netplay::DrawMessages();
  Netplay::DrawStats();
  Netplay::DrawChatDialog();
}

void Netplay::DrawMessages()
{
  if (s_netplay_messages.empty())
    return;

  const Common::Timer::Value ticks = Common::Timer::GetCurrentValue();
  const ImGuiIO& io = ImGui::GetIO();
  const float scale = ImGuiManager::GetGlobalScale();
  const float shadow_offset = 1.0f * scale;
  const float margin = 10.0f * scale;
  const float spacing = 5.0f * scale;
  const float msg_spacing = 2.0f * scale;
  ImFont* font = ImGuiManager::GetFixedFont();
  float position_y = io.DisplaySize.y - margin - (100.0f * scale) - font->FontSize - spacing;
  ImDrawList* dl = ImGui::GetBackgroundDrawList();

  // drop expired messages.. because of the reverse iteration below, we can't do it in there :/
  for (auto iter = s_netplay_messages.begin(); iter != s_netplay_messages.end();)
  {
    if (ticks >= iter->second)
      iter = s_netplay_messages.erase(iter);
    else
      ++iter;
  }

  for (auto iter = s_netplay_messages.rbegin(); iter != s_netplay_messages.rend(); ++iter)
  {
    const float remainder = static_cast<float>(Common::Timer::ConvertValueToSeconds(iter->second - ticks));
    const float opacity = std::min(remainder / NETPLAY_MESSAGE_FADE_TIME, 1.0f);
    const u32 alpha = static_cast<u32>(opacity * 255.0f);
    const u32 shadow_alpha = static_cast<u32>(opacity * 100.0f);

    // TODO: line wrapping..
    const char* text_start = iter->first.c_str();
    const char* text_end = text_start + iter->first.length();
    const ImVec2 text_size = font->CalcTextSizeA(font->FontSize, io.DisplaySize.x, 0.0f, text_start, text_end, nullptr);

    dl->AddText(font, font->FontSize, ImVec2(margin + shadow_offset, position_y + shadow_offset),
                IM_COL32(0, 0, 0, shadow_alpha), text_start, text_end);
    dl->AddText(font, font->FontSize, ImVec2(margin, position_y), IM_COL32(255, 255, 255, alpha), text_start, text_end);

    position_y -= text_size.y + msg_spacing;
  }
}

void Netplay::DrawStats()
{
  // Not much yet.. eventually we'll render chat and such here too.
  // We'll probably want to draw a graph too..

  SmallString text;
  text.format("Ping: {}\n", Netplay::GetPing());

  const float scale = ImGuiManager::GetGlobalScale();
  const float shadow_offset = 1.0f * scale;
  const float margin = 10.0f * scale;
  ImFont* font = ImGuiManager::GetFixedFont();
  const float position_y = ImGui::GetIO().DisplaySize.y - margin - (100.0f * scale);

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddText(font, font->FontSize, ImVec2(margin + shadow_offset, position_y + shadow_offset), IM_COL32(0, 0, 0, 100),
              text.c_str(), text.end_ptr());
  dl->AddText(font, font->FontSize, ImVec2(margin, position_y), IM_COL32(255, 255, 255, 255), text.c_str(),
              text.end_ptr());
}

void Netplay::DrawChatDialog()
{
  // TODO: This needs to block controller input...

  if (s_netplay_chat_dialog_opening)
  {
    ImGui::OpenPopup("Netplay Chat");
    s_netplay_chat_dialog_open = true;
    s_netplay_chat_dialog_opening = false;
  }
  else if (!s_netplay_chat_dialog_open)
  {
    return;
  }

  const bool send_message = ImGui::IsKeyPressed(ImGuiKey_Enter);
  const bool close_chat =
    send_message || (s_netplay_chat_message.empty() && (ImGui::IsKeyPressed(ImGuiKey_Backspace)) ||
                     ImGui::IsKeyPressed(ImGuiKey_Escape));

  // sending netplay message
  if (send_message && !s_netplay_chat_message.empty())
    Netplay::SendChatMessage(s_netplay_chat_message);

  const ImGuiIO& io = ImGui::GetIO();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float scale = ImGuiManager::GetGlobalScale();
  const float width = 600.0f * scale;
  const float height = 60.0f * scale;

  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowFocus();

  if (ImGui::BeginPopupModal("Netplay Chat", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
  {
    ImGui::SetNextItemWidth(width - style.WindowPadding.x * 2.0f);
    ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##chatmsg", &s_netplay_chat_message);

    if (!s_netplay_chat_dialog_open)
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  if (close_chat)
  {
    s_netplay_chat_message.clear();
    s_netplay_chat_dialog_open = false;
  }

  s_netplay_chat_dialog_opening = false;
}

void Netplay::OpenChat()
{
  if (s_netplay_chat_dialog_open)
    return;

  s_netplay_chat_dialog_opening = true;
}
