from pathlib import Path
from PIL import Image
root=Path('build');root.mkdir(exist_ok=True)
im=Image.new('RGB',(32,24),(34,103,202))
for ext in ('png','jpg','gif'):im.save(root/('image-test.'+ext))
