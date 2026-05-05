-- dos86-io_ops.adb — Sequential and random record I/O (body).
--
-- Translated from 86DOS.asm.  See dos86-io_ops.ads for covered labels.

with Interfaces; use Interfaces;
with DOS86.Fat;
with DOS86.Disk;

package body DOS86.IO_Ops is

   -- Get_Rec — Compute 32-bit record position from FCB fields.
   --
   -- ASM: GETREC  86DOS.asm:2146-2167
   --
   -- Inputs:  DS:DX = FCB pointer
   -- Outputs: DX:AX = record number; CX = 1
   --
   -- The ASM encodes RECPOS as a 16-bit NR field shifted together with EXTENT:
   --   MOV AL,[DI+NR]   ; low byte of record within extent
   --   MOV DX,[DI+EXTENT]
   --   SHL AL           ; shift NR into position
   --   SHR DX
   --   RCR AL           ; combine EXTENT bits 0 into NR msb
   -- This is equivalent to: RECPOS = EXTENT*(65536/RECSIZ) + NR
   -- NOTE: differs from ASM — Ada arithmetic is used directly.
   function Get_Rec (F : FCB) return DWord is
      Records_Per_Extent : constant DWord :=
        DWord (65536) / DWord (F.Recsiz);
   begin
      -- ASM lines 2158-2166: compute DX:AX from EXTENT and NR fields
      return DWord (F.Extent) * Records_Per_Extent + DWord (F.Nr);
   end Get_Rec;

   -- Fn_Set_Rnd_Rec — Set random-record field from sequential NR (fn 36).
   --
   -- ASM: SETRNDREC  86DOS.asm:2545-2549
   procedure Fn_Set_Rnd_Rec (F : in out FCB) is
      R : constant DWord := Get_Rec (F);
   begin
      -- ASM line 2547: MOV [DI+RR],AL   — low byte
      F.Rr (0) := Byte (R and 16#FF#);
      -- ASM line 2548: MOV [DI+RR+1],AH — middle byte
      F.Rr (1) := Byte (Shift_Right (R, 8) and 16#FF#);
      -- ASM line 2549: MOV [DI+RR+2],DL — high byte
      F.Rr (2) := Byte (Shift_Right (R, 16) and 16#FF#);
   end Fn_Set_Rnd_Rec;

   -- Fn_Set_DMA — Set the DMA (disk transfer) address (function 26).
   --
   -- ASM: SETDMA  86DOS.asm:2444-2449
   procedure Fn_Set_DMA (Seg : Word; Off : Word) is
   begin
      -- ASM line 2447: MOV [DMASEG],AX — segment of DMA buffer
      Dos.DMASEG := Seg;
      -- ASM line 2449: MOV [DMAADD],BX — offset of DMA buffer
      Dos.DMAADD := Off;
   end Fn_Set_DMA;

   -- Fn_File_Size — Compute file size in records (function 35).
   --
   -- ASM: FILESIZE  86DOS.asm:2392-2441
   --
   -- Outputs: FCB.Rr set to ceil(Filsiz / Recsiz) as 3-byte LE integer.
   procedure Fn_File_Size
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class)
   is
      pragma Unreferenced (Bios);
      Size_Recs : DWord;
   begin
      -- ASM lines 2397-2400: divide FILSIZ by RECSIZ, round up
      if F.Recsiz = 0 then
         return;
      end if;
      -- ASM line 2401: result is in DX:AX
      Size_Recs := (F.Filsiz + DWord (F.Recsiz) - 1) / DWord (F.Recsiz);
      -- ASM lines 2402-2404: store 3-byte result into FCB.Rr
      -- ASM line 2402: MOV [DI+RR],AL
      F.Rr (0) := Byte (Size_Recs and 16#FF#);
      -- ASM line 2403: MOV [DI+RR+1],AH
      F.Rr (1) := Byte (Shift_Right (Size_Recs, 8) and 16#FF#);
      -- ASM line 2404: MOV [DI+RR+2],DL
      F.Rr (2) := Byte (Shift_Right (Size_Recs, 16) and 16#FF#);
   end Fn_File_Size;

   -- Io_Load — Core load (read) transfer loop.
   --
   -- ASM: LOAD  86DOS.asm:1707-1821
   --
   -- Inputs:
   --   DS:DI = FCB pointer; DX:AX = position in file; CX = record count
   -- Outputs:
   --   DX:AX = position of last record read; CX = bytes read
   --   LSTCLUS, CLUSPOS fields in FCB set
   --
   -- NOTE: differs from ASM — segment:offset addressing is not used;
   --       actual sector I/O (BUFRD/DREAD/NEXTSEC) is elided as placeholder;
   --       we write to Dos.BUFFER as a proxy for the DMA area.
   procedure Io_Load
     (F    : in out FCB;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
      pragma Unreferenced (Bios, D);
      Recs    : constant Natural := Natural (Dos.RECCNT);
      Recsiz  : constant Natural := Natural (F.Recsiz);
      Rec_Pos : DWord := Get_Rec (F);
   begin
      -- ASM line 1753: MOV B,[TRANS],0 — no transfer yet
      Dos.TRANS := 0;
      for R in 0 .. Recs - 1 loop
         declare
            Byte_Off : constant DWord := Rec_Pos * DWord (Recsiz);
         begin
            -- ASM lines 1725-1733: compare BYTPOS against FILSIZ; JB RDERR if past EOF
            if Byte_Off >= F.Filsiz then
               -- ASM label RDERR 86DOS.asm:1701: XOR CX,CX / JMP WRTERR → DSKERR:=1
               Dos.DSKERR := 1;
               return;
            end if;
            -- ASM line 1753: MOV B,[TRANS],1 — transfer taking place
            -- NOTE: actual sector I/O (BUFRD / DREAD loop) elided — placeholder
            Dos.TRANS := 1;
            -- ASM lines 1772-1773: INC [LASTPOS] / JP RDLP — advance record
            Rec_Pos := Rec_Pos + 1;
         end;
      end loop;
      -- ASM label SETFCB 86DOS.asm:1775: update LSTCLUS and CLUSPOS in FCB
      Dos.DSKERR := 0;
      -- ASM lines 1800-1821: update FCB EXTENT and NR from new record position
      declare
         New_Pos : constant DWord := Get_Rec (F) + DWord (Recs);
         Rpe     : constant DWord := DWord (65536) / DWord (F.Recsiz);
      begin
         -- ASM: equivalent of encoding DX:AX back into EXTENT and NR
         F.Extent := Word (New_Pos / Rpe);
         F.Nr     := Byte (New_Pos mod Rpe);
      end;
   end Io_Load;

   -- Io_Store — Core store (write) transfer loop.
   --
   -- ASM: STORE  86DOS.asm:1888-2008
   --
   -- Inputs:
   --   DS:DI = FCB; DX:AX = position in file; CX = record count
   -- Outputs:
   --   DX:AX = position of last record written; CX = records written
   --   LSTCLUS, CLUSPOS fields in FCB set
   --
   -- NOTE: differs from ASM — ALLOCATE/FNDCLUS cluster walk, BUFWRT sector
   --       writes, and WRTEOF path are elided as placeholders.
   procedure Io_Store
     (F    : in out FCB;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
      pragma Unreferenced (Bios, D);
      Recs    : constant Natural := Natural (Dos.RECCNT);
      Recsiz  : constant Natural := Natural (F.Recsiz);
      Rec_Pos : DWord := Get_Rec (F);
   begin
      -- ASM line 1900: MOV B,[DI+DIRTYFIL],1 — mark file dirty immediately
      -- (deferred to inner loop below to match Ada structure)
      Dos.TRANS := 0;
      for R in 0 .. Recs - 1 loop
         declare
            Byte_Off : constant DWord := Rec_Pos * DWord (Recsiz);
         begin
            -- ASM lines 1921-1929: compute last sector accessed vs. FILSIZ
            -- to determine how many new clusters are needed (ALLOCATE path).
            -- Here we simply extend FILSIZ if writing past end.
            if Byte_Off + DWord (Recsiz) > F.Filsiz then
               -- ASM label WRTEOF 86DOS.asm:2013: CX=0 path extends file
               F.Filsiz := Byte_Off + DWord (Recsiz);
            end if;
            -- ASM line 1900: MOV B,[DI+DIRTYFIL],1
            Dos.TRANS  := 1;
            F.Dirtyfil := 1;
            -- ASM line 1772-equivalent: advance record counter
            Rec_Pos    := Rec_Pos + 1;
         end;
      end loop;
      -- ASM: after inner loop, DSKERR = 0 (success)
      Dos.DSKERR := 0;
      -- ASM lines equivalent to SETFCB: store new EXTENT and NR back into FCB
      declare
         New_Pos : constant DWord := Get_Rec (F) + DWord (Recs);
         Rpe     : constant DWord := DWord (65536) / DWord (F.Recsiz);
      begin
         F.Extent := Word (New_Pos / Rpe);
         F.Nr     := Byte (New_Pos mod Rpe);
      end;
   end Io_Store;

   -- Fn_Seq_Rd — Sequential read one record (function 20).
   --
   -- ASM: SEQRD  86DOS.asm:1707-1821  (LOAD path, CX=1)
   -- The ASM SEQRD handler calls GETREC then falls into LOAD with CX=1.
   function Fn_Seq_Rd
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D : DPB_Access renames Dos.CURDRVPT;
   begin
      if D = null then
         return 2;
      end if;
      -- ASM: MOV CX,1 — read exactly one record
      Dos.RECCNT := 1;
      -- ASM: CALL LOAD
      Io_Load (F, D.all, Bios);
      -- ASM: result in AL from DSKERR
      return Dos.DSKERR;
   end Fn_Seq_Rd;

   -- Fn_Seq_Wrt — Sequential write one record (function 21).
   --
   -- ASM: SEQWRT  86DOS.asm:1888-2008  (STORE path, CX=1)
   -- The ASM SEQWRT handler calls GETREC then falls into STORE with CX=1.
   function Fn_Seq_Wrt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D : DPB_Access renames Dos.CURDRVPT;
   begin
      if D = null then
         return 2;
      end if;
      -- ASM: MOV CX,1 — write exactly one record
      Dos.RECCNT := 1;
      -- ASM: CALL STORE
      Io_Store (F, D.all, Bios);
      return Dos.DSKERR;
   end Fn_Seq_Wrt;

   -- Fn_Rnd_Rd — Random read one record (function 33).
   --
   -- ASM: RNDRD  86DOS.asm:1707-1821
   -- The ASM RNDRD handler decodes FCB.Rr into DX:AX (record position)
   -- then falls into the LOAD path.  Rr is a 3-byte little-endian integer.
   function Fn_Rnd_Rd
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D   : DPB_Access renames Dos.CURDRVPT;
      Rr  : DWord;
   begin
      if D = null then
         return 2;
      end if;
      -- ASM: decode 3-byte Rr field → DX:AX record number
      -- ASM: MOV AL,[DI+RR] / MOV AH,[DI+RR+1] / MOV DL,[DI+RR+2] / MOV DH,0
      Rr      := DWord (F.Rr (0)) or
                 (DWord (F.Rr (1)) * 256) or
                 (DWord (F.Rr (2)) * 65536);
      -- ASM: translate DX:AX back into EXTENT and NR for GETREC compatibility
      declare
         Rpe : constant DWord := DWord (65536) / DWord (F.Recsiz);
      begin
         -- ASM equivalent: MOV [DI+EXTENT],... / MOV [DI+NR],...
         F.Extent := Word (Rr / Rpe);
         F.Nr     := Byte (Rr mod Rpe);
      end;
      -- ASM: MOV CX,1 / CALL LOAD
      Dos.RECCNT := 1;
      Io_Load (F, D.all, Bios);
      return Dos.DSKERR;
   end Fn_Rnd_Rd;

   -- Fn_Rnd_Wrt — Random write one record (function 34).
   --
   -- ASM: RNDWRT  86DOS.asm:1888-2008
   -- Same Rr decode as RNDRD, then falls into STORE path.
   function Fn_Rnd_Wrt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D  : DPB_Access renames Dos.CURDRVPT;
      Rr : DWord;
   begin
      if D = null then
         return 2;
      end if;
      -- ASM: decode 3-byte Rr field → DX:AX record number
      Rr      := DWord (F.Rr (0)) or
                 (DWord (F.Rr (1)) * 256) or
                 (DWord (F.Rr (2)) * 65536);
      -- ASM: translate DX:AX back into EXTENT and NR
      declare
         Rpe : constant DWord := DWord (65536) / DWord (F.Recsiz);
      begin
         F.Extent := Word (Rr / Rpe);
         F.Nr     := Byte (Rr mod Rpe);
      end;
      -- ASM: MOV CX,1 / CALL STORE
      Dos.RECCNT := 1;
      Io_Store (F, D.all, Bios);
      return Dos.DSKERR;
   end Fn_Rnd_Wrt;

   -- Fn_Blk_Rd — Block read CX records (function 39).
   --
   -- ASM: BLKRD  86DOS.asm:1707-1821
   -- BLKRD stores CX (record count) in [RECCNT] then calls LOAD.
   -- On return CX is updated with the number of records actually transferred.
   function Fn_Blk_Rd
     (F    : in out FCB;
      CX   : in     Word;
      CX_Out :  out Word;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D : DPB_Access renames Dos.CURDRVPT;
   begin
      CX_Out := CX;
      if D = null then
         return 2;
      end if;
      -- ASM: MOV [RECCNT],CX — store block count before LOAD
      Dos.RECCNT := CX;
      -- ASM: CALL LOAD
      Io_Load (F, D.all, Bios);
      -- ASM: updated RECCNT holds remaining (unread) count; subtract from CX
      CX_Out := CX - Dos.RECCNT;
      return Dos.DSKERR;
   end Fn_Blk_Rd;

   -- Fn_Blk_Wrt — Block write CX records (function 40).
   --
   -- ASM: BLKWRT  86DOS.asm:1888-2008
   -- BLKWRT stores CX in [RECCNT] then calls STORE; mirrors BLKRD structure.
   function Fn_Blk_Wrt
     (F    : in out FCB;
      CX   : in     Word;
      CX_Out :  out Word;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D : DPB_Access renames Dos.CURDRVPT;
   begin
      CX_Out := CX;
      if D = null then
         return 2;
      end if;
      -- ASM: MOV [RECCNT],CX — store block count before STORE
      Dos.RECCNT := CX;
      -- ASM: CALL STORE
      Io_Store (F, D.all, Bios);
      -- ASM: updated RECCNT holds remaining (unwritten) count
      CX_Out := CX - Dos.RECCNT;
      return Dos.DSKERR;
   end Fn_Blk_Wrt;

end DOS86.IO_Ops;
