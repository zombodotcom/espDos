-- dos86-init.ads — DOS initialisation routines (specification).
--
-- Translated from 86DOS.asm.  The following ASM labels are covered here:
--
--   DOSINIT    86DOS.asm:3296-3555  — main DOS initialisation entry point
--   PERDRV     86DOS.asm:3306-3423  — per-drive initialisation
--   CONTINIT   86DOS.asm:3424-3555  — continuation of initialisation
--   FIGFATSIZ  86DOS.asm:3557-3563  — compute FAT size in sectors
--   FIGMAX     86DOS.asm:3564-3584  — compute maximum cluster number
--   MYD        86DOS.asm:3586-3610  — read date from BIOS

package DOS86.Init is

   -- Per_Drv — Per-drive initialisation: fill in one DPB from BIOS DPT.
   --
   -- ASM: PERDRV  86DOS.asm:3306-3423
   --
   -- Inputs:
   --   D    : in out DPB         — drive parameter block to fill
   --   Bios : in out Bios_Vtable — BIOS interface
   --   DPT  : Byte_Array         — disk parameter table from BIOS
   procedure Per_Drv
     (D    : in out DPB;
      Bios : in out Bios_Vtable'Class;
      DPT  : Byte_Array);

   -- Fig_Fat_Siz — Compute FAT size in sectors and store in D.Fatsiz.
   --
   -- ASM: FIGFATSIZ  86DOS.asm:3557-3563
   procedure Fig_Fat_Siz (D : in out DPB);

   -- Fig_Max — Compute maximum cluster number and store in D.Maxclus.
   --
   -- ASM: FIGMAX  86DOS.asm:3564-3584
   procedure Fig_Max (D : in out DPB);

   -- My_D — Read the current date from the BIOS and store in Dos.DATE.
   --
   -- ASM: MYD  86DOS.asm:3586-3610
   procedure My_D (Bios : in out Bios_Vtable'Class);

   -- Dos_Init — Full DOS initialisation.
   --
   -- ASM: DOSINIT  86DOS.asm:3296-3555
   --
   -- Initialises all drives, sets up interrupt vectors, allocates buffers.
   -- Called once at boot.
   procedure Dos_Init
     (Bios     : in out Bios_Vtable'Class;
      Init_Tab : Byte_Array);

end DOS86.Init;
