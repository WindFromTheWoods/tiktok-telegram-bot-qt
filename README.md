# TikTok Telegram Bot

A desktop Telegram bot controller written in C++23, Qt Quick, and Qt 6.11.2. Its QML
window stores the bot settings and controls the background bot. The bot accepts TikTok
URLs from one configured administrator, downloads each video with `yt-dlp`, and
publishes it to a configured Telegram channel through the Telegram Bot HTTP API.

The application uses asynchronous Qt networking and `QProcess`; it never invokes a
shell and processes one download/upload at a time through a FIFO queue.

## Requirements

- Qt 6.11.2 with the Core, Network, QML, Quick, Quick Controls 2, and Test modules
- CMake 3.24 or newer
- A C++23-capable compiler (MSVC 2022, recent GCC, or recent Clang)
- Ninja when using the included preset, or another CMake generator
- [`yt-dlp`](https://github.com/yt-dlp/yt-dlp)
- `ffmpeg` is recommended so `yt-dlp` can merge video and audio into MP4
- A Telegram bot token and a target Telegram channel

TikTok changes its site regularly. Keep `yt-dlp` current. The standalone executable
can normally update itself with:

```text
yt-dlp -U
```

## Telegram setup

1. Open `@BotFather` in Telegram, create a bot, and copy its token.
2. Determine the numeric Telegram user ID that will be allowed to submit links. A
   trusted Telegram user-ID bot or a `getUpdates` response can provide it.
3. Create or select the destination channel.
4. Add the bot to that channel.
5. Make the bot a channel administrator and grant it permission to post messages.
6. Use the public channel username (for example, `@channel_name`) or the numeric
   channel ID (for example, `-1001234567890`) as `TELEGRAM_CHANNEL_ID`.

Never commit the bot token or other credentials. On Windows, the GUI stores the token
in Windows Credential Manager rather than in the project or CMake cache.

## Configuration

Open the application and fill in these fields:

| Field | Purpose |
| --- | --- |
| Bot token | Token issued by BotFather; hidden in the UI |
| Channel ID | Destination `@channel_name` or negative numeric ID |
| Administrator user ID | Only user allowed to submit download requests |
| yt-dlp executable | Executable name or full path; defaults to `yt-dlp` |
| Publication interval | Minutes between successful channel posts; defaults to `120` |

Select **Save and start**. Non-secret settings are stored with `QSettings`; on Windows,
the token is stored in Windows Credential Manager. At subsequent launches, the bot
starts automatically when a complete valid configuration is present. **Stop bot**
stops polling without closing the settings window.

The **Video catalog** has two tabs. **Scheduled** shows the active download/upload and
all queued TikTok links with their estimated publication slots. **Sent** keeps the 200
most recent successful publications. Click a catalog row to open its original TikTok
URL. Both the pending queue and sent history are retained across application restarts.

For migration from an older version, the application can still import
`TELEGRAM_BOT_TOKEN`, `TELEGRAM_CHANNEL_ID`, `TELEGRAM_ADMIN_USER_ID`, `YTDLP_PATH`, and
`TELEGRAM_POST_INTERVAL_MINUTES` from the process environment when no saved value
exists. Save once in the GUI and then remove those variables from Qt Creator's Run
Environment. The application never logs the token.

## Build and test

Make Qt discoverable either by using a Qt-enabled developer terminal or by setting
`CMAKE_PREFIX_PATH` to the matching Qt kit. For example, on Windows with an MSVC Qt
installation:

```powershell
$env:CMAKE_PREFIX_PATH="C:\Qt\6.11.2\msvc2022_64"
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Without presets:

```text
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt> -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The Qt compiler kit must match the selected compiler. For example, use an MSVC Qt kit
with MSVC and a MinGW Qt kit with its matching MinGW toolchain.

## Run

Run the executable from the build directory. No Run Environment entries are required.
With the included single-configuration Ninja preset on Windows:

```powershell
$env:PATH="C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"
.\build\TikTokTelegramBot.exe
```

On Linux:

```bash
./build/TikTokTelegramBot
```

The first launch opens the settings window. Enter the required values and select
**Save and start**. For a deployable directory containing the required Qt runtime and
QML files, install the project after building:

```powershell
cmake --install build --prefix dist
.\dist\bin\TikTokTelegramBot.exe
```

Send `/start`, `/help`, or one supported URL to the bot:

```text
https://www.tiktok.com/@username/video/123456789
https://tiktok.com/@username/video/123456789
https://vm.tiktok.com/ABC123/
https://vt.tiktok.com/ABC123/
```

Requests from any user other than `TELEGRAM_ADMIN_USER_ID` are rejected. Valid requests
are queued in arrival order. The first request can publish immediately; after every
successful post, the next queued request waits for the configured interval (two hours by
default). The queue and last successful publication time are retained across application
restarts.

The Telegram Bot HTTP API does not expose native scheduled channel messages. Scheduling
therefore happens inside this application, which must remain running when a publication
slot becomes due. Use `/status` to inspect the active job, queue length, and next slot.

## Runtime behavior

For each accepted URL, the bot:

1. replies `Downloading TikTok...`;
2. starts `yt-dlp` asynchronously with an isolated temporary directory;
3. uses the filepath printed by `yt-dlp` and verifies that it remains inside that
   directory;
4. rejects files over the public Bot API's 50 MB `sendVideo` limit;
5. replies `Uploading video...` and performs a multipart upload;
6. reports success or an understandable error to the requesting chat;
7. removes the temporary directory and continues with the queue.

No custom TikTok scraper or automatic transcoding is included. A local Telegram Bot
API server or compression service can be added later at the upload boundary without
changing the downloader.

## Operational notes

- The public Telegram Bot API currently limits bot `sendVideo` uploads to 50 MB. This
  project checks that limit before upload.
- Private, deleted, geographically restricted, or login-protected videos can cause
  `yt-dlp` to fail. A shortened, sanitized diagnostic is sent back while the bot stays
  alive.
- Telegram polling uses a 30-second long poll and bounded retry backoff, including the
  server-provided delay for HTTP 429 responses.
- On application shutdown, polling stops, active network requests are aborted, queued
  jobs are discarded, and an active `yt-dlp` process is asked to terminate.

## Project layout

```text
qml/            Qt Quick settings and bot-control interface
src/app/        orchestration, scheduler, and FIFO job queue
src/config/     validation, persistent settings, and credential storage
src/telegram/   Bot API networking, response parsing, and long polling
src/tiktok/     strict URL validation and QProcess-based downloader
src/ui/         C++ bridge between QML settings and bot lifecycle
tests/          offline Qt Test suites; no Telegram or TikTok access
```
