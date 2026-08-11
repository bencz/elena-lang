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
define CORE_SINGLE_CONTENT  2000Bh
define VOID           	    2000Dh
define VOIDPTR              2000Eh
define CORE_THREAD_TABLE    2000Fh

// ; --- sysenv offsets ---
define env_tls_size         0004h

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

// ; THREAD CONTENT
define et_current            0004h
define tt_stack_frame        0008h
define tt_sync_event         000Ch
define tt_flags              0010h
define tt_stack_root         0014h

define tt_size               0018h

define es_prev_struct        0000h
define es_catch_addr         0004h
define es_catch_level        0008h
define es_catch_frame        000Ch

// ; THREAD TABLE
define tt_slots             00004h

// ; --- Object header fields ---
define elSyncOffset          0001h
define elSizeOffset          0004h
define elVMTOffset           0008h
define elObjectOffset        0008h

// ; NOTE : the table is tailed with GCMGSize,GCYGSize and MaxThread fields
structure %SYSTEM_ENV

  dd 0
  dd 0
  dd data : %CORE_GC_TABLE
  dd 0
  dd data : %CORE_THREAD_TABLE
  dd 0
  dd code : %VEH_HANDLER
  // ; dd GCMGSize
  // ; dd GCYGSize
  // ; dd ThreadCounter
  // ; dd TLSSize

end

structure %CORE_THREAD_TABLE

  dd 0 // ; tt_length              : +00h

  dd 0 // ; tt_slots               : +04h
  dd 0

end
