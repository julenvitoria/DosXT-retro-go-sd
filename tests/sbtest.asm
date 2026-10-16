; Sound Blaster test boot sector: resets the DSP (expects AAh), asks the version, then
; plays a 441 Hz tone (8-bit, 11025 Hz) by auto-init DMA on channel 1 in 2048-byte
; blocks, counting IRQ 7s; prints "SB vX.YY" and a dot per IRQ.
bits 16
org 0x7C00
cli
xor ax,ax
mov ds,ax
mov ss,ax
mov sp,0x7C00
mov word [0x0F*4],irq7        ; IRQ 7 -> INT 0Fh
mov word [0x0F*4+2],0
sti
; build one second of a 441 Hz square-ish wave at 1000:0000 (25 samples a period)
mov ax,0x1000
mov es,ax
xor di,di
mov cx,11025
xor bl,bl
.gen: mov al,0x40
cmp bl,12
jb .lo
mov al,0xC0
.lo: stosb
inc bl
cmp bl,25
jb .nx
xor bl,bl
.nx: loop .gen
; DSP reset
mov dx,0x226
mov al,1
out dx,al
mov cx,100
.w1: loop .w1
xor al,al
out dx,al
mov dx,0x22E
.w2: in al,dx
test al,0x80
jz .w2
mov dx,0x22A
in al,dx
cmp al,0xAA
jne fail
mov al,0xE1
call dspw
call dspr
mov ah,al
call dspr
push ax
mov si,msg
call puts
pop ax
push ax
mov al,ah
add al,'0'
call putc
mov al,'.'
call putc
pop ax
call hex2
mov al,13
call putc
mov al,10
call putc
; unmask IRQ 7 on the PIC
in al,0x21
and al,0x7F
out 0x21,al
; DMA channel 1: page 1, addr 0, count 11024, auto-init read
mov al,5
out 0x0A,al                   ; mask ch1
out 0x0C,al                   ; clear flip-flop
mov al,0x59                   ; single, auto-init, read (mem->dev), ch1
out 0x0B,al
xor al,al
out 0x02,al
out 0x02,al
mov al,0xF0                   ; count 11024 = 2B10h
out 0x03,al
mov al,0x2A
out 0x03,al
mov al,1
out 0x83,al                   ; page register ch1
mov al,1
out 0x0A,al                   ; unmask ch1
mov al,0xD1
call dspw                     ; speaker on
mov al,0x40
call dspw
mov al,165                    ; 256 - 1000000/11025
call dspw
mov al,0x48
call dspw
mov al,0xFF                   ; block 2048
call dspw
mov al,0x07
call dspw
mov al,0x1C
call dspw                     ; auto-init 8-bit DMA
jmp $
fail: mov si,msgf
call puts
jmp $
irq7: push ax
push dx
mov dx,0x22E
in al,dx
mov al,'*'
call putc
mov al,0x20
out 0x20,al
pop dx
pop ax
iret
dspw: push dx
push ax
mov dx,0x22C
.a: in al,dx
test al,0x80
jnz .a
pop ax
out dx,al
pop dx
ret
dspr: push dx
mov dx,0x22E
.b: in al,dx
test al,0x80
jz .b
mov dx,0x22A
in al,dx
pop dx
ret
hex2: push ax
shr al,4
call hex1
pop ax
hex1: and al,15
add al,'0'
cmp al,'9'
jbe putc
add al,7
putc: push bx
mov ah,0x0E
mov bx,7
int 0x10
pop bx
ret
puts: lodsb
or al,al
jz .r
call putc
jmp puts
.r: ret
msg: db "SB v",0
msgf: db "no SB",0
times 510-($-$$) db 0
dw 0xAA55
