-- dos86-fcb_util.ads — FCB utility, search, and misc routines (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   SRCHFRST  86DOS.asm:2302-2377  — search first (fn 17)
--   SAVPLCE   86DOS.asm:2306-2356  — save search position
--   KILLSRCH  86DOS.asm:2351-2356  — kill search / clear search state
--   SRCHDEV   86DOS.asm:2358-2376  — search device entries
--   SRCHNXT   86DOS.asm:2378-2390  — search next (fn 18)
--   FILESIZE  86DOS.asm:2392-2441  — compute file size (fn 35)
--   SETDMA    86DOS.asm:2444-2449  — set DMA address (fn 26)
--   GETFATPT  86DOS.asm:2452-2469  — get FAT pointer (fn 27)
--   GETDSKPT  86DOS.asm:2472-2479  — get disk parameter pointer (fn 31)
--   SETRNDREC 86DOS.asm:2545-2549  — set random record from NR (fn 36)
--   SELDSK    86DOS.asm:2552-2563  — select disk (fn 14)
--   CURDRV    86DOS.asm:2518-2522  — current drive (fn 25)
--   INUSE     86DOS.asm:2525-2542  — get in-use list pointer (fn 24)
--   MAKEFCB   86DOS.asm:3024-3063  — parse filename into FCB (fn 41)
--   SETVECT   86DOS.asm:3116-3126  — set interrupt vector (fn 37)
--   NEWBASE   86DOS.asm:3129-3185  — set new base address (fn 38)

package DOS86.FCB_Util is

   -- Fn_Srch_Frst — Search first (function 17).
   --
   -- ASM: SRCHFRST  86DOS.asm:2302-2377
   --
   -- Copies matching directory entry to DMA area.
   -- Returns 0 on match, 16#FF# if not found.
   function Fn_Srch_Frst
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Srch_Nxt — Search next (function 18).
   --
   -- ASM: SRCHNXT  86DOS.asm:2378-2390
   function Fn_Srch_Nxt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Cur_Drv — Return current drive number (function 25).
   --
   -- ASM: CURDRV  86DOS.asm:2518-2522
   function Fn_Cur_Drv return Byte;

   -- Fn_Sel_Dsk — Select disk / set current drive (function 14).
   --
   -- ASM: SELDSK  86DOS.asm:2552-2563
   function Fn_Sel_Dsk (DL : Byte) return Byte;

   -- Fn_Set_DMA — Set DMA (disk transfer) address (function 26).
   --
   -- ASM: SETDMA  86DOS.asm:2444-2449
   procedure Fn_Set_DMA (Seg : Word; Off : Word);

   -- Fn_Get_Fat_Pt — Get FAT pointer for current drive (function 27).
   --
   -- ASM: GETFATPT  86DOS.asm:2452-2469
   --
   -- Returns a pointer to the DPB of the current drive.
   function Fn_Get_Fat_Pt return DPB_Access;

   -- Fn_Get_Dsk_Pt — Get disk parameter block pointer (function 31).
   --
   -- ASM: GETDSKPT  86DOS.asm:2472-2479
   function Fn_Get_Dsk_Pt (DL : Byte) return DPB_Access;

   -- Fn_Inuse — Get in-use list pointer (function 24).
   --
   -- ASM: INUSE  86DOS.asm:2525-2542
   function Fn_Inuse return Byte;

   -- Fn_Set_Rnd_Rec — Set random-record field from sequential NR (function 36).
   --
   -- ASM: SETRNDREC  86DOS.asm:2545-2549
   procedure Fn_Set_Rnd_Rec (F : in out FCB);

   -- Fn_Make_FCB — Parse a filename string into an FCB (function 41).
   --
   -- ASM: MAKEFCB  86DOS.asm:3024-3063
   --
   -- Inputs:
   --   Src : Byte_Array — source string (e.g. "A:FILENAME.EXT")
   --   Al  : Byte       — flag: 1 = parse wildcards ('?')
   -- Outputs:
   --   F populated; returns 0 on success.
   function Fn_Make_FCB
     (Src : Byte_Array;
      F   : in out FCB;
      Al  : Byte) return Byte;

   -- Fn_Set_Vect — Set interrupt vector (function 37).
   --
   -- ASM: SETVECT  86DOS.asm:3116-3126
   procedure Fn_Set_Vect (AL : Byte; Seg : Word; Off : Word);

   -- Fn_New_Base — Set new base address (function 38).
   --
   -- ASM: NEWBASE  86DOS.asm:3129-3185
   procedure Fn_New_Base (DX_Seg : Word);

end DOS86.FCB_Util;
