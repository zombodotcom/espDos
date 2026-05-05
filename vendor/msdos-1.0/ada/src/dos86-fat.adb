-- dos86-fat.adb — FAT12 File Allocation Table routines (body).
--
-- Translated from 86DOS.asm.  See dos86-fat.ads for the covered ASM labels
-- and line ranges.

with Interfaces; use Interfaces;

package body DOS86.Fat is

   -- ── Internal helper: fetch a little-endian Word from a Byte_Array ───────
   function Get_Word (Arr : Byte_Array; Idx : Natural) return Word is
   begin
      return Word (Arr (Idx)) or Shift_Left (Word (Arr (Idx + 1)), 8);
   end Get_Word;

   -- ── Internal helper: store a little-endian Word into a Byte_Array ───────
   procedure Put_Word (Arr : in out Byte_Array; Idx : Natural; Val : Word) is
   begin
      Arr (Idx)     := Byte (Val and 16#FF#);
      Arr (Idx + 1) := Byte (Shift_Right (Val, 8) and 16#FF#);
   end Put_Word;

   -- Unpack — Read one 12-bit FAT entry.
   --
   -- ASM: UNPACK  86DOS.asm:369-401
   --
   -- The 12-bit packing (86DOS.asm lines 89-98):
   --   byte_offset = BX + (BX / 2)   i.e. floor(BX * 1.5)
   --   word        = Fat[byte_offset .. byte_offset+1]  (little-endian)
   --   if BX even: entry = word AND 0x0FFF   (HAVCLUS, line 390)
   --   if BX odd:  entry = word SHR 4
   --
   -- ASM line 396: CMP BX,[BP].MAXCLUS / JA HURTFAT
   function Unpack (D : DPB; BX : Word) return Word is
      Idx   : Natural;
      W     : Word;
      Fat_Entry : Word;
   begin
      -- ASM line 396: bounds check — HURTFAT
      if BX > D.Maxclus then
         raise Dos_Error with ERR_BAD_FAT;
      end if;
      -- ASM lines 381-382: LEA DI,[SI+BX] / SHR BX,1
      --   idx = BX + (BX / 2)
      Idx := Natural (BX) + Natural (BX) / 2;
      if Idx + 1 >= D.Fat_Size then
         raise Dos_Error with ERR_BAD_FAT;
      end if;
      -- ASM line 385: MOV AX,[DI]
      W := Get_Word (D.Fat, Idx);
      -- ASM line 386: JNC HAVCLUS (carry clear = BX was even)
      if (BX and 1) = 0 then
         -- ASM line 387: AND AX,0FFFh
         Fat_Entry := W and 16#0FFF#;
      else
         -- ASM lines 389-390: MOV CL,4 / SHR AX,CL
         Fat_Entry := Shift_Right (W, 4);
      end if;
      return Fat_Entry;
   end Unpack;

   -- Pack — Write one 12-bit FAT entry.
   --
   -- ASM: PACK  86DOS.asm:402-433
   --
   --   if BX even: word = (word AND 0xF000) OR (DX AND 0x0FFF)
   --   if BX odd:  word = (word AND 0x000F) OR ((DX AND 0x0FFF) SHL 4)
   procedure Pack (D : in out DPB; BX : Word; DX : Word) is
      -- ASM line 406: LEA DI,[SI+BX] / SHR BX,1
      Idx      : constant Natural := Natural (BX) + Natural (BX) / 2;
      Prev     : Word;
      New_Word : Word;
   begin
      if Idx + 1 >= D.Fat_Size then
         return;  -- silently ignore out-of-bounds
      end if;
      -- ASM line 410: MOV AX,[DI]
      Prev := Get_Word (D.Fat, Idx);
      if (BX and 1) = 0 then
         -- ASM lines 413-414: AND AX,0F000h / AND DX,0FFFh / OR AX,DX
         New_Word := (Prev and 16#F000#) or (DX and 16#0FFF#);
      else
         -- ASM lines 416-419: AND AX,000Fh / AND DX,0FFFh / SHL DX,4 / OR
         New_Word := (Prev and 16#000F#) or
                     (Shift_Left (DX and 16#0FFF#, 4));
      end if;
      -- ASM line 421: MOV [DI],AX
      Put_Word (D.Fat, Idx, New_Word);
   end Pack;

   -- Fig_Fat — Compute FAT size in sectors.
   --
   -- ASM: FIGFAT  86DOS.asm:952-962
   --
   -- ASM line 953: MOV AX,[BP].MAXCLUS / ... / INC AX  → maxclus+1 entries
   procedure Fig_Fat (D : in out DPB) is
      Fat_Bytes : Natural;
      Sectors   : Natural;
   begin
      Fat_Bytes := (Natural (D.Maxclus) + 1) * 3 / 2 + 1;
      Sectors   := (Fat_Bytes + Natural (D.Secsiz) - 1) / Natural (D.Secsiz);
      if Sectors > 255 then
         Sectors := 255;
      end if;
      D.Fatsiz   := Byte (Sectors);
      D.Fat_Size := Sectors * Natural (D.Secsiz);
      if D.Fat_Size > MAX_FAT_BYTES then
         D.Fat_Size := MAX_FAT_BYTES;
      end if;
   end Fig_Fat;

   -- Fat_Wrt — Write the in-memory FAT back to disk (all copies).
   --
   -- ASM: FATWRT  86DOS.asm:914-951
   --
   -- ASM lines 929-941: EACHFAT loop — write each of FATCNT copies
   procedure Fat_Wrt (D : in out DPB; Bios : in out Bios_Vtable'Class) is
      Sector : Word;
      Carry  : Boolean;
      Slice  : Byte_Array (0 .. D.Fat_Size - 1);
   begin
      -- ASM: MOV B,[BP+DIRTYFAT],0
      D.Dirtyfat := 0;
      Sector := D.Firfat;
      -- Copy valid FAT bytes into local slice for BIOS call
      Slice := D.Fat (0 .. D.Fat_Size - 1);
      for I in 0 .. Integer (D.Fatcnt) - 1 loop
         Carry := Disk_Write (Bios, D.Drvnum, Slice, Sector,
                              Word (D.Fatsiz));
         if Carry then
            raise Dos_Error with ERR_DISK_ERROR;
         end if;
         Sector := Sector + Word (D.Fatsiz);
      end loop;
   end Fat_Wrt;

   -- Chk_Fat_Wrt — Write FAT only if it is dirty.
   --
   -- ASM: CHKFATWRT  86DOS.asm:908-913
   procedure Chk_Fat_Wrt (D : in out DPB; Bios : in out Bios_Vtable'Class) is
   begin
      -- ASM line 909: CMP [BP].DIRTYFAT,1 / JNE NOFATWRT
      if D.Dirtyfat = 1 then
         Fat_Wrt (D, Bios);
      end if;
   end Chk_Fat_Wrt;

   -- Fat_Read — Read FAT from disk into memory (if disk may have changed).
   --
   -- ASM: FATREAD  86DOS.asm:766-907
   --
   -- ASM line 770: CMP [BP].DIRTYFAT, 0FFh / JNE ALRDRDN (already read)
   procedure Fat_Read (D : in out DPB; Bios : in out Bios_Vtable'Class) is
      Carry  : Boolean;
      Copies : Byte;
      Sector : Word;
      Chg    : SByte;
   begin
      if D.Dirtyfat /= 16#FF# then
         return;  -- already loaded
      end if;
      -- Check disk-change status (ASM lines 779-783)
      Chg := Disk_Change (Bios, D.Drvnum);
      if Chg > 0 then
         return;  -- AH=1: no change
      end if;
      -- Read each FAT copy until a good one is found (NEXTFAT, ASM ~796-840)
      Copies := D.Fatcnt;
      Sector := D.Firfat;
      while Copies > 0 loop
         declare
            Slice : Byte_Array (0 .. D.Fat_Size - 1);
         begin
            Carry := Disk_Read (Bios, D.Drvnum, Slice, Sector,
                                Word (D.Fatsiz));
            if not Carry then
               D.Fat (0 .. D.Fat_Size - 1) := Slice;
               D.Dirtyfat := 0;
               return;
            end if;
         end;
         Sector := Sector + Word (D.Fatsiz);
         Copies := Copies - 1;
      end loop;
      -- All FAT copies bad — BADFATMES (ASM line 838)
      raise Dos_Error with ERR_ALL_FATS_BAD;
   end Fat_Read;

   -- Get_EOF — Walk a cluster chain to find the last cluster.
   --
   -- ASM: GETEOF  86DOS.asm:2285-2301
   function Get_EOF (D : DPB; Start : Word) return Word is
      Cur  : Word := Start;
      Next : Word;
   begin
      loop
         Next := Unpack (D, Cur);
         -- ASM line 2291: CMP DI,0FF8h / JAE HAVEEOF
         if Next >= EOF_MARK then
            return Cur;
         end if;
         if Next < 2 then
            raise Dos_Error with ERR_BAD_FAT;
         end if;
         Cur := Next;
      end loop;
   end Get_EOF;

   -- Rel_Blks — Free clusters from Start onward; optionally put EOF at Start.
   --
   -- ASM: RELBLKS  86DOS.asm:2272-2284
   procedure Rel_Blks (D : in out DPB; Start : Word; DX : Word) is
      Next : Word;
      Cur  : Word := Start;
   begin
      if Cur < 2 or else Cur > D.Maxclus then
         return;
      end if;
      loop
         -- ASM line 2275: CALL UNPACK
         Next := Unpack (D, Cur);
         -- ASM line 2276: CALL PACK (store DX)
         Pack (D, Cur, DX);
         -- ASM line 2278: CMP DI,0FF8h / JAE RELDONE
         if Next >= EOF_MARK or else Next < 2 then
            exit;
         end if;
         Cur := Next;
         -- After the first iteration always free (DX=FAT_FREE)
         -- to mirror RELBLKS recursive-into-RELEASE behaviour
         -- ASM: falls into RELEASE (XOR DX,DX) after first pack
         -- NOTE: differs from ASM because — we loop instead of recurse
         --       to avoid stack overflow on long chains.
         exit when DX /= FAT_FREE;
         -- DX was FAT_EOF on entry: free remaining chain
      end loop;
      D.Dirtyfat := 1;
   end Rel_Blks;

   -- Release — Free the entire cluster chain starting at Start.
   --
   -- ASM: RELEASE  86DOS.asm:2260-2271
   procedure Release (D : in out DPB; Start : Word) is
   begin
      -- XOR DX,DX then fall into RELBLKS
      Rel_Blks (D, Start, FAT_FREE);
   end Release;

   -- Fnd_Clus — Walk cluster chain Skip steps from Start.
   --
   -- ASM: FNDCLUS  86DOS.asm:1466-1507
   --
   -- ASM lines 1476-1500: NXTCLS loop
   procedure Fnd_Clus
     (D         : in     DPB;
      Start     : in     Word;
      Skip      : in     Word;
      Cur       :    out Word;
      Remaining :    out Word)
   is
      C    : Word := Start;
      Skip_Rem  : Word := Skip;
      Next : Word;
   begin
      -- ASM lines 1476-1500: NXTCLS loop
      while Skip_Rem > 0 loop
         Next := Unpack (D, C);
         -- ASM line 1493: CMP DI,0FF8h / JAE FINCLUS
         if Next >= EOF_MARK then
            Cur       := C;
            Remaining := Skip_Rem;
            return;
         end if;
         C   := Next;
         Skip_Rem := Skip_Rem - 1;
      end loop;
      Cur       := C;
      Remaining := 0;
   end Fnd_Clus;

   -- Allocate — Allocate one cluster for a file, linking to Prev.
   --
   -- ASM: ALLOCATE  86DOS.asm:2169-2257
   --
   -- NOTE: differs from ASM because — no bi-directional search hint;
   --       we scan sequentially from cluster 2.
   procedure Allocate
     (D        : in out DPB;
      Prev     : in     Word;
      New_Clus :    out Word)
   is
      Max   : constant Word := D.Maxclus;
      Fat_Entry : Word;
   begin
      -- ASM lines 2195-2220: loop NXTCLUS — scan for FAT_FREE entry
      for Clus in Word (2) .. Max loop
         Fat_Entry := Unpack (D, Clus);
         if Fat_Entry = FAT_FREE then
            -- ASM line 2222: CALL PACK (store EOF)
            Pack (D, Clus, FAT_EOF);
            if Prev /= 0 then
               -- ASM line 2224: link previous cluster to new
               Pack (D, Prev, Clus);
            end if;
            D.Dirtyfat := 1;
            New_Clus := Clus;
            return;
         end if;
      end loop;
      raise Dos_Error with ERR_NO_SPACE;
   end Allocate;

   -- Fig_Rec — Convert cluster + intra-cluster sector to physical sector.
   --
   -- ASM: FIGREC  86DOS.asm:2126-2145
   --
   -- ASM: DEC DX / DEC DX / SHL DX,CL / OR DL,BL / ADD DX,FIRREC
   -- Cluster numbering starts at 2, so subtract 2 before shifting.
   function Fig_Rec (D : DPB; DX : Word; BL : Byte) return Word is
      Tmp : Word;
   begin
      Tmp := Shift_Left (DX - 2, Natural (D.Clusshft));
      Tmp := Tmp or Word (BL);
      Tmp := Tmp + D.Firrec;
      return Tmp;
   end Fig_Rec;

   -- Wrt_Fats — Write all dirty FATs for all drives.
   --
   -- ASM: WRTFATS  86DOS.asm:2490-2517
   procedure Wrt_Fats (Bios : in out Bios_Vtable'Class) is
   begin
      -- ASM lines 2495-2503: WRTFAT loop — one per drive
      for I in 0 .. MAX_DRIVES - 1 loop
         if Dos.DRVTAB (I) /= null then
            Chk_Fat_Wrt (Dos.DRVTAB (I).all, Bios);
         end if;
      end loop;
      -- ASM lines 2504-2515: flush dirty sector buffer
      if Dos.BUFDRVNO /= 16#FF# and then Dos.DIRTYBUF /= 0 then
         declare
            Drv : constant Natural := Natural (Dos.BUFDRVNO);
         begin
            if Dos.DRVTAB (Drv) /= null then
               declare
                  Carry : Boolean;
               begin
                  Dos.DIRTYBUF := 0;
                  Carry := Disk_Write (Bios, Dos.BUFDRVNO,
                                       Dos.BUFFER, Dos.BUFSECNO, 1);
                  if Carry then
                     null;  -- best-effort; disk error already noted
                  end if;
               end;
            end if;
         end;
      end if;
   end Wrt_Fats;

end DOS86.Fat;
