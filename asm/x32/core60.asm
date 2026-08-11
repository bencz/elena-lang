// ; --- Predefined References  --
define GC_ALLOC	            10002h
define VEH_HANDLER          10003h
define GC_COLLECT	    10004h
define GC_ALLOCPERM	    10005h
define PREPARE	            10006h
define THREAD_WAIT          10007h

define CORE_TOC             20001h
define SYSTEM_ENV           20002h
define CORE_GC_TABLE   	    20003h
define CORE_MATH_TABLE      20004h
define CORE_SINGLE_CONTENT  2000Bh
define VOID           	    2000Dh
define VOIDPTR              2000Eh
define CORE_THREAD_TABLE    2000Fh

define ACTION_ORDER              9
define ACTION_MASK            1E0h
define ARG_MASK               01Fh
define ARG_ACTION_MASK        1DFh

// ; --- Object header fields ---
define elSizeOffset          0004h
define elVMTOffset           0008h
define elObjectOffset        0008h

// ; --- VMT header fields ---
define elVMTSizeOffset       0004h
define elVMTFlagOffset       000Ch
define elPackageOffset       0010h

// ; --- GC TABLE OFFSETS ---
define gc_header             0000h
define gc_start              0004h
define gc_yg_start           0008h
define gc_yg_current         000Ch
define gc_yg_end             0010h
define gc_shadow             0014h
define gc_shadow_end         0018h
define gc_mg_start           001Ch
define gc_mg_current         0020h
define gc_end                0024h
define gc_mg_wbar            0028h
define gc_perm_start         002Ch
define gc_perm_end           0030h
define gc_perm_current       0034h
define gc_lock               0038h
define gc_signal             003Ch
define gc_queue_sem          0040h

define et_current            0004h
define tt_stack_frame        0008h
define tt_stack_root         0014h

define es_prev_struct        0000h
define es_catch_addr         0004h
define es_catch_level        0008h
define es_catch_frame        000Ch

// ; --- Page Size ----
define page_mask        0FFFFFFF0h
define page_ceil               17h
define page_size_order          4h
define struct_mask_inv     7FFFFFh
define struct_mask         800000h

// ; --- System Core Preloaded Routines --

structure % CORE_TOC

  dd 0         // ; reserved

end

structure % CORE_MATH_TABLE

  dd 0         // ; reserved

end

structure % CORE_SINGLE_CONTENT

  dd 0 // ; et_critical_handler    ; +x00   - pointer to ELENA critical handler
  dd 0 // ; et_current             ; +x04   - pointer to the current exception struct
  dd 0 // ; tt_stack_frame         ; +x08   - pointer to the stack frame
  dd 0 // ; reserved
  dd 0 // ; reserved
  dd 0 // ; tt_stack_root

end

structure % CORE_THREAD_TABLE

  // ; dummy for STA

end

structure %CORE_GC_TABLE

  dd 0 // ; gc_header             : +00h
  dd 0 // ; gc_start              : +04h
  dd 0 // ; gc_yg_start           : +08h
  dd 0 // ; gc_yg_current         : +0Ch
  dd 0 // ; gc_yg_end             : +10h
  dd 0 // ; gc_shadow             : +14h
  dd 0 // ; gc_shadow_end         : +18h
  dd 0 // ; gc_mg_start           : +1Ch
  dd 0 // ; gc_mg_current         : +20h
  dd 0 // ; gc_end                : +24h
  dd 0 // ; gc_mg_wbar            : +28h

  dd 0 // ; gc_perm_start         : +2Ch
  dd 0 // ; gc_perm_end           : +30h
  dd 0 // ; gc_perm_current       : +34h

  dd 0 // ; gc_lock               : +38h
  dd 0 // ; gc_signal             : +3Ch
  dd 0 // ; gc_queue_sem          : +40h

end

// ; NOTE : the table is tailed with GCMGSize,GCYGSize and MaxThread fields
structure %SYSTEM_ENV

  dd 0
  dd 0
  dd data : %CORE_GC_TABLE
  dd data : %CORE_SINGLE_CONTENT
  dd 0
  dd 0
  dd code : %VEH_HANDLER
  // ; dd GCMGSize
  // ; dd GCYGSize
  // ; dd ThreadCounter

end

structure %VOID

  dd 0
  dd 0  // ; a reference to the super class class
  dd 0
  dd 0
  dd 0

end

structure %VOIDPTR

  dd rdata : %VOID + elPackageOffset
  dd 0
  dd 0

end

// ; ==== Command Set ==

// ; redirect
inline % 03h // (ebx - object, edx - message, esi - arg0, edi - arg1)

  mov   [esp+4], esi                      // ; saving arg0
  xor   ecx, ecx
  mov   edi, [ebx - elVMTOffset]
  mov   esi, [edi - elVMTSizeOffset]

