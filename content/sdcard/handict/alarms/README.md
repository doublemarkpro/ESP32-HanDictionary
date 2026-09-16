# Alarm ringtone pack

These four ringtones are stored on the microSD card and are not linked into the firmware image.
The firmware reads the selected `.ogg` file only when the user explicitly previews it or when the
alarm fires.

## Source and license

The source recordings come from KDE's `plasma-mobile-sounds` repository, commit
`14f054eb830db357f8d360017044856f6f123092`:

| XiaoZhi file | Upstream file | Creator | License |
| --- | --- | --- | --- |
| `morning-glow.ogg` | `source/ringtones/Glazed.wav` | Ivan Kiselyov | CC0 1.0 |
| `light-steps.ogg` | `source/ringtones/Lightly.wav` | Ivan Kiselyov | CC0 1.0 |
| `tiny-bells.ogg` | `source/ringtones/Miniature.wav` | Ivan Kiselyov | CC0 1.0 |
| `ready-go.ogg` | `source/ringtones/On The Way.wav` | Ivan Kiselyov | CC0 1.0 |

Upstream project: <https://github.com/KDE/plasma-mobile-sounds>

License text: <https://creativecommons.org/publicdomain/zero/1.0/legalcode>

## Conversion

Each source was limited to 15 seconds and converted to mono 24 kHz Ogg/Opus at 32 kbit/s:

```text
ffmpeg -i INPUT.wav -t 15 -ac 1 -ar 24000 -c:a libopus -b:a 32k -vbr on \
  -application audio OUTPUT.ogg
```

The conversion is required because the device audio service decodes Opus packets carried in an Ogg
container; KDE's distributed `.oga` files use Vorbis.
