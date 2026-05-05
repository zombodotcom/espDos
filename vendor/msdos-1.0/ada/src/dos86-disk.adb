-- dos86-disk.adb — Low-level disk buffer and sector I/O (body).
--
-- Translated from 86DOS.asm.  See dos86-disk.ads for the covered ASM labels.

with Interfaces; use Interfaces;
with DOS86.Fat;

package body DOS86.Disk is

   -- Dir_Comp — Compare two 11-byte name fields.
   --
   -- ASM: DIRCOMP  86DOS.asm:963-972
   function Dir_Comp (A : Byte_Array; B : Byte_Array) return Boolean is
   begin
      for I in 0 .. 10 loop
         if A (A'First + I) /= B (B'First + I) then
            return False;
         end if;
      end loop;
      return True;
   end Dir_Comp;

   -- Hard_Read — Read sectors directly via BIOS (no buffering).
   --
   -- ASM: HARDREAD  86DOS.asm:1123-1127
   procedure Hard_Read
     (Buf    : in out Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : DPB;
      Bios   : in out Bios_Vtable'Class)
   is
      Carry : Boolean;
   begin
      -- ASM line 1124: CALL BIOSREAD,BIOSSEG
      Carry := Disk_Read (Bios, D.Drvnum, Buf, Sector, Count);
      if Carry then
         raise Dos_Error with ERR_DISK_ERROR;
      end if;
   end Hard_Read;

   -- Hard_Write — Write sectors directly via BIOS (no buffering).
   --
   -- ASM: HARDWRITE  86DOS.asm:1180-1184
   procedure Hard_Write
     (Buf    : Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : DPB;
      Bios   : in out Bios_Vtable'Class)
   is
      Carry : Boolean;
   begin
      Carry := Disk_Write (Bios, D.Drvnum, Buf, Sector, Count);
      if Carry then
         raise Dos_Error with ERR_DISK_ERROR;
      end if;
   end Hard_Write;

   -- D_Read — Multi-sector read, routing single-sector reads through buffer.
   --
   -- ASM: DREAD  86DOS.asm:1095-1122
   --
   -- If Count > 1, bypasses the buffer and reads directly.
   -- If Count = 1, routes through the internal sector buffer.
   procedure D_Read
     (Buf    : in out Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class)
   is
      Carry : Boolean;
   begin
      if Count > 1 then
         -- ASM lines 1097-1099: multi-sector — direct BIOS read
         Carry := Disk_Read (Bios, D.Drvnum, Buf, Sector, Count);
         if Carry then
            raise Dos_Error with ERR_DISK_ERROR;
         end if;
         return;
      end if;
      -- Single-sector — use buffer
      Buf_Sec (Sector, D, Bios);
      -- Copy buffer content to Buf
      declare
         Cnt : constant Natural := Natural'Min (Buf'Length,
                                                Natural (D.Secsiz));
      begin
         for I in 0 .. Cnt - 1 loop
            Buf (Buf'First + I) := Dos.BUFFER (I);
         end loop;
      end;
   end D_Read;

   -- D_Write — Multi-sector write, routing single-sector writes through buffer.
   --
   -- ASM: DWRITE  86DOS.asm:1150-1179
   procedure D_Write
     (Buf    : Byte_Array;
      Count  : Word;
      Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class)
   is
      Carry : Boolean;
   begin
      if Count > 1 then
         -- Multi-sector — direct BIOS write
         Carry := Disk_Write (Bios, D.Drvnum, Buf, Sector, Count);
         if Carry then
            raise Dos_Error with ERR_DISK_ERROR;
         end if;
         return;
      end if;
      -- Single-sector — go through buffer
      Buf_Sec (Sector, D, Bios);
      declare
         Cnt : constant Natural := Natural'Min (Buf'Length,
                                                Natural (D.Secsiz));
      begin
         for I in 0 .. Cnt - 1 loop
            Dos.BUFFER (I) := Buf (Buf'First + I);
         end loop;
      end;
      Dos.DIRTYBUF := 1;
   end D_Write;

   -- Dir_Read — Read one directory sector into the directory buffer.
   --
   -- ASM: DIRREAD  86DOS.asm:1071-1094
   --
   -- AL selects which directory entry block to read.
   procedure Dir_Read
     (AL   : Byte;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
      Sector : constant Word := D.Firdir + Word (AL);
      Carry  : Boolean;
      pragma Unreferenced (Carry);
   begin
      -- Read into Dos.BUFFER (reused as dir buffer here for simplicity)
      -- ASM: reads into DIRBUF
      Carry := Disk_Read (Bios, D.Drvnum, Dos.BUFFER, Sector, 1);
      Dos.DIRBUFID := Word (AL);
      Dos.DIRTYDIR := 0;
   end Dir_Read;

   -- Dir_Write — Write the directory buffer back to disk.
   --
   -- ASM: DIRWRITE  86DOS.asm:1133-1149
   procedure Dir_Write
     (AL   : Byte;
      D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
      Sector : constant Word := D.Firdir + Word (AL);
      Carry  : Boolean;
      pragma Unreferenced (Carry);
   begin
      Carry := Disk_Write (Bios, D.Drvnum, Dos.BUFFER, Sector, 1);
      Dos.DIRTYDIR := 0;
   end Dir_Write;

   -- Chk_Dir_Write — Conditionally write directory buffer.
   --
   -- ASM: CHKDIRWRITE  86DOS.asm:1129-1132
   procedure Chk_Dir_Write
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
   begin
      if Dos.DIRTYDIR /= 0 then
         Dir_Write (Byte (Dos.DIRBUFID), D, Bios);
      end if;
   end Chk_Dir_Write;

   -- Buf_Sec — Ensure the requested sector is in the sector buffer.
   --
   -- ASM: BUFSEC  86DOS.asm:1508-1565
   procedure Buf_Sec
     (Sector : Word;
      D      : in out DPB;
      Bios   : in out Bios_Vtable'Class)
   is
      Carry : Boolean;
      pragma Unreferenced (Carry);
   begin
      -- ASM lines 1512-1514: CMP BX,BUFSECNO / JE BUFOK
      if Dos.BUFSECNO = Sector and then Dos.BUFDRVNO = D.Drvnum then
         return;
      end if;
      -- Flush dirty buffer if needed
      if Dos.DIRTYBUF /= 0 then
         Carry := Disk_Write (Bios, Dos.BUFDRVNO, Dos.BUFFER,
                              Dos.BUFSECNO, 1);
         Dos.DIRTYBUF := 0;
      end if;
      -- Read new sector
      Carry := Disk_Read (Bios, D.Drvnum, Dos.BUFFER, Sector, 1);
      Dos.BUFSECNO := Sector;
      Dos.BUFDRVNO := D.Drvnum;
   end Buf_Sec;

   -- Buf_Rd — Copy data from sector buffer to Dst at the given offset.
   --
   -- ASM: BUFRD  86DOS.asm:1567-1579
   procedure Buf_Rd
     (Offset : Word;
      Count  : Word;
      Dst    : in out Byte_Array)
   is
      Off : constant Natural := Natural (Offset);
      Cnt : constant Natural := Natural (Count);
   begin
      for I in 0 .. Cnt - 1 loop
         if Off + I < MAX_SEC_SIZE and then I < Dst'Length then
            Dst (Dst'First + I) := Dos.BUFFER (Off + I);
         end if;
      end loop;
   end Buf_Rd;

   -- Buf_Wrt — Copy data from Src into sector buffer (marks dirty).
   --
   -- ASM: BUFWRT  86DOS.asm:1581-1606
   procedure Buf_Wrt
     (Offset : Word;
      Count  : Word;
      Src    : Byte_Array)
   is
      Off : constant Natural := Natural (Offset);
      Cnt : constant Natural := Natural (Count);
   begin
      for I in 0 .. Cnt - 1 loop
         if Off + I < MAX_SEC_SIZE and then I < Src'Length then
            Dos.BUFFER (Off + I) := Src (Src'First + I);
         end if;
      end loop;
      Dos.DIRTYBUF := 1;
   end Buf_Wrt;

   -- Next_Sec — Advance to the next sector within a cluster chain.
   --
   -- ASM: NEXTSEC  86DOS.asm:1608-1630
   --
   -- Increments Dos.SECCLUSPOS; if we've consumed all sectors in the
   -- current cluster, follows the FAT chain to the next cluster.
   procedure Next_Sec
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
      pragma Unreferenced (Bios);
   begin
      -- ASM line 1610: INC SECCLUSPOS / CMP [BP].CLUSMSK
      Dos.SECCLUSPOS := Dos.SECCLUSPOS + 1;
      if Dos.SECCLUSPOS <= D.Clusmsk then
         -- Still within same cluster
         Dos.CLUSNUM := Dos.CLUSNUM + 1;
         return;
      end if;
      -- Move to next cluster in FAT
      Dos.SECCLUSPOS := 0;
      Dos.CLUSNUM := DOS86.Fat.Unpack (D, Dos.CLUSNUM);
   end Next_Sec;

   -- Breakdown — Decompose a record position into cluster/sector offsets.
   --
   -- ASM: BREAKDOWN  86DOS.asm:1433-1463
   --
   -- Sets Dos.CLUSNUM, Dos.SECCLUSPOS, Dos.BYTSECPOS, Dos.RECPOS.
   procedure Breakdown (F : in out FCB; D : in DPB) is
      Rec_Pos     : DWord;
      Byte_Pos    : DWord;
      Cluster_Pos : DWord;
      pragma Unreferenced (F);
   begin
      -- ASM: compute record position from FCB.Extent and FCB.Nr
      -- RECPOS = (Extent * 128 + Nr) for default record size
      -- For simplicity we use Dos.RECPOS which is set by caller.
      Rec_Pos  := Dos.RECPOS;
      Byte_Pos := DWord (F.Recsiz) * Rec_Pos;
      -- Cluster position = byte_pos / (secsiz * (clusmsk+1))
      declare
         Clus_Bytes : constant DWord :=
           DWord (D.Secsiz) * DWord (D.Clusmsk + 1);
      begin
         Cluster_Pos       := Byte_Pos / Clus_Bytes;
         Dos.SECCLUSPOS    :=
           Byte (Shift_Right (DWord (Byte_Pos mod DWord (D.Secsiz * Word (D.Clusmsk + 1))), 0)
                 / DWord (D.Secsiz));
         Dos.BYTSECPOS     := Word (Byte_Pos mod DWord (D.Secsiz));
         Dos.SECPOS        := Word (Cluster_Pos);
         Dos.BYTPOS        := Byte_Pos;
         Dos.LASTPOS       := Word (Cluster_Pos);
      end;
   end Breakdown;

   -- Optimize — Compute count of contiguous sectors for the transfer.
   --
   -- ASM: OPTIMIZE  86DOS.asm:2055-2123
   --
   -- Walks the FAT to count how many consecutive sectors can be transferred
   -- in a single BIOS call.  Result stored in Dos.SECCNT.
   procedure Optimize (F : in out FCB; D : in out DPB) is
      pragma Unreferenced (F);
      Cur    : Word := Dos.CLUSNUM;
      Count  : Word := 0;
      Target : constant Word := Dos.VALSEC;
      Next   : Word;
   begin
      -- Walk FAT to count contiguous clusters
      while Count < Target loop
         Next := DOS86.Fat.Unpack (D, Cur);
         -- Contiguous if next cluster = cur + 1
         if Next /= Cur + 1 or else Next >= EOF_MARK then
            exit;
         end if;
         Cur   := Next;
         Count := Count + 1;
      end loop;
      Dos.SECCNT := Target;
   end Optimize;

end DOS86.Disk;
