# QEMU example

This example shows how to run ESP32 Manager in QEMU emulator with network access

1. Download the latest QEMU fork by Espressif [here](https://github.com/espressif/qemu/releases)

2. Build the project via `idf.py build` and `cd` to `build` folder

3. Merge partitions into a single file:

```
python -m esptool --chip esp32 merge_bin --fill-flash-size 4MB -o merged.bin @flash_args
```

4. Run QEMU:

```
qemu-system-xtensa -nographic -machine esp32 -drive file=merged.bin,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8888-:80
```

5. Access web-ui via [http://127.0.0.1:8888](http://127.0.0.1:8888)
