-- test_directory.adb — Unit tests for DOS86.Directory (Lod_Name, Get_Bp,
--                       Mov_Name).
--
-- Tests derived from 86DOS.asm:660-706.
-- No AUnit dependency; pass/fail is printed to standard output.
-- Exit status 0 on all pass, 1 on any failure.

with Ada.Text_IO;    use Ada.Text_IO;
with Ada.Command_Line;
with Interfaces;     use Interfaces;
with DOS86;          use DOS86;
with DOS86.Directory;

procedure Test_Directory is

   Fails : Natural := 0;

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

   procedure Check_Bool (Name : String; Got, Expect : Boolean) is
   begin
      if Got = Expect then
         Put_Line ("PASS  " & Name);
      else
         Put_Line ("FAIL  " & Name
                   & "  got=" & Boolean'Image (Got)
                   & "  expected=" & Boolean'Image (Expect));
         Fails := Fails + 1;
      end if;
   end Check_Bool;

begin
   -- ── Lod_Name: copy + space-pad ───────────────────────────────────────────
   --
   -- ASM: LODNAME  86DOS.asm:679-695
   -- Lod_Name(Src, Dst, Len) copies up to Len bytes from Src into Dst,
   -- padding with spaces (0x20) when Src runs out or hits a space.

   declare
      Src : Byte_Array (0 .. 7) := (Character'Pos ('H'),
                                     Character'Pos ('E'),
                                     Character'Pos ('L'),
                                     Character'Pos ('L'),
                                     Character'Pos ('O'),
                                     Character'Pos (' '),  -- stop here
                                     Character'Pos ('X'),
                                     Character'Pos ('Y'));
      Dst : Byte_Array (0 .. 7) := (others => 0);
   begin
      DOS86.Directory.Lod_Name (Src, Dst, 8);
      -- First 5 chars should be 'HELLO'
      Check_Byte ("Lod_Name [0]='H'", Dst (0), Character'Pos ('H'));
      Check_Byte ("Lod_Name [1]='E'", Dst (1), Character'Pos ('E'));
      Check_Byte ("Lod_Name [4]='O'", Dst (4), Character'Pos ('O'));
      -- Positions 5..7 should be space-padded
      Check_Byte ("Lod_Name [5]=SPC", Dst (5), Character'Pos (' '));
      Check_Byte ("Lod_Name [6]=SPC", Dst (6), Character'Pos (' '));
      Check_Byte ("Lod_Name [7]=SPC", Dst (7), Character'Pos (' '));
   end;

   declare
      -- Src shorter than Len (only 3 bytes, rest is space)
      Src : Byte_Array (0 .. 4) := (Character'Pos ('A'),
                                     Character'Pos ('B'),
                                     Character'Pos ('C'),
                                     Character'Pos (' '),
                                     Character'Pos ('D'));
      Dst : Byte_Array (0 .. 4) := (others => 0);
   begin
      DOS86.Directory.Lod_Name (Src, Dst, 5);
      Check_Byte ("Lod_Name short [0]='A'",   Dst (0), Character'Pos ('A'));
      Check_Byte ("Lod_Name short [2]='C'",   Dst (2), Character'Pos ('C'));
      Check_Byte ("Lod_Name short [3]=SPC",   Dst (3), Character'Pos (' '));
      Check_Byte ("Lod_Name short [4]=SPC",   Dst (4), Character'Pos (' '));
   end;

   -- ── Get_Bp: invalid drive raises Dos_Error ────────────────────────────────
   --
   -- ASM: GETBP  86DOS.asm:696-706
   -- Get_Bp raises Dos_Error(InvalidDrive) if Drive >= Dos.NUMDRV.

   DOS86.Initialize;
   Dos.NUMDRV := 2;

   declare
      Raised : Boolean := False;
   begin
      declare
         Dummy : DPB_Access;
      begin
         Dummy := DOS86.Directory.Get_Bp (5);  -- 5 >= NUMDRV=2
      exception
         when Dos_Error => Raised := True;
      end;
      Check_Bool ("Get_Bp invalid drive raises Dos_Error", Raised, True);
   end;

   -- Get_Bp with a valid drive (drive index 0, NUMDRV=2) should NOT raise,
   -- but the DPB pointer will be null (DRVTAB not populated) — that is fine
   -- for this test; we only care it doesn't raise.
   declare
      D0     : aliased DPB;
      Raised : Boolean := False;
      Dummy  : DPB_Access;
   begin
      Dos.DRVTAB (0) := D0'Unchecked_Access;
      Dummy := DOS86.Directory.Get_Bp (1);  -- 1-based => DRVTAB(0)
      Check_Bool ("Get_Bp valid drive does not raise", Raised, False);
   end;

   -- ── Mov_Name: copies FCB.Name into Dos.NAME1 ────────────────────────────
   --
   -- ASM: MOVNAME  86DOS.asm:660-678
   -- After Mov_Name(F), Dos.NAME1 should equal F.Name.

   declare
      F    : FCB;
      D0   : aliased DPB;
   begin
      DOS86.Initialize;
      Dos.NUMDRV   := 2;
      Dos.DRVTAB (0) := D0'Unchecked_Access;
      Dos.DRVTAB (1) := D0'Unchecked_Access;

      F.Drive := 1;  -- drive A
      F.Name  := (Character'Pos ('F'),
                  Character'Pos ('O'),
                  Character'Pos ('O'),
                  Character'Pos (' '),
                  Character'Pos (' '),
                  Character'Pos (' '),
                  Character'Pos (' '),
                  Character'Pos (' '),
                  Character'Pos ('B'),
                  Character'Pos ('A'),
                  Character'Pos ('R'));

      DOS86.Directory.Mov_Name (F);

      Check_Byte ("Mov_Name NAME1[0]='F'", Dos.NAME1 (0), Character'Pos ('F'));
      Check_Byte ("Mov_Name NAME1[1]='O'", Dos.NAME1 (1), Character'Pos ('O'));
      Check_Byte ("Mov_Name NAME1[8]='B'", Dos.NAME1 (8), Character'Pos ('B'));
      Check_Byte ("Mov_Name NAME1[9]='A'", Dos.NAME1 (9), Character'Pos ('A'));
   end;

   -- ── Summary ───────────────────────────────────────────────────────────────
   New_Line;
   if Fails = 0 then
      Put_Line ("All Directory tests passed.");
   else
      Put_Line (Natural'Image (Fails) & " test(s) FAILED.");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   end if;

end Test_Directory;
