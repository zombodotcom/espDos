-- dos86-fat.ads — FAT12 File Allocation Table routines (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   UNPACK      86DOS.asm:369-401    — read one 12-bit FAT entry
--   PACK        86DOS.asm:402-433    — write one 12-bit FAT entry
--   FIGFAT      86DOS.asm:952-962    — prepare registers for FAT I/O
--   FATWRT      86DOS.asm:914-951    — write FAT back to disk
--   CHKFATWRT   86DOS.asm:908-913    — conditional FATWRT
--   FATREAD     86DOS.asm:766-907    — read FAT from disk (if needed)
--   GETEOF      86DOS.asm:2285-2301  — walk chain to last cluster
--   RELEASE     86DOS.asm:2260-2271  — free a cluster chain
--   RELBLKS     86DOS.asm:2272-2284  — partial chain free
--   ALLOCATE    86DOS.asm:2169-2284  — allocate clusters for a file
--   FNDCLUS     86DOS.asm:1466-1507  — walk cluster chain N steps
--   FIGREC      86DOS.asm:2126-2145  — cluster+sector → physical sector
--   WRTFATS     86DOS.asm:2490-2517  — write all dirty FATs

package DOS86.Fat is

   -- Unpack — Read one 12-bit FAT entry.
   --
   -- ASM: UNPACK  86DOS.asm:369-401
   --
   -- Inputs:
   --   D   : in out DPB  — drive parameter block (FAT buffer + Maxclus)
   --   BX  : in     Word — cluster number to look up
   -- Outputs:
   --   Returns the 12-bit FAT entry for cluster BX.
   --   Raises Dos_Error (BadFat) if BX > Maxclus (HURTFAT, lines 396-399).
   --
   -- 12-bit packing scheme (86DOS.asm lines 89-98):
   --   byte_offset = BX + (BX / 2)   i.e. floor(BX * 1.5)
   --   word        = Fat[byte_offset .. byte_offset+1]  (little-endian)
   --   if BX even: entry = word AND 0x0FFF   (HAVCLUS, line 390)
   --   if BX odd:  entry = word SHR 4
   function Unpack (D : DPB; BX : Word) return Word;

   -- Pack — Write one 12-bit FAT entry.
   --
   -- ASM: PACK  86DOS.asm:402-433
   --
   -- Inputs:
   --   D  : in out DPB  — drive parameter block (FAT buffer)
   --   BX : in     Word — cluster number to write
   --   DX : in     Word — 12-bit value to store
   -- Outputs:
   --   D.Fat updated in-place.
   procedure Pack (D : in out DPB; BX : Word; DX : Word);

   -- Fig_Fat — Compute FAT size in sectors and initialise the FAT buffer.
   --
   -- ASM: FIGFAT  86DOS.asm:952-962
   --
   -- Inputs:
   --   D : in out DPB — partially initialised DPB (Maxclus, Secsiz set)
   -- Outputs:
   --   D.Fatsiz — number of sectors required for one FAT copy
   --   D.Fat_Size — total bytes in one FAT image
   procedure Fig_Fat (D : in out DPB);

   -- Fat_Wrt — Write the in-memory FAT back to disk (all copies).
   --
   -- ASM: FATWRT  86DOS.asm:914-951
   --
   -- Inputs:
   --   D    : in out DPB         — drive parameter block
   --   Bios : in out Bios_Vtable — BIOS interface for disk writes
   -- Outputs:
   --   D.Dirtyfat cleared to 0.
   --   Raises Dos_Error (DiskError) on I/O failure.
   procedure Fat_Wrt (D : in out DPB; Bios : in out Bios_Vtable'Class);

   -- Chk_Fat_Wrt — Conditionally flush dirty FAT to disk.
   --
   -- ASM: CHKFATWRT  86DOS.asm:908-913
   procedure Chk_Fat_Wrt (D : in out DPB; Bios : in out Bios_Vtable'Class);

   -- Fat_Read — Read FAT from disk into memory (if disk may have changed).
   --
   -- ASM: FATREAD  86DOS.asm:766-907
   --
   -- Inputs:
   --   D    : in out DPB         — drive parameter block
   --   Bios : in out Bios_Vtable — BIOS interface
   -- Outputs:
   --   D.Fat filled from disk; D.Dirtyfat cleared to 0 on success.
   --   Raises Dos_Error (DiskError) if all FAT copies unreadable.
   procedure Fat_Read (D : in out DPB; Bios : in out Bios_Vtable'Class);

   -- Get_EOF — Walk a cluster chain to find the last cluster.
   --
   -- ASM: GETEOF  86DOS.asm:2285-2301
   --
   -- Inputs:
   --   D     : in DPB  — drive parameter block
   --   Start : in Word — any cluster in the file
   -- Outputs:
   --   Returns the last cluster in the chain (entry >= EOF_MARK).
   function Get_EOF (D : DPB; Start : Word) return Word;

   -- Rel_Blks — Free clusters from Start onward; optionally leave EOF.
   --
   -- ASM: RELBLKS  86DOS.asm:2272-2284
   --      (RELEASE enters here with DX=0; RELBLKS entered with DX=0x0FFF)
   --
   -- If DX = FAT_EOF: put EOF marker in Start, then free rest of chain.
   -- If DX = FAT_FREE: free entire chain starting at Start.
   procedure Rel_Blks (D : in out DPB; Start : Word; DX : Word);

   -- Release — Free the entire cluster chain starting at Start.
   --
   -- ASM: RELEASE  86DOS.asm:2260-2271
   procedure Release (D : in out DPB; Start : Word);

   -- Fnd_Clus — Walk cluster chain Skip steps from Start.
   --
   -- ASM: FNDCLUS  86DOS.asm:1466-1507
   --
   -- Inputs:
   --   D     : in DPB  — drive parameter block
   --   Start : in Word — first cluster of the chain
   --   Skip  : in Word — number of clusters to advance
   -- Outputs:
   --   Cur       — last cluster reached
   --   Remaining — 0 if destination reached; >0 if EOF hit early
   procedure Fnd_Clus
     (D         : in     DPB;
      Start     : in     Word;
      Skip      : in     Word;
      Cur       :    out Word;
      Remaining :    out Word);

   -- Allocate — Allocate one cluster for a file, linking to Prev.
   --
   -- ASM: ALLOCATE  86DOS.asm:2169-2257
   --
   -- Inputs:
   --   D    : in out DPB        — drive parameter block
   --   Prev : in     Word       — cluster to chain from (0 = first cluster)
   -- Outputs:
   --   New_Clus — the newly allocated cluster number
   --   Raises Dos_Error (NoSpace) if no free cluster exists.
   procedure Allocate
     (D        : in out DPB;
      Prev     : in     Word;
      New_Clus :    out Word);

   -- Fig_Rec — Convert cluster + intra-cluster sector to physical sector.
   --
   -- ASM: FIGREC  86DOS.asm:2126-2145
   --
   -- Inputs:
   --   D   : in DPB  — drive parameter block (Clusshft, Firrec)
   --   DX  : in Word — physical cluster number
   --   BL  : in Byte — sector position within cluster
   -- Outputs:
   --   Returns physical (absolute) sector number.
   function Fig_Rec (D : DPB; DX : Word; BL : Byte) return Word;

   -- Wrt_Fats — Write all dirty FAT copies to disk.
   --
   -- ASM: WRTFATS  86DOS.asm:2490-2517
   --
   -- Flushes dirty sector buffer too.  Called from Abort and Dsk_Reset.
   procedure Wrt_Fats (Bios : in out Bios_Vtable'Class);

end DOS86.Fat;