labSplit:
  test  esi, esi
  jz    short labEnd

labStart:
  shr   esi, 1
  setnc cl
  mov   eax, [edi+esi*8]
  cmp   edx, eax
  je    short labFound
  lea   eax, [edi+esi*8]
  jb    short labSplit
  lea   edi, [eax+8]
  sub   esi, ecx
  jmp   short labSplit
  nop
labFound:
  mov   eax, [edi+esi*8+4]
  mov   esi, [esp+4]
  jmp   eax

  rgw nop [eax + eax + 0]

labEnd:
  mov   esi, [esp+4]

end

// ; assign
inline %12h

  mov  eax, ebx
  mov  [ebx + edx*4], esi
  // calculate write-barrier address
  sub  eax, [data : %CORE_GC_TABLE + gc_start]
  mov  ecx, [data : %CORE_GC_TABLE + gc_header]
  shr  eax, page_size_order
  mov  byte ptr [eax + ecx], 1

end

// ; loads
inline % 14h

  mov    edx, [ebx]
  shr    edx, ACTION_ORDER
  mov    eax, mdata : %0
  mov    ecx, [eax + edx * 8]
  test   ecx, ecx
  cmovnz edx, ecx
  shl    edx, ACTION_ORDER

end

// ; dalloc
inline %16h

  lea  eax, [edx*4]
  sub  esp, eax
  mov  ecx, edx
  xor  eax, eax
  mov  edi, esp
  rep  stos

end

// ; dtrans
inline %18h

  mov  eax, esi
  mov  ecx, edx
  mov  edi, ebx
  rep  movsd
  mov  esi, eax

end

// ; xlcmp
inline % 1Ch

  push  eax
  push  edx

  mov   edi, eax
  xor   eax, eax
  sub   edi, [ebx]
  sbb   edx, [ebx+4]
  sets  ah
  or    edx, edi
  setz  al
  mov   ecx, 1
  cmp   eax, ecx

  pop   edx
  pop   eax

end

// ; bread
inline %23h

  xor  eax, eax
  mov  al, byte ptr [esi+edx]
  mov  dword ptr [ebx], eax

end

// ; fsave
inline %25h

  push edx
  fild [esp]
  fstp qword ptr [ebx]
  add  esp, 4

end

// ; wread
inline %26h

  xor  eax, eax
  mov  ax, word ptr [esi+edx*2]
  mov  dword ptr [ebx], eax

end

// ; bcopy
inline %28h

  xor  eax, eax
  mov  al, byte ptr [esi]
  mov  dword ptr [ebx], eax

end

// ; wcopy
inline %29h

  xor  eax, eax
  mov  ax, word ptr [esi]
  mov  dword ptr [ebx], eax

end

// ; xfsave
inline %30h

  fstp qword ptr [ebx]

end

// ; dfree
inline %35h

  lea  eax, [edx*4]
  add  esp, eax

end

// ; lfsave (signed 64-bit integer edx:eax -> float)
inline %37h

  push edx
  push eax
  fild qword ptr [esp]
  fstp qword ptr [ebx]
  add  esp, 8

end

// ; fiadd
inline %070h

  fld   qword ptr [ebx]
  fild  [esi]
  faddp
  fstp  qword ptr [ebx]

end

// ; fisub
inline %071h

  fld   qword ptr [ebx]
  fild  [esi]
  fsubp
  fstp  qword ptr [ebx]

end

// ; fimul
inline %072h

  fld   qword ptr [ebx]
  fild  [esi]
  fmulp
  fstp  qword ptr [ebx]

end

// ; fidiv
inline %073h

  fld   qword ptr [ebx]
  fild  [esi]
  fdivp
  fstp  qword ptr [ebx]

end

// ; fabsdp
inline %078h

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  fabs
  fstp  qword ptr [edi]    // ; store result

end

// ; fsqrtdp
inline %079h

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  fsqrt
  fstp  qword ptr [edi]    // ; store result

end

// ; fexpdp
inline %07Ah

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  xor   edx, edx

  fldl2e                  // ; ->log2(e)
  fmulp                   // ; ->log2(e)*Src

  // ; the FPU can compute the antilog only with the mantissa
  // ; the characteristic of the logarithm must thus be removed

  fld st(0)               // ; copy the logarithm
  frndint                 // ; keep only the characteristic
  fsub  st(1),st(0)       // ; keeps only the mantissa
  fxch                    // ; get the mantissa on top

  f2xm1                   // ; ->2^(mantissa)-1
  fld1
  faddp                   // ; add 1 back

  //; the number must now be readjusted for the characteristic of the logarithm

  fscale                  // ;, scale it with the characteristic

  fstsw ax                // ; retrieve exception flags from FPU
  shr   al,1              // ; test for invalid operation
  jc    short lErr        // ; clean-up and return if error

  // ; the characteristic is still on the FPU and must be removed

  fstp  st(1)             // ; get rid of the characteristic

  fstp  qword ptr [edi]    // ; store result
  mov   edx, 1
  jmp   short labEnd

