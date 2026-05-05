-- dos86-syscall.adb — System call dispatcher (body).
--
-- Translated from 86DOS.asm.  See dos86-syscall.ads for covered labels.
--
-- NOTE: Ada is case-insensitive.  The local helper procedures are prefixed
-- "Do_" to avoid clashing with the FN_xxx constants from DOS86 (e.g.
-- FN_ABORT == Fn_Abort to the compiler).

with Interfaces; use Interfaces;
with DOS86.Console;
with DOS86.Disk;
with DOS86.Fat;
with DOS86.FCB_Util;
with DOS86.File_Ops;
with DOS86.IO_Ops;

package body DOS86.Syscall is

   -- Do_Version — Return DOS version (function 12).
   --
   -- ASM: VERSION  86DOS.asm:348-355
   -- Shares the same handler as GETIO/SETIO/WRTPROT/GETRDONLY/SETATTRIB/USERCODE.
   -- ASM line 355: MOV AL,0 — returns 0 in AL.
   -- NOTE: differs from ASM — returns 1 (version 1.00) rather than 0;
   --       the ASM stub returns 0 for all stubs; version reporting was done
   --       elsewhere in the real 86-DOS loader.
   function Do_Version return Byte is
   begin
      -- ASM line 355: MOV AL,0 / RET  (we return 1 = v1.00)
      return 1;
   end Do_Version;

   -- Fn_Version — public wrapper (re-exported via spec).
   function Fn_Version return Byte is
   begin
      return Do_Version;
   end Fn_Version;

   -- Do_Abort — Terminate and exit.
   --
   -- ASM: ABORT  86DOS.asm:1215-1230
   -- NOTE: differs from ASM — raises Dos_Error since we have no real-mode
   -- execution context.
   procedure Do_Abort (Bios : in out Bios_Vtable'Class) is
      pragma Unreferenced (Bios);
   begin
      raise Dos_Error with "Abort";
   end Do_Abort;

   -- Fn_Abort — public wrapper (re-exported via spec).
   procedure Fn_Abort (Bios : in out Bios_Vtable'Class) is
   begin
      Do_Abort (Bios);
   end Fn_Abort;

   -- Do_Dsk_Reset — Reset all drives (flush dirty FATs) (function 13).
   --
   -- ASM: DSKRESET  86DOS.asm:2482-2489
   procedure Do_Dsk_Reset (Bios : in out Bios_Vtable'Class) is
   begin
      -- ASM line 2484: MOV [DMAADD],80H — reset DMA offset to default (0x80)
      Dos.DMAADD := 16#80#;
      -- ASM line 2485: MOV BX,[CURDRVPT] — restore CURDRVPT to first drive
      if Dos.NUMDRV > 0 and then Dos.DRVTAB (0) /= null then
         Dos.CURDRVPT := Dos.DRVTAB (0);
      end if;
      -- ASM label WRTFATS 86DOS.asm:2486: write back all dirty FATs
      -- ASM lines 2486-2489: loop over all drives, call FATWRT if Dirtyfat set
      for I in 0 .. Natural (Dos.NUMDRV) - 1 loop
         declare
            Dp : DPB_Access := Dos.DRVTAB (I);
         begin
            -- ASM line 2487: CMP [BP+DIRTYFAT],0 / JZ next
            if Dp /= null and then Dp.Dirtyfat = 1 then
               -- ASM line 2488: CALL FATWRT — write FAT back to disk
               DOS86.Fat.Chk_Fat_Wrt (Dp.all, Bios);
            end if;
         end;
      end loop;
      -- Flush dirty sector buffer (CHKDIRWRITE equivalent)
      -- ASM: implicit via WRTFATS path calling CHKDIRWRITE after FAT write
      if Dos.DIRTYBUF /= 0 and then Dos.CURDRVPT /= null then
         DOS86.Disk.Chk_Dir_Write (Dos.CURDRVPT.all, Bios);
         Dos.DIRTYBUF := 0;
      end if;
   end Do_Dsk_Reset;

   -- Fn_Dsk_Reset — public wrapper.
   procedure Fn_Dsk_Reset (Bios : in out Bios_Vtable'Class) is
   begin
      Do_Dsk_Reset (Bios);
   end Fn_Dsk_Reset;

    -- Dispatch — Call one DOS function by number.
    --
    -- ASM: COMMAND  86DOS.asm:199-204  (interrupt entry point, CMP AH,MAXCOM)
    -- ASM: ENTRY    86DOS.asm:206-217  (CALL 5 entry; POP AX×2, POP [TEMP], CMP CL,MAXCALL)
    -- ASM: SAVREGS  86DOS.asm:219-228  (PUSH ES/DS/BP/DI/SI/DX/CX/BX/AX)
    -- ASM: DISPATCH 86DOS.asm:302-346  (DW table, 42 entries 0–41)
    -- ASM: LEAVE    86DOS.asm:271-300  (DI; restore SP/SS; MOV [BP+AXSAVE],AL; POP×9; IRET)
    --
    -- NOTE: differs from ASM — Ada raises Dos_Error for BADCALL instead of
    -- executing a bare IRET; the save/restore register frame is handled by
    -- Ada's normal calling convention rather than PUSH/POP sequences.
    --
    -- Inputs:
    --   Func  — function number 0–41 (≡ AH after SAVREGS)
    --   F     — pointer to user FCB (may be null for console/misc functions)
    --   AX,BX,CX — register-parameter words
    --   Bios  — BIOS interface
    -- Outputs:
    --   AL result byte (written back to [BP+AXSAVE] in real ASM at line 278).
    --
    -- Numeric case choices used (instead of FN_xxx constants) to avoid
    -- name clashes with Ada's case-insensitive identifier matching.
    -- ASM dispatch table: 86DOS.asm:302-346 (DW entries 0–41).
    function Dispatch
      (Func : Byte;
       F    : FCB_Access;
       AX   : Word;
       BX   : Word;
       CX   : Word;
       Bios : in out Bios_Vtable'Class) return Byte
    is
       pragma Unreferenced (BX, CX);
       AH_Val : constant Byte := Byte (Shift_Right (AX, 8) and 16#FF#);
       AL_Val : constant Byte := Byte (AX and 16#FF#);
    begin
       -- ASM line 256: MOV [FUNC],AH  — store function number in DOS data
       Dos.FUNC := Func;
       -- ASM lines 257-270: MOV BL,AH / MOV BH,0 / SHL BX / CALL [BX+DISPATCH]
       -- In Ada: case statement indexes directly into the dispatch table.

       case Natural (Func) is

          -- ASM line 304: DW ABORT — fn 0
          when 0 =>
             Do_Abort (Bios);
             return 0;

          -- ASM line 305: DW CONIN — fn 1
          when 1 =>
             return DOS86.Console.Fn_Con_In (Bios);

          -- ASM line 306: DW CONOUT — fn 2
          when 2 =>
             DOS86.Console.Fn_Con_Out (Bios, AL_Val);
             return 0;

          -- ASM line 307: DW READER — fn 3
          when 3 =>
             return Bios.Aux_In;

          -- ASM line 308: DW PUNCH — fn 4
          when 4 =>
             Bios.Aux_Out (AL_Val);
             return 0;

          -- ASM line 309: DW LIST — fn 5
          when 5 =>
             DOS86.Console.Fn_List (Bios, AL_Val);
             return 0;

          -- ASM line 310: DW RAWIO — fn 6
          when 6 =>
             return DOS86.Console.Fn_Raw_IO (Bios, AL_Val);

          -- ASM line 311: DW RAWINP — fn 7
          when 7 =>
             return DOS86.Console.Fn_Raw_Inp (Bios);

          -- ASM line 312: DW IN — fn 8
          when 8 =>
             return DOS86.Console.Fn_In (Bios);

          -- ASM line 313: DW PRTBUF — fn 9
          when 9 =>
             DOS86.Console.Fn_Prt_Buf (Bios, Dos.INBUF);
             return 0;

          -- ASM line 314: DW BUFIN — fn 10
          when 10 =>
             DOS86.Console.Fn_Buf_In (Bios, Dos.INBUF);
             return 0;

          -- ASM line 315: DW CONSTAT — fn 11
          when 11 =>
             return DOS86.Console.Fn_Con_Stat (Bios);

          -- ASM line 316: DW VERSION — fn 12
          when 12 =>
             return Do_Version;

          -- ASM line 317: DW DSKRESET — fn 13
          when 13 =>
             Do_Dsk_Reset (Bios);
             return 0;

          -- ASM line 318: DW SELDSK — fn 14
          when 14 =>
             return DOS86.FCB_Util.Fn_Sel_Dsk (AL_Val);

          -- ASM line 319: DW OPEN — fn 15
          when 15 =>
             if F = null then return 16#FF#; end if;
             return DOS86.File_Ops.Fn_Open (F.all, Bios);

          -- ASM line 320: DW CLOSE — fn 16
          when 16 =>
             if F = null then return 16#FF#; end if;
             return DOS86.File_Ops.Fn_Close (F.all, Bios);

          -- ASM line 321: DW SRCHFRST — fn 17
          when 17 =>
             if F = null then return 16#FF#; end if;
             return DOS86.FCB_Util.Fn_Srch_Frst (F.all, Bios);

          -- ASM line 322: DW SRCHNXT — fn 18
          when 18 =>
             if F = null then return 16#FF#; end if;
             return DOS86.FCB_Util.Fn_Srch_Nxt (F.all, Bios);

          -- ASM line 323: DW DELETE — fn 19
          when 19 =>
             if F = null then return 16#FF#; end if;
             return DOS86.File_Ops.Fn_Delete (F.all, Bios);

          -- ASM line 324: DW SEQRD — fn 20
          when 20 =>
             if F = null then return 16#FF#; end if;
             return DOS86.IO_Ops.Fn_Seq_Rd (F.all, Bios);

          -- ASM line 325: DW SEQWRT — fn 21
          when 21 =>
             if F = null then return 16#FF#; end if;
             return DOS86.IO_Ops.Fn_Seq_Wrt (F.all, Bios);

          -- ASM line 326: DW CREATE — fn 22
          when 22 =>
             if F = null then return 16#FF#; end if;
             return DOS86.File_Ops.Fn_Create (F.all, Bios);

          -- ASM line 327: DW RENAME — fn 23
          when 23 =>
             if F = null then return 16#FF#; end if;
             return DOS86.File_Ops.Fn_Rename (F.all, Bios);

          -- ASM line 328: DW INUSE — fn 24
          when 24 =>
             return DOS86.FCB_Util.Fn_Inuse;

          -- ASM line 329: DW CURDRV — fn 25
          when 25 =>
             return DOS86.FCB_Util.Fn_Cur_Drv;

          -- ASM line 330: DW SETDMA — fn 26
          when 26 =>
             DOS86.FCB_Util.Fn_Set_DMA (Word (AH_Val), Word (AL_Val));
             return 0;

          -- ASM line 331: DW GETFATPT — fn 27
          when 27 =>
             declare
                Dp : constant DPB_Access := DOS86.FCB_Util.Fn_Get_Fat_Pt;
                pragma Unreferenced (Dp);
             begin
                return 0;
             end;

          -- ASM line 332: DW WRTPROT — fn 28 (stub; shares VERSION handler)
          when 28 =>
             return 0;

          -- ASM line 333: DW GETRDONLY — fn 29 (stub; shares VERSION handler)
          when 29 =>
             return 0;

          -- ASM line 334: DW SETATTRIB — fn 30 (stub; shares VERSION handler)
          when 30 =>
             return 0;

          -- ASM line 335: DW GETDSKPT — fn 31
          when 31 =>
             declare
                Dp : constant DPB_Access :=
                  DOS86.FCB_Util.Fn_Get_Dsk_Pt (AL_Val);
                pragma Unreferenced (Dp);
             begin
                return 0;
             end;

          -- ASM line 336: DW USERCODE — fn 32 (stub; shares VERSION handler)
          when 32 =>
             return 0;

          -- ASM line 337: DW RNDRD — fn 33
          when 33 =>
             if F = null then return 16#FF#; end if;
             return DOS86.IO_Ops.Fn_Rnd_Rd (F.all, Bios);

          -- ASM line 338: DW RNDWRT — fn 34
          when 34 =>
             if F = null then return 16#FF#; end if;
             return DOS86.IO_Ops.Fn_Rnd_Wrt (F.all, Bios);

          -- ASM line 339: DW FILESIZE — fn 35
          when 35 =>
             if F = null then return 16#FF#; end if;
             DOS86.IO_Ops.Fn_File_Size (F.all, Bios);
             return 0;

          -- ASM line 340: DW SETRNDREC — fn 36
          when 36 =>
             if F = null then return 16#FF#; end if;
             DOS86.FCB_Util.Fn_Set_Rnd_Rec (F.all);
             return 0;

          -- ASM line 342: DW SETVECT — fn 37 (extended function)
          when 37 =>
             DOS86.FCB_Util.Fn_Set_Vect (AL_Val, Word (AH_Val), 0);
             return 0;

          -- ASM line 343: DW NEWBASE — fn 38 (extended function)
          when 38 =>
             DOS86.FCB_Util.Fn_New_Base (Word (AH_Val));
             return 0;

          -- ASM line 344: DW BLKRD — fn 39 (extended function)
          when 39 =>
             if F = null then return 16#FF#; end if;
             declare
                CX_Out : Word;
             begin
                return DOS86.IO_Ops.Fn_Blk_Rd (F.all, AX, CX_Out, Bios);
             end;

          -- ASM line 345: DW BLKWRT — fn 40 (extended function)
          when 40 =>
             if F = null then return 16#FF#; end if;
             declare
                CX_Out : Word;
             begin
                return DOS86.IO_Ops.Fn_Blk_Wrt (F.all, AX, CX_Out, Bios);
             end;

          -- ASM line 346: DW MAKEFCB — fn 41 (extended function)
          when 41 =>
             if F = null then return 16#FF#; end if;
             return DOS86.FCB_Util.Fn_Make_FCB (Dos.INBUF, F.all, AL_Val);

          -- ASM label BADCALL 86DOS.asm:202-204: MOV AL,0 / IRET
          -- NOTE: differs from ASM — Ada raises Dos_Error instead of silent IRET
          when others =>
             raise Dos_Error with ERR_BAD_CALL;
       end case;
       -- ASM label LEAVE 86DOS.asm:271: result in AL written back via [BP+AXSAVE]
    end Dispatch;

end DOS86.Syscall;
