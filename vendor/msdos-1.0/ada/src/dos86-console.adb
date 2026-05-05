-- dos86-console.adb — Console and character I/O (body).
--
-- Translated from 86DOS.asm.  See dos86-console.ads for covered labels.

with Interfaces; use Interfaces;

package body DOS86.Console is

   -- Con_Out — Output one character, honouring PFLAG printer echo.
   --
   -- ASM: OUTCH  86DOS.asm:2855-2966
   --
   -- ASM line 2859: TEST [PFLAG],1 / JZ NOPRT — echo to printer if PFLAG set
   -- ASM line 2862: CALL BIOSOUT,BIOSSEG
   procedure Con_Out (Bios : in out Bios_Vtable'Class; Ch : Byte) is
   begin
      -- ASM line 2862: CALL BIOSOUT,BIOSSEG
      Output (Bios, Ch);
      -- ASM line 2859: TEST [PFLAG],1 / JZ NOPRT
      if Dos.PFLAG /= 0 then
         Print (Bios, Ch);
      end if;
   end Con_Out;

   -- Con_Crlf — Emit carriage-return + line-feed.
   --
   -- ASM: CRLF  86DOS.asm:2652-2656
   procedure Con_Crlf (Bios : in out Bios_Vtable'Class) is
   begin
      -- ASM: MOV AL,0DH / CALL OUT / MOV AL,0AH / CALL OUT
      Con_Out (Bios, 16#0D#);
      Con_Out (Bios, 16#0A#);
   end Con_Crlf;

   -- Fn_Con_In — Console input with echo (function 1).
   --
   -- ASM: CONIN  86DOS.asm:2975-2980
   function Fn_Con_In (Bios : in out Bios_Vtable'Class) return Byte is
      Ch : Byte;
   begin
      Ch := Input (Bios);
      Con_Out (Bios, Ch);
      return Ch;
   end Fn_Con_In;

   -- Fn_Con_Out — Console output (function 2).
   --
   -- ASM: CONOUT  86DOS.asm:2853-2855
   procedure Fn_Con_Out (Bios : in out Bios_Vtable'Class; Ch : Byte) is
   begin
      Con_Out (Bios, Ch);
   end Fn_Con_Out;

   -- Fn_Con_Stat — Console status (function 11).
   --
   -- ASM: CONSTAT  86DOS.asm:2968-2972
   function Fn_Con_Stat (Bios : in out Bios_Vtable'Class) return Byte is
      S : Byte;
   begin
      S := Stat (Bios);
      if S /= 0 then
         return 16#FF#;
      else
         return 0;
      end if;
   end Fn_Con_Stat;

   -- Fn_Raw_IO — Raw I/O (function 6).
   --
   -- ASM: RAWIO  86DOS.asm:2988-2999
   -- DL = 16#FF# → input (non-blocking), else output DL.
   function Fn_Raw_IO
     (Bios : in out Bios_Vtable'Class;
      DL   : Byte) return Byte
   is
   begin
      if DL = 16#FF# then
         -- Check status first
         if Stat (Bios) = 0 then
            return 0;  -- no character ready
         end if;
         return Input (Bios);
      else
         Output (Bios, DL);
         return DL;
      end if;
   end Fn_Raw_IO;

   -- Fn_Raw_Inp — Raw input, no echo (function 7).
   --
   -- ASM: RAWINP  86DOS.asm:2988-2999
   function Fn_Raw_Inp (Bios : in out Bios_Vtable'Class) return Byte is
   begin
      return Input (Bios);
   end Fn_Raw_Inp;

   -- Fn_In — Console input, no echo (function 8).
   --
   -- ASM: IN  86DOS.asm:2983-2986
   function Fn_In (Bios : in out Bios_Vtable'Class) return Byte is
   begin
      return Input (Bios);
   end Fn_In;

   -- Fn_List — Printer (list) output (function 5).
   --
   -- ASM: LIST  86DOS.asm:3001-3004
   procedure Fn_List (Bios : in out Bios_Vtable'Class; Ch : Byte) is
   begin
      Print (Bios, Ch);
   end Fn_List;

   -- Fn_Prt_Buf — Print a '$'-terminated string (function 9).
   --
   -- ASM: PRTBUF  86DOS.asm:3006-3013
   procedure Fn_Prt_Buf (Bios : in out Bios_Vtable'Class; Msg : Byte_Array) is
      Dollar : constant Byte := Character'Pos ('$');
   begin
      for I in Msg'Range loop
         exit when Msg (I) = Dollar;
         Con_Out (Bios, Msg (I));
      end loop;
   end Fn_Prt_Buf;

   -- Fn_Buf_In — Buffered line input (function 10).
   --
   -- ASM: BUFIN  86DOS.asm:2566-2651
   --
   -- Buf(0) = max chars to accept; Buf(1) set to actual count on return.
   -- Characters are stored starting at Buf(2).
   procedure Fn_Buf_In (Bios : in out Bios_Vtable'Class; Buf : in out Byte_Array) is
      Max   : constant Natural := Natural (Buf (Buf'First));
      Count : Natural := 0;
      Ch    : Byte;
      CR    : constant Byte := 16#0D#;
      BS    : constant Byte := 16#08#;
      Space : constant Byte := Character'Pos (' ');
   begin
      loop
         Ch := Input (Bios);
         Con_Out (Bios, Ch);
         if Ch = CR then
            exit;
         elsif Ch = BS and then Count > 0 then
            -- Backspace: erase one character
            Con_Out (Bios, Space);
            Con_Out (Bios, BS);
            Count := Count - 1;
         elsif Count < Max then
            Buf (Buf'First + 2 + Count) := Ch;
            Count := Count + 1;
         end if;
      end loop;
      Buf (Buf'First + 1) := Byte (Count);
   end Fn_Buf_In;

   -- Out_Mes — Print a '$'-terminated message from an Ada string.
   --
   -- ASM: OUTMES  86DOS.asm:3015-3021
   procedure Out_Mes (Bios : in out Bios_Vtable'Class; Msg : String) is
   begin
      for Ch of Msg loop
         exit when Ch = '$';
         Output (Bios, Byte (Character'Pos (Ch)));
      end loop;
   end Out_Mes;

end DOS86.Console;
