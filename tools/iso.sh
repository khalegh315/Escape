#!/bin/bash
ROOT=$(dirname $(dirname $(readlink -f $0)))
ISO=$BUILD/cd.iso
OSTITLE="Escape v0.2"
BINNAME=kernel.bin
KERNELBIN=$BUILD/$BINNAME
TMPDIR=$ROOT/disk

mkdir -p $TMPDIR/boot/grub
mkdir $TMPDIR/apps
mkdir $TMPDIR/bin
mkdir $TMPDIR/sbin
mkdir $TMPDIR/etc

# copy / create boot-stuff
cp $ROOT/boot/* $TMPDIR/boot/grub/
echo 'default 0' > $TMPDIR/boot/grub/menu.lst;
echo 'timeout 0' >> $TMPDIR/boot/grub/menu.lst;
echo '' >> $TMPDIR/boot/grub/menu.lst;
echo "title $OSTITLE" >> $TMPDIR/boot/grub/menu.lst;
echo "kernel /boot/$BINNAME" >> $TMPDIR/boot/grub/menu.lst;
echo 'module /sbin/ata /services/ata' >> $TMPDIR/boot/grub/menu.lst;
echo 'module /sbin/fs /services/fs /drivers/cdc1 iso9660' >> $TMPDIR/boot/grub/menu.lst;

# copy kernel
cp $KERNELBIN $TMPDIR/boot

# copy service-deps, apps, services and user-apps
cp $ROOT/services/services.txt $TMPDIR/etc/services
cp $BUILD/apps/* $TMPDIR/apps
for i in $BUILD/service_*.bin ; do
	BASE=`basename $i .bin`
	cp $i $TMPDIR/sbin/`echo $BASE | sed "s/service_\(.*\)/\1/g"`
done;
for i in $BUILD/user_*.bin ; do
	BASE=`basename $i .bin`
	cp $i $TMPDIR/bin/`echo $BASE | sed "s/user_\(.*\)/\1/g"`
done;

# add some test-data
mkdir $TMPDIR/testdir
echo "Das ist ein Test-String!!" > $TMPDIR/file.txt
cp $ROOT/user/libs/shell/test.sh $TMPDIR
cp $ROOT/user/test.bmp $TMPDIR
cp $ROOT/user/bbc.bmp $TMPDIR
cp $ROOT/user/test.bmp $TMPDIR/bla.bmp
cp $TMPDIR/file.txt $TMPDIR/testdir/file.txt
dd if=/dev/zero of=$TMPDIR/zeros bs=1024 count=1024
echo -n "" > $TMPDIR/bigfile
i=0
while [ $i != 200 ]; do
	printf 'Das ist der %d Test\n' $i >> $TMPDIR/bigfile;
	i=`expr $i + 1`;
done;

# finally create image and clean up
genisoimage -U -iso-level 3 -input-charset ascii -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 \
	-boot-info-table -o $ISO $TMPDIR
rm -fR $TMPDIR/*
