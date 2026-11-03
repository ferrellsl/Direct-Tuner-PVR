# Direct Tuner PVR

A Kodi add-on that watches and records TV straight from an HDHomeRun tuner on your network. You don't
need NextPVR or any other server, and you don't need a SiliconDust DVR subscription.

**This is a pre-release for testing.** Expect rough edges, and please [report problems](#reporting-a-problem).

**Recording with Kodi closed:** install
[Direct Tuner Recorder](https://github.com/ferrellsl/Direct-Tuner-Recorder) on one computer that's usually
on, running Windows or Linux. It records whether or not Kodi is running, wakes a Windows computer from sleep
to record, and lets every Kodi in the house share the same timers, recordings and tuners. Each Kodi finds
it by itself: there's nothing to set up in Kodi. See [Recording with Kodi closed](#recording-with-kodi-closed).

Without the recorder, the add-on still records, but only while Kodi is open.

## What you need

- Kodi 21 (Omega). The add-on won't load on other Kodi versions.
- An HDHomeRun tuner on your network.
- One of these:

| Platform | How to get the add-on |
|---|---|
| Windows (64-bit) | Included in the [AC-4 build's](https://github.com/ferrellsl/kodi-ac4/releases) installer, or install the zip below into standard Kodi 21 |
| Linux (x86_64) | Included in the AC-4 build's AppImage |
| Android and Fire TV | Included in the AC-4 build's APKs (arm64 for most phones, tablets and newer TV boxes; armv7 for Fire TV sticks) |
| macOS, iOS, other Linux CPUs | Not available |

On Linux and Android, add-ons with compiled code can't be installed from a zip, so the add-on only
comes inside the AC-4 build there. Wherever it's included, it starts **off**: skip to
[step 4](#4-turn-it-on-and-turn-off-any-other-pvr-add-on) to turn it on under **My add-ons** > **PVR clients**.

### Which Kodi to use

| Channels | Standard Kodi 21 | [AC-4 build of Kodi](https://github.com/ferrellsl/kodi-ac4/releases) |
|---|---|---|
| ATSC 1.0 (most channels) | Picture and sound | Picture and sound |
| ATSC 3.0 with AC-4 audio | Picture, **no sound** | Picture and sound |
| Copy-protected (DRM) | Can't play | Can't play |

Standard Kodi can't decode AC-4 audio. If you want sound on ATSC 3.0 channels, install the
[AC-4 build](https://github.com/ferrellsl/kodi-ac4/releases) first.

## Install

> **Most people can skip the zip.** If you installed the latest
> [AC-4 build of Kodi](https://github.com/ferrellsl/kodi-ac4/releases/latest), the add-on is already
> included: go straight to [step 4](#4-turn-it-on-and-turn-off-any-other-pvr-add-on) to turn it on. It's
> in the Windows installer from v21.3-ac4.5 on, and in the Android and Linux downloads from v21.3-ac4.6
> on.
>
> Steps 1 to 3 are only for Windows, if you use standard Kodi 21 or an AC-4 build older than
> v21.3-ac4.5, or to try a newer add-on version before it's in an AC-4 build. Updating to the latest AC-4
> build does the same job.

### 1. Download the add-on

Go to the [Releases page](https://github.com/ferrellsl/Direct-Tuner-PVR/releases) and download
`pvr.directtuner-21.2.0.zip`. Don't unzip it.

If an older version is installed, installing the new zip the same way replaces it and keeps your
settings, timers and recordings.

### 2. Allow add-ons from zip files

Kodi blocks zip files until you allow them. You only have to do this once.

In Kodi, go to **Add-ons**, then the open box icon at the top left, then **Install from zip file**. If
you see this message, choose **Settings**:

![Kodi's message that unknown sources are disabled](docs/images/01-unknown-sources-prompt.png)

Turn on **Unknown sources**:

![The Unknown sources setting](docs/images/02-unknown-sources-setting.png)

Kodi shows a warning. Choose **Yes**, then go back to the Add-ons screen:

![Kodi's warning about unknown sources](docs/images/03-unknown-sources-warning.png)

### 3. Install the zip

Choose **Install from zip file**:

![The Install from zip file option](docs/images/04-install-from-zip.png)

Find the folder you downloaded to and choose the zip:

![Choosing the zip file](docs/images/05-choose-zip.png)

After a few seconds Kodi shows **Add-on installed**:

![The Add-on installed message](docs/images/06-installed-notice.png)

That's all the setup most people need. The add-on finds your tuner by itself and loads the channels and
guide. Open **TV** on Kodi's main menu to watch.

### 4. Turn it on, and turn off any other PVR add-on

If the add-on came with the AC-4 build, it's installed but off. Go to **Add-ons** > open box icon >
**My add-ons** > **PVR clients**, choose **Direct Tuner PVR** and then **Enable**.

If you also use another PVR add-on, Kodi would show both add-ons' channels, so turn the old one off.
This disables it without deleting it, so you can go back at any time.

1. Go to **Add-ons** > open box icon > **My add-ons** > **PVR clients**.
2. Choose the old add-on, such as NextPVR, and then **Disable**.
3. Restart Kodi.

To switch back, enable the old add-on and disable Direct Tuner PVR the same way. This is also where you
open the add-on's settings, with **Configure**:

![The add-on's page with Configure and Disable](docs/images/09-addon-page.png)

### 5. Optional: show the tuner's channel numbers

Kodi numbers channels 1, 2, 3 by default. To see the real numbers, such as 3.1 and 5.1:

1. Go to **Settings** > **PVR & Live TV** > **Channels**.
2. Set the level at the bottom left to **Advanced**.
3. Turn on **Use channel numbers from backend**.
4. Restart Kodi.

## Settings

Everything here is optional. Open the settings with **Configure**, as shown above.

![The add-on's settings](docs/images/07-settings.png)

| Setting | What it does |
|---|---|
| **Direct Tuner Recorder** | Whether to use [Direct Tuner Recorder](#recording-with-kodi-closed). **Use one if found on the network** (the default) uses it whenever one is running, and otherwise works on its own. **Don't use one** always works on its own. **Use this address** is for a recorder that isn't found automatically. |
| **Recordings folder** | Where recordings are saved. If left empty, they go in `Kodi Recordings` in your Videos folder. |
| **Start recording early** | Minutes to start before a show. Default 1. |
| **Keep recording after the end** | Minutes to keep going after a show. Default 3. |
| **XMLTV guide file** | An optional guide file. See [Guide](#guide). |
| **Timeshift size** | How much live TV is kept on disk for pausing and rewinding. About 2 GB holds 15 minutes of HD. 0 turns pausing off. Default 4 GB. |
| **Hide copy-protected channels** | Hides DRM channels, which can't play. On by default. |
| **Tuner IP address** | Only needed if the tuner isn't found, for example on another subnet. Enter an address like `192.168.1.50`. |

![The last of the settings](docs/images/08-settings-more.png)

When the add-on uses Direct Tuner Recorder, the recordings folder, guide file, copy-protected channels
and tuner address are set in the recorder instead, and those settings here aren't used. The early and
late minutes and the timeshift size still apply: new timers get this Kodi's padding, and pausing live
TV happens on each Kodi's own computer.

Kodi restarts the add-on when you change most settings. Without the recorder, that stops any recording
in progress; with it, recordings carry on, since the recorder makes them.

## Guide

**With no setup,** the guide comes from SiliconDust through your tuner. It covers about 2 days and needs
no subscription.

**With an XMLTV file,** choose the file in the settings if you already make one, for example with
zap2xml. The file is used for every channel it covers, and the built-in guide fills in the rest. The
add-on matches channels by the number in a channel's display name. ATSC 3.0 channels numbered 100 and up
also match that number minus 100, so 103.1 uses the listings for 3.1. The add-on rereads the file when
it changes.

![The TV guide](docs/images/10-guide.png)

## Recording

### One show

In the **Guide**, choose a show and then **Record**:

![A show's details, with the Record button](docs/images/11-program-info.png)

### A series, or a custom time

Choose **Add timer** instead. For a show in the guide, the type starts as **Series (guide)**. You can
change it to **One time (guide)** with **Type**.

![The timer settings for a series](docs/images/12-add-timer.png)

Scroll down for the rest. Useful settings for a series:

- **Channel:** choose any channel to find the show wherever it airs.
- **Days of week** and **Start any time:** limit when it records.
- **Prevent duplicate episodes:** choose **New episodes only** to skip repeats.
- **Max recordings:** keeps only the newest ones and deletes older recordings of that series.

![More timer settings](docs/images/13-timer-settings-more.png)

Leave the padding times at 0 to use the add-on's own early and late minutes.

To record at a set time with no guide entry, go to **TV** > **Timers** > **Add timer**.

### Timers and recordings

**TV** > **Timers** lists what's coming up. A series shows each airing, and you can delete one episode
without changing the series. Kodi marks a timer that failed or has no free tuner for it.

![The Timers list](docs/images/14-timers.png)

**TV** > **Recordings** lists what you've recorded. You can play a show while it's still recording.
Open a recording's context menu to delete it, which removes the file.

![The Recordings list](docs/images/15-recordings.png)

Each recording is a `.ts` file with a `.json` file of the same name beside it.

## Recording with Kodi closed

[Direct Tuner Recorder](https://github.com/ferrellsl/Direct-Tuner-Recorder) is a separate program for
Windows 10 or 11 (64-bit) or Linux (x86_64; the Linux version is untested so far). Install it on **one**
computer that's usually on, following its README. It runs in the background and:

- Records your timers whether or not Kodi is running. On Windows it wakes the computer from sleep to
  record; on Linux, turn off automatic sleep on that computer.
- Shares one set of timers, recordings and tuners with every Kodi in the house, on any device.
- Lets a recording take a tuner back from live TV on any Kodi, so a show isn't missed when every
  tuner is in use.

You don't change anything in Kodi. With this add-on's default settings, every Kodi finds the recorder
by itself the next time it starts and shows a notice that it's using it. From then on you schedule,
watch and delete in Kodi exactly as described above, and you can close Kodi or turn that device off. Any timers a Kodi had move to
the recorder automatically. Recordings that Kodi made on its own stay where they are; to see them
alongside the new ones, move their files into the recorder's recordings folder.

Only install the recorder on one computer per tuner. If a Kodi doesn't find it, for example on another
subnet, set **Direct Tuner Recorder** to **Use this address** in the [settings](#settings) and enter the
recorder computer's address.

## Known limits

- **Without [Direct Tuner Recorder](#recording-with-kodi-closed), recordings run only while Kodi is
  open.** Close Kodi during a recording and it stops; if you reopen Kodi before the show ends, recording
  carries on in a second file. That Kodi's device must also stay awake. With the recorder, none of this
  applies.
- **A recording takes priority over live TV.** If every tuner is busy, a recording stops whatever live TV
  you're watching. Watching and recording the same channel uses only one tuner.
- **"Play recording / Switch to channel".** Kodi asks this when you open a channel that's recording. It's
  a normal Kodi message: choose **Switch to channel** for live TV.
- **AC-4 channels take about 15 seconds to start.**
- **Seeking across a signal dropout is inexact.** If the signal dropped while you were watching, rewinding
  past that point can land several seconds away from where you aimed.
- **"No signal on this channel"** means the tuner couldn't lock on. It's a reception problem. ATSC 3.0
  usually needs a stronger signal than ATSC 1.0.
- **One tuner device at a time.** If you have more than one, enter the address of the one to use.
- **Network folders aren't tested yet.** On Windows a recordings folder on a network share should work,
  but a local drive is the safe choice for now. On Linux and Android, choose a local folder or one
  already mounted by the system; Kodi's own `smb://` locations won't work.
- **On Android,** recordings go by default to Kodi's own folder,
  `Android/data/org.xbmc.kodiac4/files/Videos/Kodi Recordings`, which Android deletes if you uninstall
  Kodi. To keep them, choose a folder such as `Movies` in the settings. Kodi asks for access to all
  files the first time it starts; allow it, or only its own folder can be used.
- **Broadcast discovery on Linux and Android is untested.** If the tuner isn't found, enter its IP
  address in the settings.

## Reporting a problem

Please [open an issue](https://github.com/ferrellsl/Direct-Tuner-PVR/issues) with:

- Your Kodi version, and whether it's standard Kodi or the AC-4 build.
- Your tuner model.
- What you did, what you expected, and what happened.
- The relevant part of `kodi.log`, which is in `%APPDATA%\Kodi`.

**Before sharing a log, search it for `DeviceAuth` and remove the value.** It identifies your tuner to
SiliconDust, and Kodi's own logging can include it in the guide's web address, especially with debug
logging on. Also check the log for anything else you'd rather keep private, such as your user name in
folder paths.

## Build from source

You need Visual Studio 2022, CMake, Ninja and a Kodi 21 source tree for the add-on headers.

```
powershell tools\build.ps1 -Package -KodiInclude <kodi source>\xbmc\addons\kodi-dev-kit\include
```

The zip goes in `dist`. `build\directtuner_tests.exe` tests the core with no Kodi or tuner, and
`build\directtuner_tool.exe` tries it against a real tuner. `src/core` has no Kodi types in it.

## License and credits

GPL-2.0-or-later. See [LICENSE.md](LICENSE.md).

This add-on uses the approach of the GPL add-ons pvr.hdhomerun and pvr.nextpvr.

Direct Tuner PVR is an independent project. It isn't affiliated with or endorsed by SiliconDust, Team
Kodi or NextPVR. HDHomeRun is a trademark of SiliconDust USA Inc., and Kodi is a trademark of the XBMC
Foundation.
