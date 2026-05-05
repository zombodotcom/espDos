-- dos86-init.adb — DOS initialisation routines (body).
--
-- Translated from 86DOS.asm.  See dos86-init.ads for covered labels.

with Interfaces; use Interfaces;

package body DOS86.Init is

   -- Fig_Fat_Siz — Compute FAT size in sectors and store in D.Fatsiz.
   --
   -- ASM: FIGFATSIZ  86DOS.asm:3557-3563
   --
   -- Inputs:
   --   D.Fatcnt, D.Firfat, D.Dsksiz, D.Clusshft, D.Secsiz — DPB fields
   -- Outputs:
   --   AL = FAT size in sectors (also stored in D.Fatsiz by caller).
   --
   -- Mechanism: the iterative FAT-size calculation.  FIGFATSIZ feeds into
   -- FIGMAX (which computes MAXCLUS), and both are called multiple times
   -- until the FAT size converges (HAVFATSIZ loop, 86DOS.asm:3477-3494).
   --
   -- ASM line 3557: MUL AL,[BP+FATCNT] / ADD AX,[BP+FIRFAT]
   -- ASM line 3559: ADD AX,[SDIRSEC]   — uses small-dir sector count
   -- Falls through to FIGMAX entry point.
   --
   -- NOTE: In this translation Sdirsec is passed explicitly as a parameter
   -- because the original ASM stores it in a dedicated memory location
   -- (SDIRSEC at 86DOS.asm:3624) that is not part of the DPB record.
   --
   -- Returns the trial FAT size in sectors as a Byte.
   function Fig_Fat_Siz_Internal
     (D       : DPB;
      Sdirsec : Word) return Byte
   is
      -- ASM line 3557: MUL AL,[BP+FATCNT] — Fatsiz * Fatcnt
      AX : Word := Word (D.Fatsiz) * Word (D.Fatcnt);
   begin
      -- ASM line 3558: ADD AX,[BP+FIRFAT]
      AX := AX + D.Firfat;
      -- ASM line 3559: ADD AX,[SDIRSEC]  (small-dir sectors)
      AX := AX + Sdirsec;
      -- Falls through to FIGMAX arithmetic to get FAT size:
      -- FIGMAX: SUB AX,[BP+DSKSIZ] / NEG AX  → (Dsksiz - AX)
      -- ASM line 3565: SUB AX,[BP+DSKSIZ] / NEG AX
      AX := D.Dsksiz - AX;
      -- ASM line 3567: MOV CL,[BP+CLUSSHFT] / SHR AX,CL
      AX := Shift_Right (AX, Natural (D.Clusshft));
      -- ASM line 3569: INC AX  — MAXCLUS = AX + 1
      AX := AX + 1;
      -- ASM lines 3571-3574: compute FAT bytes needed:
      --   DX = AX / 2;  AX = AX + DX  (i.e. AX * 3 / 2, rounded up)
      --   then divide by Secsiz to get sectors.
      declare
         CX  : constant Word := AX;
         DX  : constant Word := Shift_Right (AX, 1);
         Sum : Word := AX + DX;
         -- Round up to full sectors
         Sz  : Word;
      begin
         Sum := Sum + D.Secsiz - 1;
         Sz  := Sum / D.Secsiz;
         -- ASM: return AL = FAT size in sectors
         if Sz > 255 then
            return 255;
         end if;
         pragma Unreferenced (CX);
         return Byte (Sz);
      end;
   end Fig_Fat_Siz_Internal;

   -- Fig_Fat_Siz — public wrapper that updates D.Fatsiz in place.
   --
   -- ASM: FIGFATSIZ  86DOS.asm:3557-3563
   procedure Fig_Fat_Siz (D : in out DPB) is
      -- Sdirsec: small-dir sectors = ceil(Maxent / (Secsiz/16))
      Entries_Per_Sec : constant Word := D.Secsiz / 16;
      Sdirsec         : constant Word :=
        (D.Maxent + Entries_Per_Sec - 1) / Entries_Per_Sec;
      New_Sz          : constant Byte :=
        Fig_Fat_Siz_Internal (D, Sdirsec);
   begin
      D.Fatsiz := New_Sz;
   end Fig_Fat_Siz;

   -- Fig_Max — Compute maximum cluster number from a firrec equivalent.
   --
   -- ASM: FIGMAX  86DOS.asm:3564-3584
   --
   -- Inputs:  AX equivalent = D.Firrec (first data record sector)
   -- Outputs: D.Maxclus set.
   --
   -- Mechanism:
   --   MAXCLUS = ((Dsksiz - Firrec) >> Clusshft) + 1
   -- ASM line 3564: SUB AX,[BP+DSKSIZ] / NEG AX / SHR AX,CL / INC AX
   procedure Fig_Max (D : in out DPB) is
      Diff    : Word;
      Maxclus : Word;
   begin
      -- ASM line 3565: AX = Dsksiz - Firrec
      if D.Dsksiz >= D.Firrec then
         Diff := D.Dsksiz - D.Firrec;
      else
         Diff := 0;
      end if;
      -- ASM line 3567: SHR AX,CL (CL = Clusshft)
      Maxclus := Shift_Right (Diff, Natural (D.Clusshft));
      -- ASM line 3569: INC AX
      Maxclus := Maxclus + 1;
      D.Maxclus := Maxclus;
   end Fig_Max;

   -- My_D — Parse a decimal number from Src, return in AX.
   --
   -- ASM: MYD  86DOS.asm:3586-3610
   --
   -- Inputs:
   --   Src — byte buffer containing ASCII decimal digits
   --   Max — maximum acceptable value (carry set if result = 0 or > Max)
   -- Outputs:
   --   Val — parsed number; OK=False if parse error or out of range.
   --
   -- Mechanism:
   --   Reads digits from SI, accumulates BX = BX*10 + digit.
   --   ASM line 3591: XOR BX,BX / MOV AH,0
   --   ASM line 3592: GETDIG: LODB / SUB AL,"0" / JC CHKRET
   --   ASM line 3595: CMP AL,10 / JNC CHKRET
   --   ASM lines 3596-3601: BX = BX*10 + AL
   --   ASM line 3603: CHKRET: MOV AX,BX / OR AX,AX / STC / JZ RET
   --   ASM line 3606: CMP DX,AX / RET (carry set if DX < AX)
   procedure My_D
     (Bios : in out Bios_Vtable'Class)
   is
      pragma Unreferenced (Bios);
      -- In the Ada translation the date is always accepted as 0.
      -- A real DOSINIT would call BUFIN to get the date string from the user.
      -- NOTE: differs from ASM because we have no console I/O loop here —
      -- the date is left at the power-on default (0).
   begin
      null;
   end My_D;

   -- Per_Drv — Per-drive initialisation: fill in one DPB from a DPT.
   --
   -- ASM: PERDRV  86DOS.asm:3306-3423
   --
   -- Inputs:
   --   DPT — disk parameter table byte array, format:
   --     [0..1]  Secsiz  (Word, little-endian)
   --     [2]     sectors-per-cluster (raw; CLUSMSK = this − 1)
   --     [3..4]  Firfat  (Word, little-endian) — reserved sectors
   --     [5]     Fatcnt  (Byte) — number of FAT copies
   --     [6..7]  Maxent  (Word, little-endian) — max directory entries
   --     [8..9]  Dsksiz  (Word, little-endian) — total sectors on disk
   -- Outputs:
   --   D filled; D.Fatsiz and D.Maxclus computed by Fig_Fat_Siz / Fig_Max.
   --
   -- ASM line 3311: LODW → STOW (Secsiz)
   -- ASM line 3320: LODB / DEC AL / STOB (Clusmsk = spc−1)
   -- ASM line 3321-3333: compute Clusshft = log2(spc)
   -- ASM line 3336: MOVW (Firfat)
   -- ASM line 3338: MOVB (Fatcnt)
   -- ASM line 3340: MOVW (Maxent)
   procedure Per_Drv
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class;
      DPT  : Byte_Array)
   is
      pragma Unreferenced (Bios);
      Idx : Natural := DPT'First;

      function Get_W return Word is
         V : constant Word := Word (DPT (Idx)) or
                              Shift_Left (Word (DPT (Idx + 1)), 8);
      begin
         Idx := Idx + 2;
         return V;
      end Get_W;

      function Get_B return Byte is
         V : constant Byte := DPT (Idx);
      begin
         Idx := Idx + 1;
         return V;
      end Get_B;

      SPC     : Byte;    -- sectors per cluster (raw, from DPT)
      Shift   : Byte;    -- log2(SPC)
      Tmp     : Byte;
      Sdirsec : Word;
      Trial1  : Byte;
      Trial2  : Byte;
      Current : Byte;
   begin
      -- ASM line 3311: LODW / STOW — Secsiz
      D.Secsiz := Get_W;

      -- ASM line 3320: LODB / DEC AL / STOB — Clusmsk = SPC − 1
      SPC         := Get_B;
      D.Clusmsk   := SPC - 1;

      -- ASM lines 3321-3333: compute Clusshft = log2(SPC)
      -- ASM: CBW / FIGSHFT: INC AH / SAR AL / JNZ FIGSHFT / MOV AL,AH
      Shift := 0;
      Tmp   := SPC;
      if Tmp /= 1 then
         loop
            Shift := Shift + 1;
            Tmp   := Shift_Right (Tmp, 1);
            exit when Tmp = 0 or else Tmp = 1;
         end loop;
      end if;
      D.Clusshft := Shift;

      -- ASM line 3336: MOVW — Firfat
      D.Firfat := Get_W;

      -- ASM line 3338: MOVB — Fatcnt
      D.Fatcnt := Get_B;

      -- ASM line 3340: MOVW — Maxent
      D.Maxent := Get_W;

      -- ASM line 3341: MOVW — Dsksiz (stored as temp)
      D.Dsksiz := Get_W;

      -- Compute Sdirsec = ceil(Maxent / entries-per-sector)
      -- ASM lines 3342-3349: MOV AX,SECSIZ / SHR AX,5 (entries/sec = secsiz/32)
      -- but for SMALLDIR entries/sec = secsiz / 16.
      Sdirsec := (D.Maxent + (D.Secsiz / 16) - 1) / (D.Secsiz / 16);

      -- ASM FNDFATSIZ loop (86DOS.asm:3465-3494): iterate until converged.
      D.Fatsiz := 1;
      Trial1   := 1;
      Trial2   := 1;
      loop
         Current := Fig_Fat_Siz_Internal (D, Sdirsec);
         -- ASM: CMP AL,DL / JZ HAVFATSIZ
         if Current = D.Fatsiz then
            exit;
         end if;
         -- ASM: CMP AL,DH / JNZ GETFATSIZ (continue if not oscillating)
         if Current = Trial1 then
            -- Oscillating — damp by decrementing Dsksiz
            -- ASM: DEC [BP+DSKSIZ] / JP FNDFATSIZ
            if D.Dsksiz > 0 then
               D.Dsksiz := D.Dsksiz - 1;
            end if;
            Trial1   := 1;
            Trial2   := 1;
            D.Fatsiz := 1;
         else
            Trial1   := Trial2;
            Trial2   := D.Fatsiz;
            D.Fatsiz := Current;
         end if;
      end loop;
      D.Fatsiz := Current;

      -- ASM line 3496: STOB — store Fatsiz
      -- Compute Firdir = Firfat + Fatsiz * Fatcnt
      D.Firdir := D.Firfat +
                  Word (D.Fatsiz) * Word (D.Fatcnt);

      -- Compute Firrec (SMALLDIR path):
      -- FIRREC = Firdir + ceil(Maxent*16 / Secsiz)  for 16-byte entries
      declare
         Small_Dir_Secs : constant Word := Sdirsec;
         Large_Dir_Secs : constant Word :=
           (D.Maxent + (D.Secsiz / 32) - 1) / (D.Secsiz / 32);
      begin
         D.Firrec1  := D.Firdir + Small_Dir_Secs;
         D.Firrec2  := D.Firdir + Large_Dir_Secs;
         -- Use Firrec1 (SMALLDIR) as the primary Firrec
         D.Firrec := D.Firrec1;
      end;

      -- Compute Maxclus from Firrec
      Fig_Max (D);
      D.Maxclus1 := D.Maxclus;

      -- Also compute for Firrec2 (large entries)
      declare
         Saved_Firrec : constant Word := D.Firrec;
      begin
         D.Firrec := D.Firrec2;
         Fig_Max (D);
         D.Maxclus2 := D.Maxclus;
         -- Restore primary Firrec
         D.Firrec  := Saved_Firrec;
         D.Maxclus := D.Maxclus1;
      end;

      -- ASM line 3519: STOB — Dirtyfat := 0xFF (never read)
      D.Dirtyfat := 16#FF#;
   end Per_Drv;

   -- Dos_Init — Full DOS initialisation.
   --
   -- ASM: DOSINIT  86DOS.asm:3296-3555
   --
   -- Inputs:
   --   Init_Tab — boot parameter table (one entry per drive):
   --     Byte 0    : number of drives
   --     Per drive : pointer word (skipped in Ada) + DPT bytes (10 bytes)
   -- Outputs:
   --   Dos global state initialised; all DPBs filled; CURDRVPT set to
   --   drive 0; ENDMEM set; interrupt vectors stubbed.
   --
   -- ASM line 3296: LODB — number of drives
   -- ASM line 3299: MOV BX,DRVTAB / MOV DI,MEMSTRT
   -- ASM PERDRV loop: per-drive init
   -- ASM CONTINIT: set up buffers, interrupt vectors, memory scan
   procedure Dos_Init
     (Bios     : in out Bios_Vtable'Class;
      Init_Tab : Byte_Array)
   is
      Idx    : Natural := Init_Tab'First;
      Num_Dr : Byte;
      Dr_Idx : Natural;

      function Get_B return Byte is
         V : constant Byte := Init_Tab (Idx);
      begin Idx := Idx + 1; return V; end Get_B;
   begin
      -- ASM line 3297: LODB — number of drives
      Num_Dr      := Get_B;
      Dos.NUMDRV  := Num_Dr;

      -- ASM PERDRV loop: allocate and initialise one DPB per drive
      Dr_Idx := 0;
      while Dr_Idx < Natural (Num_Dr) and then
            Dr_Idx < MAX_DRIVES
      loop
         declare
            Dp  : constant DPB_Access := new DPB;
            DPT : constant Byte_Array :=
              Init_Tab (Idx .. Idx + 9);
         begin
            Dp.Drvnum            := Byte (Dr_Idx);
            Idx                  := Idx + 10;
            Per_Drv (Dp.all, Bios, DPT);
            Dos.DRVTAB (Dr_Idx) := Dp;
         end;
         Dr_Idx := Dr_Idx + 1;
      end loop;

      -- ASM CONTINIT: set CURDRVPT to drive 0
      -- ASM line 3523: MOV AX,[DRVTAB] / MOV [CURDRVPT],AX
      if Dos.NUMDRV > 0 and then Dos.DRVTAB (0) /= null then
         Dos.CURDRVPT := Dos.DRVTAB (0);
      end if;

      -- ASM line 3530: MOV [DMAADD],80H (default DMA offset = 128)
      Dos.DMAADD := 16#80#;

      -- ASM lines 3534-3543: set up interrupt vectors (stubbed in Ada).
      -- ASM line 3555: ENDMEM — set to a nominal large value.
      -- NOTE: differs from ASM because Ada has no real-mode address space.
      Dos.ENDMEM := 16#9FFF#;

      -- Read the date (prompt the user); stubbed here — date stays at 0.
      My_D (Bios);
   end Dos_Init;

end DOS86.Init;
