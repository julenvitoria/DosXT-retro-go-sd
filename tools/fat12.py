# Minimal FAT disk-image tool for gw-dos: floppies (FAT12) and hard disk images (FAT12 up to
# 16 MB, FAT16 above, up to 500 MB).
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
#                                              hard disk image (partitioned; FAT12 up to 16 MB,
#                                              FAT16 up to 500 MB; its MBR boots the active
#                                              partition, so DOS can be installed on it);
#                                              each PC folder becomes a subdirectory (e.g. C:\\LOOM)
#   fat12.py todsk <game.img|folder> <GAME.dsk> [--run "CMD;CMD"]
#                                              a .dsk for the TinyEMU core: copies the image (or
#                                              builds one from a folder: floppy if it fits, else a
#                                              hard disk) and writes the AUTOEXEC.BAT that core
#                                              launches; without --run it guesses the program
#   fat12.py extract <img> [folder]            copy every file and folder out of the image
#                                              (default folder: the image name without .img)
#   add/ls/cat/extract also work on hard disk images; add accepts folders too
#   fat12.py help [orden]                      pantalla de ayuda (también -h / --help)
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

def is_fat16(img):
    # the FAT type is decided by the cluster count alone (Microsoft's rule)
    return total_clusters(img) >= 4085

def eoc(img):
    """end-of-chain marker for this volume"""
    return 0xFFFF if is_fat16(img) else 0xFFF

