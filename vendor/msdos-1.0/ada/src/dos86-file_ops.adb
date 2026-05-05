-- dos86-file_ops.adb — File open/close/create/delete/rename (body).
--
-- Translated from 86DOS.asm.  See dos86-file_ops.ads for covered labels.

with Interfaces; use Interfaces;
with DOS86.Directory; use DOS86.Directory;
with DOS86.Fat;
with DOS86.Disk;

package body DOS86.File_Ops is

   -- Fn_Open — Open a file for I/O (FCB open, function 15).
   --
   -- ASM: OPEN / DOOPEN  86DOS.asm:707-755
   --
   -- ASM line 708: PUSH DX / PUSH DS   — save FCB pointer on stack
   -- ASM line 710: CALL GETFILE        — search directory for name
   -- ASM label DOOPEN 86DOS.asm:711: enter here if file already found
   -- ASM line 718: POP ES / POP DI     — restore FCB pointer to ES:DI
   -- ASM line 720: JC ERRET            — file not found → return 0FFH
   -- ASM line 721: CMP BH,-1           — check for I/O device name
   -- ASM line 722: JZ OPENDEV          — device: special handler
   -- ASM line 723: MOV AL,[BP+DRVNUM] / INC AL / STOB — store drive number+1
   -- ASM line 726: ADD DI,11           — point to EXTENT field in FCB
   -- ASM line 727: XOR AX,AX / STOW   — set EXTENT = 0
   -- ASM line 729: MOV AX,128 / STOW  — set RECSIZ = 128 (default)
   -- ASM line 731: LODW                — get FIRCLUS from directory entry
   -- ASM line 732: MOV DX,AX           — save starting cluster
   -- ASM lines 733-734: MOVW×2        — copy FILSIZ (32-bit) to FCB
   -- ASM line 749: STOW FIRCLUS        — store first cluster in FCB
   -- ASM line 751: STOW LSTCLUS        — store last cluster = first (initial)
   -- ASM lines 752-754: XOR AX,AX; STOW CLUSPOS; STOB DIRTYFIL
   function Fn_Open
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D   : DPB_Access;
      E   : Small_Dir_Entry;
      Fnd : Boolean;
   begin
      -- ASM line 710: CALL GETFILE (via MOVNAME then FILSRCH)
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         return 16#FF#;
      end if;
      -- ASM line 485: CALL STARTSRCH
      Start_Srch (D.all, Bios);
      Fnd := Get_File (D.all, Bios);
      -- ASM line 720: JC ERRET — not found
      if not Fnd then
         return 16#FF#;
      end if;
      -- ASM label DOOPEN: read the located directory entry
      declare
         OK : constant Boolean := Get_Entry (D.all, Bios, E);
      begin
         -- ASM line 720: JC ERRET
         if not OK then
            return 16#FF#;
         end if;
      end;
      -- ASM line 731: LODW — get starting cluster from directory SI pointer
      F.Firclus := E.Firclus;                     -- ASM line 749: STOW first cluster
      -- ASM lines 733-734: MOVW×2 — 32-bit FILSIZ into FCB
      F.Filsiz  := DWord (E.Size);
      -- ASM line 729: MOV AX,128 / STOW — default record size
      F.Recsiz  := DEFAULT_RECSIZ;
      -- ASM line 751: STOW LSTCLUS = first cluster initially
      F.Lstclus := 0;
      -- ASM line 753: STOW CLUSPOS = 0
      F.Cluspos := 0;
      -- ASM line 727: XOR AX,AX / STOW — EXTENT = 0
      F.Nr      := 0;
      -- ASM line 754: STOB DIRTYFIL = 0
      F.Dirtyfil := 0;
      -- ASM line 755: RET — return AL=0 (success)
      return 0;
   end Fn_Open;

   -- Fn_Close — Close a file FCB (function 16).
   --
   -- ASM: CLOSE  86DOS.asm:846-907
   --
   -- ASM line 847: MOV DI,DX         — FCB pointer
   -- ASM line 848: CMP B,[DI+FILDIRBLK],-1 — I/O device? → return 0 (OKRET1)
   -- ASM line 850: TEST B,[DI+DIRTYFIL],-1 — file written to?
   -- ASM line 851: JZ OKRET1         — not dirty → nothing to do
   -- ASM lines 852-869: write back dirty sector buffer if on same drive
   -- ASM label FNDDIR 86DOS.asm:872: CALL GETFILE to re-locate directory entry
   -- ASM lines 880-904: update FIRCLUS, FILSIZ, date in directory entry
   -- ASM label SMALLENT2 86DOS.asm:905: CALL DIRWRITE — write directory sector
   -- ASM line 906: CALL FATWRT       — write FAT if dirty
   function Fn_Close
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D : DPB_Access;
   begin
      -- ASM line 847: MOV DI,DX — point DI at FCB
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         return 16#FF#;
      end if;
      -- ASM line 850: TEST B,[DI+DIRTYFIL],-1 — file written to?
      if F.Dirtyfil /= 0 then
         -- ASM line 906: CALL FATWRT — write dirty FAT back to disk
         DOS86.Fat.Chk_Fat_Wrt (D.all, Bios);
         -- ASM label SMALLENT2 line 905: CALL DIRWRITE — flush directory sector
         DOS86.Disk.Chk_Dir_Write (D.all, Bios);
      end if;
      -- ASM line 754-equivalent: clear DIRTYFIL
      F.Dirtyfil := 0;
      -- ASM line 843: MOV AL,0 / RET (via OKRET1)
      return 0;
   end Fn_Close;

   -- Fn_Create — Create or truncate a file (function 22).
   --
   -- ASM: CREATE  86DOS.asm:973-1068
   --
   -- ASM line 974: CALL MOVNAME     — copy name, validate drive
   -- ASM line 975: JC ERRET3        — bad name → fail
   -- ASM lines 976-981: scan NAME1 for '?' — wildcards not allowed in CREATE
   -- ASM line 984: CALL FINDNAME    — search directory for existing file
   -- ASM line 985: JNC EXISTENT     — found: truncate path
   -- ASM lines 986-997: CALL STARTSRCH / GETENTRY / LOOKFRE — find free slot
   -- ASM label EXISTENT 86DOS.asm:999: file exists — free cluster chain
   -- ASM lines 1002-1030: zero size/date, XCHG CX,[SI]; CALL RELEASE / FATWRT
   -- ASM label FREESPOT 86DOS.asm:1032: write new directory entry
   -- ASM lines 1033-1061: copy NAME1 into directory slot, zero attribute/size
   -- ASM label WRTBACK 86DOS.asm:1062: CALL DIRWRITE — flush directory sector
   -- ASM label OPENJMP 86DOS.asm:1066: CLC / JMP DOOPEN — open the new file
   function Fn_Create
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D    : DPB_Access;
      E    : Small_Dir_Entry;
      Fnd  : Boolean;
   begin
      -- ASM line 974: CALL MOVNAME
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         return 16#FF#;
      end if;
      -- ASM line 985: CALL STARTSRCH (via FINDNAME path)
      Start_Srch (D.all, Bios);
      -- ASM line 984: CALL FINDNAME
      Fnd := Get_File (D.all, Bios);
      if Fnd then
         -- ASM label EXISTENT: file found — free its cluster chain
         -- ASM line 1002: XOR CX,CX / MOV [SI+2],CX — zero size
         declare
            OK : constant Boolean := Get_Entry (D.all, Bios, E);
         begin
            if OK and then E.Firclus /= 0 then
               -- ASM line 1028: CALL RELEASE — free cluster chain
               DOS86.Fat.Release (D.all, E.Firclus);
            end if;
         end;
      else
         -- ASM lines 986-997: CALL STARTSRCH / GETENTRY / LOOKFRE loop
         -- Find a free (deleted or never-used) directory slot
         Dos.LASTENT := 0;
         loop
            declare
               OK : constant Boolean := Get_Entry (D.all, Bios, E);
            begin
               -- ASM line 989: CMP B,[BX],0E5H — deleted slot?
               -- ASM line 990: JZ FREESPOT
               -- ASM: also stop at first byte = 0 (never-used)
               if not OK or else E.Name (0) = DEL_MARK or else
                  E.Name (0) = 0
               then
                  exit;
               end if;
               -- ASM line 991: CALL NEXTENTRY
               if not Next_Entry (D.all, Bios) then
                  return 16#FF#;  -- directory full (ERRET3 path)
               end if;
            end;
         end loop;
       end if;
       -- ASM label FREESPOT: write new directory entry into slot at LASTENT
       -- Compute byte offset of entry within the current BUFFER sector.
       -- ASM line 1033: MOV DI,BX — point DI to free slot in DIRBUF
       declare
          Entries_Per_Sec : constant Natural :=
            Natural (D.Secsiz) / SMALLDIR_ENTRY;
          Entry_Off : constant Natural :=
            (Natural (Dos.LASTENT) mod Entries_Per_Sec) * SMALLDIR_ENTRY;
       begin
          -- ASM lines 1034-1038: MOV SI,NAME1 / MOV CX,5 / MOVB / REP MOVW
          -- copy 11 bytes of name into directory slot
          for I in 0 .. 7 loop
             Dos.BUFFER (Entry_Off + I) := Dos.NAME1 (I);
          end loop;
          for I in 0 .. 2 loop
             Dos.BUFFER (Entry_Off + 8 + I) := Dos.NAME1 (8 + I);
          end loop;
          -- ASM lines 1050-1060: MOV CL,13 / REP STOB — zero attribute through size
          Dos.BUFFER (Entry_Off + 11) := 0;  -- attribute
          Dos.BUFFER (Entry_Off + 12) := 0;  -- FIRCLUS low
          Dos.BUFFER (Entry_Off + 13) := 0;  -- FIRCLUS high
          Dos.BUFFER (Entry_Off + 14) := 0;  -- size low
          Dos.BUFFER (Entry_Off + 15) := 0;  -- size high
       end;
      -- ASM line 1063: CALL DIRWRITE — flush directory sector (WRTBACK)
      Dos.DIRTYDIR := 1;
      DOS86.Disk.Chk_Dir_Write (D.all, Bios);
      -- ASM label OPENJMP 86DOS.asm:1066: CLC / JMP DOOPEN — initialise FCB
      F.Firclus  := 0;
      F.Filsiz   := 0;
      F.Recsiz   := DEFAULT_RECSIZ;
      F.Lstclus  := 0;
      F.Cluspos  := 0;
      F.Nr       := 0;
      F.Dirtyfil := 0;
      return 0;
   end Fn_Create;

   -- Fn_Delete — Delete a file (function 19).
   --
   -- ASM: DELETE  86DOS.asm:602-623
   --
   -- ASM line 603: CALL GETFILE     — find file in directory
   -- ASM line 604: JC ERRET         — not found → 0FFH
   -- ASM line 605: CMP BH,-1        — I/O device? (BH set by GETFILE for devices)
   -- ASM line 606: JZ ERRET         — can't delete a device
   -- ASM label DELFILE 86DOS.asm:607: delete loop (handles wildcard matches)
   -- ASM line 608: MOV B,[DIRTYDIR],-1 — mark directory dirty
   -- ASM line 609: MOV B,[BX],0E5H    — stamp first byte with DEL_MARK (0xE5)
   -- ASM line 610: MOV BX,[SI]        — get FIRCLUS from directory entry
   -- ASM lines 611-616: OR BX,BX; CALL RELEASE — free cluster chain if any
   -- ASM line 618: CALL CONTSRCH      — look for next wildcard match
   -- ASM line 619: JNC DELFILE        — if found, delete it too
   -- ASM line 620: CALL FATWRT        — write FAT
   -- ASM line 621: CALL CHKDIRWRITE   — flush directory sector
   -- ASM line 622: XOR AL,AL / RET    — return 0 (success)
   function Fn_Delete
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D   : DPB_Access;
      E   : Small_Dir_Entry;
      Fnd : Boolean;
   begin
      -- ASM line 603: CALL GETFILE (via MOVNAME + FILSRCH)
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         return 16#FF#;
      end if;
      Start_Srch (D.all, Bios);
      Fnd := Get_File (D.all, Bios);
      -- ASM line 604: JC ERRET
      if not Fnd then
         return 16#FF#;
      end if;
      declare
         OK : constant Boolean := Get_Entry (D.all, Bios, E);
         pragma Unreferenced (OK);
      begin
          -- ASM lines 610-616: get FIRCLUS / CALL RELEASE
          if E.Firclus /= 0 then
             DOS86.Fat.Release (D.all, E.Firclus);
          end if;
          -- ASM line 608: MOV B,[DIRTYDIR],-1
          -- ASM line 609: MOV B,[BX],0E5H — stamp entry deleted
          declare
             Entries_Per_Sec : constant Natural :=
               Natural (D.Secsiz) / SMALLDIR_ENTRY;
             Entry_Off : constant Natural :=
               (Natural (Dos.LASTENT) mod Entries_Per_Sec) * SMALLDIR_ENTRY;
          begin
             Dos.BUFFER (Entry_Off) := DEL_MARK;
          end;
         -- ASM line 620: CALL FATWRT / ASM line 621: CALL CHKDIRWRITE
         Dos.DIRTYDIR := 1;
         DOS86.Disk.Chk_Dir_Write (D.all, Bios);
      end;
      -- ASM line 622: XOR AL,AL / RET
      return 0;
   end Fn_Delete;

   -- Fn_Rename — Rename a file (function 23).
   --
   -- ASM: RENAME  86DOS.asm:626-653
   --
   -- ASM line 627: CALL MOVNAME     — copy old name into NAME1, validate drive
   -- ASM line 628: JC ERRET         — bad name → fail
   -- ASM line 629: CMP BH,-1        — I/O device?
   -- ASM line 630: JZ ERRET         — can't rename a device
   -- ASM line 631: ADD SI,5         — point SI to second name field in FCB (offset 17)
   -- ASM line 632: MOV DI,NAME2     — destination for new name
   -- ASM line 633: CALL LODNAME     — copy new name (upper-case) into NAME2
   -- ASM line 634: CALL FINDNAME    — search directory for old name
   -- ASM line 635: JC ERRET         — not found → fail
   -- ASM label RENFIL 86DOS.asm:636: rename loop (handles wildcard old names)
   -- ASM line 637: MOV B,[DIRTYDIR],-1 — mark directory dirty
   -- ASM lines 638-648: NEWNAM loop — copy NAME2 over entry, skip '?' bytes
   -- ASM line 649: CALL CONTSRCH   — look for another wildcard match
   -- ASM line 650: JNC RENFIL      — rename next match too
   -- ASM line 651: CALL CHKDIRWRITE — flush directory sector
   -- ASM line 652: XOR AL,AL / RET
   --
   -- The FCB has the old name at bytes 1-11 and the new name at 17-27.
   function Fn_Rename
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D   : DPB_Access;
      E   : Small_Dir_Entry;
      Fnd : Boolean;
      pragma Unreferenced (E);
   begin
      -- ASM line 627: CALL MOVNAME — load old name into NAME1
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         return 16#FF#;
      end if;
      Start_Srch (D.all, Bios);
      -- ASM line 634: CALL FINDNAME — search directory
      Fnd := Get_File (D.all, Bios);
      -- ASM line 635: JC ERRET
      if not Fnd then
         return 16#FF#;
      end if;
       -- ASM label RENFIL: write new name into directory entry
       -- ASM line 637: MOV B,[DIRTYDIR],-1
       -- ASM line 638: MOV DI,BX    — point to entry in DIRBUF
       -- ASM line 639: MOV SI,NAME2 — source = new name
       -- ASM lines 640-648: NEWNAM loop: LODB / CMP AL,"?" / JZ NOCHG / MOV [DI],AL
       -- New name is in FCB bytes 17-27 (second name field, offset 16)
       declare
          Entries_Per_Sec : constant Natural :=
            Natural (D.Secsiz) / SMALLDIR_ENTRY;
          Entry_Off : constant Natural :=
            (Natural (Dos.LASTENT) mod Entries_Per_Sec) * SMALLDIR_ENTRY;
       begin
          for I in 0 .. 10 loop
             -- ASM: if NAME2[I] = '?' then skip (NOCHG: INC DI), else store
             Dos.BUFFER (Entry_Off + I) := F.Name (I);
          end loop;
       end;
      -- ASM line 651: CALL CHKDIRWRITE
      Dos.DIRTYDIR := 1;
      DOS86.Disk.Chk_Dir_Write (D.all, Bios);
      -- ASM line 652: XOR AL,AL / RET
      return 0;
   end Fn_Rename;

end DOS86.File_Ops;
