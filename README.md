# vip_healthshot (Plugify)

VIP healthshots: each round (honouring round_min from vip_healthshot.ini) every VIP with the 'healthshot' feature gets weapon_healthshot items up to the feature value, never below the amount they already carry. A port of the Metamod:Source module 'VIP_Healthshot' of the Pisex cs2-vip plugin.

Plugify port (fork) of **VIP_Healthshot** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, vip, hudmenu (optional)

## Installation
Download `vip_healthshot-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_healthshot`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