def fat_next(img, c):
    bps, spc, rsv, *_ = geom(img)
    fat = rsv * bps
    if is_fat16(img):
        return struct.unpack_from("<H", img, fat + c * 2)[0]
    v = struct.unpack_from("<H", img, fat + c * 3 // 2)[0]
    return (v >> 4) if c & 1 else (v & 0xFFF)

def chain(img, c):
    out = []
    end = 0xFFF8 if is_fat16(img) else 0xFF8
    while 2 <= c < end:
        out.append(c); c = fat_next(img, c)
    return out

def fat_set(img, c, v):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    f16 = is_fat16(img)
    for k in range(nfat):
        if f16:
            struct.pack_into("<H", img, (rsv + k * spf) * bps + c * 2, v)
            continue
        off = (rsv + k * spf) * bps + c * 3 // 2
        cur = struct.unpack_from("<H", img, off)[0]
        cur = (cur & 0x000F) | (v << 4) if c & 1 else (cur & 0xF000) | v
        struct.pack_into("<H", img, off, cur)

def total_clusters(img):
    bps, spc, rsv, nfat, nroot, spf, root, data = geom(img)
    tot = struct.unpack_from("<H", img, 19)[0] or struct.unpack_from("<I", img, 32)[0]
    return (tot * bps - data) // (bps * spc)

def free_bytes(img):
    bps, spc, rsv, *_ = geom(img)
    n = total_clusters(img)
    if is_fat16(img):
        fat = struct.unpack_from("<%dH" % (n + 2), img, rsv * bps)
        return sum(1 for v in fat[2:] if v == 0) * bps * spc
    return sum(1 for c in range(2, n + 2) if fat_next(img, c) == 0) * bps * spc

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
        fat_set(img, last, new[0]); fat_set(img, new[0], eoc(img))
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
    c = c[0]; fat_set(img, c, eoc(img))
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
        fat_set(img, c, free[i + 1] if i + 1 < len(free) else eoc(img))
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
HD_MAX_MB = 500                           # 1024 cylinders of 16 x 63 sectors: the INT 13h limit

# MBR boot code (tools/src/mbr.asm): loads the active partition's boot sector, like MS-DOS's own
MBR_CODE = bytes.fromhex(
    "fa31c08ed0bc007c8ed88ec0fbfcbe007cbf0006b90001f3a5ea1e060000bebe07b90400f60480750a83c610e2f6"
    "be7706eb33bf0500bb007cb801028a74018b4c0256cd135e730c31c0cd134f75e7be8e06eb12813efe7d55aa7405"
    "bea706eb05ea007c0000ac08c07409b40ebb0700cd10ebf2f4ebfd53696e20706172746963696f6e206163746976"
    "610d0a004572726f72206c6579656e646f20656c20646973636f0d0a00446973636f2073696e2073697374656d61"
    "3a20617272616e636120646573646520756e206469737175657465206520696e7374616c6120444f5320286f2053"
    "595320433a290d0a00")

def new_hd(size_mb):
    cyl_bytes = HD_HEADS * HD_SPT * 512
    cyls = max(6, min(1024, int(round(size_mb * 1024 * 1024 / cyl_bytes))))   # >= 3 MB: smaller is taken for a floppy
    total = cyls * HD_HEADS * HD_SPT
    psec = total - HD_SPT                                # partition starts at LBA 63 (cyl 0, head 1)
    def layout(spc, f16):
        spf = 1
        while True:
            clusters = (psec - 1 - 2 * spf - 32) // spc
            need = (clusters + 2) * 2 if f16 else (clusters + 2) * 3 // 2
            if need <= spf * 512: return spf, clusters
            spf += 1
    spc = 8; f16 = False
    while True:                                          # FAT12 while it stays below 4085 clusters
        spf, clusters = layout(spc, False)
        if clusters < 4085: break
        spc *= 2
        if spc > 16: f16 = True; break
    if f16:                                              # FAT16: MS-DOS FORMAT's cluster sizes
        spc = 4
        while True:
            spf, clusters = layout(spc, True)
            if 4085 <= clusters < 65525: break
            spc *= 2
            if spc > 64: sys.exit("too big for FAT16")
    disk = bytearray(total * 512)
    disk[0:len(MBR_CODE)] = MBR_CODE
    def chs(lba):
        c = lba // (HD_HEADS * HD_SPT); h = (lba // HD_SPT) % HD_HEADS; sct = lba % HD_SPT + 1
        return bytes([h, ((c >> 2) & 0xC0) | sct, c & 0xFF])
    ptype = 0x01 if not f16 else (0x04 if psec < 65536 else 0x06)
    pe = bytes([0x80]) + chs(HD_SPT) + bytes([ptype]) + chs(total - 1) + struct.pack("<II", HD_SPT, psec)
    disk[0x1BE:0x1CE] = pe
    disk[510:512] = b"\x55\xAA"
    # partition boot sector (BPB) + FATs + root directory; until DOS is installed (SYS C:) its
    # code just says so
    msg = b"Disco sin sistema: arranca desde un disquete e instala DOS (o SYS C:)\r\n"
    code = bytes([0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0xFB, 0xBE, 0x5A, 0x7C,
                  0xAC, 0x08, 0xC0, 0x74, 0x06, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5, 0xF4, 0xEB, 0xFD])
    vol = bytearray(psec * 512)
    vol[0:3] = bytes([0xEB, 0x3C, 0x90]); vol[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", vol, 11, 512, spc, 1, 2, 512, psec if psec < 65536 else 0, 0xF8, spf,
                     HD_SPT, HD_HEADS, HD_SPT, psec if psec >= 65536 else 0)
    struct.pack_into("<BBBI", vol, 36, 0x80, 0, 0x29, int(time.time()) & 0xFFFFFFFF)
    vol[43:54] = b"GWDOS-HD   "; vol[54:62] = b"FAT16   " if f16 else b"FAT12   "
    vol[62:62 + len(code)] = code
    vol[0x5A:0x5A + len(msg)] = msg
    vol[510:512] = b"\x55\xAA"
    for k in range(2):
        o = 512 + k * spf * 512
        vol[o:o + 4 if f16 else o + 3] = bytes([0xF8, 0xFF, 0xFF, 0xFF]) if f16 else bytes([0xF8, 0xFF, 0xFF])
    return disk, vol

def split_volume(img):
    """(offset, volume) for an image: whole image for a floppy, first partition for a hard disk."""
    if struct.unpack_from("<H", img, 11)[0] == 512 and img[21] in (0xF0, 0xF8, 0xF9, 0xFD, 0xFF) and img[0] in (0xEB, 0xE9):
        return 0, img
    if img[510:512] != b"\x55\xAA": sys.exit("not a FAT disk image")
    lba, count = struct.unpack_from("<II", img, 0x1BE + 8)
    return lba * 512, bytearray(img[lba * 512:(lba + count) * 512])

def find(img, name):
    for off, n, attr, clus, size in entries(img):
        if n.upper() == name.upper(): return off, clus, size
    sys.exit("not found: " + name)


# ---- todsk: a .dsk for the TinyEMU core (its built-in DOS runs what AUTOEXEC.BAT names) ----
NOT_THE_GAME = ("INSTALL", "SETUP", "CONFIG", "LOADFIX", "SOUND", "READKEY", "SIMCGA", "CGA", "EGA",
                "VGA", "TANDY", "MOUSE", "KEYB", "AUTOEXEC", "UNINSTAL", "CHOICE")

def root_names(img, dc=0):
    out = []
    for off in dir_slot_offsets(img, dc):
        e = img[off:off + 32]
        if e[0] == 0: break
        if e[0] == 0xE5 or e[11] & 0x08 or e[0] == 0x2E: continue
        b = e[0:8].decode("cp437").rstrip(); x = e[8:11].decode("cp437").rstrip()
        out.append((b, x, bool(e[11] & 0x10), struct.unpack_from("<H", e, 26)[0], off))
    return out

def delete_entry(img, name83):
    for b, x, isdir, clus, off in root_names(img):
        if (b + ("." + x if x else "")).upper() == name83.upper() and not isdir:
            for c in chain(img, clus): fat_set(img, c, 0)
            img[off] = 0xE5
            return True
    return False

def guess_run(img, hint):
    """Pick the program to launch: the only .EXE/.COM that is not an installer or a driver, preferring
    one whose name looks like the disk/folder name; else the same inside the only subdirectory."""
    def candidates(dc):
        return [(b, x) for b, x, isdir, c, o in root_names(img, dc)
                if not isdir and x in ("EXE", "COM") and not any(b.startswith(n) for n in NOT_THE_GAME)]
    def pick(cands):
        if not cands: return None
        h = "".join(ch for ch in hint.upper() if ch.isalnum())
        for b, x in cands:
            if b and (b in h or h.startswith(b) or b.startswith(h[:max(3, len(b) - 1)])): return b
        return sorted(cands)[0][0]
    c = candidates(0)
    if c:
        return [pick(c)], [b + "." + x for b, x in c]
    dirs = [(b, clus) for b, x, isdir, clus, o in root_names(img) if isdir]
    if len(dirs) == 1:
        c = candidates(dirs[0][1])
        if c:
            return ["CD " + dirs[0][0], pick(c)], [dirs[0][0] + "\\" + b + "." + x for b, x in c]
    return None, []

def folder_bytes(path):
    t = 0
    for root, _d, files in os.walk(path):
        for f in files: t += os.path.getsize(os.path.join(root, f))
    return t

# ---- ayuda (fat12.py, fat12.py -h, fat12.py help <orden>) ----
HELP_INTRO = """fat12.py - herramienta para imágenes de disco DOS (FAT12/FAT16) de gw-dos y TinyEMU
Disquetes y discos duros (hasta 500 MB). Solo necesita Python 3.

Uso:  python3 fat12.py <orden> <argumentos...>
      python3 fat12.py help <orden>      ayuda detallada de una orden

Órdenes:"""

HELP = [
    ("ls", "ls <img>",
     "Lista los archivos y carpetas de la imagen (raíz y subcarpetas).",
     ["python3 fat12.py ls juego.img"]),
    ("extract", "extract <img> [carpeta]",
     "Copia todos los archivos y carpetas de la imagen a una carpeta del PC.\n"
     "Si no indicas carpeta, usa el nombre de la imagen sin .img.",
     ["python3 fat12.py extract loom.img", "python3 fat12.py extract loom.img loom_archivos"]),
    ("cat", "cat <img> <NOMBRE.EXT>",
     "Muestra un archivo de la imagen por pantalla (útil para AUTOEXEC.BAT).",
     ["python3 fat12.py cat juego.img AUTOEXEC.BAT"]),
    ("add", "add <img> <archivo|carpeta> [...]",
     "Copia archivos o carpetas del PC dentro de la imagen (en la raíz;\n"
     "cada carpeta se convierte en un subdirectorio).",
     ["python3 fat12.py add juego.img GAME.EXE DATOS.DAT"]),
    ("put", "put <img> <NOMBRE.EXT> <archivo>",
     "Reescribe un archivo que ya existe en la imagen. Los datos nuevos deben\n"
     "caber en el espacio que ya ocupaba.",
     ["python3 fat12.py put juego.img AUTOEXEC.BAT autoexec.txt"]),
    ("new", "new <img> [archivo ...]",
     "Crea un disquete DOS vacío de 1,44 MB (opcionalmente con archivos).",
     ["python3 fat12.py new vacio.img", "python3 fat12.py new juego.img GAME.EXE"]),
    ("newhd", "newhd <img> [--size MB] [archivo|carpeta ...]",
     "Crea una imagen de disco duro particionada y formateada (por defecto\n"
     "16 MB, máximo 500; FAT12 hasta 16 MB, FAT16 por encima). Cada carpeta\n"
     "del PC pasa a ser un subdirectorio (C:\\LOOM). El disco arranca solo en\n"
     "cuanto tenga DOS: instálalo arrancando desde un disquete (o usa SYS C:).",
     ["python3 fat12.py newhd c.img --size 8 loom", "python3 fat12.py newhd c.img loom monkey",
      "python3 fat12.py newhd dos5.img --size 32"]),
    ("bootdisk", "bootdisk <Disk01.img> <a.img>",
     "A partir del disco 1 de instalación de MS-DOS 6.x crea un disco de\n"
     "arranque que se queda en A:\\> (sin el programa de instalación).",
     ["python3 fat12.py bootdisk Disk01.img !BOOT.img"]),
    ("sysdisk", "sysdisk <Disk01.img> <img> [--run \"orden\"] [archivo ...]",
     "Disquete de 1,44 MB arrancable con un DOS mínimo (IO.SYS, MSDOS.SYS,\n"
     "COMMAND.COM sacados de un disco de MS-DOS) más tus archivos. Con --run\n"
     "escribe un AUTOEXEC.BAT que lanza esa orden.",
     ["python3 fat12.py sysdisk Disk01.img alley.img --run \"ALLEYCAT\" ALLEYCAT.EXE"]),
    ("todsk", "todsk <juego.img|carpeta> <JUEGO.dsk> [--run \"ORDEN;ORDEN\"]",
     "Crea un .dsk para el core TinyEMU (dos386.bin). Copia la imagen, o la\n"
     "construye desde una carpeta (disquete si cabe, si no disco duro), y\n"
     "escribe el AUTOEXEC.BAT con el programa que el core arranca. Sin --run\n"
     "intenta adivinar el programa y te dice cuál ha elegido; varias órdenes\n"
     "se separan con ';'. Copia el .dsk (y su .cfg) a /roms/dos/.",
     ["python3 fat12.py todsk alleycat.img ALLEYCAT.dsk",
      "python3 fat12.py todsk loom LOOM.dsk",
      "python3 fat12.py todsk rick2.img RICK2.dsk --run \"RICK2\"",
      "python3 fat12.py todsk monkey MONKEY.dsk --run \"CD MONKEY;MONKEY\""]),
]

def print_help(cmd=None):
    if cmd:
        for name, syn, desc, ex in HELP:
            if name == cmd:
                print("Uso:  python3 fat12.py " + syn + "\n")
                print(desc + "\n")
                print("Ejemplos:")
                for e in ex: print("  " + e)
                return
        print("orden desconocida: %s\n" % cmd)
    print(HELP_INTRO)
    short = {
        "ls": "lista el contenido de una imagen",
        "extract": "saca todos los archivos de una imagen a una carpeta",
        "cat": "muestra un archivo de la imagen",
        "add": "mete archivos o carpetas del PC en una imagen",
        "put": "reescribe un archivo que ya está en la imagen",
        "new": "crea un disquete vacío de 1,44 MB",
        "newhd": "crea un disco duro (hasta 500 MB) vacío o desde carpetas",
        "bootdisk": "disco de arranque desde el disco 1 de MS-DOS 6.x",
        "sysdisk": "disquete arrancable con DOS mínimo + tu juego",
        "todsk": "crea un .dsk para TinyEMU (dos386.bin)",
    }
    for name, syn, desc, ex in HELP:
        print("  %-10s %s" % (name, short[name]))
    print("\nls, cat, add y extract también funcionan con imágenes de disco duro.")
    print("Ejemplo:  python3 fat12.py todsk alleycat.img ALLEYCAT.dsk")

def need_args(cmd, n):
    if len(sys.argv) < n:
        print_help(cmd)
        sys.exit(1)

def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help", "help", "ayuda", "-?", "/?"):
        print_help(a[1] if len(a) > 1 else None)
        return
    if len(a) > 1 and a[1] in ("-h", "--help"):
        print_help(a[0])
        return
    cmd = a[0]
    if cmd not in [h[0] for h in HELP]:
        print_help(cmd)
        sys.exit(1)
    need_args(cmd, {"ls": 3, "extract": 3, "cat": 4, "add": 4, "put": 5, "new": 3,
                    "newhd": 3, "bootdisk": 4, "sysdisk": 4, "todsk": 4}[cmd])
    try:
        run_cmd()
    except FileNotFoundError as e:
        sys.exit("no encuentro el archivo: %s" % e.filename)

def run_cmd():
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
        if size > HD_MAX_MB: sys.exit("maximum --size is %d (MB)" % HD_MAX_MB)
        if os.path.exists(path): sys.exit(path + " already exists")
        disk, vol = new_hd(size)
        for f in args: add_path(vol, f)
        off = struct.unpack_from("<I", disk, 0x1BE + 8)[0] * 512
        disk[off:off + len(vol)] = vol
        open(path, "wb").write(disk)
        print("%s created: %.1f MB hard disk, %d bytes free" % (path, len(disk) / 1048576.0, free_bytes(vol)))
        return
    if cmd == "todsk":
        args = sys.argv[3:]
        out = args.pop(0); run = None
        if args and args[0] == "--run":
            args.pop(0); run = [c.strip() for c in args.pop(0).split(";") if c.strip()]
        if os.path.exists(out): sys.exit(out + " already exists")
        hint = os.path.splitext(os.path.basename(os.path.normpath(path)))[0]
        if os.path.isdir(path):
            entries_ = sorted(os.listdir(path))
            total = folder_bytes(path)
            if total + 64 * 1024 <= 1400 * 1024:
                disk = new_floppy(); vol = disk; voff = 0
            else:
                mb = min(16, max(3, int(total * 1.25 / 1048576) + 2))
                disk, vol = new_hd(mb); voff = struct.unpack_from("<I", disk, 0x1BE + 8)[0] * 512
            for f in entries_:
                if f.upper() == "AUTOEXEC.BAT" and run is None: pass
                if f.upper() == "AUTOEXEC.BAT": continue
                add_path(vol, os.path.join(path, f))
        else:
            disk = bytearray(open(path, "rb").read())
            voff, vol = split_volume(disk)
            if run is None:
                for b, x, isdir, c, o in root_names(vol):
                    if (b, x) == ("AUTOEXEC", "BAT"):
                        print("the image already has an AUTOEXEC.BAT: kept (use --run to replace it)")
                        run = False
        if run is None:
            run, cands = guess_run(vol, hint)
            if not run: sys.exit("no program found to launch: say which one with --run \"NAME\"")
            print("program to launch: %s   (candidates: %s)" % (" ; ".join(run), ", ".join(cands)))
            if len(cands) > 1: print("  if it is the wrong one, run again with --run \"NAME\"")
        if run:
            delete_entry(vol, "AUTOEXEC.BAT")
            add_bytes(vol, "AUTOEXEC", "BAT", ("\r\n".join(run) + "\r\n").encode("ascii"))
        disk[voff:voff + len(vol)] = vol
        open(out, "wb").write(disk)
        print("%s created, %d bytes free" % (out, free_bytes(vol)))
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
