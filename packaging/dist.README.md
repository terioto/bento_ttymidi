# bento_ttymidi distribution artifacts

Built by `setup/pack_bento_ttymidi.sh` for integration into a custom OS or image build.

## Install into rootfs

| File in `dist/` | Target on image |
|-----------------|-----------------|
| `bento_ttymidi` | `/usr/local/bin/bento_ttymidi` (mode 755) |
| `bento_ttymidi.service` | `/etc/systemd/system/bento_ttymidi.service` (mode 644) |

Enable at image build time:

```bash
ln -sf /etc/systemd/system/bento_ttymidi.service \
  $ROOTFS/etc/systemd/system/multi-user.target.wants/bento_ttymidi.service
```

## Not included in `dist/` (configure in your image build)

- `/boot/firmware/config.txt`: `enable_uart=1`, `dtoverlay=disable-bt`, `dtoverlay=midi-uart0-pi5`
- Disable `serial-getty@ttyAMA0.service`
- Service user in groups `dialout` and `audio`

See the main [README.md](../README.md#uart-boot-configuration) in the bento_ttymidi repository.

## Staging

```bash
cd bento_ttymidi
./setup/pack_bento_ttymidi.sh
cp -a dist/* /path/to/your-build/files/bento_ttymidi/
```
