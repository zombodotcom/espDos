-- dos86-directory.adb — Directory search and FCB matching (body).
--
-- Translated from 86DOS.asm.  See dos86-directory.ads for covered labels.

with Interfaces; use Interfaces;
with DOS86.Disk; use DOS86.Disk;
with DOS86.Fat;

package body DOS86.Directory is

   -- Get_Bp — Return DPB access for the given 1-based drive number.
   --
   -- ASM: GETBP  86DOS.asm:696-704
   --
   -- Inputs:  AL = drive number (0 = current drive, ≥1 = specific drive)
   -- Outputs: BP = base of drive parameter block; carry set if invalid
   function Get_Bp (Drive : Byte) return DPB_Access is
      Dr : Byte := Drive;
   begin
      -- ASM line 698: CMP [NUMDRV],AL — check drive number against count
      -- ASM line 699: JC RET          — carry set → invalid drive
      -- Drive 0 means "default drive" (1-based in user FCB; 0=current)
      if Dr = 0 then
         Dr := Byte (1);  -- will map to DRVTAB(0) below
      end if;
      if Dr > Byte (Dos.NUMDRV) then
         raise Dos_Error with ERR_INVALID_DRV;
      end if;
      -- ASM line 700: CBW            — zero-extend AL to AX
      -- ASM line 701: XCHG BP,AX    — move drive index to BP
      -- ASM line 702: SHL BP         — multiply by 2 (word table)
      -- ASM line 703: MOV BP,[BP+CURDRVPT] — load DPB pointer
      return Dos.DRVTAB (Natural (Dr) - 1);
   end Get_Bp;

   -- IO_Chk — Validate FCB drive number and set Dos.CURDRVPT.
   --
   -- ASM: IOCHK  86DOS.asm:434-446
   --
   -- In the ASM, IOCHK is reached after a name match on an I/O device;
   -- it verifies the rest of the name is spaces and sets BP to the DPB.
   -- In Ada the device-name path is omitted; we only perform the drive-
   -- parameter lookup that all callers need.
   procedure IO_Chk (F : in out FCB) is
      Dp : DPB_Access;
   begin
      -- ASM line 434: CALL GETBP — get drive parameter block
      Dp := Get_Bp (F.Drive);
      if Dp = null then
         raise Dos_Error with ERR_INVALID_DRV;
      end if;
      -- ASM: MOV [CURDRVPT],BP — store current drive pointer
      Dos.CURDRVPT := Dp;
   end IO_Chk;

   -- Mov_Name — Copy 11-byte name from FCB into Dos.NAME1.
   --
   -- ASM: MOVNAME  86DOS.asm:660-678
   --
   -- Inputs:  DS,DX = FCB pointer
   -- Outputs: ES=CS; BP = DPB; [NAME1] has upper-case file name; carry on error
   procedure Mov_Name (F : in out FCB) is
   begin
      -- ASM line 672: MOV AX,CS / MOV ES,AX — set ES = CS
      -- ASM line 674: MOV DI,NAME1 — destination for name copy
      -- ASM line 675: MOV SI,DX    — source = FCB
      -- ASM line 676: LODB         — load drive byte (FCB[0])
      -- ASM line 677: CALL GETBP   — validate drive and set BP
      IO_Chk (F);
      -- ASM line 682: MOV CX,11 / (LODNAME loop copies 11 bytes upper-cased)
      -- ASM line 692: STOB — store each character into NAME1
      -- ASM lines 684-693: REP MOVSB with upper-case conversion
      for I in 0 .. 10 loop
         Dos.NAME1 (I) := F.Name (I);
      end loop;
   end Mov_Name;

   -- Lod_Name — Load/pad one name field into a destination buffer.
   --
   -- ASM: LODNAME  86DOS.asm:679-695
   procedure Lod_Name
     (Src : Byte_Array;
      Dst : in out Byte_Array;
      Len : Natural)
   is
      Space : constant Byte := Character'Pos (' ');
   begin
      for I in 0 .. Len - 1 loop
         if I < Src'Length then
            declare
               Ch : constant Byte := Src (Src'First + I);
            begin
               if Ch = Space then
                  -- Pad remaining with spaces
                  for J in I .. Len - 1 loop
                     Dst (Dst'First + J) := Space;
                  end loop;
                  return;
               end if;
               Dst (Dst'First + I) := Ch;
            end;
         else
            Dst (Dst'First + I) := Space;
         end if;
      end loop;
   end Lod_Name;

   -- Start_Srch — Initialise directory search state and read FAT.
   --
   -- ASM: STARTSRCH  86DOS.asm:764-765
   --
   -- ASM line 765: MOV [LASTENT],-1 — initialise search to "before first entry"
   -- Then falls through to FATREAD which conditionally re-reads the FAT.
   procedure Start_Srch
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class)
   is
   begin
      -- ASM line 765: MOV [LASTENT],-1 — start before entry 0
      -- NOTE: Ada uses 0 instead of −1 because our GETENTRY doesn't pre-increment;
      --       the ASM GETENTRY increments LASTENT before using it (line 531).
      Dos.LASTENT := 0;
      -- ASM: FATREAD path — re-read FAT if disk may have changed
      DOS86.Fat.Fat_Read (D, Bios);
   end Start_Srch;

   -- Name_Match — Return True if 11-byte Name matches Pattern (with '?').
   function Name_Match (Name : Byte_Array; Pattern : Byte_Array)
     return Boolean
   is
      QM : constant Byte := Character'Pos ('?');
   begin
      for I in 0 .. 10 loop
         declare
            P : constant Byte := Pattern (Pattern'First + I);
         begin
            if P /= QM then
               if P /= Name (Name'First + I) then
                  return False;
               end if;
            end if;
         end;
      end loop;
      return True;
   end Name_Match;

   -- Decode_Small_Entry — Unpack a 16-byte raw directory entry.
   function Decode_Small (Raw : Byte_Array; Off : Natural)
     return Small_Dir_Entry
   is
      E : Small_Dir_Entry;
   begin
      for I in 0 .. 7 loop
         E.Name (I) := Raw (Off + I);
      end loop;
      for I in 0 .. 2 loop
         E.Ext (I) := Raw (Off + 8 + I);
      end loop;
      E.Attr    := Raw (Off + 11);
      E.Firclus := Word (Raw (Off + 12)) or
                   (Word (Raw (Off + 13)) * 256);
      E.Size    := Word (Raw (Off + 14)) or
                   (Word (Raw (Off + 15)) * 256);
      return E;
   end Decode_Small;

   -- Get_File — Search directory for a file matching Dos.NAME1.
   --
   -- ASM: GETFILE  86DOS.asm:448-515
   --
   -- Inputs:  DS,DX = FCB; [NAME1] set by MOVNAME
   -- Outputs: carry clear + BX/SI set if found; carry set if not found
   --
   -- ASM line 468: CALL MOVNAME    — copy name (already done by caller here)
   -- ASM line 469: JC RET          — bad file name → return with carry
   -- ASM label FILSRCH 86DOS.asm:484: CALL STARTSRCH — init search
   -- ASM label CONTSRCH 86DOS.asm:486: CALL GETENTRY / loop
   function Get_File
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean
   is
   begin
      -- ASM label FILSRCH line 485: CALL STARTSRCH — already called by callers;
      -- we reset LASTENT here to mirror GETFILE resetting the position.
      -- ASM line 484: falls into CONTSRCH immediately after STARTSRCH
      Dos.LASTENT := 0;
      return Cont_Srch (D, Bios);
   end Get_File;

   -- Cont_Srch — Continue directory search for the next matching entry.
   --
   -- ASM: CONTSRCH  86DOS.asm:486-515
   --
   -- ASM line 487: CALL GETENTRY — load next entry; carry set → not found
   -- ASM label SRCH 86DOS.asm:489: compare first byte of entry
   -- ASM line 490: CMP B,[BX],0E5H — deleted entry?
   -- ASM line 491: JZ NEXTENT      — yes, skip it
   -- ASM lines 492-500: WILDCRD loop — REP CMPB with '?' wildcard
   -- ASM line 498: JZ FOUND        — all 11 bytes matched
   -- ASM label NEXTENT 86DOS.asm:501: CALL NEXTENTRY / JNC SRCH
   -- ASM label FOUND 86DOS.asm:506: RET (carry clear)
   function Cont_Srch
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean
   is
      Entries_Per_Sec : constant Natural :=
        Natural (D.Secsiz) / SMALLDIR_ENTRY;
      Max_Entries     : constant Natural := Natural (D.Maxent);
      Name_Pat        : Byte_Array (0 .. 10);
   begin
      for I in 0 .. 10 loop
         Name_Pat (I) := Dos.NAME1 (I);
      end loop;

      -- ASM label SRCH: loop over directory entries
      while Natural (Dos.LASTENT) < Max_Entries loop
         declare
            Sec_Idx : constant Natural :=
              Natural (Dos.LASTENT) / Entries_Per_Sec;
            Ent_Idx : constant Natural :=
              Natural (Dos.LASTENT) mod Entries_Per_Sec;
         begin
            -- ASM: CALL DIRREAD — load directory sector into DIRBUF
            Dir_Read (Byte (Sec_Idx), D, Bios);
            declare
               Raw_Off : constant Natural := Ent_Idx * SMALLDIR_ENTRY;
               First   : constant Byte := Dos.BUFFER (Raw_Off);
            begin
               -- ASM line 490: CMP B,[BX],0  — never-used entry ends directory
               if First = 0 then
                  return False;
               end if;
               -- ASM line 490: CMP B,[BX],0E5H — DEL_MARK = deleted, skip
               if First /= DEL_MARK then
                  -- ASM lines 492-500: WILDCRD — compare 11-byte name with '?' wildcard
                  declare
                     Ename : Byte_Array (0 .. 10);
                  begin
                     for I in 0 .. 10 loop
                        Ename (I) := Dos.BUFFER (Raw_Off + I);
                     end loop;
                     -- ASM line 498: JZ FOUND — match
                     if Name_Match (Ename, Name_Pat) then
                        return True;
                     end if;
                  end;
               end if;
               -- ASM label NEXTENT line 502: CALL NEXTENTRY — advance LASTENT
               Dos.LASTENT := Dos.LASTENT + 1;
            end;
         end;
      end loop;
      -- ASM label NONE 86DOS.asm:596: CALL CHKDIRWRITE / STC / RET
      return False;
   end Cont_Srch;

   -- Get_Entry — Get the directory entry at Dos.LASTENT.
   --
   -- ASM: GETENTRY  86DOS.asm:516-562
   --
   -- Inputs:  [LASTENT] = entry number to fetch
   -- Outputs: carry set if none; BX = ptr into DIRBUF; AL = sector no.
   --
   -- ASM line 530: MOV AX,[LASTENT]   — load entry index
   -- ASM line 531: INC AX             — start with NEXT entry (pre-increment)
   -- ASM line 532: CMP AX,[BP+MAXENT] — past end of directory?
   -- ASM line 533: JAE NONE           — yes → carry set, return
   -- ASM line 534: MOV [LASTENT],AX   — save updated index
   -- ASM lines 535-549: compute sector number (AX = entry * entsize / secsiz)
   -- ASM line 550: MOV BX,DX          — position within sector
   -- ASM lines 552-556: CALL DIRREAD if sector not already buffered
   -- ASM label HAVDIRBUF 86DOS.asm:557: BX now points into DIRBUF
   function Get_Entry
     (D         : in out DPB;
      Bios      : in out Bios_Vtable'Class;
      Entry_Out :    out Small_Dir_Entry) return Boolean
   is
      Entries_Per_Sec : constant Natural :=
        Natural (D.Secsiz) / SMALLDIR_ENTRY;
      -- ASM lines 535-549: AX = LASTENT * ENTSIZE; divide by SECSIZ
      Sec_Idx : constant Natural :=
        Natural (Dos.LASTENT) / Entries_Per_Sec;
      -- ASM line 550: BX = DX = position within sector
      Ent_Idx : constant Natural :=
        Natural (Dos.LASTENT) mod Entries_Per_Sec;
      Raw_Off : constant Natural := Ent_Idx * SMALLDIR_ENTRY;
   begin
      -- ASM lines 554-556: CALL DIRREAD — load sector into DIRBUF
      Dir_Read (Byte (Sec_Idx), D, Bios);
      -- ASM label HAVDIRBUF: decode the entry at BX within DIRBUF
      Entry_Out := Decode_Small (Dos.BUFFER, Raw_Off);
      -- Carry clear if first byte non-zero (valid entry)
      return Entry_Out.Name (0) /= 0;
   end Get_Entry;

   -- Next_Entry — Advance to the next directory entry.
   --
   -- ASM: NEXTENTRY  86DOS.asm:563-595
   --
   -- Inputs:  same as outputs of GETENTRY (BX, DX, [LASTENT], AL)
   -- Outputs: carry set if no more entries; updated BX/AL/[LASTENT]
   --
   -- ASM line 571: MOV DI,[LASTENT]  — load current entry index
   -- ASM line 572: INC DI            — advance to next
   -- ASM line 573: CMP DI,[BP+MAXENT]— past end?
   -- ASM line 574: JAE NONE          — yes → carry set
   -- ASM line 575: MOV [LASTENT],DI  — save new index
   -- ASM line 576: ADD BX,32         — advance pointer by one 32-byte entry
   --   (SMALLDIR: SUB BX,16 to get 16-byte entry size net +16)
   -- ASM lines 585-591: if BX≥DX (past sector end), call DIRREAD for next sector
   -- ASM label HAVIT 86DOS.asm:592: CLC / RET — carry clear = more entries
   function Next_Entry
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class) return Boolean
   is
      pragma Unreferenced (Bios);
   begin
      -- ASM line 572: INC DI / ASM line 575: MOV [LASTENT],DI
      Dos.LASTENT := Dos.LASTENT + 1;
      -- ASM line 573-574: CMP DI,[BP+MAXENT] / JAE NONE → carry set
      return Natural (Dos.LASTENT) < Natural (D.Maxent);
   end Next_Entry;

end DOS86.Directory;
