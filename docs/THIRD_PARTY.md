# Third-party attribution

The ATF1502AS and ATF1504AS fuse permutations and programming algorithm are
adapted from
[Project Bureau](https://github.com/whitequark/prjbureau), an open-source reverse
engineering project by whitequark. The relevant source snapshot is commit
`8b8a97122ec238b6a09868a4bc7def20acef798c`:

- [Fuse mapping and IDCODE](https://github.com/whitequark/prjbureau/blob/8b8a97122ec238b6a09868a4bc7def20acef798c/util/device.py)
- [Programming sequences](https://github.com/whitequark/prjbureau/blob/8b8a97122ec238b6a09868a4bc7def20acef798c/util/fuseconv.py)
- [Configuration fuse database](https://github.com/whitequark/prjbureau/blob/8b8a97122ec238b6a09868a4bc7def20acef798c/database.json)
- [Programming options and ordering](https://github.com/whitequark/prjbureau/blob/8b8a97122ec238b6a09868a4bc7def20acef798c/docs/options/program.rst)

Adapted portions are in `cli/src/jedec.cc`, `cli/src/main.cc`,
`firmware/atf1502_programmer/jtag.h`, `protocol.h`, and the tests. The upstream
license notice is retained verbatim below for the upstream portions. The
combined software and project modifications are distributed under
`GPL-3.0-only`; see [LICENSE](../LICENSE). This does not replace the original
upstream notice. The new parser, transport, firmware command protocol and CLI
do not require upstream Python packages at runtime.

```text
Unless otherwise noted, the files in this repository are covered by:

Copyright (C) 2019-2020 whitequark@whitequark.org

Permission to use, copy, modify, and/or distribute this software for
any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```
