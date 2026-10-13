bits 16
org 0x7C00
cli
xor ax,ax
mov ds,ax
mov ss,ax
mov sp,0x7C00
sti
mov si,msg
.next:
lodsb
or al,al
jz .done
mov ah,0x0E
mov bx,0x0007
int 0x10
jmp .next
.done:
jmp $
msg: db "Hello from a boot sector on a G&W-class XT core!",13,10,"BIOS INT 10h + INT 13h boot path OK",13,10,0
times 510-($-$$) db 0
dw 0xAA55
