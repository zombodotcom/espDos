-- test_fat.adb — Unit tests for DOS86.Fat (Unpack, Pack, Fnd_Clus, Fig_Rec).
--
-- Derived from known-good inputs in 86DOS.asm:369-433.
-- No AUnit dependency; pass/fail is printed to standard output.
-- Exit status 0 on all pass, 1 on any failure.

with Ada.Text_IO;    use Ada.Text_IO;
with Ada.Command_Line;
with Interfaces;     use Interfaces;
with DOS86;          use DOS86;
with DOS86.Fat;

procedure Test_Fat is

   Fails : Natural := 0;

   procedure Check (Name : String; Got, Expect : Word) is
   begin
      if Got = Expect then
         Put_Line ("PASS  " & Name);
      else
         Put_Line ("FAIL  " & Name
                   & "  got=" & Word'Image (Got)
                   & "  expected=" & Word'Image (Expect));
         Fails := Fails + 1;
      end if;
   end Check;

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

   -- Build a minimal DPB with a hand-crafted FAT image.
   -- FAT byte layout for a 12-bit FAT (all entries as raw bytes):
   --   cluster 2 -> 3   (bytes: F8 FF 03 ...)
   --   cluster 3 -> 4   (chain continues)
   --   cluster 4 -> FFF (EOF)
   --   cluster 5 -> 000 (FREE)
   --
   -- 12-bit byte encoding:
   --   Entry 0 (media): F8h  -> bytes [0..1] = F8, 0F   (0x0FF8)
   --   Entry 1 (reserved):   -> bytes [1..2]: upper nibble of byte 1, byte 2
   --                            = FF, FF  -> 0xFFF
   --   Entry 2 -> 3:         offset=3, bytes [3..4]
   --     BX=2 even: (word >> 0) & 0xFFF
   --     offset = 2 + 1 = 3
   --   Entry 3 -> 4:         offset = 3 + 1 = 4  (BX=3 odd: word >> 4)
   --   Entry 4 -> FFF:       offset = 4 + 2 = 6  (BX=4 even: & 0xFFF)
   --   Entry 5 -> 000:       offset = 5 + 2 = 7  (BX=5 odd: >> 4)
   --
   -- Let's build the FAT manually:
   --
   --   Cluster 0 (media byte): 0xF8  -> stored in nibbles of byte 0 and byte 1
   --   Cluster 1 (reserved):           -> upper nibbles of byte 1, byte 2
   --
   -- Full 12-entry FAT (clusters 0-5) encoded as bytes:
   --   cluster 0 (media) = F8 0F => bytes 0,1
   --   Actually let's use a simpler known value:
   --
   -- Easier approach: compute the bytes by hand for specific test cases.
   --
   -- We want:
   --   Unpack(D, 2) = 3
   --   Unpack(D, 3) = 4
   --   Unpack(D, 4) = 16#FFF#
   --   Unpack(D, 5) = 0
   --
   -- byte_offset for BX:  off = BX + BX/2
   --   BX=2: off=3, even  => word[3..4] & 0xFFF = 3
   --   BX=3: off=4, odd   => word[4..5] >> 4     = 4
   --   BX=4: off=6, even  => word[6..7] & 0xFFF = FFF
   --   BX=5: off=7, odd   => word[7..8] >> 4     = 0
   --
   -- Work out the bytes needed:
   --   word[3..4] & 0xFFF = 3 => bytes[3]=03, bytes[4]=00  (upper nibble free)
   --   word[4..5] >> 4 = 4    => bytes[4] and bytes[5]:
   --       word = bytes[4] | (bytes[5] << 8); word >> 4 = 4
   --       bytes[4]=40, bytes[5]=00  -- but bytes[4] already =00 from above!
   --   Reconcile bytes[4]: need (bytes[4] | bytes[5]<<8) >> 4 = 4
   --     and bytes[3..4] & 0xFFF = 3 => low 12 bits of word[3..4] = 3
   --       => bytes[3]=03, (bytes[4] & 0x0F) = 0  => bytes[4] = 0x40
   --       => word[3..4] = 0x4003 => & 0x0FFF = 3  ✓
   --       => word[4..5] = bytes[4] | (bytes[5]<<8) = 0x40 | (bytes[5]<<8)
   --          >> 4 = 4  => 0x40 | (bytes[5]<<8) = 0x0040 >> ... hmm
   --          need ((0x40 | bytes[5]<<8) >> 4) = 4
   --          => (0x40 | bytes[5]<<8) = 0x0040 => >> 4 = 4  ✓  (bytes[5]=0)
   --   word[6..7] & 0xFFF = 0xFFF => bytes[6]=FF, bytes[7]=0F (or FF)
   --     word[7..8] >> 4 = 0  => word[7..8]=0x00FF => >> 4 = 0x0F  ≠ 0
   --     Need bytes[7]: word[7..8] >> 4 = 0
   --       => word = bytes[7] | bytes[8]<<8; >> 4 = 0
   --       => bytes[7]=0x00, bytes[8]=0x00
   --     But bytes[7] must also satisfy: word[6..7] & 0xFFF = 0xFFF
   --       => bytes[6]=0xFF, bytes[7] & 0x0F = 0xF => bytes[7]=0x00 conflicts!
   --     Fix: bytes[7]=0xF0 => (0xFF | 0xF0<<8) = 0xF0FF & 0x0FFF = 0x0FF ≠ FFF
   --     Need bytes[6]=0xFF, (bytes[7] & 0x0F) = 0xF => bytes[7] = 0xF0
   --       word[6..7] = 0xFF | (0xF0 << 8) = 0xF0FF & 0x0FFF = 0x0FF  ≠ FFF
   --
   --   The mask is & 0x0FFF (12 bits from LSB):
   --     word[6..7] & 0x0FFF = 0xFFF
   --     => low 12 bits of (bytes[6] | bytes[7]<<8) = 0xFFF
   --     => bytes[6]=0xFF, bytes[7] & 0x0F = 0x0F => bytes[7]=0x?F
   --     For BX=5 (odd): word[7..8] >> 4 = 0
   --       => (bytes[7] | bytes[8]<<8) >> 4 = 0
   --       => bytes[7] | bytes[8]<<8 < 16 => bytes[7] must have low nibble = F
   --          and upper nibble = 0, so bytes[7] = 0x0F, bytes[8] = 0x00
   --          word[7..8] = 0x0F >> 4 = 0  ✓
   --          word[6..7] = 0xFF | (0x0F << 8) = 0x0FFF & 0x0FFF = 0xFFF  ✓
   --
   -- Final FAT bytes (indices 0-8):
   --   [0]=0x00, [1]=0x00, [2]=0x00,   (clusters 0,1 don't matter for these tests)
   --   [3]=0x03, [4]=0x40, [5]=0x00,   (clusters 2->3, 3->4)
   --   [6]=0xFF, [7]=0x0F, [8]=0x00    (cluster 4->FFF, 5->0)

   D : DPB;

begin
   -- Initialise a minimal DPB with Maxclus=5, Fat_Size=9
   D.Maxclus  := 5;
   D.Fat_Size := 9;
   D.Fat := (others => 0);

   -- Inject FAT bytes as computed above
   D.Fat (3) := 16#03#;
   D.Fat (4) := 16#40#;
   D.Fat (5) := 16#00#;
   D.Fat (6) := 16#FF#;
   D.Fat (7) := 16#0F#;
   D.Fat (8) := 16#00#;

   -- ── Unpack tests ──────────────────────────────────────────────────────────
   Check ("Unpack even cluster 2 -> 3",    DOS86.Fat.Unpack (D, 2), 3);
   Check ("Unpack odd  cluster 3 -> 4",    DOS86.Fat.Unpack (D, 3), 4);
   Check ("Unpack even cluster 4 -> FFF",  DOS86.Fat.Unpack (D, 4), 16#FFF#);
   Check ("Unpack odd  cluster 5 -> 000",  DOS86.Fat.Unpack (D, 5), 16#000#);

   -- ── Pack/Unpack roundtrip ─────────────────────────────────────────────────

   -- Pack cluster 2 -> 7 (even), then Unpack
   DOS86.Fat.Pack (D, 2, 7);
   Check ("Pack/Unpack roundtrip even (2->7)",  DOS86.Fat.Unpack (D, 2), 7);
   -- Cluster 3 should be unchanged (4)
   Check ("Pack even doesn't corrupt odd neighbour 3", DOS86.Fat.Unpack (D, 3), 4);

   -- Pack cluster 3 -> 5 (odd), then Unpack
   DOS86.Fat.Pack (D, 3, 5);
   Check ("Pack/Unpack roundtrip odd  (3->5)",  DOS86.Fat.Unpack (D, 3), 5);
   -- Cluster 2 should be unchanged (7 from above)
   Check ("Pack odd  doesn't corrupt even neighbour 2", DOS86.Fat.Unpack (D, 2), 7);

   -- Pack EOF into cluster 4
   DOS86.Fat.Pack (D, 4, FAT_EOF);
   Check ("Pack EOF  cluster 4", DOS86.Fat.Unpack (D, 4), FAT_EOF);

   -- Pack FREE into cluster 5
   DOS86.Fat.Pack (D, 5, FAT_FREE);
   Check ("Pack FREE cluster 5", DOS86.Fat.Unpack (D, 5), FAT_FREE);

   -- ── Unpack bounds check ───────────────────────────────────────────────────
   declare
      Raised : Boolean := False;
   begin
      declare
         Dummy : Word;
      begin
         Dummy := DOS86.Fat.Unpack (D, D.Maxclus + 1);
      exception
         when Dos_Error => Raised := True;
      end;
      Check_Bool ("Unpack beyond Maxclus raises Dos_Error", Raised, True);
   end;

   -- ── Fnd_Clus ──────────────────────────────────────────────────────────────
   -- Build a 5-cluster chain: 2->3->4->5->FFF (EOF)
   -- (cluster 2 currently -> 7 from above; reset the chain first)
   D.Fat := (others => 0);
   D.Fat_Size := 12;
   D.Maxclus  := 6;
   -- Cluster 2->3, 3->4, 4->5, 5->FFF  (skip=0 means stay at 2)
   D.Fat (3) := 16#03#;  -- cluster 2 -> 3 (bytes 3..4)
   D.Fat (4) := 16#40#;  -- cluster 3 -> 4
   D.Fat (5) := 16#00#;
   D.Fat (6) := 16#05#;  -- cluster 4 -> 5 (bytes 6..7)
   D.Fat (7) := 16#FF#;  -- cluster 5 -> FFF (bytes 7..8)
   D.Fat (8) := 16#0F#;  -- (upper byte of cluster 5 entry)

   -- Recalculate cluster 4 -> 5 and cluster 5 -> FFF:
   -- cluster 4 even: off=6; word[6..7] & 0xFFF = 5
   --   bytes[6]=0x05, (bytes[7] & 0x0F) = 0x00 => bytes[7]=0x?0
   -- cluster 5 odd:  off=7; word[7..8] >> 4 = 0xFFF
   --   (bytes[7] | bytes[8]<<8) >> 4 = 0xFFF
   --   => bytes[7] | bytes[8]<<8 = 0xFFF0
   --   => bytes[7]=0xF0, bytes[8]=0xFF  ... but that conflicts with cluster 4!
   --   bytes[7] must satisfy both:
   --     (bytes[7] & 0x0F) = 0  (cluster 4 constraint)
   --     bytes[7] high nibble can be anything
   --   => bytes[7]=0xF0, bytes[8]=0xFF
   --     word[6..7] = 0x05 | (0xF0 << 8) = 0xF005 & 0x0FFF = 0x005 = 5 ✓
   --     word[7..8] = 0xF0 | (0xFF << 8) = 0xFFF0 >> 4 = 0x0FFF ✓
   D.Fat (6) := 16#05#;
   D.Fat (7) := 16#F0#;
   D.Fat (8) := 16#FF#;

   declare
      Cur, Remaining : Word;
   begin
      -- Skip 0: stay at start (cluster 2)
      DOS86.Fat.Fnd_Clus (D, 2, 0, Cur, Remaining);
      Check      ("Fnd_Clus skip=0 cur",       Cur,       2);
      Check      ("Fnd_Clus skip=0 remaining", Remaining, 0);

      -- Skip 1: advance one step -> cluster 3
      DOS86.Fat.Fnd_Clus (D, 2, 1, Cur, Remaining);
      Check      ("Fnd_Clus skip=1 cur",       Cur,       3);
      Check      ("Fnd_Clus skip=1 remaining", Remaining, 0);

      -- Skip 3: reach cluster 5 (last in chain)
      DOS86.Fat.Fnd_Clus (D, 2, 3, Cur, Remaining);
      Check      ("Fnd_Clus skip=3 cur",       Cur,       5);
      Check      ("Fnd_Clus skip=3 remaining", Remaining, 0);

      -- Skip 10: hit EOF early -> remaining > 0
      DOS86.Fat.Fnd_Clus (D, 2, 10, Cur, Remaining);
      Check_Bool ("Fnd_Clus skip=10 remaining>0", Remaining > 0, True);
   end;

   -- ── Fig_Rec ───────────────────────────────────────────────────────────────
   -- D.Clusshft=1 (2 sectors/cluster), D.Firrec=8
   D.Clusshft := 1;
   D.Firrec   := 8;
   -- Fig_Rec(D, cluster=2, BL=0): physical = (2-2)*2 + 8 + 0 = 8
   Check ("Fig_Rec cluster=2 BL=0", DOS86.Fat.Fig_Rec (D, 2, 0), 8);
   -- Fig_Rec(D, cluster=3, BL=1): physical = (3-2)*2 + 8 + 1 = 11
   Check ("Fig_Rec cluster=3 BL=1", DOS86.Fat.Fig_Rec (D, 3, 1), 11);

   -- ── Summary ───────────────────────────────────────────────────────────────
   New_Line;
   if Fails = 0 then
      Put_Line ("All FAT tests passed.");
   else
      Put_Line (Natural'Image (Fails) & " test(s) FAILED.");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
   end if;

end Test_Fat;
