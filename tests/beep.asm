; PC speaker test boot sector: 440 Hz, 1000 Hz and 3000 Hz square waves through
; PIT channel 2 (mode 3), about 2 s each (timed with the BIOS tick count), then
; a direct-drive section that toggles port 61h bit 1 by hand (~500 Hz), then silence.
bits 16
org 0x7C00
cli
xor ax,ax
mov ds,ax
mov ss,ax
mov sp,0x7C00
sti
mov si,tones
.tone:
lodsw
or ax,ax
jz .direct
mov bx,ax
mov al,0xB6            ; channel 2, LSB+MSB, mode 3
out 0x43,al
mov ax,bx
out 0x42,al
mov al,ah
out 0x42,al
in al,0x61
or al,3                ; gate + data
out 0x61,al
mov cx,36              ; ~2 s of ticks
call wait_ticks
in al,0x61
and al,0xFC
out 0x61,al
mov cx,9
call wait_ticks
jmp .tone
.direct:
mov di,1000            ; 1000 half periods
in al,0x61
and al,0xFC            ; gate off: the speaker follows bit 1 alone
mov bl,al
.d2:
xor bl,2
mov al,bl
out 0x61,al
mov cx,600             ; busy wait
.d3:
loop .d3
dec di
jnz .d2
in al,0x61
and al,0xFC
out 0x61,al
jmp $
wait_ticks:            ; cx = BIOS ticks to wait
mov bx,[0x46C]
.w: mov ax,[0x46C]
sub ax,bx
cmp ax,cx
jb .w
ret
tones: dw 2712, 1193, 398, 0
times 510-($-$$) db 0
dw 0xAA55
