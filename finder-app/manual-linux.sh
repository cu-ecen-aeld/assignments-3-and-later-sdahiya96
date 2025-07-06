#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-


SYSROOT_PATH=$(${CROSS_COMPILE}gcc --print-sysroot)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}
sudo chown "$(id -u):$(id -g)" $OUTDIR

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    
    # clean old build artifacts and remove any existing configurations in the .config file 
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper

    #setup default config
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig

    #build linux vm target
    make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all

    #build modules and device tree
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs 
fi

echo "Adding the Image in outdir"
cp $OUTDIR/linux-stable/arch/arm64/boot/Image $OUTDIR/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories

cd "${OUTDIR}"
mkdir rootfs && cd rootfs
mkdir -p bin dev etc home lib lib64 proc sys sbin temp usr var
mkdir -p usr/bin usr/sbin usr/lib var/log

echo "Installing Busybox"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone https://git.busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig 
else
    cd busybox
fi

# TODO: Make and install busybox
make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE

make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE CONFIG_PREFIX=${OUTDIR}/rootfs install
sudo chown root:root $OUTDIR/rootfs/bin/busybox
sudo chmod 4755 $OUTDIR/rootfs/bin/busybox

cd $OUTDIR/rootfs/bin
for app in $(find $OUTDIR/rootfs/usr/bin -type f); do
	ln -s $app $(basename $app)
done

cd $OUTDIR/rootfs

echo "Library dependencies"

cd "${OUTDIR}/rootfs" || { echo "Failed to cd into rootfs"; exit 1; }

${CROSS_COMPILE}readelf -a $OUTDIR/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a $OUTDIR/rootfs/bin/busybox | grep "Shared library"

echo "SYSROOT_PATH: ${SYSROOT_PATH}"
# TODO: Add library dependencies to rootfs
cp $SYSROOT_PATH/lib/ld-linux-aarch64.so.1 $OUTDIR/rootfs/lib
cp $SYSROOT_PATH/lib64/libm.so.6 $OUTDIR/rootfs/lib64
cp $SYSROOT_PATH/lib64/libresolv.so.2 $OUTDIR/rootfs/lib64
cp $SYSROOT_PATH/lib64/libc.so.6 $OUTDIR/rootfs/lib64

# TODO: Make device nodes
sudo mknod -m 666 $OUTDIR/rootfs/dev/null c 1 3 
sudo mknod -m 666 $OUTDIR/rootfs/dev/console c 5 1
sudo mknod -m 666 $OUTDIR/rootfs/dev/tty1 c 4 1

# TODO: Clean and build the writer utility
cd $FINDER_APP_DIR
echo "Rebuilding Writer Utility"
make clean
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copy artifacts to home directory"

mkdir -p $OUTDIR/rootfs/home/conf 
cp conf/username.txt conf/assignment.txt $OUTDIR/rootfs/home/conf
cp -r finder.sh finder-test.sh writer writer.c $OUTDIR/rootfs/home
cp autorun-qemu.sh $OUTDIR/rootfs/home
chmod +x $OUTDIR/rootfs/home/finder.sh
chmod +x $OUTDIR/rootfs/home/finder-test.sh
chmod +x $OUTDIR/rootfs/home/autorun-qemu.sh
chmod +x $OUTDIR/rootfs/home/writer

# TODO: Chown the root directory
cd $OUTDIR/rootfs
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
cd $OUTDIR/rootfs
sudo find . | cpio -H newc -ov --owner root:root > $OUTDIR/initramfs.cpio
cd $OUTDIR
gzip -f initramfs.cpio
