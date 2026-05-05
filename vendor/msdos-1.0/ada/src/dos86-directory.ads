-- dos86-directory.ads — Directory search and FCB matching (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   IOCHK        86DOS.asm:434-446   — validate FCB drive number
--   GETFILE      86DOS.asm:448-515   — search directory for FCB name
--   FILSRCH      86DOS.asm:484       — initial directory search
--   CONTSRCH     86DOS.asm:486-515   — continue directory search
--   GETENTRY     86DOS.asm:516-562   — fetch next matching entry
--   NEXTENTRY    86DOS.asm:563-595   — advance to next directory entry
--   NONE         86DOS.asm:596-601   — not-found path
--   MOVNAME      86DOS.asm:660-678   — copy name from FCB to NAME1
--   LODNAME      86DOS.asm:679-695   — load/pad name field
--   GETBP        86DOS.asm:696-706   — get DPB pointer for drive
--   STARTSRCH    86DOS.asm:764-765   — start directory search (fall-through)

package DOS86.Directory is

   -- IO_Chk — Validate FCB drive number and set Dos.CURDRVPT.
   --
   -- ASM: IOCHK  86DOS.asm:434-446
   --
   -- Inputs:  F.Drive — 1-based drive number (0 = default)
   -- Outputs: Sets Dos.CURDRVPT; raises Dos_Error (InvalidDrive) if bad.
   procedure IO_Chk (F : in out FCB);

   -- Get_Bp — Return DPB access for the given 1-based drive number.
   --
   -- ASM: GETBP  86DOS.asm:696-706
   --
   -- Raises Dos_Error (InvalidDrive) if drive >= Dos.NUMDRV.
   function Get_Bp (Drive : Byte) return DPB_Access;

   -- Mov_Name — Copy 11-byte name from FCB into Dos.NAME1.
   --
   -- ASM: MOVNAME  86DOS.asm:660-678
   --
   -- Transfers FCB.Name (bytes 0-10) into Dos.NAME1;
   -- also sets Dos.CURDRVPT via IO_Chk.
   procedure Mov_Name (F : in out FCB);

   -- Lod_Name — Load/pad one name field into a destination buffer.
   --
   -- ASM: LODNAME  86DOS.asm:679-695
   --
   -- Copies up to 8 bytes from Src into Dst, padding with spaces,
   -- stopping at ' ' or the end of Src.
   procedure Lod_Name
     (Src : Byte_Array;
      Dst : in out Byte_Array;
      Len : Natural);

   -- Start_Srch — Initialise directory search state and read FAT.
   --
   -- ASM: STARTSRCH  86DOS.asm:764-765
   procedure Start_Srch
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Get_File — Search directory for a file matching Dos.NAME1.
   --
   -- ASM: GETFILE  86DOS.asm:448-515
   --
   -- Searches starting at directory sector Dos.LASTENT.
   -- On success: returns True and fills D_Out with DPB, sets Dos.LASTENT.
   -- On failure: returns False (NONE path).
   function Get_File
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean;

   -- Cont_Srch — Continue directory search for the next matching entry.
   --
   -- ASM: CONTSRCH  86DOS.asm:486-515
   --
   -- Called after Get_File to find the next matching entry.
   -- Returns True if another match was found.
   function Cont_Srch
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean;

   -- Get_Entry — Get the directory entry at Dos.LASTENT.
   --
   -- ASM: GETENTRY  86DOS.asm:516-562
   --
   -- Returns True and fills Entry_Out if the entry matches Dos.NAME1
   -- (or NAME1 has wildcards).  Returns False if not found.
   function Get_Entry
     (D         : in out DPB;
      Bios      : in out Bios_Vtable'Class;
      Entry_Out :    out Small_Dir_Entry) return Boolean;

   -- Next_Entry — Advance to the next directory entry.
   --
   -- ASM: NEXTENTRY  86DOS.asm:563-595
   --
   -- Increments Dos.LASTENT; reads the next directory sector if needed.
   -- Returns False when all entries have been exhausted (NONE path).
   function Next_Entry
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean;

end DOS86.Directory;
