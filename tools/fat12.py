# Minimal FAT12 disk-image tool for gw-dos: floppies and small (<= 16 MB) hard disk images.
#   fat12.py ls  <img>
#   fat12.py cat <img> <NAME.EXT>
#   fat12.py put <img> <NAME.EXT> <hostfile>   (rewrites an existing file in place; new data must
#                                              fit in the clusters already allocated to it)
#   fat12.py bootdisk <Disk01.img> <a.img>     copy an MS-DOS 6.x setup disk 1 and make its
#                                              AUTOEXEC.BAT stop at the A:\> prompt (keeps
#                                              NLSFUNC and KEYB, drops the setup program)
#   fat12.py add <img> <file> [file ...]       copy PC files into the image's root directory
#   fat12.py new <img> [file ...]              create an empty 1.44 MB DOS floppy (+ files)
#   fat12.py sysdisk <Disk01.img> <img> [--run "command"] [file ...]
#                                              bootable 1.44 MB floppy: minimal DOS (IO.SYS,
#                                              MSDOS.SYS, COMMAND.COM from an MS-DOS boot disk)
#                                              + files; --run writes an AUTOEXEC.BAT that starts it
#   fat12.py newhd <img> [--size MB] [file|folder ...]
#                                              hard disk image (partitioned, FAT12, up to 16 MB);
#                                              each PC folder becomes a subdirectory (e.g. C:\\LOOM)
#   fat12.py extract <img> [folder]            copy every file and folder out of the image
#                                              (default folder: the image name without .img)
#   add/ls/cat/extract also work on hard disk images; add accepts folders too
import sys, struct, os, time

def geom(img):
    bps, spc, rsv, nfat, nroot, _, _, spf = struct.unpack_from("<HBHBHHBH", img, 11)
    root = (rsv + nfat * spf) * bps
    data = root + nroot * 32
    return bps, spc, rsv, nfat, nroot, spf, root, data

def entries(img):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    for i in range(nroot):
        off = root + i * 32
        e = img[off:off + 32]
        if e[0] == 0: break
        if e[0] == 0xE5 or e[11] & 0x08: continue
        name = e[0:8].decode("cp437").rstrip() + ("." + e[8:11].decode("cp437").rstrip() if e[8:11].strip() else "")
        clus, size = struct.unpack_from("<HI", e, 26)
        yield off, name, e[11], clus, size