lErr:
  ffree st(1)

labEnd:

end

// ; flndp
inline %07Bh

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]

  fldln2
  fxch
  fyl2x                   // ->[log2(Src)]*ln(2) = ln(Src)

  fstsw ax                // retrieve exception flags from FPU
  shr   al,1              // test for invalid operation
  jc    short lErr        // clean-up and return error

  fstp  qword ptr [edi]    // store result
  mov   edx, 1
  jmp   short labEnd

lErr:
  ffree st(0)

labEnd:

end

// ; fsindp
inline %07Ch

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  fldpi
  fadd  st(0),st(0)       // ; ->2pi
  fxch

lReduce:
  fprem                   // ; reduce the angle
  fsin
  fstsw ax                // ; retrieve exception flags from FPU
  shr   al,1              // ; test for invalid operation
  // ; jc    short lErr        // ; clean-up and return error
  sahf                    // ; transfer to the CPU flags
  jpe   short lReduce     // ; reduce angle again if necessary
  fstp  st(1)             // ; get rid of the 2pi

  fstp  qword ptr [edi]    // ; store result

end

// ; fcosdp
inline %07Dh

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  fcos
  fstp  qword ptr [edi]    // ; store result

end

// ; farctandp
inline %07Eh

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [esi]
  fld1
  fpatan                   // i.e. arctan(Src/1)
  fstp  qword ptr [edi]    // ; store result

end

// ; fpidp
inline %07Fh

  lea   edi, [ebp + __arg32_1]
  fldpi
  fstp  qword ptr [edi]    // ; store result

end

// ; xswapsi
inline %86h

  mov  eax, [esp+__arg32_1]
  mov  [esp+__arg32_1], esi
  mov  esi, eax

end

// ; xswapsi 0
inline %186h


end

// ; swapsi
inline %87h

  mov  eax, [esp+__arg32_1]
  mov  [esp+__arg32_1], ebx
  mov  ebx, eax

end

// ; xswapsi 0
inline %187h

  mov  eax, ebx
  mov  ebx, esi
  mov  esi, eax

end

// ; alloci
inline %92h

  sub  esp, __arg32_1
  xor  eax, eax
  mov  ecx, __n_1
  mov  edi, esp
  rep  stos

end

// ; alloci 0
inline %192h

end

// ; alloci 1
inline %292h

  push 0

end

// ; alloci 2
inline %392h

  push 0
  push 0

end

// ; alloci 3
inline %492h

  push 0
  push 0
  push 0

end

// ; alloci 4
inline %592h

  push 0
  push 0
  push 0
  push 0

end

// ; freei
inline %93h

  add  esp, __arg32_1

end

// ; readn
inline %95h

  mov  eax, edx
  mov  ecx, __n_1
  imul eax, ecx
  mov  edi, esi
  add  esi, eax
  mov  eax, edi
  mov  edi, ebx
  rep  movsb
  mov  esi, eax

end

// ; read 0
inline %195h

end

// ; read 1
inline %295h

  lea  eax, [esi+edx]
  mov  ecx, [eax]
  mov  byte ptr [ebx], cl

end

// ; read 2
inline %395h

  lea  eax, [esi+edx*2]
  mov  ecx, [eax]
  mov  word ptr [ebx], cx

end

// ; read 4
inline %595h

  lea  eax, [esi+edx*4]
  mov  ecx, [eax]
  mov  [ebx], ecx

end

// ; read 8
inline %795h

  lea  eax, [esi+edx*8]
  mov  ecx, [eax]
  mov  edi, [eax+4]
  mov  [ebx], ecx
  mov  [ebx+4], edi

end

// ; writen
inline %96h

  mov  eax, edx
  mov  ecx, __n_1
  imul eax, ecx
  mov  edi, esi
  add  esi, eax
  mov  eax, edi
  mov  edi, esi
  mov  esi, ebx
  rep  movsb
  mov  esi, eax

end

// ; write 0
inline %196h

end

// ; write 1
inline %296h

  lea  eax, [esi+edx]
  mov  ecx, [ebx]
  mov  byte ptr [eax], cl

end

// ; write 2
inline %396h

  lea  eax, [esi+edx*2]
  mov  ecx, [ebx]
  mov  word ptr [eax], cx

end

// ; write 4
inline %596h

  lea  eax, [esi+edx*4]
  mov  ecx, [ebx]
  mov  [eax], ecx

end

// ; write 8
inline %796h

  lea  eax, [esi+edx*8]
  mov  ecx, [ebx]
  mov  edi, [ebx+4]
  mov  [eax], ecx
  mov  [eax+4], edi

end

