Place custom `.bdf` source fonts in this folder.

For U8g2 firmware use, convert each `.bdf` into a C header (`.h`) using `bdfconv`, then include that header from `src/main.cpp`.

Typical flow:

1. Put source font here, for example: `fonts/my_font.bdf`
2. Run conversion (example command):
   `bdfconv -f 1 -m "32-127,176" -n my_font_tr -o src/fonts/my_font_tr.h fonts/my_font.bdf`
3. In `main.cpp`:
   - `#include "fonts/my_font_tr.h"`
   - `u8g2.setFont(my_font_tr);`

Notes:
- Keep generated headers under `src/fonts/` so PlatformIO compiles them with the project.
- Include glyph 176 if you need the degree symbol (`°`).
