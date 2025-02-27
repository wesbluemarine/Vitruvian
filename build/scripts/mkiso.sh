#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)
normal=$(tput sgr0)

# Check if any local deb file exist
count=`ls -1 $basedir/*.deb 2>/dev/null | wc -l`
if [ $count != 0 ]; then
sudo mount -o bind $basedir/ $basedir/LIVE_BOOT/chroot/tmp/
else
echo ${bold}No deb file generated! Please run cpack inside /$basedir/
echo ${normal}
exit
fi

cleanup() {
  echo "Unmounting $basedir/LIVE_BOOT/chroot/tmp/"
  sudo umount $basedir/LIVE_BOOT/chroot/tmp/
}
trap cleanup EXIT

sudo chroot $basedir/LIVE_BOOT/chroot /bin/bash -c "echo "vitruvian-live" > /etc/hostname &\
apt install -y -f --reinstall /tmp/*.deb && depmod --basedir=./ 6.1.0-29-amd64 && exit"

echo ${bold}Create Directories for Live Environment Files...
echo ${normal}

mkdir -p $basedir/LIVE_BOOT/scratch
mkdir -p $basedir/LIVE_BOOT/image/live

echo ${bold}Chroot Environment Compression...
echo ${normal}

sudo mksquashfs \
    $basedir/LIVE_BOOT/chroot \
    $basedir/LIVE_BOOT/image/live/filesystem.squashfs \
    -b 1048576 -comp xz -Xdict-size 100% -e boot

echo ${bold}Copy Kernel and Initramfs from Chroot to Live Directory...
echo ${normal}

cp $basedir/LIVE_BOOT/chroot/boot/vmlinuz-* $basedir/LIVE_BOOT/image/vmlinuz
cp $basedir/LIVE_BOOT/chroot/boot/initrd.img-* $basedir/LIVE_BOOT/image/initrd

echo ${bold}Create Grub Menu...
echo ${normal}

cat <<'EOF' >$basedir/LIVE_BOOT/scratch/grub.cfg
insmod all_video
search --set=root --file /VITRUVIAN_CUSTOM
set default="0"
set timeout=0
set hidden_timeout=0
menuentry "Vitruvian Live" {
    linux /vmlinuz boot=live quiet splash
    initrd /initrd
}
EOF

touch $basedir/LIVE_BOOT/image/VITRUVIAN_CUSTOM

echo ${bold}GRUB cfg...
echo ${normal}

grub-mkstandalone \
    --format=x86_64-efi \
    --output=$basedir/LIVE_BOOT/scratch/bootx64.efi \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$basedir/LIVE_BOOT/scratch/grub.cfg"

echo ${bold}FAT16 Efiboot...
echo ${normal}

cd $basedir/LIVE_BOOT/scratch
dd if=/dev/zero of=efiboot.img bs=1M count=10
sudo mkfs.vfat efiboot.img
mmd -i efiboot.img efi efi/boot
mcopy -i efiboot.img ./bootx64.efi ::efi/boot/

echo ${bold}Grub cfg Modules...
echo ${normal}

grub-mkstandalone \
    --format=i386-pc \
    --output=$basedir/LIVE_BOOT/scratch/core.img \
    --install-modules="linux normal iso9660 biosdisk memdisk search tar ls" \
    --modules="linux normal iso9660 biosdisk search" \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$basedir/LIVE_BOOT/scratch/grub.cfg"

cat \
    /usr/lib/grub/i386-pc/cdboot.img \
    $basedir/LIVE_BOOT/scratch/core.img \
> $basedir/LIVE_BOOT/scratch/bios.img

echo ${bold}Generate ISO File...
echo ${normal}

xorriso \
    -as mkisofs \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "VITRUVIAN_CUSTOM" \
    -eltorito-boot \
        boot/grub/bios.img \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        --eltorito-catalog boot/grub/boot.cat \
    --grub2-boot-info \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
    -eltorito-alt-boot \
        -e EFI/efiboot.img \
        -no-emul-boot \
    -append_partition 2 0xef $basedir/LIVE_BOOT/scratch/efiboot.img \
    -output "$basedir/LIVE_BOOT/vitruvian-custom.iso" \
    -graft-points \
        "$basedir/LIVE_BOOT/image" \
        /boot/grub/bios.img=$basedir/LIVE_BOOT/scratch/bios.img \
        /EFI/efiboot.img=$basedir/LIVE_BOOT/scratch/efiboot.img

echo ${bold}Finished!