// ; nconf dp
inline %098h

  lea   edi, [ebp + __arg32_1]
  fld   qword ptr [ebx]
  fistp dword ptr [edi]

end

// ; ftrunc dp
inline %099h

  lea   edi, [ebp + __arg32_1]

  mov   ecx, 0
  fld   qword ptr [esi]

  push  ecx                // reserve space on stack
  fstcw word ptr [esp]     // get current control word
  mov   edx, [esp]
  or    dx,0c00h           // code it for truncating
  push  edx
  fldcw word ptr [esp]    // change rounding code of FPU to truncate

  frndint                  // truncate the number
  pop   edx                // remove modified CW from CPU stack
  fldcw word ptr [esp]     // load back the former control word
  pop   edx                // clean CPU stack

  fstsw ax                 // retrieve exception flags from FPU
  shr   al,1               // test for invalid operation
  jc    short labErr       // clean-up and return error

labSave:
  fstp  qword ptr [edi]    // store result
  jmp   short labEnd

labErr:
  ffree st(1)

labEnd:

end

// ; dcopy
inline %9Ah

  mov  ecx, __n_1
  imul ecx, edx
  mov  eax, esi
  mov  edi, ebx
  rep  movsb
  mov  esi, eax

end

// ; dcopy 1
inline %29Ah

  mov  ecx, edx
  mov  eax, esi
  mov  edi, ebx
  rep  movsb
  mov  esi, eax

end

// ; dcopy 2
inline %39Ah

  mov  ecx, edx
  shl  ecx, 1
  mov  eax, esi
  mov  edi, ebx
  rep  movsb
  mov  esi, eax

end

// ; dcopy 4
inline %59Ah

  mov  ecx, edx
  mov  eax, esi
  mov  edi, ebx
  rep  movsd
  mov  esi, eax

end

// ; frounddp
inline %09Fh

  lea   edi, [ebp + __arg32_1]

  mov   ecx, 0
  fld   qword ptr [esi]

  push  ecx                // reserve space on stack
  fstcw word ptr [esp]     // get current control word

  mov   edx, [esp]
  and   dx,0F3FFh          // code it for code it for rounding
  push  edx
  fldcw word ptr [esp]     // change rounding code of FPU to truncate

  frndint                  // round the number
  pop   edx                // remove modified CW from CPU stack
  fldcw word ptr [esp]     // load back the former control word
  pop   edx                // clean CPU stack

  fstsw ax                 // retrieve exception flags from FPU
  shr   al,1               // test for invalid operation
  jc    short labErr       // clean-up and return error

labSave:
  fstp  qword ptr [edi]    // store result
  jmp   short labEnd

labErr:
  ffree st(1)

labEnd:

end

// ; assigni
inline %0A6h

  mov  eax, ebx
  mov  [ebx + __arg32_1], esi
  // calculate write-barrier address
  sub  eax, [data : %CORE_GC_TABLE + gc_start]
  mov  ecx, [data : %CORE_GC_TABLE + gc_header]
  shr  eax, page_size_order
  mov  byte ptr [eax + ecx], 1

end

// ; lsavesi
inline %0ABh

  lea  edi, [esp + __arg32_1]
  mov [edi], eax
  mov [edi+4], edx

end

// ; lsavesi 0
inline %1ABh

  mov eax, esi
  xor edx, edx

end

// ; xfillr
inline % 0ADh
  mov  eax, __ptr32_1
  mov  edi, ebx
  mov  ecx, [esi]
  rep  stos

end

// ; xfillr 0
inline % 1ADh
  xor  eax, eax
  mov  edi, ebx
  mov  ecx, [esi]
  rep  stos

end

// ; xredirect
inline % 0B6h // (ebx - object, edx - message, esi - arg0, edi - arg1)

  mov   [esp+4], esi                      // ; saving arg0
  xor   ecx, ecx
  push  edx
  mov   edi, [ebx - elVMTOffset]
  mov   eax, __arg32_1
  and   edx, ARG_ACTION_MASK
  and   eax, ~ARG_MASK
  mov   esi, [edi - elVMTSizeOffset]
  or    edx, eax

labSplit:
  test  esi, esi
  jz    short labEnd

labStart:
  shr   esi, 1
  setnc cl
  mov   eax, [edi+esi*8]
  cmp   edx, eax
  je    short labFound
  lea   eax, [edi+esi*8]
  jb    short labSplit
  lea   edi, [eax+8]
  sub   esi, ecx
  jmp   short labSplit
labFound:
  pop   edx
  mov   eax, [edi+esi*8+4]
  mov   esi, [esp+4]
  jmp   eax

labEnd:
  pop   edx
  mov   esi, [esp+4]

end

// ; xladddpn
inline %0BDh

  lea  edi, [ebp+__arg32_1]
  mov  ecx, [edi]
  add  eax, ecx
  mov  ecx, [edi + 4]
  adc  edx, ecx

