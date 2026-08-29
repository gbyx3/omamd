# Security fixture

Used by `make test`. Safe links stay links; the rest is rendered as text.

[ok https](https://example.com/ok)

[ok relative](other.md)

[javascript](javascript:alert(1))

[file passwd](file:///etc/passwd)

[data html](data:text/html,<script>alert(1)</script>)

[rooted](/etc/passwd)

![bad data](data:text/html,<script>alert(1)</script>)

![ok image](https://example.com/pix.png)

![rooted img](/etc/passwd)
