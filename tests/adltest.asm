; AdLib test boot sector: detection the AdLib way (timer 1 -> status C0h), then one
; sustained tone on channel 0 (about 440 Hz) for good; prints "AdLib OK" or "no AdLib".
bits 16
org 0x7C00
cli
xor ax,ax
mov ds,ax
mov ss,ax
mov sp,0x7C00
sti
mov ax,0x6004                 ; reset both timers' flags... (reg 4 = 60h)
call opl
mov ax,0x8004                 ; reset IRQ
call opl
mov dx,0x388
in al,dx
mov bl,al                     ; status 1
mov ax,0xFF02                 ; timer 1 = FFh
call opl
mov ax,0x2104                 ; start timer 1
call opl
mov cx,200
.d: in al,dx                  ; ~200 us of ISA reads
loop .d
in al,dx
mov bh,al                     ; status 2
mov ax,0x6004
call opl
mov ax,0x8004
call opl
and bl,0xE0
jnz fail
and bh,0xE0
cmp bh,0xC0
jne fail
mov si,ok
call puts
; one voice: modulator/carrier on channel 0
; channel 0: mult 1, TL, AR/DR, SL/RR, F-number 241h block 4 (~440 Hz), key on
mov ax,0x0120
call opl
mov ax,0x0123
call opl
mov ax,0x1040
call opl
mov ax,0x0043
call opl
mov ax,0xF060
call opl
mov ax,0xF063
call opl
mov ax,0x7780
call opl
mov ax,0x7783
call opl
mov ax,0x41A0
call opl
mov ax,0x32B0
call opl
jmp $
fail: mov si,no
call puts
jmp $
opl: push dx                  ; ah = value, al = register
push cx
mov dx,0x388
out dx,al
mov cx,6
.a: in al,dx
loop .a
inc dx
mov al,ah
out dx,al
dec dx
mov cx,35
.b: in al,dx
loop .b
pop cx
pop dx
ret
puts: lodsb
or al,al
jz .r
mov ah,0x0E
mov bx,7
int 0x10
jmp puts
.r: ret
ok: db "AdLib OK",13,10,0
no: db "no AdLib",13,10,0
times 510-($-$$) db 0
dw 0xAA55