end

// ; cmpr r
inline %0C0h

  cmp  ebx, __ptr32_1

end

// ; cmpr 0
inline %1C0h

  test ebx, ebx

end

// ; cmpr -1
inline %9C0h

  cmp  ebx, __arg32_1

end

// ; fcmpn 8
inline %0C1h

  xor    eax, eax
  fld    qword ptr [ebx]
  mov    ecx, 1
  fld    qword ptr [esi]
  fcomip st, st(1)
  sete   al
  seta   ah
  fstp   st(0)
  cmp    eax, ecx

end

// ; icmpn 4
inline %0C2h

  mov  eax, [esi]
  cmp  eax, [ebx]

end

// ; icmpn 1
inline %1C2h

  mov  eax, [esi]
  cmp  al, byte ptr [ebx]

end

// ; icmpn 2
inline %2C2h

  mov  eax, [esi]
  cmp  ax, word ptr [ebx]

end

// ; icmpn 8
inline %4C2h

  xor   eax, eax
  mov   edi, [esi]
  sub   edi, [ebx]
  mov   ecx, [esi+4]
  sbb   ecx, [ebx+4]
  sets  ah
  or    ecx, edi
  setz  al
  mov   ecx, 1
  cmp   ecx, eax

end

// ; tstflg
inline %0C3h

  mov  ecx, [ebx - elVMTOffset]
  mov  eax, [ecx - elVMTFlagOffset]
  test eax, __n_1

end

// ; tstn
inline %0C4h

  test edx, __n_1

end

// ; tstm
inline % 0C5h // (ebx - object)

  mov   [esp+__n_2], esi                      // ; saving arg0
  xor   ecx, ecx
  mov   edi, [ebx - elVMTOffset]
  mov   esi, [edi - elVMTSizeOffset]

labSplit:
  test  esi, esi
  jz    short labEnd

labStart:
  shr   esi, 1
  setnc cl
  mov   eax, __arg32_1
  cmp   eax, [edi+esi*8]
  je    short labFound
  lea   eax, [edi+esi*8]
  jb    short labSplit
  lea   edi, [eax+8]
  sub   esi, ecx
  jmp   short labSplit

labFound:
  mov   esi, 1

labEnd:
  cmp   esi, 1
  mov   esi, [esp+__n_2]

end

// ; xcmpsi
inline %0C6h

  mov  eax, [esp + __arg32_1]
  cmp  edx, eax

end

// ; xcmpsi 0
inline %1C6h

  cmp  edx, esi

end

// ; cmpfi
inline %0C8h

  mov  eax, [ebp + __arg32_1]
  cmp  ebx, eax

end

// ; cmpsi
inline %0C9h

  mov  eax, [esp + __arg32_1]
  cmp  ebx, eax

end

// ; cmpsi 0
inline %1C9h

  cmp  ebx, esi

end

// ; faddndp
inline %0D0h

  lea  edi, [ebp + __arg32_1]

  fld   qword ptr [edi]
  fadd  qword ptr [esi]
  fstp  qword ptr [edi]

end

// ; fsubndp
inline %0D1h

  lea  edi, [ebp + __arg32_1]

  fld   qword ptr [edi]
  fsub  qword ptr [esi]
  fstp  qword ptr [edi]

end

// ; fmulndp
inline %0D2h

  lea  edi, [ebp + __arg32_1]

  fld   qword ptr [edi]
  fmul  qword ptr [esi]
  fstp  qword ptr [edi]

end

// ; fdivndp
inline %0D3h

  lea  edi, [ebp + __arg32_1]

  fld   qword ptr [edi]
  fdiv  qword ptr [esi]
  fstp  qword ptr [edi]

end

// ; udivndp
inline %0D4h

  xor  edx, edx
  mov  eax, [ebp+__arg32_1]
  div  dword ptr [esi]
  mov  [ebp+__arg32_1], eax

end

// ; xsavedispn
inline %0D5h

  mov  eax, __n_2
  mov  [ebx+__arg32_1], eax

end

// ; xlabeldpr
inline %0D6h

  lea  edi, [ebp + __arg32_1]
  mov  eax, __ptr32_2
  mov  [edi], eax

end

// ; selgrrr
inline %0D7h

  mov   eax, __ptr32_1
  mov   ebx, __ptr32_2
  cmovg ebx, eax

end

// ; ianddpn
inline %0D8h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  and  [edi], eax

end

// ; ianddpn
inline %1D8h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  and  byte ptr [edi], al

end

// ; ianddpn
inline %2D8h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  and  word ptr [edi], ax

end

// ; ianddpn
inline %4D8h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  and  [edi], ecx
  and  [edi+4], eax

end

// ; iordpn
inline %0D9h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  or   [edi], eax

end

