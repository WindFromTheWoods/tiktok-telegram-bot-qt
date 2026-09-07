# TikTok Telegram Bot

Production-oriented desktop publisher written in C++23, Qt 6.11, and QML. One
authorized Telegram user sends TikTok URLs to the bot; the application downloads each
video in advance and publishes it to one of the configured Telegram channels according
to a daily/weekly calendar or a fixed fallback interval.

The application is event-driven. Telegram requests use `QNetworkAccessManager`,
`yt-dlp` runs through `QProcess` without a shell, and one download plus one upload are
processed at a time.

## Implemented features

- Dashboard with queue, publication, failure, cache, disk, download, and next-slot
  metrics.
- Persistent SQLite queue and history with automatic recovery after an interrupted
  download or upload.
- Immediate pre-download into a persistent cache, including title, creator, duration,
  thumbnail, size, and progress.
- Batch URL import with a validation preview, deliberate-repeat override, draft mode,
  and channel-local first publication time.
- URL and source-video deduplication per channel, including published history.
- Draft approval, editable 1024-character captions, reusable `{title}`, `{author}` and
  `{url}` templates, and safe local MP4 preview.
- Search and filters for title, creator, URL, ID, channel, state, and date; multi-select
  approve/hold/retry/cancel/channel/schedule actions.
- Week/month calendar with per-channel time zones, free-slot hints, and drag-to-date
  rescheduling that preserves channel-local wall time.
- Optional Telegram server queue powered by TDLib and a user account. Videos can be
  uploaded days ahead, after which Telegram stores, publishes, reschedules, or cancels
  the scheduled post without keeping the application online.
- Separate **Scheduled**, **Failed**, and **Sent** video catalogs.
- Multiple Telegram channels with one default channel.
- Daily and weekday-specific calendar slots. The default fallback remains 120 minutes.
- JSON backup/import for channels, schedule, queue, and history. Bot tokens are never
  exported.
- Confirmed Telegram message IDs for completed publications. Network ambiguity enters
  `delivery_unknown` and requires Telegram reconciliation or explicit user verification;
  it is never blindly resent.
- Durable retry deadlines and failure categories in SQLite, plus bounded per-video event
  history that survives application restarts.
- Configurable cache budget, free-disk reserve, terminal-media retention, background
  cleanup, bounded downloader runtime/output/working space, and automatic queue resume.
- On-demand diagnostics for `yt-dlp`, FFmpeg, TDLib, bot identity, and channel posting
  permissions.
- Single-instance activation: a repeated launch brings the existing window forward.
- Windows Credential Manager storage for the bot token, TDLib API hash, and TDLib
  database encryption key.
- Windows autostart, minimize-to-tray behavior, and tray notifications.
- Persistent activity log in the UI.
- Daily application and `yt-dlp` update checks. Application updates are installed by
  Qt Installer Framework's maintenance tool.
- Offline tests for configuration and URL validation, Telegram response parsing,
  scheduling boundaries, SQLite queue invariants and recovery, backup protection,
  TDLib transport lifecycle, and server-side publication operations.
- Beningo-inspired C++ formatting rules and generated Doxygen API documentation.

The interface contains five pages: **Dashboard**, **Videos**, **Schedule**,
**Settings**, and **Logs**. Environment variables and CMake cache variables are not
needed after the settings have been saved in the application.

## Requirements

- Qt 6.11.2: Core, Concurrent, Network, SQL, Widgets, QML, Quick, Quick Controls 2,
  Linguist Tools, and Test
