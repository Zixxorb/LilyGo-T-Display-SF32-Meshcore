# Changes this port needs in the LilyGo SiFli SDK (v2.5.1)

`../setup.sh` applies all of these. By hand:

- `drv_adc-quiet.patch` (`patch -d <sdk> -p1 < drv_adc-quiet.patch`): the ADC driver
  printed a line on every read (`ch[7]voltage=...`). Battery reads then flooded the
  console, which carries MeshCore's CLI. The two `rt_kprintf`s now need
  `DRV_ADC_DEBUG_PRINT`.
- `stubs/`: two Kconfig files LilyGo's Kconfig refers to but their repo doesn't contain.
  Copy them into the SDK at the same paths.
- `rtos/env/packages/`: missing from LilyGo's SDK download. Copy it from the upstream
  SDK (https://github.com/OpenSiFli/SiFli-SDK).