// ; iordpn
inline %1D9h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  or   byte ptr [edi], al

end

// ; iordpn
inline %2D9h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  or   word ptr [edi], ax

end

// ; iordpn
inline %4D9h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  or   [edi], ecx
  or   [edi+4], eax

end

// ; ixordpn
inline %0DAh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  xor  [edi], eax

end

// ; ixordpn
inline %1DAh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  xor  byte ptr [edi], al

end

// ; ixordpn
inline %2DAh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  xor  word ptr [edi], ax

end

// ; ixordpn
inline %4DAh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  xor  [edi], ecx
  xor  [edi+4], eax

end

// ; inotdpn
inline %0DBh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  not  eax
  mov  [edi], eax

end

// ; inotdpn 1
inline %1DBh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  not  eax
  mov  byte ptr [edi], al

end

// ; inotdpn 2
inline %2DBh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  not  eax
  mov  word ptr [edi], ax

end

// ; inotdpn 8
inline %4DBh

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  not  eax
  not  ecx
  mov  [edi], ecx
  mov  [edi+4], eax

end

// ; ishldpn
inline %0DCh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  shl  eax, cl
  mov  [edi], eax

end

// ; ishldpn 1
inline %1DCh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  and  eax, 0FFh
  shl  eax, cl
  mov  byte ptr [edi], al

end

// ; ishldpn 2
inline %2DCh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  and  eax, 0FFFFh
  shl  eax, cl
  mov  word ptr [edi], ax

end

// ; ishldpn 8
inline %4DCh

  push edx
  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  mov  edx, [edi+4]

  cmp  cl, 40h
  jae  short lErr
  cmp  cl, 20h
  jae  short LL32
  shld eax, edx, cl
  shl  edx, cl
  jmp  short lEnd

LL32:
  mov  edx, eax
  xor  eax, eax
  sub  cl, 20h
  shl  eax, cl
  jmp  short lEnd

lErr:
  xor  eax, eax
  xor  edx, edx
  jmp  short lEnd2

lEnd:
  mov  [edi], eax
  mov  [edi+4], edx

lEnd2:
  pop   edx

end

// ; ishrdpn
inline %0DDh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  shr  eax, cl
  mov  [edi], eax

end

// ; ishrdpn 1
inline %1DDh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  and  eax, 0FFh
  shr  eax, cl
  mov  byte ptr [edi], al

end

// ; ishrdpn 2
inline %2DDh

  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  and  eax, 0FFFFh
  shr  eax, cl
  mov  word ptr [edi], ax

end

// ; ishrdpn 8
inline %4DDh

  push edx
  lea  edi, [ebp + __arg32_1]
  mov  ecx, [esi]
  mov  eax, [edi]
  mov  edx, [edi+4]

  cmp  cl, 64
  jae  short lErr

  cmp  cl, 32
  jae  short LR32
  shrd eax, edx, cl
  sar  edx, cl
  jmp  short lEnd

LR32:
  mov  eax, edx
  xor  edx, edx
  sub  cl, 20h
  shr  eax, cl
  jmp  short lEnd

lErr:
  xor  eax, eax
  xor  edx, edx
  jmp  short lEnd2

lEnd:
  mov  [edi], eax
  mov  [edi+4], edx

lEnd2:
  pop   edx

end

// ; selultrr
inline %0DFh

  mov   eax, [esi]
  cmp   eax, [ebx]
  mov   ecx, __ptr32_1
  mov   ebx, __ptr32_2
  cmovb ebx, ecx

end

// ; copydpn
inline %0E0h

  mov  eax, esi
  lea  edi, [ebp + __arg32_1]
  mov  ecx, __n_2
  rep  movsb
  mov  esi, eax

end

// ; copydpn dpn, 1
inline %1E0h

  mov  eax, [esi]
  mov  byte ptr [ebp + __arg32_1], al

end

// ; copydpn dpn, 2
inline %2E0h

  mov  eax, [esi]
  mov  word ptr [ebp + __arg32_1], ax

end

// ; copydpn dpn, 4
inline %3E0h

  mov  eax, [esi]
  mov  [ebp + __arg32_1], eax

end

// ; copydpn dpn, 8
inline %4E0h

  movq xmm0, qword ptr [esi]
  movq qword ptr [ebp + __arg32_1] , xmm0

end

// ; iaddndp
inline %0E1h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  add  [edi], eax

end

// ; iaddndp
inline %1E1h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  add  byte ptr [edi], al

end

// ; iaddndp
inline %2E1h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  add  word ptr [edi], ax

end

// ; iaddndp
inline %4E1h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  add  [edi], ecx
  adc  [edi+4], eax

end

// ; isubndp 4
inline %0E2h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  sub  [edi], eax

end

// ; isubndp 1
inline %1E2h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  sub  byte ptr [edi], al

end