- CMake 3.24 or newer
- A C++23 compiler (MSVC 2022, recent GCC, or recent Clang)
- Ninja or another CMake generator
- [`yt-dlp`](https://github.com/yt-dlp/yt-dlp)
- `ffmpeg`, recommended for merging video and audio streams into MP4
- A Telegram bot token, administrator user ID, and destination channel
- Optional for server scheduling: a 64-bit `tdjson.dll`, a separate Telegram user
  account with channel administrator rights, and that application's `api_id` and
  `api_hash`
- Qt Installer Framework when producing installers and application updates

## Telegram setup

1. Create a bot through `@BotFather` and copy its token.
2. Determine the numeric user ID that is allowed to submit links.
3. Add the bot to each destination channel as an administrator with permission to post.
4. Use a public channel username such as `@channel_name`, or a numeric channel ID such
   as `-1001234567890`.
5. Start the application, enter the token, channel ID, administrator ID, and `yt-dlp`
   path, then select **Save and start**.

### Telegram server scheduling with TDLib

The standard Bot API cannot create native scheduled posts. The optional server mode
therefore keeps Bot API polling for incoming TikTok links, but publishes through an
authorized Telegram **user** account using [TDLib](https://core.telegram.org/tdlib).

1. Create Telegram API credentials at [my.telegram.org](https://my.telegram.org/).
2. Build `tdjson.dll` from the official
   [TDLib sources and instructions](https://github.com/tdlib/td#building), or use a
   trusted build matching the MSVC x64 application. Keep its dependency DLLs beside
   `tdjson.dll`.
3. Add the user account as an administrator of every configured channel, with
   permission to post.
4. In **Settings → Publication mode**, choose **Telegram server queue**. Select
   `tdjson.dll`, enter `api_id`, `api_hash`, and the phone number, then press
   **Connect TDLib**.
5. Complete the Telegram login prompt (code, email, or two-step-verification password)
   in the same settings card, then save and start the bot.

Use `@channel_username` for the most reliable TDLib channel lookup. A private channel
without a public username can use its TDLib chat identifier if the account has already
opened that chat. Never use the bot token as `api_hash`; they are unrelated credentials.
The API hash and the local TDLib database encryption key are kept in Windows Credential
Manager. Login codes and two-step-verification passwords are never saved.

On Windows, the token is stored in Windows Credential Manager. The remaining settings
use `QSettings`; queue, schedule, channels, metadata, and history are stored in SQLite.
The current database path is visible on the Settings page.

If a token has appeared in a screenshot, terminal log, CMake option, or repository,
revoke it through `@BotFather` and save a newly issued token.

## Scheduling behavior

When the bot receives a valid URL from the configured administrator, it:

1. rejects a URL or canonical TikTok video ID already present in that channel's queue or
   history unless a deliberate repeat was explicitly enabled;
2. assigns the default channel and its next free calendar slot;
3. saves the task in SQLite and replies with its queue position and planned time;
4. downloads the video and metadata immediately into the application cache;
5. in local mode, waits until the selected slot and uploads the cached MP4 through the
   Bot API; in server mode, uploads immediately to Telegram with the future schedule;
6. records a publication only after Telegram returns a usable chat and message ID. If
   delivery cannot be proven, the item remains quarantined as `delivery_unknown` until
   it is reconciled or verified by the user;
7. retains terminal cache files according to the configured policy. A server-scheduled
   card can still be rescheduled, published immediately, or cancelled from the application.

A slot with **Every day** is considered on every date. Weekday slots apply only to the
selected weekday. If a channel has no calendar slots, publications use the configured
fixed interval, which defaults to two hours. In local mode the application must be
running (it may be hidden in the system tray) when publication becomes due. In server
mode the application only needs to stay online until TDLib finishes uploading and the
card says `server_scheduled`; Telegram handles the due time after that. Telegram accepts
server dates from 10 seconds to 367 days in the future.

The public Telegram Bot API currently accepts `sendVideo` uploads up to 50 MB, so larger
downloads are rejected in local mode. Server mode uses a conservative 2 GB upload
safety limit. Private, deleted, restricted, or login-protected TikTok posts may still
fail in `yt-dlp`.

## Build and test

With an MSVC Qt installation:

```powershell
cmake -S . -B build/release -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

The Qt kit and compiler must match. For example, build an MSVC Qt kit with MSVC, not
MinGW. In Qt Creator, select **Desktop Qt 6.11.2 MSVC2022 64bit**, configure, build, and
run. No Run Environment entries are required.

For a safe UI-only smoke test that does not start Telegram polling:

```powershell
./build/release/TikTokTelegramBot.exe --ui-smoke-test
```

The smoke process opens the QML engine and exits automatically after 1.5 seconds.
It uses a temporary database, settings directory, and single-instance endpoint, so it
does not read credentials or modify the live queue. `--ui-demo`, `--ui-page=N`, and
`--ui-screenshot=PATH` are available for repeatable visual verification.

### Code style and API documentation

The project uses a Beningo-inspired style adapted to Qt: Allman braces, mandatory
braces for control flow, four-space indentation, consistent naming, and documented
public contracts. The complete rules are in
[`docs/CODING_STYLE.md`](docs/CODING_STYLE.md); `.clang-format` is the executable
formatting profile.

Qt Creator can apply the repository profile through **Preferences → C++ → Code Style
→ ClangFormat**. When `clang-format` is available to CMake, these targets are added:

```powershell
cmake --build build/release --target format
cmake --build build/release --target format-check
```

To generate the Doxygen HTML reference:

```powershell
cmake -S . -B build/docs -DBUILD_DOCUMENTATION=ON
cmake --build build/docs --target docs
```

Open `build/docs/documentation/html/index.html`. Documentation warnings fail the
`docs` target so incomplete public contracts cannot silently enter the reference.

## Run and operate

Send one of these to the bot:

```text
/start
/help
/status
https://www.tiktok.com/@username/video/123456789
https://vm.tiktok.com/ABC123/
https://vt.tiktok.com/ABC123/
```

Requests from other users are rejected. Use the desktop catalog for queue actions and
the Schedule page for channel-specific calendar slots. Closing the window minimizes the
running application to the tray; select **Exit** from the tray menu to terminate it.

Legacy variables are imported only when a saved value does not exist:

```text
TELEGRAM_BOT_TOKEN
TELEGRAM_CHANNEL_ID
TELEGRAM_ADMIN_USER_ID
YTDLP_PATH
TELEGRAM_POST_INTERVAL_MINUTES
```

After saving through the UI, remove secret environment entries from Qt Creator and
remove any token accidentally placed in CMake configuration options.

## Installer and automatic updates

First build the Release configuration. Then install Qt Installer Framework and create
the installer, passing the HTTPS URL where the update repository will be hosted:

```powershell
./scripts/Build-Installer.ps1 `
  -QtIfwRoot C:/Qt/Tools/QtInstallerFramework/4.10 `
  -UpdateRepositoryUrl https://updates.example.org/tiktok-telegram-bot `
  -BuildDirectory build/release
```

This creates `artifacts/TikTokTelegramBot-2.1.0-Setup.exe` and a staged package under
`.packaging`. Generate the remote repository from that exact package:

```powershell
./scripts/Publish-UpdateRepository.ps1 `
  -QtIfwRoot C:/Qt/Tools/QtInstallerFramework/4.10
```

Upload the complete `artifacts/update-repository` directory to the URL passed to
`Build-Installer.ps1`. For a new release, update the project/package version and release
date, rebuild, regenerate the repository, and replace the hosted repository contents.

Installed copies check the maintenance tool and `yt-dlp` once per day. The UI announces
an application update and lets the user start the maintenance tool. `yt-dlp` can be
updated directly from Settings. A build launched directly from Qt Creator has no
maintenance tool, so the UI correctly reports that application updates require the
installer build.

## Project layout

```text
installer/      Qt Installer Framework configuration and package metadata
qml/            dashboard, catalog, schedule, settings, and log pages
scripts/        installer and update-repository build scripts
src/app/        orchestration, scheduling, retries, updates, and autostart
src/config/     validation and credential storage
src/storage/    SQLite schema, queue operations, and backup/restore
src/telegram/   Bot API polling plus optional dynamically loaded TDLib publishing
src/tiktok/     URL validation, yt-dlp execution, progress, and metadata
src/ui/         QML bridge and Windows tray integration
tests/          offline Qt Test suites
```
