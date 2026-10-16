; mbr.asm - master boot record for the hard disk images fat12.py newhd makes.
; Standard behaviour: move out of the way to 0000:0600, load the boot sector of the ACTIVE
; partition to 0000:7C00 (CHS from the partition entry, 5 tries) and jump to it with DL =
; the boot drive and DS:SI = the partition entry. So once DOS is installed on the disk
; (SYS C:, or a DOS setup program) the image boots by itself.
; Build: nasm -f bin -o mbr.bin mbr.asm   (the bytes are pasted into fat12.py as MBR_CODE)
bits 16
org 0x600
start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    mov es, ax
    sti
    cld
    mov si, 0x7C00
    mov di, 0x0600
    mov cx, 256
    rep movsw
    jmp 0:reloc
reloc:
    mov si, 0x07BE                ; partition table
    mov cx, 4
.find:
    test byte [si], 0x80
    jnz found
    add si, 16
    loop .find
    mov si, msg_noact
    jmp fail
found:
    mov di, 5
.read:
    mov bx, 0x7C00
    mov ax, 0x0201
    mov dh, [si + 1]
    mov cx, [si + 2]
    push si
    int 0x13
    pop si
    jnc .ok
    xor ax, ax
    int 0x13
    dec di
    jnz .read
    mov si, msg_err
    jmp fail
.ok:
    cmp word [0x7DFE], 0xAA55
    je .go
    mov si, msg_nos
    jmp fail
.go:
    jmp 0:0x7C00
fail:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp fail
.halt:
    hlt
    jmp .halt
msg_noact: db "Sin particion activa", 13, 10, 0
msg_err:   db "Error leyendo el disco", 13, 10, 0
msg_nos:   db "Disco sin sistema: arranca desde un disquete e instala DOS (o SYS C:)", 13, 10, 0
