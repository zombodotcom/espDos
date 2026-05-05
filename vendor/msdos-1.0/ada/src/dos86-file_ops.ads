-- dos86-file_ops.ads — File open/close/create/delete/rename (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   OPEN / DOOPEN  86DOS.asm:707 / 711-756  — open a file FCB
--   OPENDEV        86DOS.asm:757-763         — open a device FCB
--   CLOSE          86DOS.asm:846-907         — close a file FCB
--   CREATE         86DOS.asm:973-1068        — create / truncate a file
--   DELETE         86DOS.asm:602-625         — delete a file
--   RENAME         86DOS.asm:626-659         — rename a file

package DOS86.File_Ops is

   -- Open — Open a file for I/O (FCB open, function 15).
   --
   -- ASM: OPEN / DOOPEN  86DOS.asm:707-756
   --
   -- Inputs:
   --   F    : in out FCB         — user FCB (Drive, Name must be set)
   --   Bios : in out Bios_Vtable — BIOS interface
   -- Outputs:
   --   F fields populated (Recsiz, Filsiz, Firclus, etc.).
   --   Returns 0 on success, 16#FF# on error (file not found).
   function Fn_Open
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Close — Close a file FCB (function 16).
   --
   -- ASM: CLOSE  86DOS.asm:846-907
   --
   -- Flushes dirty FAT and directory entry to disk.
   -- Returns 0 on success, 16#FF# on error.
   function Fn_Close
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Create — Create or truncate a file (function 22).
   --
   -- ASM: CREATE  86DOS.asm:973-1068
   --
   -- Creates a new directory entry; if the file already exists its
   -- cluster chain is released and the entry is reused.
   -- Returns 0 on success, 16#FF# on error (directory full).
   function Fn_Create
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Delete — Delete a file (function 19).
   --
   -- ASM: DELETE  86DOS.asm:602-625
   --
   -- Marks the directory entry deleted (DEL_MARK) and frees cluster chain.
   -- Returns 0 on success, 16#FF# if file not found.
   function Fn_Delete
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Rename — Rename a file (function 23).
   --
   -- ASM: RENAME  86DOS.asm:626-659
   --
   -- The FCB contains the old name at offset 1 and new name at offset 17.
   -- Returns 0 on success, 16#FF# if file not found.
   function Fn_Rename
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

end DOS86.File_Ops;
