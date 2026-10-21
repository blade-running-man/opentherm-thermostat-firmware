# First setup

A freshly installed device has no Wi-Fi stored, so it puts its own setup network on the air. You
join that network, tell the device which Wi-Fi to use, and finish the rest of the configuration
from your own network. The boiler is polled from the first second regardless; nothing on this page
stops or delays the conversation with it.

## 1. Join the setup network

| | |
| --- | --- |
| Network name | `opentherm-xxxx` — `xxxx` is four hex digits from the board's station MAC, unique per board |
| Security | **Open**, no password; up to four clients |
| Address | `http://192.168.4.1` |
| Status LED | **Blue, slow blink** (one second on, one second off) |

The console prints the exact name: `net: provisioning: nothing stored, access point 'opentherm-xxxx'`.

Join `opentherm-xxxx` from a phone or a laptop, then **open a browser yourself** and go to
`http://192.168.4.1`. While the setup network is up the device answers every DNS name with its own
address, so any plain `http://` address you type lands on its web interface.

::: tip No sign-in window opens by itself
The device answers the connectivity checks of Android, Apple systems, Windows and Firefox with the
replies those systems expect from a working network (`components/ot_captive`), so the operating
system concludes the network is fine and does **not** open its captive-portal sign-in window. That
window is a cut-down browser that drops the page as soon as the device retunes its radio — the
moment that matters most here. Use a normal browser tab instead.

The phone may still say the network has **no internet**. That is true; stay connected.
:::

The setup network is open on purpose: there is no way to tell you a key for it before you can reach
the device. While it is on the air the device counts as **unclaimed**, and it accepts exactly one
kind of write — a Wi-Fi network to join (`POST /api/provision`). Setting a password, a broker or
anything else is refused until the device is on your network. Reading is open, so the pages load.

## 2. Mind the setup window

The setup network does not stay up for ever (`components/ot_provision`):

| Situation | How long the setup network stays |
| --- | --- |
| The first window of the device's life | **15 minutes** from when the network appears |
| Every later window — once a window has run out, at each boot | **5 minutes** |

Every request from your browser restarts the countdown, but no window lasts more than **60
minutes** from when the network appeared.

If the window runs out with no Wi-Fi saved, the setup network goes off the air, the log says
`setup window closed; the access point comes back at the next boot`, and the LED turns to **blue,
breathing**. To get it back, **power-cycle the board** (or press RESET): it comes back for five
minutes. Nothing is erased.

## 3. Choose your Wi-Fi

Open **Settings**. Only the **Wi-Fi** card can be saved on the setup network.

1. Press **Scan for networks**. The list is what the *device* can hear, not what your phone shows:
   - both chips have **no 5 GHz radio** — a router that publishes one name on both bands appears
     only if its 2.4 GHz side is in range;
   - **hidden networks never appear** — choose *Other / hidden network* and type the name exactly;
   - the scan takes a few seconds and moves the radio off the setup channel, so the page may drop
     for a moment. If the list does not come back, type the name instead.
2. Enter the Wi-Fi password. The device accepts 8 to 63 characters, exactly 64 hexadecimal digits,
   or an empty password for an open network (`ot_config_check_psk`).
3. Press **Save Wi-Fi and reconnect**.

The device **answers before it starts connecting**, so "Accepted — the device is trying now" means
the credentials were stored, not that your router took them. The device then tunes its single
radio to your router's channel and **the page usually stops responding** within a few seconds. That
is the normal outcome, not a failure.

A newly saved network gets **90 seconds** to hand the device an address. The moment it does, the
setup network goes off the air.

## 4. Find the device on your network

The device is now at whatever address your router gave it. There is **no mDNS name**; find it by:

- your router's list of connected clients;
- the console over USB — ESP-IDF logs the address it was given (`sta ip: …`), followed by
  `opentherm: on the owner's network`.

**Open it by its numeric address** — `http://192.168.1.42`, not a name from the router's list.
Once the device has an address on your network it refuses (`403 {"error":"forbidden"}`) every
request whose `Host` is anything other than that IP — and `192.168.4.1` while its setup network is
still up — the web page included. This is the guard against DNS rebinding: a hostile page on some
name that resolves to your device cannot drive it.

The same four hex digits appear three times, all taken from the station MAC: the setup network
`opentherm-xxxx`, the default device name `Термостат OpenTherm xxxx`, and the MQTT topic prefix
`opentherm/<12 hex digits of the MAC>`. Identity is the MAC, never the name: renaming the device
later re-keys nothing.

::: info The default name is Russian on purpose
`Термостат OpenTherm xxxx` is the owner's own Home Assistant device name, kept as the default so an
existing installation is not renamed. Change it in **Settings → Device** — it is a display name only.
:::

## 5. Finish the configuration

Open **Settings** again at the device's new address:

1. **Set a web interface password** (8 to 128 characters) in the **Device** card. It is optional,
   but without one anyone on your network can read everything, change the settings and drive the
   heating. There is no "forgot password" — read
   [Forgotten password](/guide/troubleshooting#forgotten-password) before you set one.
2. **Enter the MQTT broker** in the **MQTT broker** card: host, port (default 1883), user name and
   password.
3. Leave **Publish Home Assistant discovery** on (it is on by default).
4. Press **Save settings**. Settings take effect at once; the device does not reboot.

Then, when Home Assistant should be the controller, switch **Settings → Controller → Control mode**
to **Home Assistant** — see [Home Assistant](/guide/home-assistant). Every field is described in
[Configuration](/guide/configuration).

::: warning Changing the Wi-Fi later needs a password
Once the device is on your network, a new Wi-Fi network is accepted only from someone who presents
the web interface password. **With no password set, the Wi-Fi card is refused** (`403`) on a
connected device: the only open way to hand it a network is its own setup network, which comes up
by itself when the stored network fails (see below). Set a password if you expect to move the
device to another network.
:::

## If the Wi-Fi password was wrong

The device keeps "the credentials are stored" and "the device is on a network" as separate facts,
so a mistyped password never looks like a working connection.

- **On a device that has been on a network before**, a new pair that does not produce an address
  within 90 seconds is undone: the device puts back the last network that actually gave it an
  address (`net: the network just saved did not work; '…' put back`) and returns there by itself.
- **On a brand-new device** there is nothing to go back to. It keeps retrying, and the setup network
  stays on the air for the rest of its window, so you can correct the password from it. If the
  window has run out, power-cycle the board for a fresh five minutes.

A device whose stored network keeps failing raises its own setup network again after **30
minutes** of sustained failure, so it is never left with no network and no way in. (A router that
lets the device associate but never hands it an address is the one exception: that reopens the setup
window at the next boot instead.) Why a connection fails is classified into one of four answers —
`network-not-found`, `wrong-password`, `refused`, `no-address` — reported as `failure` in
`GET /api/status`. The web interface does not show it; read it with
`curl http://192.168.4.1/api/status` from the setup network (add `-u ":<password>"` if a web
interface password is set).

## What the LED shows during setup

| Status LED | Meaning |
| --- | --- |
| Blue, slow blink (1 s on, 1 s off) | The setup network is on the air and the device has no address yet — come and set it up |
| Blue, breathing | Joining a network; or the setup window has run out with nothing stored — power-cycle |
| Red, slow blink | A network is stored but the device has been off it for more than 10 seconds |
| Green, faint heartbeat | On the network and, if a broker is set, connected to it |

The full table, including amber (broker) and orange (failsafe), is in
[Troubleshooting](/guide/troubleshooting#the-status-led); how the LED is driven is in
[Indication](/device/indication). How the radio, the window and the rollback work inside is in
[Networking](/device/networking).
