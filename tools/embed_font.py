from pathlib import Path
import sys
source=Path(sys.argv[1]).resolve();out=Path(sys.argv[2])
text='.section .rodata\n.balign 16\n.global desktop_font_start\ndesktop_font_start:\n.incbin "'+str(source)+'"\n.global desktop_font_end\ndesktop_font_end:\n'
interface=source.with_name('Interface.ttf')
text+='\n.balign 16\n.global interface_font_start\ninterface_font_start:\n.incbin "'+str(interface)+'"\n.section .note.GNU-stack,"",@progbits\n'
out.write_text(text)
