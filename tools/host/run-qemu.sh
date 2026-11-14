#!/bin/bash
# Boot the QNX 8.0 Quick Start QEMU image headless on this host (TCG, no KVM).
# usage: run-qemu.sh [gpu]   gpu = std (default) | virgl
GPU=${1:-std}
Q=$HOME/qnx-setup/qemu-root
IMG=$HOME/qnx800/images/qemu/qemu/output
RUN=$HOME/qnx-setup/run
mkdir -p "$RUN"
export LD_LIBRARY_PATH=$Q/usr/lib/x86_64-linux-gnu:$Q/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$Q/usr/lib/x86_64-linux-gnu/qemu
export __EGL_VENDOR_LIBRARY_DIRS=$Q/usr/share/glvnd/egl_vendor.d
export LIBGL_DRIVERS_PATH=$Q/usr/lib/x86_64-linux-gnu/dri
export GBM_BACKENDS_PATH=$Q/usr/lib/x86_64-linux-gnu/gbm
export EGL_PLATFORM=surfaceless
case $GPU in
  virgl) GFX=(-device virtio-vga-gl -display egl-headless,rendernode=none) ;;
  *)     GFX=(-vga std -display none) ;;
esac
rm -f "$RUN/serial.log"
exec "$Q/usr/bin/qemu-system-x86_64" \
  -L "$Q/usr/share/qemu" -L "$Q/usr/share/seabios" \
  -accel tcg,thread=multi -m 4G -smp 4 -cpu max \
  -drive file="$IMG/disk-qemu.vmdk",if=ide,id=drv0 \
  -kernel "$IMG/ifs.bin" \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,netdev=net0 \
  "${GFX[@]}" -vnc 127.0.0.1:7 \
  -chardev socket,id=s0,path="$RUN/serial.sock",server=on,wait=off,logfile="$RUN/serial.log" -serial chardev:s0 -monitor none \
  -qmp unix:"$RUN/qmp.sock",server,nowait \
  -pidfile "$RUN/qemu.pid" -daemonize
