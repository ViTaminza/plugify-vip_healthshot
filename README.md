# vip_healthshot (Plugify)

VIP healthshots: each round (honouring round_min from vip_healthshot.ini) every VIP with the 'healthshot' feature gets weapon_healthshot items up to the feature value, never below the amount they already carry. Pure s2sdk: the map gate, the client guards, the schema-offset cache and the KV1 config reader live in the plugin itself, no pisexcompat. A port of the Metamod:Source module 'VIP_Healthshot' of the Pisex cs2-vip plugin.

Plugify port (fork) of **VIP_Healthshot** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.1.0
- Plugify dependencies: s2sdk (>=2.18.2), vip (>=1.0.0)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Installation
Download `vip_healthshot-1.1.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_healthshot`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