def fat_next(img, c):
    bps, spc, rsv, *_ = geom(img)
    fat = rsv * bps
    v = struct.unpack_from("<H", img, fat + c * 3 // 2)[0]
    return (v >> 4) if c & 1 else (v & 0xFFF)

def chain(img, c):
    out = []
    while 2 <= c < 0xFF8:
        out.append(c); c = fat_next(img, c)
    return out

def fat_set(img, c, v):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    for k in range(nfat):
        off = (rsv + k * spf) * bps + c * 3 // 2
        cur = struct.unpack_from("<H", img, off)[0]
        cur = (cur & 0x000F) | (v << 4) if c & 1 else (cur & 0xF000) | v
        struct.pack_into("<H", img, off, cur)

def total_clusters(img):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    tot = struct.unpack_from("<H", img, 19)[0] or struct.unpack_from("<I", img, 32)[0]
    return (tot * bps - data) // (bps * spc)

def free_bytes(img):
    bps, spc, *_ = geom(img)
    return sum(1 for c in range(2, total_clusters(img) + 2) if fat_next(img, c) == 0) * bps * spc

def dos83(fn):
    base, ext = os.path.splitext(os.path.basename(fn))
    ok = lambda t: "".join(ch for ch in t.upper() if ch.isalnum() or ch in "!#$%&'()-@^_`{}~")
    b, e = ok(base)[:8], ok(ext.lstrip("."))[:3]
    if not b: sys.exit("cannot make a DOS 8.3 name from " + fn)
    return b, e

def add_file(img, hostfile):
    b, e = dos83(hostfile)
    add_bytes(img, b, e, open(hostfile, "rb").read(), 0x20, os.path.getmtime(hostfile))

def alloc_clusters(img, need):
    free = []
    for c in range(2, total_clusters(img) + 2):
        if len(free) >= need: break
        if fat_next(img, c) == 0: free.append(c)
    return free

def dir_slot_offsets(img, dir_cluster):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    if dir_cluster == 0:
        return [root + i * 32 for i in range(nroot)]
    csize = bps * spc
    return [data + (c - 2) * csize + i * 32 for c in chain(img, dir_cluster) for i in range(csize // 32)]

def free_slot(img, dir_cluster, name):
    slot = None
    for off in dir_slot_offsets(img, dir_cluster):
        if img[off:off + 11] == name: sys.exit("%s already exists in the image" % name.decode().strip())
        if slot is None and img[off] in (0, 0xE5): slot = off
    if slot is None and dir_cluster:                     # grow the subdirectory by one cluster
        bps, spc, *_r = geom(img); data = geom(img)[7]; csize = bps * spc
        new = alloc_clusters(img, 1)
        if not new: sys.exit("disk full")
        last = chain(img, dir_cluster)[-1]
        fat_set(img, last, new[0]); fat_set(img, new[0], 0xFFF)
        img[data + (new[0] - 2) * csize: data + (new[0] - 1) * csize] = bytes(csize)
        slot = data + (new[0] - 2) * csize
    if slot is None: sys.exit("root directory full")
    return slot

def dos_datetime(mtime):
    t = time.localtime(mtime if mtime is not None else time.time())
    return ((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2),
            ((max(t.tm_year, 1980) - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday)

def make_dir(img, b, parent=0, mtime=None):
    bps, spc, *_r = geom(img); data = geom(img)[7]; csize = bps * spc
    name = (b.ljust(8) + "   ").encode("ascii")
    slot = free_slot(img, parent, name)
    c = alloc_clusters(img, 1)
    if not c: sys.exit("disk full")
    c = c[0]; fat_set(img, c, 0xFFF)
    dt, dd = dos_datetime(mtime)
    blk = bytearray(csize)
    blk[0:32] = b".          " + bytes([0x10]) + bytes(10) + struct.pack("<HHHI", dt, dd, c, 0)
    blk[32:64] = b"..         " + bytes([0x10]) + bytes(10) + struct.pack("<HHHI", dt, dd, parent, 0)
    img[data + (c - 2) * csize: data + (c - 1) * csize] = blk
    img[slot:slot + 32] = name + bytes([0x10]) + bytes(10) + struct.pack("<HHHI", dt, dd, c, 0)
    print("mkdir %s" % b)
    return c

def add_path(img, hostpath, parent=0, prefix=""):
    if os.path.isdir(hostpath):
        b, _e = dos83(os.path.basename(os.path.normpath(hostpath)) + ".")
        c = make_dir(img, b, parent, os.path.getmtime(hostpath))
        for f in sorted(os.listdir(hostpath)):
            add_path(img, os.path.join(hostpath, f), c, prefix + b + "\\")
    else:
        b, e = dos83(hostpath)
        add_bytes(img, b, e, open(hostpath, "rb").read(), 0x20, os.path.getmtime(hostpath), parent, prefix)

def add_bytes(img, b, e, content, attr=0x20, mtime=None, dir_cluster=0, prefix=""):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    csize = bps * spc
    hostfile = "%s.%s" % (b, e) if e else b
    name = (b.ljust(8) + e.ljust(3)).encode("ascii")
    slot = free_slot(img, dir_cluster, name)
    need = (len(content) + csize - 1) // csize
    free = alloc_clusters(img, need)
    if len(free) < need:
        sys.exit("%s does not fit: needs %d bytes, image has %d free" % (hostfile, need * csize, free_bytes(img)))
    for i, c in enumerate(free):
        fat_set(img, c, free[i + 1] if i + 1 < len(free) else 0xFFF)
        chunk = content[i * csize:(i + 1) * csize]
        img[data + (c - 2) * csize: data + (c - 1) * csize] = chunk + b"\0" * (csize - len(chunk))
    dtime, ddate = dos_datetime(mtime)
    ent = name + bytes([attr]) + bytes(10) + struct.pack("<HHHI", dtime, ddate, free[0] if free else 0, len(content))
    img[slot:slot + 32] = ent
    print("added %-20s %8d bytes" % (prefix + hostfile, len(content)))

def read_file(img, name):
    bps, spc, *_rest = geom(img)
    data = geom(img)[7]; csize = bps * spc
    for off, n, attr, clus, size in entries(img):
        if n.upper() == name.upper():
            buf = b"".join(img[data + (c - 2) * csize: data + (c - 1) * csize] for c in chain(img, clus))
            return buf[:size], attr
    sys.exit("%s not found in the source disk" % name)

def sysdisk(src, out, run, files):
    s = bytearray(open(src, "rb").read())
    std = struct.pack("<HBHBHHBHHH", 512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2)
    if bytes(s[11:11 + len(std)]) != std:
        sys.exit("the source must be a standard 1.44 MB MS-DOS boot floppy")
    img = new_floppy(label=None)
    img[0:512] = s[0:512]                  # DOS boot sector: loads IO.SYS from the first root entry
    for n in ("IO.SYS", "MSDOS.SYS", "COMMAND.COM"):
        data, attr = read_file(s, n)
        b, e = n.split(".")
        add_bytes(img, b, e, data, attr)   # IO.SYS lands at cluster 2, contiguous, as DOS requires
    if run:
        add_bytes(img, "AUTOEXEC", "BAT", ("@echo off\r\n" + run.replace(";", "\r\n") + "\r\n").encode("cp437"))
    for f in files: add_file(img, f)
    open(out, "wb").write(img)
    print("%s created (bootable), %d bytes free" % (out, free_bytes(img)))

def new_floppy(label="GWDOS"):
    img = bytearray(1474560)
    # boot sector: BPB of a standard 1.44 MB disk; the code just prints a message and waits
    msg = b"No es un disco de arranque. Arranca el DOS y usa este disco desde A:.\r\n"
    code = bytes([0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB,   # cli; ds=ss=0; sp=7C00; sti
                  0xBE, 0x5A, 0x7C,                                                   # mov si, 7C5A (message)
                  0xAC, 0x08, 0xC0, 0x74, 0x06, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5,   # print loop
                  0xF4, 0xEB, 0xFD])                                                  # hlt; jmp $
    img[0:3] = bytes([0xEB, 0x3C, 0x90])
    img[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", img, 11, 512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2, 0, 0)
    struct.pack_into("<BBBI", img, 36, 0, 0, 0x29, int(time.time()) & 0xFFFFFFFF)
    img[43:54] = (label or "NO NAME").upper().ljust(11)[:11].encode("ascii")
    img[54:62] = b"FAT12   "
    img[62:62 + len(code)] = code
    img[0x5A:0x5A + len(msg)] = msg
    img[510:512] = b"\x55\xAA"
    for k in range(2):                                  # FAT: media byte + end marker
        img[512 + k * 9 * 512: 512 + k * 9 * 512 + 3] = bytes([0xF0, 0xFF, 0xFF])
    if label:                                           # volume label entry
        root = (1 + 2 * 9) * 512
        img[root:root + 11] = label.upper().ljust(11)[:11].encode("ascii")
        img[root + 11] = 0x08
    return img

HD_HEADS, HD_SPT = 16, 63                 # gw-dos (XTulator biosdisk) hard disk geometry

def new_hd(size_mb):
    cyl_bytes = HD_HEADS * HD_SPT * 512
    cyls = max(6, min(1024, int(round(size_mb * 1024 * 1024 / cyl_bytes))))   # >= 3 MB: smaller is taken for a floppy
    total = cyls * HD_HEADS * HD_SPT
    psec = total - HD_SPT                                # partition starts at LBA 63 (cyl 0, head 1)
    spc = 8
    while True:                                          # FAT12 must stay below 4085 clusters
        spf = 1
        while True:
            clusters = (psec - 1 - 2 * spf - 32) // spc
            if (clusters + 2) * 3 // 2 <= spf * 512: break
            spf += 1
        if clusters < 4085: break
        spc *= 2
        if spc > 64: sys.exit("too big for FAT12: use --size 16 or less")
    disk = bytearray(total * 512)
    # MBR: tiny message (boot from a floppy instead) + one FAT12 partition
    msg = b"Disco duro de datos de gw-dos: arranca el DOS desde un disquete.\r\n"
    code = bytes([0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB, 0xBE, 0x1C, 0x7C,
                  0xAC, 0x08, 0xC0, 0x74, 0x06, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5, 0xF4, 0xEB, 0xFD])
    disk[0:len(code)] = code
    disk[0x1C:0x1C + len(msg)] = msg
    def chs(lba):
        c = lba // (HD_HEADS * HD_SPT); h = (lba // HD_SPT) % HD_HEADS; sct = lba % HD_SPT + 1
        return bytes([h, ((c >> 2) & 0xC0) | sct, c & 0xFF])
    pe = bytes([0x80]) + chs(HD_SPT) + bytes([0x01]) + chs(total - 1) + struct.pack("<II", HD_SPT, psec)
    disk[0x1BE:0x1CE] = pe
    disk[510:512] = b"\x55\xAA"
    # partition boot sector (BPB) + FATs + root directory
    vol = bytearray(psec * 512)
    vol[0:3] = bytes([0xEB, 0x3C, 0x90]); vol[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", vol, 11, 512, spc, 1, 2, 512, psec if psec < 65536 else 0, 0xF8, spf,
                     HD_SPT, HD_HEADS, HD_SPT, psec if psec >= 65536 else 0)
    struct.pack_into("<BBBI", vol, 36, 0x80, 0, 0x29, int(time.time()) & 0xFFFFFFFF)
    vol[43:54] = b"GWDOS-HD   "; vol[54:62] = b"FAT12   "
    vol[62:62 + len(code)] = code[:11] + bytes([0xBE, 0x5A, 0x7C]) + code[14:]
    vol[0x5A:0x5A + len(msg)] = msg
    vol[510:512] = b"\x55\xAA"
    for k in range(2):
        o = 512 + k * spf * 512
        vol[o:o + 3] = bytes([0xF8, 0xFF, 0xFF])
    return disk, vol

def split_volume(img):
    """(offset, volume) for an image: whole image for a floppy, first partition for a hard disk."""
    if struct.unpack_from("<H", img, 11)[0] == 512 and img[21] in (0xF0, 0xF8, 0xF9, 0xFD, 0xFF):
        return 0, img
    if img[510:512] != b"\x55\xAA": sys.exit("not a FAT disk image")
    lba, count = struct.unpack_from("<II", img, 0x1BE + 8)
    return lba * 512, bytearray(img[lba * 512:(lba + count) * 512])

def find(img, name):
    for off, n, attr, clus, size in entries(img):
        if n.upper() == name.upper(): return off, clus, size
    sys.exit("not found: " + name)

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__ or "see the comment at the top of fat12.py")
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "sysdisk":
        args = sys.argv[3:]
        out = args.pop(0); run = None
        if args and args[0] == "--run":
            args.pop(0); run = args.pop(0)
        if os.path.exists(out): sys.exit(out + " already exists")
        sysdisk(path, out, run, args)
        return
    if cmd == "newhd":
        args = sys.argv[3:]; size = 16
        if args and args[0] == "--size":
            args.pop(0); size = float(args.pop(0))
        if size > 16: sys.exit("maximum --size is 16 (MB)")
        if os.path.exists(path): sys.exit(path + " already exists")
        disk, vol = new_hd(size)
        for f in args: add_path(vol, f)
        off = struct.unpack_from("<I", disk, 0x1BE + 8)[0] * 512
        disk[off:off + len(vol)] = vol
        open(path, "wb").write(disk)
        print("%s created: %.1f MB hard disk, %d bytes free" % (path, len(disk) / 1048576.0, free_bytes(vol)))
        return
    if cmd == "new":
        if os.path.exists(path): sys.exit(path + " already exists")
        img = new_floppy()
        for f in sys.argv[3:]: add_file(img, f)
        open(path, "wb").write(img)
        print("%s created, %d bytes free" % (path, free_bytes(img)))
        return
    whole = bytearray(open(path, "rb").read())
    voff, img = split_volume(whole)
    bps, spc, *_ , data = geom(img)
    csize = bps * spc
    if cmd == "ls":
        def walk(dc, prefix):
            for off in dir_slot_offsets(img, dc):
                e = img[off:off + 32]
                if e[0] == 0: break
                if e[0] == 0xE5 or e[11] & 0x08 or e[0] == 0x2E: continue
                n = e[0:8].decode("cp437").rstrip() + ("." + e[8:11].decode("cp437").rstrip() if e[8:11].strip() else "")
                clus, size = struct.unpack_from("<HI", e, 26)
                if e[11] & 0x10:
                    print("%-24s   <DIR>" % (prefix + n)); walk(clus, prefix + n + "\\")
                else:
                    print("%-24s %8d  attr=%02X" % (prefix + n, size, e[11]))
        walk(0, "")
        print("%d bytes free" % free_bytes(img))
    elif cmd == "extract":
        dest = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(path))[0]
        count = [0, 0]
        def walk(dc, outdir):
            os.makedirs(outdir, exist_ok=True)
            for off in dir_slot_offsets(img, dc):
                e = img[off:off + 32]
                if e[0] == 0: break
                if e[0] == 0xE5 or e[11] & 0x08 or e[0] == 0x2E: continue
                n = e[0:8].decode("cp437").rstrip() + ("." + e[8:11].decode("cp437").rstrip() if e[8:11].strip() else "")
                clus, size = struct.unpack_from("<HI", e, 26)
                target = os.path.join(outdir, n)
                if e[11] & 0x10:
                    walk(clus, target)
                else:
                    buf = b"".join(img[data + (c - 2) * csize: data + (c - 1) * csize] for c in chain(img, clus))
                    with open(target, "wb") as fh: fh.write(buf[:size])
                    count[0] += 1; count[1] += size
        walk(0, dest)
        print("%d files (%d bytes) extracted to %s" % (count[0], count[1], dest))
    elif cmd == "cat":
        off, clus, size = find(img, sys.argv[3])
        buf = b"".join(img[data + (c - 2) * csize: data + (c - 1) * csize] for c in chain(img, clus))
        sys.stdout.buffer.write(buf[:size])
    elif cmd == "bootdisk":
        out = sys.argv[3]
        off, clus, size = find(img, "AUTOEXEC.BAT")
        buf = b"".join(img[data + (c - 2) * csize: data + (c - 1) * csize] for c in chain(img, clus))[:size]
        lines = [l for l in buf.decode("cp437").replace("\r", "").split("\n") if l.strip()]
        setup = ("instalar", "setup", "install")
        keep = [l for l in lines if l.strip().lower().split(" ")[0] not in setup]
        new = ("\r\n".join(keep) + "\r\n").encode("cp437")
        cl = chain(img, clus)
        if len(new) > len(cl) * csize: sys.exit("new AUTOEXEC.BAT does not fit")
        pad = new + b"\0" * (len(cl) * csize - len(new))
        for i, c in enumerate(cl):
            img[data + (c - 2) * csize: data + (c - 1) * csize] = pad[i * csize:(i + 1) * csize]
        struct.pack_into("<I", img, off + 28, len(new))
        open(out, "wb").write(img)
        print("%s written; AUTOEXEC.BAT now:" % out)
        print(new.decode("cp437"))
    elif cmd == "add":
        for f in sys.argv[3:]: add_path(img, f)
        whole[voff:voff + len(img)] = img
        open(path, "wb").write(whole)
        print("%s: %d bytes free" % (path, free_bytes(img)))
    elif cmd == "put":
        off, clus, size = find(img, sys.argv[3])
        new = open(sys.argv[4], "rb").read()
        cl = chain(img, clus)
        if len(new) > len(cl) * csize: sys.exit("does not fit (%d > %d)" % (len(new), len(cl) * csize))
        pad = new + b"\0" * (len(cl) * csize - len(new))
        for i, c in enumerate(cl):
            img[data + (c - 2) * csize: data + (c - 1) * csize] = pad[i * csize:(i + 1) * csize]
        struct.pack_into("<I", img, off + 28, len(new))
        whole[voff:voff + len(img)] = img
        open(path, "wb").write(whole)
        print("wrote %s (%d bytes)" % (sys.argv[3], len(new)))

if __name__ == "__main__":
    main()