// ; isubndp 2
inline %2E2h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi]
  sub  word ptr [edi], ax

end

// ; isubndp 8
inline %4E2h

  lea  edi, [ebp + __arg32_1]
  mov  eax, [esi + 4]
  mov  ecx, [esi]
  sub  [edi], ecx
  sbb  [edi+4], eax

end

// ; imulndp
inline %0E3h

  mov  eax, [ebp+__arg32_1]
  imul dword ptr [esi]
  mov  [ebp+__arg32_1], eax

end

// ; imulndp
inline %1E3h

  mov  ecx, [esi]
  mov  eax, [ebp+__arg32_1]
  imul cl
  mov  byte ptr [ebp+__arg32_1], al

end

// ; imulndp
inline %2E3h

  mov  ecx, [esi]
  mov  eax, [ebp+__arg32_1]
  imul cx
  mov  word ptr [ebp+__arg32_1], ax

end

// ; imulndp
inline %4E3h

  lea  edi, [ebp+__arg32_1]
  mov  edx, edi        // dest

  push ebx

  mov  ecx, [edx+4]   // DHI
  mov  eax, [esi+4]   // SHI
  or   eax, ecx
  mov  ecx, [edx]     // DLO
  jnz  short lLong
  mov  ecx, [edx]
  mov  eax, [esi]
  mul  ecx
  jmp  short lEnd

lLong:
  mov  eax, [esi+4]
  mov  edi, edx
  mul  ecx               // SHI * DLO
  mov  ebx, eax
  mov  eax, dword ptr [esi]
  mul  dword ptr [edi+4]  // SLO * DHI
  add  ebx, eax
  mov  eax, dword ptr [esi] // SLO * DLO
  mul  ecx
  add  edx, ebx

lEnd:
  mov  [edi], eax
  pop  ebx
  mov  [edi+4], edx

end

// ; idivndp
inline %0E4h

  mov  eax, [ebp+__arg32_1]
  cdq
  idiv dword ptr [esi]
  mov  [ebp+__arg32_1], eax

end

// ; idivndp
inline %1E4h

  mov  ecx, [esi]
  mov  eax, [ebp+__arg32_1]
  and  eax, 0FFh
  cdq
  idiv cl
  mov  byte ptr [ebp+__arg32_1], al

end

// ; idivndp
inline %2E4h

  mov  ecx, [esi]
  mov  eax, [ebp+__arg32_1]
  and  eax, 0FFFFh
  cdq
  idiv cx
  mov  word ptr [ebp+__arg32_1], ax

end

// ; idivndp
inline %4E4h

  push ebx
  mov  [esp+4], esi
  mov  ebx, esi
  lea  esi, [ebp+__arg32_1] // ; esi - DVND, ebx - DVSR

  push [esi+4]    // ; DVND hi dword
  push [esi]      // ; DVND lo dword
  push [ebx+4]    // ; DVSR hi dword
  push [ebx]      // ; DVSR lo dword

  xor  edi, edi

  mov  eax, [esp+0Ch]    // hi DVND
  or   eax, eax
  jge  short L1
  add  edi, 1
  mov  edx, [esp+8]      // lo DVND
  neg  eax
  neg  edx
  sbb  eax, 0
  mov  [esp+0Ch], eax    // hi DVND
  mov  [esp+8], edx      // lo DVND

L1:
  mov  eax, [esp+4]      // hi DVSR
  or   eax, eax
  jge  short L2
  add  edi, 1
  mov  edx, [esp]        // lo DVSR
  neg  eax
  neg  edx
  sbb  eax, 0
  mov  [esp+4], eax      // hi DVSR
  mov  [esp], edx        // lo DVSR

L2:
  or   eax, eax
  jnz  short L3
  mov  ecx, [esp]        // lo DVSR
  mov  eax, [esp+0Ch]    // hi DVND
  xor  edx, edx
  div  ecx
  mov  ebx, eax
  mov  eax, [esp+8]      // lo DVND
  div  ecx

  mov  esi, eax          // result
  jmp  short L4

L3:
  mov  ebx, eax
  mov  ecx, [esp]        // lo DVSR
  mov  edx, [esp+0Ch]    // hi DVND
  mov  eax, [esp+8]      // lo DVDN
L5:
  shr  ebx, 1
  rcr  ecx, 1
  shr  edx, 1
  rcr  eax, 1
  or   ebx, ebx
  jnz  short L5
  div  ecx
  mov  esi, eax          // result

  // check the result with the original
  mul  [esp+4]           // hi DVSR
  mov  ecx, eax
  mov  eax, [esp]        // lo DVSR
  mul  esi
  add  edx, ecx

  // carry means Quotient is off by 1
  jb   short L6

  cmp  edx, [esp+0Ch]    // hi DVND
  ja   short L6
  jb   short L7
  cmp  eax, [esp+8]      // lo DVND
  jbe  short L7

