-- test_io.adb — Unit tests for DOS86.IO_Ops (Get_Rec, Fn_Set_Rnd_Rec,
--               Fn_Set_DMA).
--
-- Tests derived from 86DOS.asm:2146-2167 and 2545-2549.
-- No AUnit dependency; pass/fail is printed to standard output.
-- Exit status 0 on all pass, 1 on any failure.

with Ada.Text_IO;    use Ada.Text_IO;
with Ada.Command_Line;
with Interfaces;     use Interfaces;
with DOS86;          use DOS86;
with DOS86.IO_Ops;

procedure Test_IO is

   Fails : Natural := 0;

   procedure Check_DWord (Name : String; Got, Expect : DWord) is
   begin
      if Got = Expect then
         Put_Line ("PASS  " & Name);
      else
         Put_Line ("FAIL  " & Name
                   & "  got=" & DWord'Image (Got)
                   & "  expected=" & DWord'Image (Expect));
         Fails := Fails + 1;
      end if;
   end Check_DWord;

   procedure Check_Byte (Name : String; Got, Expect : Byte) is
   begin
      if Got = Expect then
         Put_Line ("PASS  " & Name);
      else
         Put_Line ("FAIL  " & Name
                   & "  got=" & Byte'Image (Got)
                   & "  expected=" & Byte'Image (Expect));
         Fails := Fails + 1;
      end if;
   end Check_Byte;

   procedure Check_Word (Name : String; Got, Expect : Word) is
   begin
      if Got = Expect then
         Put_Line ("PASS  " & Name);
      else
         Put_Line ("FAIL  " & Name
                   & "  got=" & Word'Image (Got)
                   & "  expected=" & Word'Image (Expect));
         Fails := Fails + 1;
      end if;
   end Check_Word;

begin
   -- ── Get_Rec ───────────────────────────────────────────────────────────────
   --
   -- ASM: GETREC  86DOS.asm:2146-2167
   --
   -- Get_Rec returns a 32-bit record number from FCB.Extent and FCB.Nr.
   -- Formula: rec32 = Extent * (65536 / Recsiz) + Nr
   --
   -- With Recsiz = 128 (default), Records_Per_Extent = 512:
   --   Get_Rec(Extent=0, Nr=0)   = 0
   --   Get_Rec(Extent=0, Nr=5)   = 5
   --   Get_Rec(Extent=1, Nr=0)   = 512
   --   Get_Rec(Extent=2, Nr=10)  = 1034

   declare
      F : FCB;
   begin
      F.Recsiz := 128;
      F.Extent := 0;
      F.Nr     := 0;
      Check_DWord ("Get_Rec Extent=0 Nr=0",
                   DOS86.IO_Ops.Get_Rec (F), 0);

      F.Nr := 5;
      Check_DWord ("Get_Rec Extent=0 Nr=5",
                   DOS86.IO_Ops.Get_Rec (F), 5);

      F.Extent := 1;
      F.Nr     := 0;
      Check_DWord ("Get_Rec Extent=1 Nr=0  (512 recs/extent)",
                   DOS86.IO_Ops.Get_Rec (F), 512);

      F.Extent := 2;
      F.Nr     := 10;
      Check_DWord ("Get_Rec Extent=2 Nr=10",
                   DOS86.IO_Ops.Get_Rec (F), 1034);
   end;

   -- ── Fn_Set_Rnd_Rec ────────────────────────────────────────────────────────
   --
   -- ASM: SETRNDREC  86DOS.asm:2545-2549
   --
   -- Fn_Set_Rnd_Rec sets F.Rr (3-byte little-endian random record) from
   -- Get_Rec(F).  The Rr field stores:
   --   Rr[0] = low byte of rec32
   --   Rr[1] = next byte
   --   Rr[2] = high byte (only low 8 bits used)

   declare
      F   : FCB;
      Rec : DWord;
   begin
      F.Recsiz := 128;
      F.Extent := 1;
      F.Nr     := 2;
      -- rec = 1 * (65536/128) + 2 = 512 + 2 = 514 = 0x00000202

      DOS86.IO_Ops.Fn_Set_Rnd_Rec (F);
      Rec := 514;

      Check_Byte ("Fn_Set_Rnd_Rec Rr[0] = low byte",
                  F.Rr (0), Byte (Rec and 16#FF#));
      Check_Byte ("Fn_Set_Rnd_Rec Rr[1] = next byte",
                  F.Rr (1), Byte (Shift_Right (Rec, 8) and 16#FF#));
      Check_Byte ("Fn_Set_Rnd_Rec Rr[2] = high byte",
                  F.Rr (2), Byte (Shift_Right (Rec, 16) and 16#FF#));
   end;

   declare
      F   : FCB;
      Rec : DWord;
   begin
      -- Larger value to exercise multi-byte encoding
      -- Extent=2, Nr=0, Recsiz=128: rec = 2*512 + 0 = 1024 = 0x00000400
      F.Recsiz := 128;
      F.Extent := 2;
      F.Nr     := 0;

      DOS86.IO_Ops.Fn_Set_Rnd_Rec (F);
      Rec := 1024;

      Check_Byte ("Fn_Set_Rnd_Rec large Rr[0]",
                  F.Rr (0), Byte (Rec and 16#FF#));
      Check_Byte ("Fn_Set_Rnd_Rec large Rr[1]",
                  F.Rr (1), Byte (Shift_Right (Rec, 8) and 16#FF#));
      Check_Byte ("Fn_Set_Rnd_Rec large Rr[2]",
                  F.Rr (2), Byte (Shift_Right (Rec, 16) and 16#FF#));
   end;

   -- ── Fn_Set_DMA ────────────────────────────────────────────────────────────
   --
   -- ASM: SETDMA  86DOS.asm:2444-2449
   -- Sets Dos.DMAADD and Dos.DMASEG.

   DOS86.Initialize;
   DOS86.IO_Ops.Fn_Set_DMA (16#1234#, 16#5678#);
   Check_Word ("Fn_Set_DMA DMASEG", Dos.DMASEG, 16#1234#);
   Check_Word ("Fn_Set_DMA DMAADD", Dos.DMAADD, 16#5678#);

   -- ── Summary ───────────────────────────────────────────────────────────────
   New_Line;
   if Fails = 0 then
      Put_Line ("All IO tests passed.");
   else
      Put_Line (Natural'Image (Fails) & " test(s) FAILED.");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   end if;

end Test_IO;
