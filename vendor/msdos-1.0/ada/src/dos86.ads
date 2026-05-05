-- dos86.ads — Root package: types, constants, BIOS interface, DPB, FCB,
--              directory entry layouts, DOS state, and Dos_Error exception.
--
-- Translated from 86DOS.asm.  The following ASM data definitions are
-- covered here:
--
--   MAXCALL / MAXCOM equates  86DOS.asm:206-215   — function-number limits
--   ESCCH / INTBASE / INTTAB  86DOS.asm:60-100    — interrupt/keyboard equates
--   BIOSSEG / EOF_MARK        86DOS.asm:100-120   — BIOS segment, FAT sentinel
--   DPB layout                86DOS.asm:3306-3423 — per-drive parameter block
--   FCB layout                86DOS.asm:707-756   — file control block
--   DMAADD / ENDMEM globals   86DOS.asm:3228-3240 — global data area equates
--   Function dispatch table   86DOS.asm:302-346   — INT 21h / CALL 5 numbers

with Interfaces;

package DOS86 is

   -- ── Fixed-width integer subtypes (mirrors C uint8_t / uint16_t / uint32_t)
   subtype Byte  is Interfaces.Unsigned_8;
   subtype Word  is Interfaces.Unsigned_16;
   subtype DWord is Interfaces.Unsigned_32;

   -- Signed counterparts used for BIOS disk-change return value, etc.
   subtype SByte is Interfaces.Integer_8;

   -- Byte array type used for FAT buffers, sector buffers, etc.
   type Byte_Array is array (Natural range <>) of Byte;

   -- ── Constants derived from ASM equates ──────────────────────────────────

   -- MAXCALL — highest function number accepted via ENTRY (CL path).
   -- ASM: CMP CL,MAXCALL  86DOS.asm:213
   MAXCALL : constant Byte := 36;

   -- MAXCOM — highest function number accepted via COMMAND (INT 20h / CALL 0).
   -- ASM: CMP AH,MAXCOM  86DOS.asm:200
   MAXCOM : constant Byte := 41;

   -- ESCCH — ASCII escape character used in console template editing.
   -- ASM: ESCCH EQU 1BH  86DOS.asm:85
   ESCCH : constant Byte := 16#1B#;

   -- INTBASE — base interrupt vector for DOS (INT 20h–3Fh mapped here).
   -- ASM: INTBASE EQU 80H  86DOS.asm:60
   INTBASE : constant Word := 16#80#;

   -- INTTAB — base of user interrupt table (segment 0, offset 0x20×4).
   -- ASM: INTTAB EQU 20H  86DOS.asm:62
   INTTAB : constant Word := 16#20#;

   -- BIOSSEG — segment address of the ROM BIOS data area / entry points.
   -- ASM: BIOSSEG EQU 40H  86DOS.asm:84
   BIOSSEG : constant Word := 16#40#;

   -- EOF_MARK — FAT12 end-of-chain sentinel (entry >= 0xFF8 means EOF).
   -- ASM: used in UNPACK/PACK comparisons  86DOS.asm:390-395
   EOF_MARK : constant Word := 16#FF8#;

   -- FAT_FREE — FAT12 value for a free (unallocated) cluster.
   -- ASM: cluster entry of 0 means free  86DOS.asm:395
   FAT_FREE : constant Word := 16#000#;

   -- FAT_EOF — canonical EOF marker written by PACK.
   -- ASM: PACK writes 0x0FFF for EOF  86DOS.asm:402-433
   FAT_EOF : constant Word := 16#FFF#;

   -- DEL_MARK — first byte of a deleted directory entry.
   -- ASM: DB 0E5H used as FREEDIRBLK sentinel  86DOS.asm:630
   DEL_MARK : constant Byte := 16#E5#;

   -- SMALLDIR_ENTRY — size in bytes of a SMALLDIR=1 directory entry (16 bytes).
   SMALLDIR_ENTRY : constant Natural := 16;

   -- LARGE_ENTRY — size in bytes of a standard 32-byte directory entry.
   LARGE_ENTRY : constant Natural := 32;

   -- DEFAULT_RECSIZ — default FCB record size (128 bytes).
   -- ASM: RECSIZ field initialised to 128 when FCB is opened  86DOS.asm:730
   DEFAULT_RECSIZ : constant Word := 128;

   -- MAX_DRIVES — number of entries in DRVTAB.
   -- ASM: DRVTAB DS 15  86DOS.asm:3231
   MAX_DRIVES : constant := 15;

   -- MAX_FAT_BYTES — maximum size of an in-memory FAT image (8 sectors × 512).
   MAX_FAT_BYTES : constant := 8 * 512;

   -- MAX_SEC_SIZE — maximum sector size supported.
   MAX_SEC_SIZE : constant := 512;

   -- INBUF_SIZE / CONBUF_SIZE — internal line-input buffer sizes.
   -- ASM data area lines 3232-3239
   INBUF_SIZE  : constant := 128;
   CONBUF_SIZE : constant := 130;

   -- LONGJUMP — opcode byte for a far (long) jump instruction.
   -- ASM: LONGJUMP EQU 0EAH  86DOS.asm:70
   LONGJUMP : constant Byte := 16#EA#;

   -- LONGCALL — opcode byte for a far (long) call instruction.
   -- ASM: LONGCALL EQU 9AH  86DOS.asm:72
   LONGCALL : constant Byte := 16#9A#;

   -- ── System-call function numbers (DISPATCH table, 86DOS.asm:302-346) ──
   FN_ABORT     : constant Byte := 0;
   FN_CONIN     : constant Byte := 1;
   FN_CONOUT    : constant Byte := 2;
   FN_READER    : constant Byte := 3;
   FN_PUNCH     : constant Byte := 4;
   FN_LIST      : constant Byte := 5;
   FN_RAWIO     : constant Byte := 6;
   FN_RAWINP    : constant Byte := 7;
   FN_IN        : constant Byte := 8;
   FN_PRTBUF    : constant Byte := 9;
   FN_BUFIN     : constant Byte := 10;
   FN_CONSTAT   : constant Byte := 11;
   FN_VERSION   : constant Byte := 12;
   FN_DSKRESET  : constant Byte := 13;
   FN_SELDSK    : constant Byte := 14;
   FN_OPEN      : constant Byte := 15;
   FN_CLOSE     : constant Byte := 16;
   FN_SRCHFRST  : constant Byte := 17;
   FN_SRCHNXT   : constant Byte := 18;
   FN_DELETE    : constant Byte := 19;
   FN_SEQRD     : constant Byte := 20;
   FN_SEQWRT    : constant Byte := 21;
   FN_CREATE    : constant Byte := 22;
   FN_RENAME    : constant Byte := 23;
   FN_INUSE     : constant Byte := 24;
   FN_CURDRV    : constant Byte := 25;
   FN_SETDMA    : constant Byte := 26;
   FN_GETFATPT  : constant Byte := 27;
   FN_WRTPROT   : constant Byte := 28;
   FN_GETRDONLY : constant Byte := 29;
   FN_SETATTRIB : constant Byte := 30;
   FN_GETDSKPT  : constant Byte := 31;
   FN_USERCODE  : constant Byte := 32;
   FN_RNDRD     : constant Byte := 33;
   FN_RNDWRT    : constant Byte := 34;
   FN_FILESIZE  : constant Byte := 35;
   FN_SETRNDREC : constant Byte := 36;
   FN_SETVECT   : constant Byte := 37;
   FN_NEWBASE   : constant Byte := 38;
   FN_BLKRD     : constant Byte := 39;
   FN_BLKWRT    : constant Byte := 40;
   FN_MAKEFCB   : constant Byte := 41;

   -- ── Dos_Error exception ─────────────────────────────────────────────────
   --
   -- Raised by kernel routines on error conditions that correspond to
   -- carry-set / jump-to-ERROR paths in 86DOS.asm.
   -- Callers receive the error name as the exception message.
   Dos_Error : exception;

   -- Error message constants passed to Dos_Error.
   ERR_NOT_FOUND    : constant String := "NotFound";
   ERR_INVALID_DRV  : constant String := "InvalidDrive";
   ERR_DISK_ERROR   : constant String := "DiskError";
   ERR_NO_SPACE     : constant String := "NoSpace";
   ERR_BAD_NAME     : constant String := "BadFileName";
   ERR_BAD_FAT      : constant String := "BadFat";
   ERR_ALL_FATS_BAD : constant String := "AllFatsBad";
   ERR_BAD_CALL     : constant String := "BadCall";

   -- ── BIOS vtable (abstract tagged type) ──────────────────────────────────
   --
   -- Every far-call to the BIOS in 86DOS.asm (CALL BIOSxxx,BIOSSEG) maps to
   -- one dispatching operation here.  Concrete implementations are provided
   -- by callers (e.g. test stubs).
   type Bios_Vtable is abstract tagged limited null record;
   type Bios_Access is access all Bios_Vtable'Class;

   -- BIOSSTAT: returns 0 if no character ready, nonzero if one is waiting.
   -- ASM: CALL BIOSSTAT,BIOSSEG  86DOS.asm:2968
   function Stat (Bios : in out Bios_Vtable) return Byte is abstract;

   -- BIOSIN: read one character from the console (blocks until available).
   -- ASM: CALL BIOSIN,BIOSSEG  86DOS.asm:2975
   function Input (Bios : in out Bios_Vtable) return Byte is abstract;

   -- BIOSOUT: write one character to the console output device.
   -- ASM: CALL BIOSOUT,BIOSSEG  86DOS.asm:2853
   procedure Output (Bios : in out Bios_Vtable; C : Byte) is abstract;

   -- BIOSPRINT: write one character to the list (printer) device.
   -- ASM: CALL BIOSPRINT,BIOSSEG  86DOS.asm:3001
   procedure Print (Bios : in out Bios_Vtable; C : Byte) is abstract;

   -- BIOSAUXIN: read one character from the auxiliary (serial) port.
   -- ASM: CALL BIOSAUXIN,BIOSSEG  86DOS.asm:359
   function Aux_In (Bios : in out Bios_Vtable) return Byte is abstract;

   -- BIOSAUXOUT: write one character to the auxiliary (serial) port.
   -- ASM: CALL BIOSAUXOUT,BIOSSEG  86DOS.asm:363
   procedure Aux_Out (Bios : in out Bios_Vtable; C : Byte) is abstract;

   -- BIOSREAD: read Count sectors from Drive starting at Sector into Buf.
   -- Returns True (carry set) on error, False on success.
   -- ASM: CALL BIOSREAD,BIOSSEG  86DOS.asm:1095
   function Disk_Read
     (Bios   : in out Bios_Vtable;
      Drive  : Byte;
      Buf    : in out Byte_Array;
      Sector : Word;
      Count  : Word) return Boolean is abstract;

   -- BIOSWRITE: write Count sectors to Drive starting at Sector from Buf.
   -- Returns True (carry set) on error, False on success.
   -- ASM: CALL BIOSWRITE,BIOSSEG  86DOS.asm:1150
   function Disk_Write
     (Bios   : in out Bios_Vtable;
      Drive  : Byte;
      Buf    : Byte_Array;
      Sector : Word;
      Count  : Word) return Boolean is abstract;

   -- BIOSDSKCHG: query whether the disk in Drive has changed.
   -- Returns +1 = no change, 0 = unknown, -1 = changed.
   -- ASM: CALL BIOSDSKCHG,BIOSSEG  86DOS.asm:766
   function Disk_Change
     (Bios  : in out Bios_Vtable;
      Drive : Byte) return SByte is abstract;

   -- ── FAT buffer subtype (fixed size, avoids dynamic allocation) ──────────
   subtype Fat_Buffer is Byte_Array (0 .. MAX_FAT_BYTES - 1);

   -- ── DPB — Drive Parameter Block ─────────────────────────────────────────
   --
   -- ASM: one DPB allocated per drive starting at MEMSTRT  86DOS.asm:3306-3423
   --
   -- Fields correspond 1:1 to the DPB fields stored by PERDRV:
   --   Drvnum   — physical drive number passed to BIOS
   --   Secsiz   — bytes per sector (LODW from DPT, 86DOS.asm:3311)
   --   Clusmsk  — sectors-per-cluster − 1  (LODB − 1, 86DOS.asm:3320)
   --   Clusshft — log₂(sectors-per-cluster)  (computed, 86DOS.asm:3321-3333)
   --   Firfat   — sector number of first FAT copy (86DOS.asm:3336)
   --   Fatcnt   — number of FAT copies (86DOS.asm:3338)
   --   Maxent   — maximum directory entries (86DOS.asm:3340)
   --   Firrec   — first data record sector (86DOS.asm:3343)
   --   Maxclus  — highest valid cluster number (set by Fig_Max)
   --   Fatsiz   — sectors per FAT copy (set by Fig_Fat_Siz)
   --   Firdir   — first directory sector (86DOS.asm:3344)
   --   Dirtyfat — 0=clean, 1=dirty, 16#FF#=never read
   --   Dirsiz   — 16#FF#=small 16-byte entries, 0=large 32-byte entries
   --   Fat      — in-memory FAT image
   --   Fat_Size — number of valid bytes currently in Fat
   type DPB is record
      Drvnum   : Byte  := 0;
      Secsiz   : Word  := 512;
      Clusmsk  : Byte  := 0;
      Clusshft : Byte  := 0;
      Firfat   : Word  := 1;
      Fatcnt   : Byte  := 2;
      Maxent   : Word  := 64;
       Firrec   : Word  := 8;
       Maxclus  : Word  := 354;
       Fatsiz   : Byte  := 2;
       Firdir   : Word  := 3;
       Dsksiz   : Word  := 360;  -- total sectors on disk (86DOS.asm:3374)
      -- SMALLDIR fields (always compiled in; SMALLDIR=1 per project rules)
      Firrec1  : Word  := 0;   -- SMALLDIR alternate firrec (drive B side 1)
      Maxclus1 : Word  := 0;   -- SMALLDIR alternate maxclus (drive B side 1)
      Firrec2  : Word  := 0;   -- SMALLDIR alternate firrec (drive B side 2)
      Maxclus2 : Word  := 0;   -- SMALLDIR alternate maxclus (drive B side 2)
      Dirtyfat : Byte  := 16#FF#;  -- 0=clean, 1=dirty, 0xFF=not yet read
      Dirsiz   : Byte  := 0;       -- 0xFF=small (16-byte), 0=large (32-byte)
      Fat      : Fat_Buffer := (others => 16#FF#);
      Fat_Size : Natural    := 0;  -- valid bytes in Fat
   end record;

   -- Pointer type used by DRVTAB / CURDRVPT in Dos_State.
   type DPB_Access is access all DPB;

   -- ── FCB — File Control Block ─────────────────────────────────────────────
   --
   -- ASM: FCB DS 2 (pointer to user FCB)  86DOS.asm:3261
   --      FCB layout described in 86DOS.asm:707-756 (OPEN/DOOPEN)
   type FCB is record
      Drive     : Byte := 0;              -- 0=default drive, 1=A, 2=B, …
      Name      : Byte_Array (0 .. 10) := (others => Character'Pos (' '));
                                           -- 8-byte name + 3-byte ext, space-padded
      Extent    : Word := 0;              -- current file extent
      Recsiz    : Word := DEFAULT_RECSIZ; -- logical record size in bytes
      Filsiz    : DWord := 0;             -- file size in bytes
      Fdate     : Word := 0;              -- packed file date
      Fildirblk : Word := 0;             -- directory block number
      Firclus   : Word := 0;             -- first cluster of file chain (0=empty)
      Lstclus   : Word := 0;             -- last cluster walked to
      Cluspos   : Word := 0;             -- chain offset of Lstclus
      Dirtyfil  : Byte := 0;             -- 1 if file data has been written
      Nr        : Byte := 0;             -- next sequential record number
      Rr        : Byte_Array (0 .. 2) := (others => 0);  -- 3-byte random record
   end record;

   type FCB_Access is access all FCB;

   -- ── Directory entry layouts ──────────────────────────────────────────────

   -- Small_Dir_Entry — 16-byte directory entry (SMALLDIR=1 compilation).
   -- ASM: SMALLDIR EQU 1 conditional assembly  86DOS.asm:3306
   type Small_Dir_Entry is record
      Name    : Byte_Array (0 .. 7) := (others => Character'Pos (' '));
      Ext     : Byte_Array (0 .. 2) := (others => Character'Pos (' '));
      Attr    : Byte  := 0;
      Firclus : Word  := 0;
      Size    : Word  := 0;
   end record;

   -- Large_Dir_Entry — standard 32-byte directory entry (SMALLDIR=0).
   -- ASM: large (non-SMALLDIR) layout  86DOS.asm:3306 (else branch)
   type Large_Dir_Entry is record
      Name     : Byte_Array (0 .. 7)  := (others => Character'Pos (' '));
      Ext      : Byte_Array (0 .. 2)  := (others => Character'Pos (' '));
      Attr     : Byte  := 0;
      Reserved : Byte_Array (0 .. 9)  := (others => 0);
      Time     : Word  := 0;
      Date     : Word  := 0;
      Firclus  : Word  := 0;
      Size     : DWord := 0;
   end record;

   -- Named types/subtypes for record components.
   type DPB_Tab   is array (0 .. MAX_DRIVES - 1) of DPB_Access;
   type Word_Pair is array (0 .. 1) of Word;
   subtype Name_Buf is Byte_Array (0 .. 10);
   subtype Sec_Buf  is Byte_Array (0 .. MAX_SEC_SIZE - 1);
   subtype In_Buf   is Byte_Array (0 .. INBUF_SIZE - 1);
   subtype Con_Buf  is Byte_Array (0 .. CONBUF_SIZE - 1);

   -- ── DOS global state ────────────────────────────────────────────────────
   --
   -- In the original ASM all kernel variables live in the CS segment (code
   -- and data share the same segment).  Here they are collected into a
   -- single record; a package-level variable Dos holds the live state.
   --
   -- Variable names match the ASM labels exactly (upper-case) so a reader
   -- can grep for them in 86DOS.asm.
   --
   -- ASM data area: lines 3206-3268.
   type Dos_State is record
      -- Console state (86DOS.asm:3214-3216)
      CARPOS   : Byte := 0;   -- current cursor column position
      STARTPOS : Byte := 0;   -- column at start of current input line
      PFLAG    : Byte := 0;   -- 1 = echo console output to printer

      -- Directory dirty flag (86DOS.asm:3217)
      DIRTYDIR : Byte := 0;

      -- Drive / disk state (86DOS.asm:3218-3228)
      NUMDRV   : Byte := 0;   -- number of logical drives
      CONTPOS  : Word := 0;   -- continuation position in CONBUF
      DMAADD   : Word := 0;   -- user's DMA (disk transfer) address
      DMASEG   : Word := 0;   -- segment of DMA address
      ENDMEM   : Word := 0;   -- first unavailable segment
      MAXSEC   : Word := 0;   -- largest sector size seen during init
      BUFSECNO : Word := 0;   -- sector number currently in buffer
      BUFDRVNO : Byte := 16#FF#;  -- drive number of buffered sector (0xFF=none)
      DIRTYBUF : Byte := 0;   -- sector buffer is dirty
      DIRBUFID : Word := 16#FFFF#;  -- ID of directory sector (0xFFFF=none)
      DATE     : Word := 0;   -- current date (packed: yr|mo|day)

       -- Drive tables (86DOS.asm:3230-3231)
       CURDRVPT : DPB_Access := null;
       DRVTAB   : DPB_Tab := (others => null);

      -- Currently-executing function (86DOS.asm:3240)
      FUNC     : Byte := 0;

      -- Directory search state (86DOS.asm:3241)
      LASTENT  : Word := 0;

       -- Exit / abort addresses (86DOS.asm:3242-3243)
       EXITHOLD : Word_Pair := (others => 0);
      FATBASE  : Word := 0;

       -- Filename buffers (86DOS.asm:3244-3245)
       NAME1    : Name_Buf := (others => 0);
       NAME2    : Name_Buf := (others => 0);

      -- Stack / segment save (86DOS.asm:3246-3249)
      TEMP     : Word := 0;
      CSLOC    : Word := 0;
      SPSAVE   : Word := 0;
      SSSAVE   : Word := 0;

      -- I/O transfer state (86DOS.asm:3250-3252)
      SECCLUSPOS : Byte := 0;
      DSKERR     : Byte := 0;
      TRANS      : Byte := 0;

      -- Record I/O work area (86DOS.asm:3255-3267)
      FCB_PTR    : FCB_Access := null;
      NEXTADD    : Word  := 0;
      RECPOS     : DWord := 0;
      RECCNT     : Word  := 0;
      LASTPOS    : Word  := 0;
      CLUSNUM    : Word  := 0;
      SECPOS     : Word  := 0;
      VALSEC     : Word  := 0;
      BYTSECPOS  : Word  := 0;
      BYTPOS     : DWord := 0;
      BYTCNT1    : Word  := 0;
      BYTCNT2    : Word  := 0;
      SECCNT     : Word  := 0;

       -- Line-input buffers
       INBUF  : In_Buf  := (others => 0);
       CONBUF : Con_Buf := (others => 0);

       -- Sector buffer (allocated separately; pointer stored here)
       BUFFER : Sec_Buf := (others => 0);
   end record;

   -- Package-level global DOS state (initialised by Initialize / Dos_Init).
   Dos : Dos_State;

   -- Initialize — reset Dos to its power-on defaults.
   -- Called before Dos_Init during unit tests or re-initialisation.
   procedure Initialize;

end DOS86;
