bits 16
org 0x7C00
cli
xor ax,ax
mov ds,ax
mov ss,ax
mov sp,0x7C00
sti
mov si,msg
.p: lodsb
or al,al
jz .k
mov ah,0x0E
mov bx,7
int 0x10
jmp .p
.k:
xor ax,ax
int 0x16          ; wait for a key (BIOS keyboard service, IRQ1 + 8255 path)
mov ah,0x0E
mov bx,7
int 0x10          ; echo it
jmp .k
msg: db "Press keys: ",0
times 510-($-$$) db 0
dw 0xAA55
