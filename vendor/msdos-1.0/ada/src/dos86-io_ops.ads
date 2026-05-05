-- dos86-io_ops.ads — Sequential and random record I/O (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   LOAD        86DOS.asm:1707-1821  — set up transfer parameters (read)
--   STORE       86DOS.asm:1888-2008  — set up transfer parameters (write)
--   WRTEOF      86DOS.asm:2013-2053  — write EOF cluster marker
--   GETREC      86DOS.asm:2146-2167  — compute record position from FCB
--   FIGREC      86DOS.asm:2126-2145  — cluster+sector → physical sector
--   SEQRD       86DOS.asm:(LOAD path) — sequential read (fn 20)
--   SEQWRT      86DOS.asm:(STORE path)— sequential write (fn 21)
--   RNDRD       86DOS.asm:(LOAD path) — random read (fn 33)
--   RNDWRT      86DOS.asm:(STORE path)— random write (fn 34)
--   BLKRD       86DOS.asm:1707-1821  — block read (fn 39)
--   BLKWRT      86DOS.asm:1888-2008  — block write (fn 40)
--   SETDMA      86DOS.asm:2444-2449  — set DMA address (fn 26)
--   SETRNDREC   86DOS.asm:2545-2549  — set random record from NR (fn 36)
--   FILESIZE    86DOS.asm:2392-2441  — compute file size in records (fn 35)

package DOS86.IO_Ops is

   -- Fn_Seq_Rd — Sequential read one record (function 20).
   --
   -- ASM: SEQRD  86DOS.asm:1707-1821  (LOAD path)
   --
   -- Reads one logical record at FCB.Nr into Dos.DMAADD.
   -- Returns: 0=ok, 1=EOF, 2=no data, 3=partial EOF.
   function Fn_Seq_Rd
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Seq_Wrt — Sequential write one record (function 21).
   --
   -- ASM: SEQWRT  86DOS.asm:1888-2008  (STORE path)
   --
   -- Returns: 0=ok, 1=disk full, 2=FCB not open.
   function Fn_Seq_Wrt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Rnd_Rd — Random read one record (function 33).
   --
   -- ASM: RNDRD  86DOS.asm:1707-1821
   function Fn_Rnd_Rd
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Rnd_Wrt — Random write one record (function 34).
   --
   -- ASM: RNDWRT  86DOS.asm:1888-2008
   function Fn_Rnd_Wrt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Blk_Rd — Block read CX records (function 39).
   --
   -- ASM: BLKRD  86DOS.asm:1707-1821
   --
   -- Returns: 0=ok, 1=EOF before all records, 3=partial on last.
   function Fn_Blk_Rd
     (F    : in out FCB;
      CX   : in     Word;
      CX_Out :  out Word;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Blk_Wrt — Block write CX records (function 40).
   --
   -- ASM: BLKWRT  86DOS.asm:1888-2008
   function Fn_Blk_Wrt
     (F    : in out FCB;
      CX   : in     Word;
      CX_Out :  out Word;
      Bios : in out Bios_Vtable'Class) return Byte;

   -- Fn_Set_DMA — Set the DMA (disk transfer) address (function 26).
   --
   -- ASM: SETDMA  86DOS.asm:2444-2449
   procedure Fn_Set_DMA (Seg : Word; Off : Word);

   -- Fn_File_Size — Compute file size in records (function 35).
   --
   -- ASM: FILESIZE  86DOS.asm:2392-2441
   procedure Fn_File_Size
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class);

   -- Fn_Set_Rnd_Rec — Set random-record field from sequential NR (function 36).
   --
   -- ASM: SETRNDREC  86DOS.asm:2545-2549
   procedure Fn_Set_Rnd_Rec (F : in out FCB);

   -- Get_Rec — Compute 32-bit record position from FCB fields.
   --
   -- ASM: GETREC  86DOS.asm:2146-2167
   --
   -- Returns the record number corresponding to FCB.Extent and FCB.Nr.
   function Get_Rec (F : FCB) return DWord;

   -- Io_Load — Core load (read) transfer loop.
   --
   -- ASM: LOAD  86DOS.asm:1707-1821
   procedure Io_Load
     (F    : in out FCB;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

   -- Io_Store — Core store (write) transfer loop.
   --
   -- ASM: STORE  86DOS.asm:1888-2008
   procedure Io_Store
     (F    : in out FCB;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class);

end DOS86.IO_Ops;
