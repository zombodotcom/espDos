-- dos86.adb — Root package body: Initialize procedure.
--
-- ASM reference: 86DOS.asm data area 3206-3268

package body DOS86 is

   -- Initialize — reset all fields of Dos to power-on defaults.
   procedure Initialize is
   begin
      Dos := (others => <>);
      Dos.BUFDRVNO := 16#FF#;
      Dos.DIRBUFID := 16#FFFF#;
   end Initialize;

end DOS86;
