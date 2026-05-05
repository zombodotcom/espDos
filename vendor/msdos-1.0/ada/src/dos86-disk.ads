-- dos86-disk.ads — Low-level disk buffer and sector I/O (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   HARDREAD     86DOS.asm:1123-1127  — BIOS absolute read
--   HARDWRITE    86DOS.asm:1180-1184  — BIOS absolute write
--   HARDERR      86DOS.asm:1186-1214  — hard-error retry loop
--   DREAD        86DOS.asm:1095-1122  — multi-sector read via buffer
--   DWRITE       86DOS.asm:1150-1179  — multi-sector write via buffer
--   DIRREAD      86DOS.asm:1071-1094  — read directory sector into DIRBUF
--   DIRWRITE     86DOS.asm:1133-1149  — write directory sector from DIRBUF
--   CHKDIRWRITE  86DOS.asm:1129-1132  — conditional DIRWRITE
--   BUFSEC       86DOS.asm:1508-1565  — ensure correct sector in buffer
--   BUFRD        86DOS.asm:1567-1579  — read from buffer into DMA
--   BUFWRT       86DOS.asm:1581-1606  — write from DMA into buffer
--   NEXTSEC      86DOS.asm:1608-1630  — advance to next sector in cluster
--   BREAKDOWN    86DOS.asm:1433-1463  — decompose record position
--   OPTIMIZE     86DOS.asm:2055-2123  — compute contiguous-sector count
--   DIRCOMP      86DOS.asm:963-972    — compare directory entry name

package DOS86.Disk is

   -- Hard_Read — Read sectors directly via BIOS (no buffering).
   --
   -- ASM: HARDREAD  86DOS.asm:1123-1127
   procedure Hard_Read
     (Buf    : in out Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : DPB;
      Bios   : in out Bios_Vtable'Class);

   -- Hard_Write — Write sectors directly via BIOS (no buffering).
   --
   -- ASM: HARDWRITE  86DOS.asm:1180-1184
   procedure Hard_Write
     (Buf    : Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : DPB;
      Bios   : in out Bios_Vtable'Class);

   -- D_Read — Multi-sector read, routing single-sector reads through buffer.
   --
   -- ASM: DREAD  86DOS.asm:1095-1122
   procedure D_Read
     (Buf    : in out Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class);

   -- D_Write — Multi-sector write, routing single-sector writes through buffer.
   --
   -- ASM: DWRITE  86DOS.asm:1150-1179
   procedure D_Write
     (Buf    : Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class);

   -- Dir_Read — Read one directory sector into the directory buffer.
   --
   -- ASM: DIRREAD  86DOS.asm:1071-1094
   procedure Dir_Read
     (AL   : Byte;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Dir_Write — Write the directory buffer back to disk.
   --
   -- ASM: DIRWRITE  86DOS.asm:1133-1149
   procedure Dir_Write
     (AL   : Byte;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Chk_Dir_Write — Conditionally write directory buffer.
   --
   -- ASM: CHKDIRWRITE  86DOS.asm:1129-1132
   procedure Chk_Dir_Write
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Dir_Comp — Compare two 11-byte name fields.
   --
   -- ASM: DIRCOMP  86DOS.asm:963-972
   --
   -- Returns True if the 11 bytes at A and B are identical.
   function Dir_Comp (A : Byte_Array; B : Byte_Array) return Boolean;

   -- Buf_Sec — Ensure the requested sector is in the sector buffer.
   --
   -- ASM: BUFSEC  86DOS.asm:1508-1565
   procedure Buf_Sec
     (Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class);

   -- Buf_Rd — Copy data from sector buffer to DMA area.
   --
   -- ASM: BUFRD  86DOS.asm:1567-1579
   procedure Buf_Rd
     (Offset : Word;
      Count  : Word;
      Dst    : in out Byte_Array);

   -- Buf_Wrt — Copy data from DMA area into sector buffer (marks dirty).
   --
   -- ASM: BUFWRT  86DOS.asm:1581-1606
   procedure Buf_Wrt
     (Offset : Word;
      Count  : Word;
      Src    : Byte_Array);

   -- Next_Sec — Advance to the next sector within a cluster chain.
   --
   -- ASM: NEXTSEC  86DOS.asm:1608-1630
   procedure Next_Sec
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Breakdown — Decompose a record position into sector/byte offsets.
   --
   -- ASM: BREAKDOWN  86DOS.asm:1433-1463
   procedure Breakdown (F : in out FCB; D : in DPB);

   -- Optimize — Compute count of contiguous sectors for the transfer.
   --
   -- ASM: OPTIMIZE  86DOS.asm:2055-2123
   procedure Optimize (F : in out FCB; D : in out DPB);

end DOS86.Disk;
