# Privacy

Riverhorse has no accounts, no analytics, no telemetry and no adverts. It only
goes online for the features below. Everything else, including your games,
BIOS images, settings and saves, stays on your device.

## What goes online

| Feature | Connects to | When | What is sent |
|---|---|---|---|
| Update check | GitHub (`api.github.com`) | At start-up, unless Enable Automatic Update Check is turned off in Settings > Interface | The version you run, to compare it with the latest release |
| Cover downloads | GitHub (`libretro-thumbnails`) | Only when you turn on Fetch Missing Cover Art or use Download Covers | The names of the games missing a cover |
| RetroAchievements | RetroAchievements (`retroachievements.org`) | Only after you log in | Your username and login token, the game you play and your unlocks. [RetroAchievements' privacy policy](https://retroachievements.org/privacy-policy) applies |
| Netplay | The other players, and the relay server | Only while you host or join a session | See below |
| Save sync | The WebDAV server you enter | Only when you set up WebDAV save sync | Your memory cards, save states and game settings |
| Discord Rich Presence | The Discord app on your device | Only when you turn it on (off by default) | The game you play |

## Netplay relay

Unless you turn the relay off or enter your own, netplay uses the Riverhorse
relay at `relay.riverhorse.app`. It introduces players to each other and
passes on traffic for players whose routers block direct connections.

- **What it sees:** the IP address and port of each player, and the netplay
  traffic it passes on.
- **The public session list:** only when the host chooses to list a session,
  the relay shows its nickname, game title and serial, Riverhorse version,
  player count and whether it needs a password. Host addresses are never
  listed.
- **How long it is kept:** in memory only, while the session runs. A session
  is dropped 30 seconds after the host stops or stops answering. The relay
  writes no IP addresses, nicknames or game details to its logs.
- **Where it runs:** Oracle Cloud, London, United Kingdom.

Players in the same session connect to each other directly, so they can see
each other's IP addresses, as with any peer-to-peer game.

## Your rights

Riverhorse keeps no personal data except the relay's in-memory session state
above, which is used only to run netplay and is gone when the session ends.
Under the UK GDPR and EU GDPR you can ask what is held about you or ask for it
to be deleted; since nothing is kept after a session ends, there is normally
nothing to return or delete. Ask through
[GitHub Issues](https://github.com/thenotoriousfox/riverhorse/issues).

Riverhorse is maintained by thenotoriousfox. Changes to this policy are listed
in the [repository history](https://github.com/thenotoriousfox/riverhorse/commits/main/site/privacy.md).
