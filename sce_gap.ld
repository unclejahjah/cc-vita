/* vita-elf-create appends ~2.5 KB of module data after the text segment, but the
   data segment starts at the next 64 KB boundary. If the text ends less than 4 KB
   below that boundary the two collide ("segment 1 overlaps"), so in that case move
   the location counter past the boundary; the data segment then starts 64 KB later.
   Nothing is emitted, so the binary doesn't grow. */
SECTIONS
{
  . = (ALIGN(0x10000) - . < 0x1000) ? ALIGN(0x10000) + 0x10 : .;
}
INSERT AFTER .eh_frame;
