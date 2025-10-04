# PlotTool

Simple, portable, network and system plotting tool. 

Supports multiple graphs with:

- Ping / ICMP Echo
- SNMP bandwidth monitoring
- Some local stats like CPU, Mem, load avg
- Shell commands

Inspired by SunOS Performance Meter, Plan 9 stats, gping. Not as fancy as Conky.

## Supported OS

- macOS
- Linux
- *BSD
- Solaris/Illumos

## Supported Graphics/UI backends

- X11
- GTK3
- SDL2/3
- GLFW

## Elevated permissions

Unfortunately in 2025 ICMP Echo still requires elevated (root) privileges. To get ping working you will need to run it with `sudo`, `setuid`, `setcap` raw net capabilities, unprivileged ping or shell subprocess. Choose your poison:

```
sudo /path/to/plottool
```

```
sudo chmod +s /path/to/plottool
```

```
sudo setcap cap_net_raw+ep /path/to/plottool
```

```
sudo sysctl net.ipv4.ping_group_range="$(id -u) $(id -u)"
```

```
[targets]
shell=ping -i 10 1.1.1.1 | sed -l 's/^.*time=//g; s/ ms//g'
```

## Config file

If plottool.ini doesnt exist the app will create you a sample one on start.

The app looks at current working directory then various system config locations like `$XDG_CONFIG_HOME` or `~/Library/Preferences` on macOS.



## Max val autoscale

At present the "max" value and the vertical scale is computed based on runtime max value and never decreases. This is probably not the best choice, I find it work quite well in practice.

## Performance Considerations

PlotTool supports many UI/Graphics backends. 

If you care about low CPU usage, wasted cycles, power usage - prefer GFX=X11, then GTK3. Avoid SDL. Its designed for high performance games running at 60 FPS and it's pretty hard to make it yeld CPU back.

## Development libraries

### Net-SNMP 

```
apt install libsnmp-dev
```

### SDL2

```
apt install libsdl2-dev libsdl2-ttf-dev libfontconfig1-dev 
```

### SDL3

```
apt install libsdl3-dev libsdl3-ttf-dev libfontconfig1-dev
```

### GTK3

```
apt install libgtk-3-dev libfontconfig1-dev
```

### X11

```
apt install libx11-dev xfonts-base xfonts-traditional
```

restart X may be required

### Misc

```
apt install fontconfig pkg-config
```

## Illegal

It's illegal to download and use this software
