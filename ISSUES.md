# Security issues

A review of the Markdown viewer (omamd) for untrusted `.md` files. Local notes you wrote yourself are a milder threat; a README from the internet is not.

Detected by walking `src/markdown.c` and `src/main.c` and asking what WebKit would do with a crafted document: follow a `file://` link, honour `javascript:` in `--html` output, load an image from `/etc`, or recurse until the stack dies.

## 1. Local files via `file://` links

**Problem.** A click on a `file://` URL that was not a `.md`/`.txt` file was handed to WebKit (`return FALSE` in `on_decide_policy`). Relative `../` paths resolved against the document directory. A malicious file could do:

```markdown
[notes](file:///etc/passwd)
[keys](../../.ssh/id_rsa)
```

and the contents would appear in the preview. `.txt` was treated as Markdown, so any text file on disk was in reach.

**Fix.** Clicks only open `http:`, `https:`, and `mailto:` (in the system browser). A local Markdown file is opened in omamd only if `realpath` shows it sits under the current document’s directory. All other navigations, including `target=_blank`, are dropped. Image and other subresource loads go through the same directory check (`WEBKIT_POLICY_DECISION_TYPE_RESPONSE`). With no file open, the HTML base URI is `about:blank` instead of `file:///`.

**Tested.** `examples/security.md` contains `file:///etc/passwd` and `/etc/passwd`. `omamd --html` must not emit those as `href`/`src` (`make test`). Policy confinement is in `on_decide_policy` and `path_is_under_dir`; it is not exercised by `--html`, which is why the parser also refuses `file:` and rooted paths so dumped HTML is safe if opened in a browser.

## 2. `javascript:` and `data:text/html` URLs

**Problem.** Links and images were HTML-escaped but the URL *scheme* was not filtered. In the GTK window, JavaScript is off and `javascript:` clicks were ignored. `omamd --html file.md > out.html` opened in a real browser would run script:

```markdown
[x](javascript:alert(1))
![](data:text/html,<script>alert(1)</script>)
```

**Fix.** Allowlists in `url_is_safe_href` / `url_is_safe_img` (`src/markdown.c`):

| Kind  | Allowed                                      |
|-------|----------------------------------------------|
| Link  | `http`, `https`, `mailto`, relative (no `/` at the start, no `//`) |
| Image | `http`, `https`, `data:image/…`, same relative rule |

Rejected URLs still show their label text; they are not tags. WebKit also has `enable-javascript` and `enable-javascript-markup` set to false, plus local storage, database, media stream, WebGL, and `file`/`universal` access from `file:` URLs turned off.

**Tested.** `make test` asserts `examples/security.md` still produces `href="https://example.com/ok"` and `src="https://example.com/pix.png"`, and that the HTML contains none of `javascript:`, `file:///etc/passwd`, `data:text/html`, or `src="/etc/passwd"`. Manual check of the article body: unsafe links become `<p>javascript</p>` (text only).

## 3. Unbounded reads and nested blocks

**Problem.** `read_entire_file` / `read_entire_stdin` would `malloc` the whole stream. A huge file or pipe could OOM the process. Lists and block quotes called `parse_blocks` with no depth cap, so a few thousand nested `>` lines could blow the C stack (inline markup already capped at 32).

**Fix.** Files and stdin larger than 32 MiB are refused. Block nesting (quotes and lists) stops at 32; leftover markers are rendered as paragraph text.

**Tested.** `python3` wrote a 33 MiB file; `omamd --html` exited 1 with `cannot read`. Forty nested `>` lines under AddressSanitizer (`-fsanitize=address,undefined`) completed. `examples/welcome.md` still produces nested `<ul>` for the sample list. `make test` and the ASan `--html` runs of `welcome.md` and `security.md` were clean.

## What was already fine

Body text is escaped (`& < > "`). Raw HTML in a `.md` file is not passed through. Theme colours from Omarchy `colors.toml` are accepted only as `#rgb` / `#rrggbb` before they go into CSS or SVG.
