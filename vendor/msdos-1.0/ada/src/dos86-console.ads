-- dos86-console.ads — Console and character I/O (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   BUFIN    86DOS.asm:2566-2651  — buffered line input (fn 10)
--   CRLF     86DOS.asm:2652-2656  — emit carriage-return + line-feed
--   CONOUT   86DOS.asm:2853-2855  — console output (fn 2)
--   OUT/OUTCH 86DOS.asm:2855-2966 — character output with PFLAG echo
--   CONSTAT  86DOS.asm:2968-2972  — console status (fn 11)
--   CONIN    86DOS.asm:2975-2980  — console input with echo (fn 1)
--   IN       86DOS.asm:2983-2986  — raw console input (fn 8)
--   RAWIO    86DOS.asm:2988-2999  — raw I/O (fn 6)
--   RAWINP   86DOS.asm:2988-2999  — raw input (fn 7)
--   LIST     86DOS.asm:3001-3004  — list (printer) output (fn 5)
--   PRTBUF   86DOS.asm:3006-3013  — print a $-terminated string (fn 9)
--   OUTMES   86DOS.asm:3015-3021  — print a $-terminated internal string

package DOS86.Console is

   -- Fn_Con_In — Console input with echo (function 1).
   --
   -- ASM: CONIN  86DOS.asm:2975-2980
   function Fn_Con_In (Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Con_Out — Console output (function 2).
   --
   -- ASM: CONOUT  86DOS.asm:2853-2855
   procedure Fn_Con_Out (Bios : in out Bios_Vtable'Class; Ch : Byte);

   -- Fn_Con_Stat — Console status (function 11).
   --
   -- ASM: CONSTAT  86DOS.asm:2968-2972
   -- Returns: 16#FF# if character ready, 0 if not.
   function Fn_Con_Stat (Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Raw_IO — Raw I/O (function 6): DL=16#FF# → input, else output DL.
   --
   -- ASM: RAWIO  86DOS.asm:2988-2999
   function Fn_Raw_IO
     (Bios : in out Bios_Vtable'Class;
      DL   : Byte) return Byte;

   -- Fn_Raw_Inp — Raw input, no echo (function 7).
   --
   -- ASM: RAWINP  86DOS.asm:2988-2999
   function Fn_Raw_Inp (Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_In — Console input, no echo (function 8).
   --
   -- ASM: IN  86DOS.asm:2983-2986
   function Fn_In (Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_List — Printer (list) output (function 5).
   --
   -- ASM: LIST  86DOS.asm:3001-3004
   procedure Fn_List (Bios : in out Bios_Vtable'Class; Ch : Byte);

   -- Fn_Prt_Buf — Print a '$'-terminated string (function 9).
   --
   -- ASM: PRTBUF  86DOS.asm:3006-3013
   procedure Fn_Prt_Buf (Bios : in out Bios_Vtable'Class; Msg : Byte_Array);

   -- Fn_Buf_In — Buffered line input (function 10).
   --
   -- ASM: BUFIN  86DOS.asm:2566-2651
   procedure Fn_Buf_In (Bios : in out Bios_Vtable'Class; Buf : in out Byte_Array);

   -- Con_Out — Output one character, honouring PFLAG printer echo.
   --
   -- ASM: OUTCH  86DOS.asm:2855-2966
   procedure Con_Out (Bios : in out Bios_Vtable'Class; Ch : Byte);

   -- Con_Crlf — Emit carriage-return + line-feed.
   --
   -- ASM: CRLF  86DOS.asm:2652-2656
   procedure Con_Crlf (Bios : in out Bios_Vtable'Class);

   -- Out_Mes — Print a '$'-terminated message from an internal Ada string.
   --
   -- ASM: OUTMES  86DOS.asm:3015-3021
   procedure Out_Mes (Bios : in out Bios_Vtable'Class; Msg : String);

end DOS86.Console;
