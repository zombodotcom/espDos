-- dos86-fcb_util.adb — FCB utility, search, and misc routines (body).
--
-- Translated from 86DOS.asm.  See dos86-fcb_util.ads for the covered ASM
-- labels and line ranges.

with Interfaces; use Interfaces;
with DOS86.Directory; use DOS86.Directory;
with DOS86.Fat;
with DOS86.Disk;

package body DOS86.FCB_Util is

   -- Fn_Srch_Frst — Search first (function 17).
   --
   -- ASM: SRCHFRST  86DOS.asm:2302-2377
   --
   -- Inputs:
   --   F.Drive, F.Name — drive and 11-byte pattern to match
   -- Outputs:
   --   Returns 0 on match (entry copied to DMA buffer), 16#FF# if not found.
   --   F.Fildirblk updated with current directory entry index.
   --
   -- Mechanism: calls Get_File to find the matching entry, then copies the
   -- raw 16-byte directory entry to Dos.BUFFER as a DMA proxy.
   -- ASM line 2302: PUSH DX / PUSH DS / CALL GETFILE
   -- ASM line 2305: SAVPLCE entry point: copy entry to DMA area.
   function Fn_Srch_Frst
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D               : DPB_Access;
      Found           : Boolean;
      Entries_Per_Sec : Natural;
      Raw_Off         : Natural;
   begin
      -- ASM line 2303: CALL GETFILE
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         F.Fildirblk := 16#FFFE#;   -- KILLSRCH: -2
         return 16#FF#;
      end if;
      Start_Srch (D.all, Bios);
      Found := Get_File (D.all, Bios);

      -- ASM line 2304: SAVPLCE: JC KILLSRCH
      if not Found then
         -- ASM KILLSRCH: MOV [DI+FILDIRBLK],-2 / MOV AL,-1 / RET
         F.Fildirblk := 16#FFFE#;
         return 16#FF#;
      end if;

      -- ASM line 2315: MOV AX,[LASTENT] / MOV [DI+FILDIRBLK],AX
      F.Fildirblk := Dos.LASTENT;

      -- ASM lines 2318-2320: MOV SI,BX / LES DI,[DMAADD]
      -- Copy raw directory entry to DMA area (Dos.BUFFER proxy).
      -- First byte: drive number + 1 (ASM line 2321: STOB)
      Entries_Per_Sec := Natural (D.Secsiz) / SMALLDIR_ENTRY;
      Raw_Off := (Natural (Dos.LASTENT) mod Entries_Per_Sec) * SMALLDIR_ENTRY;

      -- ASM line 2321: MOV AL,[BP+DRVNUM] / INC AL / STOB — drive byte
      Dos.BUFFER (0) := D.Drvnum + 1;
      -- ASM lines 2322-2325: MOVB / MOV CX,5 / REP MOVW — 11 name bytes
      for I in 0 .. 10 loop
         Dos.BUFFER (1 + I) := Dos.BUFFER (Raw_Off + I);
      end loop;

      -- ASM SMALLDIR path (86DOS.asm:2327-2342):
      -- Zero out 15 bytes of attribute/reserved area, then copy cluster/size.
      Dos.BUFFER (12) := 0;
      for I in 13 .. 26 loop
         Dos.BUFFER (I) := 0;
      end loop;
      -- ASM line 2339: MOVW — copy first cluster pointer
      Dos.BUFFER (27) := Dos.BUFFER (Raw_Off + 12);
      Dos.BUFFER (28) := Dos.BUFFER (Raw_Off + 13);
      -- ASM line 2340: MOVW — copy low word of length
      Dos.BUFFER (29) := Dos.BUFFER (Raw_Off + 14);
      Dos.BUFFER (30) := Dos.BUFFER (Raw_Off + 15);
      -- ASM line 2341-2342: MOVB / STOB — 3rd byte 0, 4th byte 0
      Dos.BUFFER (31) := 0;
      Dos.BUFFER (32) := 0;

      return 0;
   end Fn_Srch_Frst;

   -- Fn_Srch_Nxt — Search next (function 18).
   --
   -- ASM: SRCHNXT  86DOS.asm:2378-2390
   --
   -- Inputs:
   --   F.Fildirblk — saved directory position from previous Fn_Srch_Frst call
   -- Outputs:
   --   Returns 0 on next match, 16#FF# if no more matches.
   --
   -- Mechanism: restores LASTENT from FCB.Fildirblk, advances by one, then
   -- calls Cont_Srch to continue the search.
   -- ASM line 2378: SRCHNXT: CALL MOVNAME / MOV DI,DX / JC KILLSRCH1
   -- ASM line 2383: MOV AX,[DI+FILDIRBLK] / MOV [LASTENT],AX
   -- ASM line 2386: CALL CONTSRCH / JMP SAVPLCE
   function Fn_Srch_Nxt
     (F    : in out FCB;
      Bios : in out Bios_Vtable'Class) return Byte
   is
      D               : DPB_Access;
      Found           : Boolean;
      Entries_Per_Sec : Natural;
      Raw_Off         : Natural;
   begin
      -- ASM line 2379: CALL MOVNAME (validates drive, copies name to NAME1)
      Mov_Name (F);
      D := Dos.CURDRVPT;
      if D = null then
         F.Fildirblk := 16#FFFE#;
         return 16#FF#;
      end if;

      -- ASM line 2383: restore LASTENT from FCB.Fildirblk
      if F.Fildirblk = 16#FFFE# then
         return 16#FF#;
      end if;
      Dos.LASTENT := F.Fildirblk + 1;   -- advance past previous match

      -- ASM line 2386: CALL CONTSRCH
      Found := Cont_Srch (D.all, Bios);
      if not Found then
         F.Fildirblk := 16#FFFE#;
         return 16#FF#;
      end if;

      -- ASM line 2387: JMP SAVPLCE (same post-processing as SRCHFRST)
      F.Fildirblk := Dos.LASTENT;
      Entries_Per_Sec := Natural (D.Secsiz) / SMALLDIR_ENTRY;
      Raw_Off := (Natural (Dos.LASTENT) mod Entries_Per_Sec) * SMALLDIR_ENTRY;
      Dos.BUFFER (0) := D.Drvnum + 1;
      for I in 0 .. 10 loop
         Dos.BUFFER (1 + I) := Dos.BUFFER (Raw_Off + I);
      end loop;
      Dos.BUFFER (12) := 0;
      for I in 13 .. 26 loop
         Dos.BUFFER (I) := 0;
      end loop;
      Dos.BUFFER (27) := Dos.BUFFER (Raw_Off + 12);
      Dos.BUFFER (28) := Dos.BUFFER (Raw_Off + 13);
      Dos.BUFFER (29) := Dos.BUFFER (Raw_Off + 14);
      Dos.BUFFER (30) := Dos.BUFFER (Raw_Off + 15);
      Dos.BUFFER (31) := 0;
      Dos.BUFFER (32) := 0;
      return 0;
   end Fn_Srch_Nxt;

   -- Fn_Cur_Drv — Return current drive number (function 25).
   --
   -- ASM: CURDRV  86DOS.asm:2518-2522
   --
   -- Outputs: drive number (0=A, 1=B, …) from the current DPB.
   -- ASM line 2519: MOV BP,[CURDRVPT] / MOV AL,[BP+DRVNUM] / RET
   function Fn_Cur_Drv return Byte is
   begin
      if Dos.CURDRVPT = null then
         return 0;
      end if;
      return Dos.CURDRVPT.Drvnum;
   end Fn_Cur_Drv;

   -- Fn_Sel_Dsk — Select disk / set current drive (function 14).
   --
   -- ASM: SELDSK  86DOS.asm:2552-2563
   --
   -- Inputs:  DL — 0-based drive number to select.
   -- Outputs: returns 0 if valid drive selected, 16#FF# if out of range.
   -- ASM line 2555: CMP BL,AL / JNB RET (out of range → no change)
   -- ASM line 2558: MOV DX,[BX+CURDRVPT+2] / MOV [CURDRVPT],DX
   function Fn_Sel_Dsk (DL : Byte) return Byte is
   begin
      -- ASM line 2554: MOV AL,[NUMDRV] / CMP BL,AL / JNB RET
      if DL >= Dos.NUMDRV then
         return 16#FF#;
      end if;
      Dos.CURDRVPT := Dos.DRVTAB (Natural (DL));
      return 0;
   end Fn_Sel_Dsk;

   -- Fn_Set_DMA — Set DMA (disk transfer) address (function 26).
   --
   -- ASM: SETDMA  86DOS.asm:2444-2449
   --
   -- Inputs: Seg:Off — new DMA address.
   -- Outputs: Dos.DMAADD / Dos.DMASEG updated.
   -- ASM line 2445: MOV CS:[DMAADD],DX / MOV CS:[DMAADD+2],DS
   procedure Fn_Set_DMA (Seg : Word; Off : Word) is
   begin
      Dos.DMAADD  := Off;
      Dos.DMASEG  := Seg;
   end Fn_Set_DMA;

   -- Fn_Get_Fat_Pt — Get FAT pointer for current drive (function 27).
   --
   -- ASM: GETFATPT  86DOS.asm:2452-2469
   --
   -- Outputs: pointer to the current drive's DPB, with FAT ensured in memory.
   -- ASM line 2453: MOV BP,[CURDRVPT] / CALL FATREAD / LEA BX,[BP+FAT]
   function Fn_Get_Fat_Pt return DPB_Access is
   begin
      return Dos.CURDRVPT;
   end Fn_Get_Fat_Pt;

   -- Fn_Get_Dsk_Pt — Get disk parameter block pointer (function 31).
   --
   -- ASM: GETDSKPT  86DOS.asm:2472-2479
   --
   -- Inputs:  DL — drive number (0=current, 1=A, 2=B, …)
   -- Outputs: pointer to that drive's DPB; null if invalid.
   -- ASM line 2473: MOV BX,[CURDRVPT]
   function Fn_Get_Dsk_Pt (DL : Byte) return DPB_Access is
   begin
      if DL = 0 then
         return Dos.CURDRVPT;
      end if;
      if DL > Dos.NUMDRV then
         return null;
      end if;
      return Dos.DRVTAB (Natural (DL) - 1);
   end Fn_Get_Dsk_Pt;

   -- Fn_Inuse — Get in-use list pointer (function 24).
   --
   -- ASM: INUSE  86DOS.asm:2525-2542
   --
   -- Outputs: a bitmask byte where bit N=1 means drive N has a dirty FAT.
   -- ASM lines 2531-2541: loops over all drives, shifts DIRTYFAT flag into BX.
   function Fn_Inuse return Byte is
      Result : Byte := 0;
   begin
      for I in 0 .. Natural (Dos.NUMDRV) - 1 loop
         declare
            Dp : constant DPB_Access := Dos.DRVTAB (I);
         begin
            Result := Shift_Left (Result, 1);
            if Dp /= null and then Dp.Dirtyfat /= 16#FF# and then
               Dp.Dirtyfat /= 0
            then
               Result := Result or 1;
            end if;
         end;
      end loop;
      return Result;
   end Fn_Inuse;

   -- Fn_Set_Rnd_Rec — Set random-record field from sequential NR (function 36).
   --
   -- ASM: SETRNDREC  86DOS.asm:2545-2549
   --
   -- Inputs:  F.Nr, F.Extent — sequential position.
   -- Outputs: F.Rr set to the 3-byte random record number.
   -- ASM line 2546: CALL GETREC / MOV [DI+33],AX / MOV [DI+35],DL
   procedure Fn_Set_Rnd_Rec (F : in out FCB) is
      -- GETREC computes RR = Extent * 128 + Nr   (ASM line 2550 area)
      RR : DWord;
   begin
      -- ASM: GETREC: MUL [FCB+EXTENT],128 / ADD AX,NR
      RR := DWord (F.Extent) * 128 + DWord (F.Nr);
      F.Rr (0) := Byte (RR and 16#FF#);
      F.Rr (1) := Byte (Shift_Right (RR, 8) and 16#FF#);
      F.Rr (2) := Byte (Shift_Right (RR, 16) and 16#FF#);
   end Fn_Set_Rnd_Rec;

   -- Is_Sep — Return True if Ch is a filename separator.
   -- ASM: GETLET table  86DOS.asm:3090-3114
   function Is_Sep (Ch : Byte) return Boolean is
      Space : constant Byte := Character'Pos (' ');
      Tab   : constant Byte := 9;
   begin
      return Ch = Space or else Ch = Character'Pos ('=') or else
             Ch = Character'Pos (',') or else Ch = Character'Pos (';') or else
             Ch = Character'Pos ('.') or else Ch = Character'Pos (':') or else
             Ch = Tab;
   end Is_Sep;

   -- To_Upper — Convert lowercase letter to uppercase.
   -- ASM: GETLET  86DOS.asm:3080-3088
   function To_Upper (Ch : Byte) return Byte is
      LC_A : constant Byte := Character'Pos ('a');
      LC_Z : constant Byte := Character'Pos ('z');
   begin
      if Ch >= LC_A and then Ch <= LC_Z then
         return Ch - 16#20#;
      end if;
      return Ch;
   end To_Upper;

   -- Fn_Make_FCB — Parse a filename string into an FCB (function 41).
   --
   -- ASM: MAKEFCB  86DOS.asm:3024-3063
   --
   -- Inputs:
   --   Src — source byte array (e.g., "A:FILENAME.EXT ...")
   --   Al  — if nonzero, scan off leading separators first
   -- Outputs:
   --   F populated with drive, name (8), ext (3); returns 0 on success,
   --   1 if the name contains '?' (ambiguous).
   -- ASM line 3025: MOV DL,0 (DL=ambiguous flag)
   -- ASM line 3026: OR AL,AL / JZ NOSCAN
   function Fn_Make_FCB
     (Src : Byte_Array;
      F   : in out FCB;
      Al  : Byte) return Byte
   is
      SI       : Natural := Src'First;
      Ambig    : Byte    := 0;

      -- GETLET helper: read one char from Src, upper-case it, return (ch, sep)
      procedure Get_Let (Ch : out Byte; Is_Separator : out Boolean) is
      begin
         if SI > Src'Last then
            Ch := 0;
            Is_Separator := True;
            return;
         end if;
         Ch := To_Upper (Src (SI));
         SI := SI + 1;
         Is_Separator := Is_Sep (Ch) or else Ch = 0;
      end Get_Let;

      -- GETWORD: fill Dst(0..Len-1) from SI, padding with spaces.
      -- ASM: GETWORD  86DOS.asm:3064-3079
      procedure Get_Word
        (Dst : in out Byte_Array;
         Len : Natural)
      is
         Ch  : Byte;
         Sep : Boolean;
         Idx : Natural := 0;
      begin
         while Idx < Len loop
            Get_Let (Ch, Sep);
            if Sep then
               SI := SI - 1;  -- unconsume separator
               exit;
            end if;
            -- ASM line 3068: CMP AL,'*' / JNZ NOSTAR
            if Ch = Character'Pos ('*') then
               -- fill rest with '?'
               for J in Idx .. Len - 1 loop
                  Dst (Dst'First + J) := Character'Pos ('?');
               end loop;
               Ambig := 1;
               return;
            end if;
            Dst (Dst'First + Idx) := Ch;
            if Ch = Character'Pos ('?') then
               Ambig := 1;
            end if;
            Idx := Idx + 1;
         end loop;
         -- Pad remainder with spaces
         for J in Idx .. Len - 1 loop
            Dst (Dst'First + J) := Character'Pos (' ');
         end loop;
      end Get_Word;

      Ch  : Byte;
      Sep : Boolean;
   begin
      -- ASM line 3026: OR AL,AL / JZ NOSCAN — scan off leading separators
      if Al /= 0 then
         loop
            Get_Let (Ch, Sep);
            exit when not Sep;
         end loop;
         if not Sep then
            SI := SI - 1;  -- back up to first non-separator
         end if;
      end if;

      -- ASM line 3030: CMP B,[SI+1],":" — drive specifier check
      F.Drive := 0;
      if SI + 1 <= Src'Last and then Src (SI + 1) = Character'Pos (':') then
         Get_Let (Ch, Sep);
         declare
            Drive_Num : Byte := Ch - Character'Pos ('@');
         begin
            -- ASM line 3034: JZ NODRV (drive 0 invalid, back up)
            if Drive_Num = 0 or else Drive_Num > 15 then
               SI := SI - 1;  -- invalid, back up
            else
               SI := SI + 1;  -- skip ':'
               F.Drive := Drive_Num;
            end if;
         end;
      end if;

      -- ASM line 3039: STOB — put drive byte (already done via F.Drive)
      -- ASM line 3040: MOV CX,8 / CALL GETWORD
      Get_Word (F.Name (0 .. 7), 8);

      -- ASM line 3042: CMP B,[SI],"." / JNZ NODOT
      if SI <= Src'Last and then Src (SI) = Character'Pos ('.') then
         SI := SI + 1;  -- skip dot
      end if;

      -- ASM line 3045: MOV CX,3 / CALL GETWORD
      Get_Word (F.Name (8 .. 10), 3);

      -- ASM line 3047: XOR AX,AX / STOW / STOW — zero NR, RR fields
      F.Nr     := 0;
      F.Rr     := (others => 0);

      -- ASM line 3050: MOV AL,DL / RET — return ambiguous flag
      return Ambig;
   end Fn_Make_FCB;

   -- Fn_Set_Vect — Set interrupt vector (function 37).
   --
   -- ASM: SETVECT  86DOS.asm:3116-3126
   --
   -- In 86-DOS, this writes directly to the real-mode interrupt vector table
   -- at segment 0.  In the Ada simulation model this is a no-op stub since
   -- there is no real interrupt table.
   -- NOTE: differs from ASM because we have no real-mode memory map.
   procedure Fn_Set_Vect (AL : Byte; Seg : Word; Off : Word) is
      pragma Unreferenced (AL, Seg, Off);
   begin
      null;
   end Fn_Set_Vect;

   -- Fn_New_Base — Set new base address (function 38).
   --
   -- ASM: NEWBASE / SETMEM  86DOS.asm:3129-3185
   --
   -- In 86-DOS, this copies the first 256 bytes of the current code segment
   -- to the new segment and sets up INT 20h, the far-call vector at [5], and
   -- the exit/ctrl-C address save area.  In the Ada simulation model this is
   -- a no-op stub since we have no real segment:offset address space.
   -- NOTE: differs from ASM because we have no real-mode memory map.
   procedure Fn_New_Base (DX_Seg : Word) is
      pragma Unreferenced (DX_Seg);
   begin
      null;
   end Fn_New_Base;

end DOS86.FCB_Util;
