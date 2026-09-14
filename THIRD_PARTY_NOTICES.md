# Third-party components and their notices

Unbind Vanilla Controls as a whole is GPL-3.0-or-later (`LICENSE`, `NOTICE.md`). These components are included under their own
GPL-compatible licences; their notices are reproduced as those licences require.

## CommonLibSSE-NG 7.2.0 - Skyrim 1.7.x build line

https://github.com/alandtse/CommonLibSSE-NG (commit 7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH
Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source); the exceptions ship as
`CommonLibSSE-NG-EXCEPTIONS.md` beside the 1.7 build.

## CommonLibSSE-NG 3.7.0 - SE 1.5.97 / AE 1.6.1170 build line

MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## DevBench consumer API (`include/DevBench/`, `source/DevBench/`)

MIT - the notice is `include/DevBench/DevBenchAPI.LICENSE.txt`, kept with the files.

## controlmap.txt - Controlmap.txt Fixed and Cleaned - Updated 1.3 (optional files in the installer)

https://www.nexusmods.com/skyrimspecialedition/mods/175609 by DEEJMASTER333 and others. The author's permissions on that
page: "I don't care what you do with this." The two `controlmap.txt` builds this mod's installer offers (pre-1.6.1130 and
1.6.1130+) are that mod's files with two lines changed: System Tab (the Pause control) gets the controller's Start button
and can be remapped on the controller, and Journal has no controller button.

Credits carried from that page: DavidJCobb for the original Cobb Controlmap Fix, and mistaabushido for Controlmap.txt Fixed
and Cleaned, which were the basis of that mod; Hawkbar for Skyrim Control Mapper.

## Notes

* This mod ships no code from any other mod: the DLL is original code built on CommonLibSSE-NG (above).
* devbench.dll is a separate, optional program this mod only talks to through the DevBench consumer API.