L6:
  sub  esi, 1

L7:
  xor  ebx, ebx

L4:
  mov  edx, ebx
  mov  eax, esi

  sub  edi, 1
  jnz  short L8
  neg  edx
  neg  eax
  sbb  edx, 0

L8:
  lea  esp, [esp+10h]
  lea  edi, [ebp+__arg32_1]

  mov  [edi], eax
  pop  ebx
  mov  [edi+4], edx
  mov  esi, [esp]

end

// ; nsavedpn
inline %0E5h

  mov  eax, __n_2
  mov  [ebp+__arg32_1], eax

end

// ; nadddpn
inline %0E8h

  mov  eax, __n_2
  add  [ebp+__arg32_1], eax

end

// ; dcopydpn
inline %0E9h

  mov  eax, esi
  lea  edi, [ebp + __arg32_1]
  mov  ecx, __n_2
  imul ecx, edx
  rep  movsb
  mov  esi, eax

end

// ; xwriteon
inline %0EAh

  mov  eax, esi

  mov  edi, esi
  mov  ecx, __n_2
  lea  esi, [ebx + __arg32_1]
  rep  movsb

  mov  esi, eax

end

// ; xwrite o,1
inline %1EAh

  mov  ecx, [ebx + __arg32_1]
  mov  byte ptr [esi], cl

end

// ; xwrite o,2
inline %2EAh

  mov  ecx, [ebx + __arg32_1]
  mov  word ptr [esi], cx

end

// ; xwrite o,4
inline %3EAh

  mov  ecx, [ebx + __arg32_1]
  mov  [esi], ecx

end

// ; xwrite o,8
inline %4EAh

  lea  eax, [ebx + __arg32_1]
  mov  ecx, [eax]
  mov  edi, [eax+4]
  mov  [esi], ecx
  mov  [esi+4], edi

end

// ; xcopyon
inline %0EBh

  mov  ecx, __n_2
  lea  edi, [ebx + __arg32_1]
  rep  movsb
  sub  esi, __n_2          // ; to set back ESI register

end

// ; xcopy 0, 1
inline %1EBh

  mov  eax, [esi]
  mov  byte ptr [ebx + __arg32_1], al

end

// ; xcopy 0, 2
inline %2EBh

  mov  eax, [esi]
  mov  word ptr [ebx + __arg32_1], ax

end

// ; xcopy 0, 4
inline %3EBh

  mov  eax, [esi]
  mov  [ebx + __arg32_1], eax

end

// ; xcopy 0, 8
inline %4EBh

  lea  eax, [ebx + __arg32_1]
  mov  ecx, [esi]
  mov  edi, [esi+4]
  mov  [eax], ecx
  mov  [eax+4], edi

end

// ; seleqrr
inline %0EEh

  mov   eax, __ptr32_1
  mov   ebx, __ptr32_2
  cmovz ebx, eax

end

// ; selltrr
inline %0EFh

  mov   eax, __ptr32_1
  mov   ebx, __ptr32_2
  cmovl ebx, eax

end

// ; xstoresir
inline %0F1h

  mov  eax, __ptr32_2
  mov  [esp+__arg32_1], eax

end

// ; xstoresir :0, ...
inline %1F1h

  mov  esi, __ptr32_2

end

// ; xstoresir :0, 0
inline %6F1h

  mov  esi, 0

end

// ; xstoresir :0, -1
inline %9F1h

  mov  esi, __ptr32_2

end

// ; movsifi
inline %0F3h

  mov  eax, [ebp+__arg32_2]
  mov  [esp+__arg32_1], eax

end

// ; movsifi sp:0, fp:i2
inline %1F3h

  mov  esi, [ebp+__arg32_2]

end

// ; xmovsisi
inline %0F6h

  mov  eax, [esp+__arg32_2]
  mov  [esp+__arg32_1], eax

end

// ; xmovsisi 0, n
inline %1F6h

  mov  esi, [esp+__arg32_2]

end

// ; xmovsisi n, 0
inline %2F6h

  mov  [esp+__arg32_1], esi

end

// ; xmovsisi 0, 1
inline %5F6h

  mov  esi, [esp+4]

end

// ; xmovsisi 1, 0
inline %6F6h

  mov  [esp+__arg32_1], esi

end

// ; xstorefir
inline %0F9h

  mov  eax, __ptr32_2
  mov  [ebp+__arg32_1], eax

end

// ; callext
inline %0FEh

  mov  [esp], esi
  call extern __ptr32_1
  mov  edx, eax

end

// ; callext
inline %1FEh

  call extern __ptr32_1
  mov  edx, eax

end

// ; callext
inline %6FEh

  mov  [esp], esi
  call extern __ptr32_1

end

// ; callext
inline %7FEh

  call extern __ptr32_1

end
