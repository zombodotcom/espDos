-- dos86-syscall.ads — System call dispatcher (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   ENTRY / COMMAND  86DOS.asm:146-301  — INT 21h / CALL 5 entry points
--   SAVREGS          86DOS.asm:162-175  — save user registers
--   LEAVE            86DOS.asm:220-240  — restore registers and return
--   ABORT            86DOS.asm:1215-1230 — process abort (fn 0)
--   BADCALL          86DOS.asm:248-252  — unsupported function
--   DSKRESET         86DOS.asm:2482-2489 — flush all FATs (fn 13)
--   VERSION          86DOS.asm:(fn 12)  — return DOS version

package DOS86.Syscall is

   -- Dispatch — Call one DOS function by number.
   --
   -- ASM: ENTRY / COMMAND dispatch table  86DOS.asm:146-346
   --
   -- Inputs:
   --   Func  : Byte           — function number (0–41)
   --   F     : FCB_Access     — pointer to user FCB (may be null for some fns)
   --   AX,BX,CX : Word        — register parameters
   --   Bios  : Bios_Vtable    — BIOS interface
   -- Outputs:
   --   Returns AL result byte (0=ok, 16#FF#=error, etc.).
   function Dispatch
     (Func : Byte;
      F    : FCB_Access;
      AX   : Word;
      BX   : Word;
      CX   : Word;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Abort — Terminate and exit (function 0).
   --
   -- ASM: ABORT  86DOS.asm:1215-1230
   procedure Fn_Abort (Bios : in out Bios_Vtable'Class);

   -- Fn_Version — Return DOS version (function 12).
   --
   -- ASM: VERSION  86DOS.asm (fn 12)
   -- Returns: Byte representing the version (1 for 86-DOS 1.00).
   function Fn_Version return Byte;

   -- Fn_Dsk_Reset — Reset all drives (flush dirty FATs) (function 13).
   --
   -- ASM: DSKRESET  86DOS.asm:2482-2489
   procedure Fn_Dsk_Reset (Bios : in out Bios_Vtable'Class);

end DOS86.Syscall;
